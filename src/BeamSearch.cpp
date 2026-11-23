#include "BeamSearch.hpp"

#include "BeamSelection.hpp"
#include "PartialRoute.hpp"
#include "Problem.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <random>
#include <tuple>
#include <unordered_set>
#include <utility>

std::vector<std::size_t> beam_detail::selectTemporal(const std::vector<ArrivalCandidate> &ranked,
                                                     std::size_t width, double latestArrival, double maxBinDays)
{
    constexpr int binCount = 4;
    std::vector<std::size_t> selected;
    if (ranked.empty() || width == 0)
    {
        return selected;
    }

    const double earliest = std::min_element(ranked.begin(), ranked.end(), [](const auto &a, const auto &b)
    {
        return a.arrivalDays < b.arrivalDays;
    })->arrivalDays;
    // Four bins may use at most a quarter of the spare time beyond the earliest candidate.
    const double binDays = std::min(maxBinDays, (latestArrival - earliest) / (4.0 * binCount));
    const auto binOf = [earliest, binDays](double arrival)
    {
        if (binDays <= 0.0)
        {
            return -1;
        }
        const double position = (arrival - earliest) / binDays;
        return position < binCount ? static_cast<int>(position) : -1;
    };

    std::unordered_set<int> endpoints;
    const auto take = [&ranked, &selected, &endpoints](std::size_t index)
    {
        if (!endpoints.insert(ranked[index].endpoint).second)
        {
            return false;
        }
        selected.push_back(index);
        return true;
    };

    take(0);
    const int firstBin = binOf(ranked.front().arrivalDays);
    for (int bin = 0; bin < binCount && selected.size() < width; ++bin)
    {
        if (bin == firstBin)
        {
            continue;
        }
        for (std::size_t i = 1; i < ranked.size(); ++i)
        {
            if (binOf(ranked[i].arrivalDays) == bin && take(i))
            {
                break;
            }
        }
    }
    for (std::size_t i = 1; i < ranked.size() && selected.size() < width; ++i)
    {
        take(i);
    }
    return selected;
}

namespace
{
struct CheckedLeg
{
    int destination;
    LegTiming timing;
    PartialRoute child;
    std::size_t proposalOrder;
};

struct RankedRoute
{
    PartialRoute route;
    double score;
    std::size_t proposalOrder;
    std::optional<CheckedLeg> witness = std::nullopt;
};

bool ranksBefore(const RankedRoute &left, const RankedRoute &right)
{
    return std::make_tuple(left.score, left.route.arrivalDays(), left.route.exceptionsUsed(), left.proposalOrder) <
           std::make_tuple(right.score, right.route.arrivalDays(), right.route.exceptionsUsed(), right.proposalOrder);
}

void keepCandidate(std::vector<RankedRoute> &beam, RankedRoute candidate, std::size_t width, BeamDepthStats &depth)
{
    const int endpoint = candidate.route.prefix().visitingOrder.back();
    const auto sameEndpoint = std::find_if(beam.begin(), beam.end(), [endpoint](const auto &entry)
    {
        return entry.route.prefix().visitingOrder.back() == endpoint;
    });
    if (sameEndpoint != beam.end())
    {
        ++depth.sameEndpointPruned;
        if (ranksBefore(candidate, *sameEndpoint))
        {
            *sameEndpoint = std::move(candidate);
        }
    }
    else if (beam.size() < width)
    {
        beam.push_back(std::move(candidate));
    }
    else
    {
        ++depth.widthPruned;
        const auto worst = std::max_element(beam.begin(), beam.end(), ranksBefore);
        if (ranksBefore(candidate, *worst))
        {
            *worst = std::move(candidate);
        }
    }
}

void recordLatest(std::optional<double> &latest, double arrival)
{
    if (!latest || arrival > *latest)
    {
        latest = arrival;
    }
}

void recordTiming(BeamTimingBucket &bucket, const RouteExtensionResult &extension)
{
    ++bucket.evaluations;
    bucket.accepted += extension.route.has_value();
    bucket.deltaVExceeded += extension.status == RouteExtensionStatus::DeltaVLimitExceeded;
}

std::size_t timingBin(double days)
{
    return static_cast<std::size_t>(std::upper_bound(beamTimingBinEdges.begin(), beamTimingBinEdges.end(), days) -
                                    beamTimingBinEdges.begin());
}

class Search
{
public:
    Search(const Problem &problem, BeamSearchResult &result)
        : problem_(problem), result_(result), config_(result.config), stats_(result.stats),
          legCount_(problem.tomatoes.size() - 1), width_(std::min(config_.beamWidth, problem.tomatoes.size())),
          orderRandom_(config_.seed), timingSampler_(orderRandom_()),
          lookaheadOrderRandom_(~config_.seed), lookaheadSampler_(lookaheadOrderRandom_())
    {
        startOrder_.reserve(problem.tomatoes.size());
        for (std::size_t i = 0; i < problem.tomatoes.size(); ++i)
        {
            startOrder_.push_back(static_cast<int>(i));
        }
        std::shuffle(startOrder_.begin(), startOrder_.end(), orderRandom_);
    }

    void run()
    {
        // A route needs at least one extension pass and one validation pass.
        const std::size_t affordable = legCount_ == 0 ? 1 : config_.maxEvaluations / legCount_ / 2;
        const std::size_t attempts = std::min(config_.restartCount + 1, std::max<std::size_t>(1, affordable));
        for (std::size_t attempt = 0; attempt < attempts; ++attempt)
        {
            attemptLimited_ = false;
            const std::size_t remaining = config_.maxEvaluations - stats_.evaluationsAttempted;
            const std::size_t count = attempts - attempt;
            const std::size_t allowance = remaining / count + (remaining % count != 0);
            const std::size_t limit = stats_.evaluationsAttempted + allowance;
            const std::size_t reserve = legCount_ <= allowance / 2 ? legCount_ : 0;

            auto beam = initialBeam();
            if (beam.empty())
            {
                result_.stop = BeamSearchStop::InvalidProblem;
                return;
            }
            if (legCount_ == 0)
            {
                ++stats_.completeRoutesFound;
            }

            while (!beam.empty() && beam.front().route.remainingLegs() != 0)
            {
                if (stats_.evaluationsAttempted == limit - reserve &&
                    std::none_of(beam.begin(), beam.end(), [](const auto &entry) { return entry.witness.has_value(); }))
                {
                    attemptLimited_ = true;
                    break;
                }
                const auto visited = beam.front().route.prefix().visitingOrder.size();
                if (stats_.byDepth.size() < visited)
                {
                    stats_.byDepth.resize(visited);
                }
                auto &depth = stats_.byDepth[visited - 1];
                beam = expand(beam, limit - reserve, depth);
                depth.retained += beam.size();
                for (const auto &entry : beam)
                {
                    recordLatest(depth.latestRetainedArrivalDays, entry.route.arrivalDays());
                    const double elapsed = entry.route.arrivalDays() - problem_.startTime;
                    const auto bin = std::upper_bound(beamArrivalBinEdges.begin(), beamArrivalBinEdges.end(), elapsed);
                    ++depth.retainedArrivalBins[static_cast<std::size_t>(bin - beamArrivalBinEdges.begin())];
                    depth.retainedAtExceptionLimit +=
                        entry.route.exceptionsUsed() == static_cast<std::size_t>(problem_.maxExceptions);
                }
                std::sort(beam.begin(), beam.end(), ranksBefore);
                stats_.peakBeamSize = std::max(stats_.peakBeamSize, beam.size());
            }
            if (!beam.empty() && beam.front().route.remainingLegs() == 0)
            {
                validateComplete(beam, limit);
            }
            if (attemptLimited_)
            {
                ++stats_.attemptsLimitedByBudget;
                result_.stop = BeamSearchStop::EvaluationLimit;
            }
        }
    }

private:
    std::vector<RankedRoute> initialBeam()
    {
        std::vector<RankedRoute> beam;
        beam.reserve(width_);
        for (std::size_t i = 0; i < width_; ++i)
        {
            const int tomato = startOrder_[nextStart_];
            auto route = PartialRoute::start(problem_, tomato, config_.policy);
            if (!route)
            {
                return {};
            }
            nextStart_ = (nextStart_ + 1) % startOrder_.size();
            stats_.startingTomatoes.push_back(tomato);
            beam.push_back({std::move(*route), 0.0, i});
        }
        ++stats_.attemptsStarted;
        stats_.deepestPrefix = std::max<std::size_t>(1, stats_.deepestPrefix);
        stats_.peakBeamSize = std::max(stats_.peakBeamSize, beam.size());
        return beam;
    }

    double score(const PartialRoute &route) const
    {
        if (route.remainingLegs() == 0)
        {
            return route.arrivalDays();
        }
        const double horizon = std::max(problem_.maxTotalTime - problem_.startTime,
                                        std::numeric_limits<double>::min());
        const double elapsed = route.arrivalDays() - problem_.startTime;
        const double remainingFlight = static_cast<double>(route.remainingLegs()) * problem_.minTimeOfFlight;
        return elapsed / horizon + remainingFlight / horizon +
               static_cast<double>(route.exceptionsUsed()) / static_cast<double>(legCount_);
    }

    std::vector<int> unvisited(const PartialRoute &parent) const
    {
        std::vector<int> candidates;
        candidates.reserve(parent.remainingLegs());
        for (std::size_t i = 0; i < problem_.tomatoes.size(); ++i)
        {
            const int tomato = static_cast<int>(i);
            if (!parent.hasVisited(tomato))
            {
                candidates.push_back(tomato);
            }
        }
        return candidates;
    }

    std::vector<int> destinations(const PartialRoute &parent, BeamDepthStats &depth)
    {
        auto candidates = unvisited(parent);
        depth.visitedDestinationsSkipped += problem_.tomatoes.size() - candidates.size();
        std::shuffle(candidates.begin(), candidates.end(), orderRandom_);
        const auto count = std::min(candidates.size(), config_.destinationsPerRoute);
        depth.destinationsClipped += candidates.size() - count;
        candidates.resize(count);
        return candidates;
    }

    void recordRejection(const RouteExtensionResult &extension, BeamDepthStats &depth)
    {
        ++stats_.rejectedExtensions;
        ++depth.rejected;
        switch (extension.status)
        {
        case RouteExtensionStatus::TransferFailed:
            ++stats_.transferFailures;
            switch (extension.transfer->status)
            {
            case TransferStatus::InvalidInput: ++depth.orbitalInvalidInput; break;
            case TransferStatus::NoFiniteCandidate: ++depth.noLambertCandidate; break;
            case TransferStatus::NumericalFailure: ++depth.numericalFailure; break;
            case TransferStatus::Ok: break;
            }
            break;
        case RouteExtensionStatus::DeltaVLimitExceeded:
            ++stats_.deltaVRejections;
            ++depth.deltaVExceeded;
            break;
        case RouteExtensionStatus::ExceptionBudgetExceeded:
            ++stats_.exceptionBudgetRejections;
            ++depth.exceptionBudgetExceeded;
            break;
        case RouteExtensionStatus::InvalidTiming: ++depth.invalidTiming; break;
        case RouteExtensionStatus::DeadlineExceeded: ++depth.deadlineExceeded; break;
        case RouteExtensionStatus::InvalidDestination: ++depth.invalidDestination; break;
        case RouteExtensionStatus::AlreadyVisited: ++depth.duplicateDestination; break;
        case RouteExtensionStatus::Ok: break;
        }
    }

    double localSlack(const PartialRoute &parent) const
    {
        const double daysPerLeg = (problem_.maxTotalTime - parent.arrivalDays()) /
                                  static_cast<double>(parent.remainingLegs());
        return std::max(0.0, daysPerLeg - problem_.minTimeOfFlight);
    }

    std::optional<LegTiming> sampleTiming(TimingSampler &sampler, const PartialRoute &parent, double slack,
                                          std::size_t sample, std::size_t sampleCount)
    {
        if (sample % 4 == 3)
        {
            return sampler.sample(parent);
        }
        if (config_.timingStrategy == BeamTimingStrategy::Stratified)
        {
            const auto localSamples = sampleCount - sampleCount / 4;
            return sampler.sampleStratified(parent, slack, sample - sample / 4, localSamples);
        }
        return sampler.sample(parent, slack);
    }

    void probe(RankedRoute &candidate, std::size_t limit, BeamDepthStats &depth)
    {
        auto targets = unvisited(candidate.route);
        std::shuffle(targets.begin(), targets.end(), lookaheadOrderRandom_);
        const auto targetCount = std::min(targets.size(), config_.destinationsPerRoute);
        const double slack = localSlack(candidate.route);
        for (std::size_t i = 0; i < config_.lookaheadProbes && stats_.evaluationsAttempted < limit; ++i)
        {
            const auto timing = sampleTiming(lookaheadSampler_, candidate.route, slack, i, config_.lookaheadProbes);
            if (!timing)
            {
                break;
            }
            const int target = targets[i % targetCount];
            auto extension = candidate.route.extend(target, *timing);
            if (!extension.transfer)
            {
                continue;
            }
            ++stats_.evaluationsAttempted;
            ++stats_.lookaheadEvaluations;
            ++depth.lookaheadEvaluations;
            if (extension.route)
            {
                ++stats_.lookaheadAccepted;
                candidate.witness = CheckedLeg{target, *timing, std::move(*extension.route),
                                               stats_.evaluationsAttempted};
                return;
            }
            ++stats_.lookaheadRejected;
            stats_.lookaheadDeltaVRejections += extension.status == RouteExtensionStatus::DeltaVLimitExceeded;
            stats_.lookaheadExceptionBudgetRejections +=
                extension.status == RouteExtensionStatus::ExceptionBudgetExceeded;
        }
    }

    void selectGuided(std::vector<RankedRoute> &candidates, std::size_t limit,
                      std::size_t expansionCalls, BeamDepthStats &depth)
    {
        if (candidates.size() <= width_)
        {
            return;
        }

        std::sort(candidates.begin(), candidates.end(), ranksBefore);
        // Limit extra work to one tenth of the calls made to generate this level.
        const auto quota = std::min(expansionCalls / 10, limit - stats_.evaluationsAttempted);
        const auto probeLimit = stats_.evaluationsAttempted + quota;
        std::size_t witnessed = 0;
        for (auto &candidate : candidates)
        {
            if (stats_.evaluationsAttempted == probeLimit || witnessed == width_)
            {
                break;
            }
            probe(candidate, probeLimit, depth);
            witnessed += candidate.witness.has_value();
        }

        // An unsuccessful probe is inconclusive. Keep such candidates in baseline order as fallback.
        const auto witnessedFirst = [](const RankedRoute &entry) { return entry.witness.has_value(); };
        stats_.lookaheadPromoted += static_cast<std::size_t>(
            std::count_if(candidates.begin() + width_, candidates.end(), witnessedFirst));
        std::stable_partition(candidates.begin(), candidates.end(), witnessedFirst);
        depth.widthPruned += candidates.size() - width_;
        candidates.erase(candidates.begin() + width_, candidates.end());
    }

    std::vector<RankedRoute> selectFrontier(std::vector<RankedRoute> candidates, bool temporal, bool guided,
                                           std::size_t limit, std::size_t beforeExpansion, BeamDepthStats &depth)
    {
        if (guided)
        {
            selectGuided(candidates, limit, stats_.evaluationsAttempted - beforeExpansion, depth);
            return candidates;
        }
        if (!temporal || candidates.empty())
        {
            return candidates;
        }

        std::sort(candidates.begin(), candidates.end(), ranksBefore);
        std::vector<beam_detail::ArrivalCandidate> arrivals;
        arrivals.reserve(candidates.size());
        for (const auto &candidate : candidates)
        {
            arrivals.push_back({candidate.route.prefix().visitingOrder.back(), candidate.route.arrivalDays()});
        }
        const auto remaining = static_cast<double>(candidates.front().route.remainingLegs());
        const double latest = problem_.maxTotalTime - remaining * problem_.minTimeOfFlight;
        const auto selected = beam_detail::selectTemporal(arrivals, width_, latest, config_.arrivalBinDays);
        std::vector<RankedRoute> frontier;
        frontier.reserve(selected.size());
        std::unordered_set<int> endpoints;
        std::vector<bool> retained(candidates.size(), false);
        for (const auto index : selected)
        {
            retained[index] = true;
            endpoints.insert(arrivals[index].endpoint);
            frontier.push_back(std::move(candidates[index]));
        }
        for (std::size_t i = 0; i < arrivals.size(); ++i)
        {
            if (retained[i])
            {
                continue;
            }
            if (endpoints.count(arrivals[i].endpoint) != 0)
            {
                ++depth.sameEndpointPruned;
            }
            else
            {
                ++depth.widthPruned;
            }
        }
        return frontier;
    }

    void offerExtension(std::vector<RankedRoute> &next, PartialRoute child, std::size_t order,
                        std::size_t capacity, bool temporal, BeamDepthStats &depth)
    {
        ++stats_.successfulExtensions;
        ++depth.accepted;
        recordLatest(depth.latestAcceptedArrivalDays, child.arrivalDays());
        stats_.deepestPrefix = std::max(stats_.deepestPrefix, child.prefix().visitingOrder.size());
        stats_.completeRoutesFound += child.remainingLegs() == 0;
        const double priority = score(child);
        RankedRoute candidate{std::move(child), priority, order};
        if (temporal)
        {
            next.push_back(std::move(candidate));
        }
        else
        {
            keepCandidate(next, std::move(candidate), capacity, depth);
        }
    }

    std::vector<RankedRoute> expand(std::vector<RankedRoute> &beam, std::size_t limit, BeamDepthStats &depth)
    {
        std::vector<RankedRoute> next;
        const bool incomplete = beam.front().route.remainingLegs() > 1;
        const bool temporal = config_.selectionStrategy == BeamSelectionStrategy::Temporal &&
                              width_ > 1 && incomplete;
        const bool guided = config_.selectionStrategy == BeamSelectionStrategy::Lookahead && incomplete;
        const std::size_t capacity = guided ? width_ + std::min(width_, problem_.tomatoes.size() - width_) : width_;
        const std::size_t beforeExpansion = stats_.evaluationsAttempted;
        next.reserve(capacity);
        for (auto &entry : beam)
        {
            if (entry.witness)
            {
                ++stats_.lookaheadReused;
                offerExtension(next, std::move(entry.witness->child), entry.witness->proposalOrder, capacity,
                               temporal, depth);
            }
        }

        for (const auto &entry : beam)
        {
            ++depth.prefixesExamined;
            const auto &parent = entry.route;
            const double slack = localSlack(parent);
            for (const int destination : destinations(parent, depth))
            {
                for (std::size_t i = 0; i < config_.samplesPerDestination; ++i)
                {
                    if (stats_.evaluationsAttempted == limit)
                    {
                        attemptLimited_ = true;
                        return selectFrontier(std::move(next), temporal, guided, limit, beforeExpansion, depth);
                    }
                    const auto timing = sampleTiming(timingSampler_, parent, slack, i, config_.samplesPerDestination);
                    if (!timing)
                    {
                        ++depth.destinationsWithoutTiming;
                        break;
                    }
                    if (entry.witness && entry.witness->destination == destination &&
                        entry.witness->timing.departureDays == timing->departureDays &&
                        entry.witness->timing.flightDays == timing->flightDays)
                    {
                        continue;
                    }
                    auto extension = parent.extend(destination, *timing);
                    if (extension.transfer)
                    {
                        ++stats_.evaluationsAttempted;
                        ++depth.evaluations;
                        recordTiming(stats_.byWait[timingBin(timing->departureDays - parent.arrivalDays())], extension);
                        recordTiming(stats_.byFlight[timingBin(timing->flightDays)], extension);
                    }
                    if (!extension.route)
                    {
                        recordRejection(extension, depth);
                        continue;
                    }

                    offerExtension(next, std::move(*extension.route), stats_.evaluationsAttempted, capacity,
                                   temporal, depth);
                }
            }
        }
        return selectFrontier(std::move(next), temporal, guided, limit, beforeExpansion, depth);
    }

    void validateComplete(const std::vector<RankedRoute> &beam, std::size_t limit)
    {
        for (const auto &entry : beam)
        {
            if (legCount_ > limit - stats_.evaluationsAttempted)
            {
                attemptLimited_ = true;
                return;
            }
            auto complete = entry.route.finish();
            if (!complete)
            {
                continue;
            }
            const auto &evaluation = complete->evaluation;
            ++stats_.completeRoutesValidated;
            stats_.evaluationsAttempted += evaluation.legs.size();
            stats_.validationEvaluations += evaluation.legs.size();
            if (evaluation.isFeasible() && evaluation.missionCompletionDays)
            {
                ++stats_.completeFeasibleRoutes;
                if (!result_.bestCompletionDays || *evaluation.missionCompletionDays < *result_.bestCompletionDays)
                {
                    result_.bestCompletionDays = evaluation.missionCompletionDays;
                    result_.bestSolution = std::move(complete->solution);
                }
            }
        }
    }

    const Problem &problem_;
    BeamSearchResult &result_;
    const BeamSearchConfig &config_;
    BeamSearchStats &stats_;
    std::size_t legCount_;
    std::size_t width_;
    std::mt19937_64 orderRandom_;
    TimingSampler timingSampler_;
    std::mt19937_64 lookaheadOrderRandom_;
    TimingSampler lookaheadSampler_;
    std::vector<int> startOrder_;
    std::size_t nextStart_ = 0;
    bool attemptLimited_ = false;
};
}

BeamSearchResult BeamSearch::run(const Problem &problem, const BeamSearchConfig &config)
{
    const auto start = std::chrono::steady_clock::now();
    BeamSearchResult result;
    result.config = config;
    if (config.beamWidth == 0 || config.destinationsPerRoute == 0 || config.samplesPerDestination == 0 ||
        config.restartCount == std::numeric_limits<std::size_t>::max() ||
        !std::isfinite(config.arrivalBinDays) || config.arrivalBinDays <= 0.0 ||
        (config.selectionStrategy == BeamSelectionStrategy::Lookahead && config.lookaheadProbes == 0))
    {
        result.stop = BeamSearchStop::InvalidConfig;
    }
    else if (problem.tomatoes.empty() ||
             problem.tomatoes.size() - 1 > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    {
        result.stop = BeamSearchStop::InvalidProblem;
    }
    else
    {
        Search(problem, result).run();
    }
    result.stats.elapsedSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    return result;
}
