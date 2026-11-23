#include "SolutionEvaluator.hpp"

#include "Problem.hpp"
#include "Solution.hpp"

SolutionEvaluationResult SolutionEvaluator::evaluate(const Problem &problem, const Solution &solution,
                                                     DeltaVPolicy policy)
{
    SolutionEvaluationResult result;
    if (!solution.isStructurallyValid(problem))
    {
        result.firstFailure = SolutionFailure{SolutionFailureReason::InvalidStructure, std::nullopt};
        return result;
    }

    const std::size_t legCount = solution.departureTimes.size();
    result.missionCompletionDays = legCount == 0 ? problem.startTime
                                                 : solution.departureTimes.back() + solution.timesOfFlight.back();

    // Every valid pair of Delta-V limits classifies a zero-cost leg as normal.
    if (problem.maxExceptions < 0 || classifyDeltaV(problem, 0.0, policy) != DeltaVClassification::Normal)
    {
        result.firstFailure = SolutionFailure{SolutionFailureReason::InvalidDeltaVLimits, std::nullopt};
        return result;
    }
    const auto budget = static_cast<std::size_t>(problem.maxExceptions);

    result.legs.reserve(legCount);
    const auto recordFailure = [&result](SolutionFailureReason reason, std::size_t leg)
    {
        if (!result.firstFailure)
        {
            result.firstFailure = SolutionFailure{reason, leg};
        }
    };

    for (std::size_t leg = 0; leg < legCount; ++leg)
    {
        // Structural validation has already checked the indices and the array lengths.
        const auto source = static_cast<std::size_t>(solution.visitingOrder[leg]);
        const auto destination = static_cast<std::size_t>(solution.visitingOrder[leg + 1]);
        const auto transfer = TransferSolver::evaluate(problem, source, destination,
                                                        solution.departureTimes[leg], solution.timesOfFlight[leg]);
        const auto classification = transfer.status == TransferStatus::Ok
            ? classifyDeltaV(problem, transfer.deltaVMps, policy)
            : DeltaVClassification::Infeasible;
        result.legs.push_back({transfer.deltaVMps, classification, transfer.status});

        switch (transfer.status)
        {
        case TransferStatus::InvalidInput:
            recordFailure(SolutionFailureReason::InvalidTransferInput, leg);
            continue;
        case TransferStatus::NumericalFailure:
            recordFailure(SolutionFailureReason::NumericalFailure, leg);
            continue;
        case TransferStatus::NoFiniteCandidate:
            recordFailure(SolutionFailureReason::NoLambertCandidate, leg);
            continue;
        case TransferStatus::Ok:
            break;
        }

        if (classification == DeltaVClassification::Infeasible)
        {
            recordFailure(SolutionFailureReason::DeltaVLimitExceeded, leg);
            continue;
        }

        result.totalDeltaVMps += transfer.deltaVMps;
        if (classification == DeltaVClassification::Exception)
        {
            ++result.exceptionCount;
            if (result.exceptionCount > budget)
            {
                recordFailure(SolutionFailureReason::ExceptionBudgetExceeded, leg);
            }
        }
    }
    return result;
}
