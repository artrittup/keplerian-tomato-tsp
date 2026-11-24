#pragma once

#include "Solution.hpp"
#include "TransferWindowExplorer.hpp"

#include <iosfwd>

struct FixedOrderConfig
{
    std::size_t maxOrbitalCalls = 200000; // Per method, including one independent validation.
    std::size_t departureBands = 32;
    std::size_t refinementPercent = 50;
    double flightScaleDays = 2.0;
    std::uint64_t seed = 0;
    std::size_t cacheCapacity = 32768;
    TransferCacheIndex cacheIndex = TransferCacheIndex::Tree;
};

enum class FixedOrderStop
{
    InvalidInput,
    EvaluationBudget,
    NoTransferFound,
    NoChronologicalTransfer,
    ExceptionBudget,
    ValidationFailed,
    Feasible
};

struct FixedOrderLegCoverage
{
    std::size_t normal = 0;
    std::size_t exception = 0;
    std::size_t solverFailures = 0; // Includes failures replayed from cache.
    std::size_t solverCalls = 0;
    std::optional<double> lowestDeltaVMps;
};

struct FixedOrderDepth
{
    std::size_t incomingLabels = 0;
    std::size_t chronologicalNormal = 0; // Unique opportunities reachable in time by at least one label.
    std::size_t chronologicalException = 0;
    std::size_t outgoingLabels = 0;
};

struct FixedOrderOutcome
{
    FixedOrderStop stop = FixedOrderStop::InvalidInput;
    std::vector<FixedOrderDepth> depth; // One entry per attempted leg, including the first blocked leg.
    std::vector<TransferOpportunity> prefix;
    std::vector<std::size_t> exceptionsByDepth; // Starts at zero for the first tomato in a valid experiment.
    std::optional<Solution> solution; // Present only after independent strict validation.
    std::optional<double> completionDays;
    std::size_t validationCalls = 0;
    double schedulingSeconds = 0.0; // Includes independent validation.
};

struct FixedOrderExperiment
{
    std::vector<FixedOrderLegCoverage> coverage;
    TransferCacheStats discovery;
    double discoverySeconds = 0.0;
    FixedOrderOutcome greedy;
    FixedOrderOutcome earliest;
};

std::optional<std::vector<int>> readFixedOrder(std::istream &input, std::size_t tomatoCount);

class FixedOrderScheduler
{
public:
    // Build one strict table without a schedule as input, then run both decoders on it.
    static FixedOrderExperiment compare(const Problem &problem, const std::vector<int> &order,
                                        const FixedOrderConfig &config = {});
};
