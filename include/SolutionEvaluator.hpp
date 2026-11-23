#pragma once

#include "TransferSolver.hpp"

#include <cstddef>
#include <optional>
#include <vector>

struct Solution;

enum class SolutionFailureReason
{
    InvalidStructure,
    InvalidDeltaVLimits,
    InvalidTransferInput,
    NumericalFailure,
    NoLambertCandidate,
    DeltaVLimitExceeded,
    ExceptionBudgetExceeded
};

struct SolutionFailure
{
    SolutionFailureReason reason;
    std::optional<std::size_t> legIndex; // Present for a zero-based leg failure.
};

struct LegEvaluation
{
    double deltaVMps;
    DeltaVClassification classification;
    TransferStatus transferStatus;
};

struct SolutionEvaluationResult
{
    // Absolute final arrival in days, available once the structure is valid.
    std::optional<double> missionCompletionDays;
    // Sum of normal and exception leg costs, even if the route exceeds its budget.
    double totalDeltaVMps = 0.0;
    std::size_t exceptionCount = 0;
    std::vector<LegEvaluation> legs; // Indexed by leg in the solution's visit order.
    std::optional<SolutionFailure> firstFailure;

    bool isFeasible() const { return !firstFailure.has_value(); }
};

class SolutionEvaluator
{
public:
    // Checks structure first, then reports every leg if the problem's Delta-V limits are valid.
    static SolutionEvaluationResult evaluate(const Problem &problem, const Solution &solution, DeltaVPolicy policy);
};
