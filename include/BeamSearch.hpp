#pragma once

#include "Solution.hpp"
#include "TransferSolver.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

enum class BeamTimingStrategy
{
    Baseline,
    Stratified
};

enum class BeamSelectionStrategy
{
    Baseline,
    Temporal
};

struct BeamSearchConfig
{
    std::size_t beamWidth = 4;
    std::size_t destinationsPerRoute = 8;
    std::size_t samplesPerDestination = 6;
    std::uint64_t seed = 0;
    std::size_t maxEvaluations = 10000;
    std::size_t restartCount = 2; // Additional attempts after the first.
    DeltaVPolicy policy = DeltaVPolicy::Strict;
    BeamTimingStrategy timingStrategy = BeamTimingStrategy::Baseline;
    BeamSelectionStrategy selectionStrategy = BeamSelectionStrategy::Baseline;
    double arrivalBinDays = 1.0; // Maximum width of each preferred temporal bin.
};

enum class BeamSearchStop
{
    AttemptsExhausted,
    EvaluationLimit,
    InvalidConfig,
    InvalidProblem
};

// Retained arrival bins in elapsed mission days are [0, 2), [2, 4), [4, 8), [8, 16), [16, infinity).
inline constexpr std::array<double, 4> beamArrivalBinEdges{2.0, 4.0, 8.0, 16.0};

struct BeamDepthStats
{
    std::size_t prefixesExamined = 0;
    std::size_t evaluations = 0;
    std::size_t accepted = 0;
    std::size_t rejected = 0;
    std::size_t invalidTiming = 0;
    std::size_t deadlineExceeded = 0;
    std::size_t deltaVExceeded = 0;
    std::size_t exceptionBudgetExceeded = 0;
    std::size_t orbitalInvalidInput = 0;
    std::size_t noLambertCandidate = 0;
    std::size_t numericalFailure = 0;
    std::size_t invalidDestination = 0;
    std::size_t duplicateDestination = 0;
    std::size_t visitedDestinationsSkipped = 0;
    std::size_t destinationsClipped = 0;
    std::size_t destinationsWithoutTiming = 0;
    std::size_t sameEndpointPruned = 0;
    std::size_t widthPruned = 0;
    std::size_t retained = 0;
    std::size_t retainedAtExceptionLimit = 0;
    std::optional<double> latestAcceptedArrivalDays;
    std::optional<double> latestRetainedArrivalDays;
    std::array<std::size_t, beamArrivalBinEdges.size() + 1> retainedArrivalBins{};
};

struct BeamTimingBucket
{
    std::size_t evaluations = 0;
    std::size_t accepted = 0;
    std::size_t deltaVExceeded = 0;
};

// Timing bins in days are [0, .25), [.25, 1), [1, 4), and [4, infinity).
inline constexpr std::array<double, 3> beamTimingBinEdges{0.25, 1.0, 4.0};

struct BeamSearchStats
{
    // Actual TransferSolver calls, including failures and full route validation.
    std::size_t evaluationsAttempted = 0;
    std::size_t validationEvaluations = 0;
    std::size_t successfulExtensions = 0;
    std::size_t rejectedExtensions = 0;
    std::size_t transferFailures = 0;
    std::size_t deltaVRejections = 0;
    std::size_t exceptionBudgetRejections = 0;
    std::size_t deepestPrefix = 0; // Number of tomatoes visited.
    std::size_t peakBeamSize = 0;
    std::size_t attemptsStarted = 0;
    std::size_t attemptsLimitedByBudget = 0;
    std::size_t completeRoutesFound = 0; // Complete prefixes before pruning or validation.
    std::size_t completeRoutesValidated = 0;
    std::vector<int> startingTomatoes;
    // Entry zero contains extensions from one visited tomato. Validation calls are tracked separately.
    std::vector<BeamDepthStats> byDepth;
    std::array<BeamTimingBucket, beamTimingBinEdges.size() + 1> byWait{};
    std::array<BeamTimingBucket, beamTimingBinEdges.size() + 1> byFlight{};
    double elapsedSeconds = 0.0;
};

struct BeamSearchResult
{
    BeamSearchConfig config;
    BeamSearchStats stats;
    BeamSearchStop stop = BeamSearchStop::AttemptsExhausted;
    std::optional<Solution> bestSolution;
    std::optional<double> bestCompletionDays;
};

class BeamSearch
{
public:
    static BeamSearchResult run(const Problem &problem, const BeamSearchConfig &config = {});
};
