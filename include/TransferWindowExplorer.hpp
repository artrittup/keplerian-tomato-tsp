#pragma once

#include "TransferCache.hpp"

#include <optional>
#include <vector>

// Optional absolute MJD2000 limits and durations in days. Intersected with the mission limits.
struct TransferWindowBounds
{
    std::optional<double> earliestDeparture;
    std::optional<double> latestDeparture;
    std::optional<double> minimumFlight;
    std::optional<double> maximumFlight;
    std::optional<double> latestArrival;
};

struct TransferWindowConfig
{
    std::size_t maxEvaluations = 512; // Additional actual solver calls allowed on this cache.
    std::size_t maxSamples = 4096;  // Includes hits, denied requests, and duplicate grid points.
    unsigned coarseDivisions = 8;
    unsigned refinementLevels = 8;
    DeltaVPolicy policy = DeltaVPolicy::Strict;
};

enum class TransferWindowStop
{
    Exhausted,
    EvaluationLimit,
    SampleLimit,
    InvalidInput
};

struct TransferOpportunity
{
    TransferQuery query;
    double arrivalDays;
    double deltaVMps;
    DeltaVClassification classification;
};

struct TransferWindowStats
{
    std::size_t coarseSamples = 0;
    std::size_t refinementSamples = 0;
    std::size_t duplicateSamples = 0;
    std::size_t solverCalls = 0;
    std::size_t cacheHits = 0;
    std::size_t cacheMisses = 0; // Includes budgetDenials, as in TransferCache.
    std::size_t budgetDenials = 0;
    std::size_t solverFailures = 0;
};

struct TransferWindowResult
{
    std::vector<TransferOpportunity> opportunities; // Unique exact timings in increasing Delta-V order.
    std::optional<double> lowestDeltaVMps; // Lowest successful solve, also if above the policy ceiling.
    TransferWindowStats stats;
    TransferWindowStop stop = TransferWindowStop::Exhausted;
};

class TransferWindowExplorer
{
public:
    // Evaluate the directed pair on the cache's immutable problem. Exception budgets belong to routes.
    static TransferWindowResult explore(TransferCache &cache, std::size_t source, std::size_t destination,
                                        const TransferWindowConfig &config = {},
                                        const TransferWindowBounds &bounds = {});
};
