#pragma once

#include "Solution.hpp"
#include "TransferSolver.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

struct BeamSearchConfig
{
    std::size_t beamWidth = 4;
    std::size_t destinationsPerRoute = 8;
    std::size_t samplesPerDestination = 6;
    std::uint64_t seed = 0;
    std::size_t maxEvaluations = 10000;
    std::size_t restartCount = 2; // Additional attempts after the first.
    DeltaVPolicy policy = DeltaVPolicy::Strict;
};

enum class BeamSearchStop
{
    AttemptsExhausted,
    EvaluationLimit,
    InvalidConfig,
    InvalidProblem
};

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
    std::size_t completeRoutesFound = 0; // Complete prefixes before pruning or validation.
    std::size_t completeRoutesValidated = 0;
    std::vector<int> startingTomatoes;
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
