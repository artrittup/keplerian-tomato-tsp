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
struct RankedRoute
{
    PartialRoute route;
    double score;
    std::size_t proposalOrder;
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
          orderRandom_(config_.seed), timingSampler_(orderRandom_())
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
                if (stats_.evaluationsAttempted == limit - reserve)
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

    std::vector<int> destinations(const PartialRoute &parent, BeamDepthStats &depth)
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

    std::optional<LegTiming> sampleTiming(const PartialRoute &parent, double localSlack, std::size_t sample)
    {
        if (sample % 4 == 3)
        {
            return timingSampler_.sample(parent);
        }
        if (config_.timingStrategy == BeamTimingStrategy::Stratified)
        {
            const auto localSamples = config_.samplesPerDestination - config_.samplesPerDestination / 4;
            return timingSampler_.sampleStratified(parent, localSlack, sample - sample / 4, localSamples);
        }
        return timingSampler_.sample(parent, localSlack);
    }

    std::vector<RankedRoute> selectFrontier(std::vector<RankedRoute> candidates, bool temporal, BeamDepthStats &depth)
    {
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

    std::vector<RankedRoute> expand(const std::vector<RankedRoute> &beam, std::size_t limit, BeamDepthStats &depth)
    {
        std::vector<RankedRoute> next;
        next.reserve(width_);
        const bool temporal = config_.selectionStrategy == BeamSelectionStrategy::Temporal &&
                              width_ > 1 && beam.front().route.remainingLegs() > 1;
        for (const auto &entry : beam)
        {
            ++depth.prefixesExamined;
            const auto &parent = entry.route;
            const double daysPerLeg = (problem_.maxTotalTime - parent.arrivalDays()) /
                                      static_cast<double>(parent.remainingLegs());
            const double localSlack = std::max(0.0, daysPerLeg - problem_.minTimeOfFlight);
            for (const int destination : destinations(parent, depth))
            {
                for (std::size_t i = 0; i < config_.samplesPerDestination; ++i)
                {
                    if (stats_.evaluationsAttempted == limit)
                    {
                        attemptLimited_ = true;
                        return selectFrontier(std::move(next), temporal, depth);
                    }
                    const auto timing = sampleTiming(parent, localSlack, i);
                    if (!timing)
                    {
                        ++depth.destinationsWithoutTiming;
                        break;
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

                    ++stats_.successfulExtensions;
                    ++depth.accepted;
                    auto &child = *extension.route;
                    recordLatest(depth.latestAcceptedArrivalDays, child.arrivalDays());
                    stats_.deepestPrefix = std::max(stats_.deepestPrefix, child.prefix().visitingOrder.size());
                    stats_.completeRoutesFound += child.remainingLegs() == 0;
                    const double priority = score(child);
                    RankedRoute candidate{std::move(child), priority, stats_.evaluationsAttempted};
                    if (temporal)
                    {
                        next.push_back(std::move(candidate));
                    }
                    else
                    {
                        keepCandidate(next, std::move(candidate), width_, depth);
                    }
                }
            }
        }
        return selectFrontier(std::move(next), temporal, depth);
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
            if (evaluation.isFeasible() && evaluation.missionCompletionDays &&
                (!result_.bestCompletionDays || *evaluation.missionCompletionDays < *result_.bestCompletionDays))
            {
                result_.bestCompletionDays = evaluation.missionCompletionDays;
                result_.bestSolution = std::move(complete->solution);
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
        !std::isfinite(config.arrivalBinDays) || config.arrivalBinDays <= 0.0)
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
