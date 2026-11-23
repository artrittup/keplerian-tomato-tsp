#include "BeamSearch.hpp"

#include "PartialRoute.hpp"
#include "Problem.hpp"

#include <algorithm>
#include <chrono>
#include <limits>
#include <random>
#include <tuple>
#include <utility>

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

void keepCandidate(std::vector<RankedRoute> &beam, RankedRoute candidate, std::size_t width)
{
    const int endpoint = candidate.route.prefix().visitingOrder.back();
    const auto sameEndpoint = std::find_if(beam.begin(), beam.end(), [endpoint](const auto &entry)
    {
        return entry.route.prefix().visitingOrder.back() == endpoint;
    });
    if (sameEndpoint != beam.end())
    {
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
        const auto worst = std::max_element(beam.begin(), beam.end(), ranksBefore);
        if (ranksBefore(candidate, *worst))
        {
            *worst = std::move(candidate);
        }
    }
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
                    result_.stop = BeamSearchStop::EvaluationLimit;
                    break;
                }
                beam = expand(beam, limit - reserve);
                std::sort(beam.begin(), beam.end(), ranksBefore);
                stats_.peakBeamSize = std::max(stats_.peakBeamSize, beam.size());
            }
            if (!beam.empty() && beam.front().route.remainingLegs() == 0)
            {
                validateComplete(beam, limit);
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

    std::vector<int> destinations(const PartialRoute &parent)
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
        std::shuffle(candidates.begin(), candidates.end(), orderRandom_);
        candidates.resize(std::min(candidates.size(), config_.destinationsPerRoute));
        return candidates;
    }

    void recordRejection(RouteExtensionStatus status)
    {
        ++stats_.rejectedExtensions;
        switch (status)
        {
        case RouteExtensionStatus::TransferFailed: ++stats_.transferFailures; break;
        case RouteExtensionStatus::DeltaVLimitExceeded: ++stats_.deltaVRejections; break;
        case RouteExtensionStatus::ExceptionBudgetExceeded: ++stats_.exceptionBudgetRejections; break;
        default: break;
        }
    }

    std::vector<RankedRoute> expand(const std::vector<RankedRoute> &beam, std::size_t limit)
    {
        std::vector<RankedRoute> next;
        next.reserve(width_);
        for (const auto &entry : beam)
        {
            const auto &parent = entry.route;
            const double daysPerLeg = (problem_.maxTotalTime - parent.arrivalDays()) /
                                      static_cast<double>(parent.remainingLegs());
            const double localSlack = std::max(0.0, daysPerLeg - problem_.minTimeOfFlight);
            for (const int destination : destinations(parent))
            {
                for (std::size_t i = 0; i < config_.samplesPerDestination; ++i)
                {
                    if (stats_.evaluationsAttempted == limit)
                    {
                        result_.stop = BeamSearchStop::EvaluationLimit;
                        return next;
                    }
                    const auto timing = i % 4 == 3 ? timingSampler_.sample(parent)
                                                    : timingSampler_.sample(parent, localSlack);
                    if (!timing)
                    {
                        break;
                    }
                    auto extension = parent.extend(destination, *timing);
                    stats_.evaluationsAttempted += extension.transfer.has_value();
                    if (!extension.route)
                    {
                        recordRejection(extension.status);
                        continue;
                    }

                    ++stats_.successfulExtensions;
                    auto &child = *extension.route;
                    stats_.deepestPrefix = std::max(stats_.deepestPrefix, child.prefix().visitingOrder.size());
                    stats_.completeRoutesFound += child.remainingLegs() == 0;
                    const double priority = score(child);
                    keepCandidate(next, {std::move(child), priority, stats_.evaluationsAttempted}, width_);
                }
            }
        }
        return next;
    }

    void validateComplete(const std::vector<RankedRoute> &beam, std::size_t limit)
    {
        for (const auto &entry : beam)
        {
            if (legCount_ > limit - stats_.evaluationsAttempted)
            {
                result_.stop = BeamSearchStop::EvaluationLimit;
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
};
}

BeamSearchResult BeamSearch::run(const Problem &problem, const BeamSearchConfig &config)
{
    const auto start = std::chrono::steady_clock::now();
    BeamSearchResult result;
    result.config = config;
    if (config.beamWidth == 0 || config.destinationsPerRoute == 0 || config.samplesPerDestination == 0 ||
        config.restartCount == std::numeric_limits<std::size_t>::max())
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
