#include "GraphSearch.hpp"

#include "PartialRoute.hpp"
#include "SolutionEvaluator.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numeric>
#include <random>
#include <tuple>
#include <utility>

namespace
{
struct Choice
{
    TransferEdgeId id;
    bool reused;
};

struct Frame
{
    TransferNodeId node;
    std::size_t exceptionsUsed;
    double arrivalDays;
    double remainingDays;
    std::vector<Choice> choices;
    std::size_t nextChoice = 0;
};

class Search
{
public:
    Search(const Problem &problem, const GraphSearchConfig &config, GraphSearchResult &result)
        : config_(config), result_(result), stats_(result.stats), cache_(problem, config.cacheCapacity),
          graph_(cache_, config.policy, config.maxEdges, config.maxNodes), problem_(cache_.problem()),
          legCount_(problem_.tomatoes.size() - 1), reserve_(legCount_ <= config.maxEvaluations / 2 ? legCount_ : 0),
          visited_(problem_.tomatoes.size()), random_(config.seed), sampler_(random_())
    {
    }

    void run(const std::vector<TransferQuery> &seedTransfers)
    {
        for (const auto &query : seedTransfers)
        {
            graph_.addTransfer(query, constructionLimit());
        }

        std::vector<std::size_t> starts(problem_.tomatoes.size());
        std::iota(starts.begin(), starts.end(), std::size_t{0});
        std::shuffle(starts.begin(), starts.end(), random_);
        if (config_.startTomato)
        {
            starts.assign(1, *config_.startTomato);
        }
        for (const auto tomato : starts)
        {
            if (!canOpenState())
            {
                break;
            }
            const auto start = graph_.addStart(tomato);
            if (start && !explore(*start))
            {
                break;
            }
        }

        stats_.cache = cache_.stats();
        stats_.orbitalCalls = stats_.cache.solverCalls + stats_.validationCalls;
        stats_.graphNodes = graph_.nodes().size();
        stats_.graphEdges = graph_.edges().size();
        stats_.graphCapacityDenials = graph_.capacityDenials();
        if (result_.stop == GraphSearchStop::Exhausted)
        {
            if (budgetLimited_ || stats_.cache.budgetDenials != 0)
            {
                result_.stop = GraphSearchStop::EvaluationLimit;
            }
            else if (stats_.graphCapacityDenials != 0)
            {
                result_.stop = GraphSearchStop::GraphCapacity;
            }
        }
    }

private:
    std::size_t constructionLimit() const
    {
        const std::size_t left = config_.maxEvaluations - stats_.validationCalls;
        return left > reserve_ ? left - reserve_ : 0;
    }

    bool canOpenState()
    {
        if (stats_.expandedStates == config_.maxStates)
        {
            result_.stop = GraphSearchStop::StateLimit;
            return false;
        }
        return true;
    }

    bool canTake(const Frame &frame, TransferEdgeId id, double latestArrival)
    {
        const auto &edge = graph_.edges()[id];
        if (visited_[graph_.nodes()[edge.to].tomato])
        {
            ++stats_.visitedRejections;
            return false;
        }
        const auto exceptions = frame.exceptionsUsed + (edge.classification == DeltaVClassification::Exception);
        if (exceptions > static_cast<std::size_t>(problem_.maxExceptions))
        {
            ++stats_.exceptionRejections;
            return false;
        }
        return edge.departureDays >= frame.arrivalDays && edge.arrivalDays <= latestArrival;
    }

    void sampleChoices(const Frame &frame, TimingWindow window, std::size_t remaining, std::vector<Choice> &choices)
    {
        std::vector<std::size_t> destinations;
        destinations.reserve(remaining);
        for (std::size_t i = 0; i < visited_.size(); ++i)
        {
            if (!visited_[i])
            {
                destinations.push_back(i);
            }
        }
        std::shuffle(destinations.begin(), destinations.end(), random_);
        destinations.resize(std::min(destinations.size(), config_.destinationsPerState));
        const double slack = std::max(0.0, frame.remainingDays / static_cast<double>(remaining) - window.minimumFlightDays);
        const auto localSamples = config_.samplesPerDestination - config_.samplesPerDestination / 4;
        const auto source = graph_.nodes()[frame.node].tomato;
        const auto limit = constructionLimit();
        for (const auto target : destinations)
        {
            for (std::size_t i = 0; i < config_.samplesPerDestination; ++i)
            {
                const auto timing = i % 4 == 3
                    ? sampler_.sample(window, std::numeric_limits<double>::infinity())
                    : sampler_.sampleStratified(window, slack, i - i / 4, localSamples);
                if (!timing)
                {
                    break;
                }
                if (cache_.stats().solverCalls >= limit)
                {
                    budgetLimited_ = true;
                    return; // The frame can still follow edges already in the graph.
                }
                const auto firstNew = graph_.edges().size();
                const auto id = graph_.addTransfer({source, target, timing->departureDays, timing->flightDays}, limit);
                if (id && *id >= firstNew && canTake(frame, *id, window.latestArrivalDays))
                {
                    choices.push_back({*id, false});
                }
            }
        }
    }

    std::vector<Choice> select(std::vector<Choice> choices) const
    {
        std::sort(choices.begin(), choices.end(), [this](const Choice &a, const Choice &b)
        {
            const auto rank = [this](TransferEdgeId id)
            {
                const auto &edge = graph_.edges()[id];
                return std::make_tuple(edge.classification == DeltaVClassification::Exception, edge.arrivalDays, id);
            };
            return rank(a.id) < rank(b.id);
        });
        const auto count = std::min(choices.size(), config_.alternativesPerState);
        std::vector<Choice> selected;
        selected.reserve(count);
        std::vector<bool> usedDestination(visited_.size());
        std::vector<bool> usedChoice(choices.size());
        // Keep some slots for different destinations and use the remainder for any timing alternative.
        for (std::size_t i = 0; i < choices.size() && selected.size() < count / 2 + count % 2; ++i)
        {
            const auto target = graph_.nodes()[graph_.edges()[choices[i].id].to].tomato;
            if (!usedDestination[target])
            {
                usedDestination[target] = true;
                usedChoice[i] = true;
                selected.push_back(choices[i]);
            }
        }
        for (std::size_t i = 0; i < choices.size() && selected.size() < count; ++i)
        {
            if (!usedChoice[i])
            {
                selected.push_back(choices[i]);
            }
        }
        return selected;
    }

    std::vector<Choice> expand(const Frame &frame)
    {
        const auto remaining = legCount_ - path_.size();
        const double latest = problem_.maxTotalTime - static_cast<double>(remaining - 1) * problem_.minTimeOfFlight;
        std::vector<Choice> choices;
        for (const auto id : graph_.departuresAfter(frame.node))
        {
            if (canTake(frame, id, latest))
            {
                choices.push_back({id, true});
            }
        }
        if (choices.size() < config_.alternativesPerState)
        {
            sampleChoices(frame, {frame.arrivalDays, latest, problem_.minTimeOfFlight}, remaining, choices);
        }
        return select(std::move(choices));
    }

    bool verify(TransferNodeId root)
    {
        if (legCount_ > config_.maxEvaluations - cache_.stats().solverCalls - stats_.validationCalls)
        {
            result_.stop = GraphSearchStop::EvaluationLimit;
            return false;
        }
        auto solution = graph_.toSolution(root, path_);
        if (solution)
        {
            const auto evaluation = SolutionEvaluator::evaluate(problem_, *solution, config_.policy);
            stats_.validationCalls += evaluation.legs.size();
            if (evaluation.isFeasible() && evaluation.missionCompletionDays)
            {
                ++stats_.completeFeasibleRoutes;
                if (!result_.bestCompletionDays || *evaluation.missionCompletionDays < *result_.bestCompletionDays)
                {
                    result_.bestCompletionDays = evaluation.missionCompletionDays;
                    result_.bestSolution = std::move(solution);
                }
            }
        }
        return true;
    }

    bool open(TransferNodeId node, std::size_t exceptions, TransferNodeId root)
    {
        ++stats_.expandedStates;
        stats_.deepestPrefix = std::max(stats_.deepestPrefix, path_.size() + 1);
        const double arrival = graph_.nodes()[node].arrivalDays;
        Frame frame{node, exceptions, arrival, problem_.maxTotalTime - arrival, {}};
        if (path_.size() == legCount_)
        {
            if (!verify(root))
            {
                return false;
            }
        }
        else
        {
            frame.choices = expand(frame);
        }
        stack_.push_back(std::move(frame));
        return true;
    }

    bool explore(TransferNodeId root)
    {
        visited_[graph_.nodes()[root].tomato] = true;
        if (!open(root, 0, root))
        {
            return false;
        }
        while (!stack_.empty())
        {
            auto &frame = stack_.back();
            if (frame.nextChoice < frame.choices.size())
            {
                if (!canOpenState())
                {
                    return false;
                }
                const auto choice = frame.choices[frame.nextChoice++];
                const auto &edge = graph_.edges()[choice.id];
                path_.push_back(choice.id);
                visited_[graph_.nodes()[edge.to].tomato] = true;
                stats_.reusedEdges += choice.reused;
                const auto exceptions = frame.exceptionsUsed + (edge.classification == DeltaVClassification::Exception);
                if (!open(edge.to, exceptions, root))
                {
                    return false;
                }
            }
            else
            {
                if (!path_.empty())
                {
                    if (stats_.backtracks == config_.maxBacktracks)
                    {
                        result_.stop = GraphSearchStop::BacktrackLimit;
                        return false;
                    }
                    ++stats_.backtracks;
                    path_.pop_back();
                }
                visited_[graph_.nodes()[frame.node].tomato] = false;
                stack_.pop_back();
            }
        }
        return true;
    }

    const GraphSearchConfig &config_;
    GraphSearchResult &result_;
    GraphSearchStats &stats_;
    TransferCache cache_;
    TransferGraph graph_;
    const Problem &problem_;
    const std::size_t legCount_;
    const std::size_t reserve_;
    std::vector<bool> visited_;
    std::vector<TransferEdgeId> path_;
    std::vector<Frame> stack_;
    std::mt19937_64 random_;
    TimingSampler sampler_;
    bool budgetLimited_ = false;
};
}

GraphSearchResult GraphSearch::run(const Problem &problem, const GraphSearchConfig &config,
                                  const std::vector<TransferQuery> &seedTransfers)
{
    const auto start = std::chrono::steady_clock::now();
    GraphSearchResult result;
    if (config.destinationsPerState == 0 || config.alternativesPerState == 0 ||
        (config.startTomato && *config.startTomato >= problem.tomatoes.size()))
    {
        result.stop = GraphSearchStop::InvalidConfig;
    }
    else if (problem.tomatoes.empty() ||
             problem.tomatoes.size() - 1 > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
             !std::isfinite(problem.startTime) || problem.startTime < 0.0 ||
             !std::isfinite(problem.minTimeOfFlight) || problem.minTimeOfFlight < 0.0 ||
             !std::isfinite(problem.maxTotalTime) || problem.maxTotalTime < problem.startTime ||
             problem.maxExceptions < 0 || classifyDeltaV(problem, 0.0, config.policy) != DeltaVClassification::Normal)
    {
        result.stop = GraphSearchStop::InvalidProblem;
    }
    else
    {
        Search(problem, config, result).run(seedTransfers);
    }
    result.stats.elapsedSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    return result;
}
