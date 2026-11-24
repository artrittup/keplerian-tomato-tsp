#pragma once

#include "TransferGraph.hpp"

struct GraphSearchConfig
{
    std::size_t maxEvaluations = 10000;
    std::size_t maxStates = 4000;
    std::size_t maxEdges = 12000;
    std::size_t maxNodes = 24000;
    std::size_t cacheCapacity = 12000;
    std::size_t maxBacktracks = 4000;
    std::size_t destinationsPerState = 12;
    std::size_t samplesPerDestination = 12; // Zero searches only already stored edges.
    std::size_t alternativesPerState = 12;
    std::uint64_t seed = 0;
    DeltaVPolicy policy = DeltaVPolicy::Strict;
    std::optional<std::size_t> startTomato;
};

enum class GraphSearchStop
{
    Exhausted,
    EvaluationLimit,
    StateLimit,
    BacktrackLimit,
    GraphCapacity,
    InvalidConfig,
    InvalidProblem
};

struct GraphSearchStats
{
    TransferCacheStats cache;
    std::size_t orbitalCalls = 0;
    std::size_t validationCalls = 0;
    std::size_t graphNodes = 0;
    std::size_t graphEdges = 0;
    std::size_t graphCapacityDenials = 0;
    std::size_t reusedEdges = 0; // Taken edges already stored when their parent state was opened.
    std::size_t expandedStates = 0; // Includes starting and complete prefixes.
    std::size_t backtracks = 0; // Number of legs undone.
    std::size_t visitedRejections = 0;
    std::size_t exceptionRejections = 0;
    std::size_t deepestPrefix = 0;
    std::size_t completeFeasibleRoutes = 0;
    double elapsedSeconds = 0.0;
};

struct GraphSearchResult
{
    GraphSearchStats stats;
    GraphSearchStop stop = GraphSearchStop::Exhausted;
    std::optional<double> bestCompletionDays;
    std::optional<Solution> bestSolution;
};

class GraphSearch
{
public:
    // Optional exact proposals seed the graph. Their cache misses consume the same orbital budget.
    static GraphSearchResult run(const Problem &problem, const GraphSearchConfig &config = {},
                                 const std::vector<TransferQuery> &seedTransfers = {});
};
