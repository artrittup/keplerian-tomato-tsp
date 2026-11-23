#include "Problem.hpp"
#include "Solution.hpp"
#include "SolutionEvaluator.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <string>

namespace
{
using Classification = DeltaVClassification;
using Policy = DeltaVPolicy;
using Failure = SolutionFailureReason;

bool check(bool condition, const char *message)
{
    if (!condition)
    {
        std::cerr << message << '\n';
    }
    return condition;
}

bool hasFailure(const SolutionEvaluationResult &result, Failure reason,
                std::optional<std::size_t> legIndex = std::nullopt)
{
    return !result.isFeasible() && result.firstFailure && result.firstFailure->reason == reason &&
           result.firstFailure->legIndex == legIndex;
}

Problem circularProblem()
{
    Problem problem;
    // The tomatoes share a circular orbit, so following it costs almost zero.
    problem.tomatoes.assign(3, Tomato{10000000.0, 0.0, 0.0, 0.0, 0.0, 0.0});
    problem.startTime = 2.0;
    problem.minTimeOfFlight = 0.125;
    problem.maxTotalTime = 3.0;
    problem.maxDeltaV = 0.01;
    problem.maxExceptionDeltaV = 1.0;
    return problem;
}

bool testStructure()
{
    Problem problem = circularProblem();
    for (const Solution &solution : {Solution{{0, 1, 1}, {2.0, 2.25}, {0.25, 0.25}},
                                     {{0, 1, 3}, {2.0, 2.25}, {0.25, 0.25}},
                                     {{0, 1, 2}, {2.0}, {0.25, 0.25}}})
    {
        const auto result = SolutionEvaluator::evaluate(problem, solution, Policy::Strict);
        if (!check(hasFailure(result, Failure::InvalidStructure) && !result.missionCompletionDays &&
                   result.legs.empty() && result.totalDeltaVMps == 0.0 && result.exceptionCount == 0,
                   "Structural rejection must happen before evaluating any legs"))
        {
            return false;
        }
    }

    const Solution valid{{0, 1, 2}, {2.0, 2.25}, {0.25, 0.25}};
    problem.maxExceptions = -1;
    const auto negativeBudget = SolutionEvaluator::evaluate(problem, valid, Policy::Strict);
    problem.maxExceptions = 0;
    problem.maxExceptionDeltaV = 0.0;
    const auto reversedLimits = SolutionEvaluator::evaluate(problem, valid, Policy::Strict);
    return check(hasFailure(negativeBudget, Failure::InvalidDeltaVLimits) && negativeBudget.legs.empty(),
                 "Negative exception budget must be rejected") &&
           check(hasFailure(reversedLimits, Failure::InvalidDeltaVLimits) && reversedLimits.legs.empty(),
                 "Exception ceiling below the normal limit must be rejected");
}

bool testChronology()
{
    const Problem problem = circularProblem();
    Solution solution{{0, 1, 2}, {2.0, 2.25}, {0.25, 0.25}};
    const auto immediate = SolutionEvaluator::evaluate(problem, solution, Policy::Strict);
    solution.departureTimes[1] = 2.75;
    const auto waiting = SolutionEvaluator::evaluate(problem, solution, Policy::Strict);
    // PublishedEvaluator only selects the Delta-V policy. Structure still requires exact chronology.
    solution.departureTimes[1] = std::nextafter(2.25, 0.0);
    const auto overlap = SolutionEvaluator::evaluate(problem, solution, Policy::PublishedEvaluator);
    return check(immediate.isFeasible() && immediate.missionCompletionDays == 2.5,
                 "Departure exactly at the previous arrival must be allowed") &&
           check(waiting.isFeasible() && waiting.missionCompletionDays == problem.maxTotalTime,
                 "Waiting must be reflected in completion time at the inclusive deadline") &&
           check(hasFailure(overlap, Failure::InvalidStructure) && overlap.legs.empty(),
                 "Overlapping legs must be rejected before orbital evaluation");
}

bool testNormal()
{
    const Problem problem = circularProblem();
    const Solution solution{{2, 0, 1}, {2.0, 2.75}, {0.25, 0.25}};
    const auto result = SolutionEvaluator::evaluate(problem, solution, Policy::Strict);
    return check(result.isFeasible() && result.legs.size() == 2 && result.exceptionCount == 0,
                 "Expected a feasible route with two normal legs") &&
           check(result.legs[0].classification == Classification::Normal &&
                 result.legs[1].classification == Classification::Normal &&
                 result.legs[0].transferStatus == TransferStatus::Ok &&
                 result.legs[1].transferStatus == TransferStatus::Ok &&
                 result.totalDeltaVMps == result.legs[0].deltaVMps + result.legs[1].deltaVMps &&
                 result.totalDeltaVMps < 1e-6, "Normal leg costs must be recorded and summed");
}

bool testException(Problem problem)
{
    problem.tomatoes = {problem.tomatoes[1], problem.tomatoes[0]};
    const Solution solution{{1, 0}, {0.125}, {5.0}}; // Phase 3 easy_multirev case, with the indices swapped.
    const auto result = SolutionEvaluator::evaluate(problem, solution, Policy::Strict);
    return check(result.isFeasible() && result.legs.size() == 1 && result.exceptionCount == 1,
                 "Expected the known ESA transfer to use one exception") &&
           check(result.legs[0].classification == Classification::Exception &&
                 result.totalDeltaVMps == result.legs[0].deltaVMps &&
                 std::abs(result.totalDeltaVMps - 417.0047391125741) < 1e-6,
                 "Expected the validated minimum Delta-V for the specified visit order");
}

bool testBudget(Problem problem)
{
    problem.tomatoes = {problem.tomatoes[0], problem.tomatoes[1], problem.tomatoes[0], problem.tomatoes[0]};
    // The returning leg costs about 1564 m/s, so use a custom ceiling for this test.
    problem.maxExceptionDeltaV = 2000.0;
    problem.maxExceptions = 2;
    const Solution solution{{0, 1, 2, 3}, {0.125, 5.125, 10.125}, {5.0, 5.0, 0.25}};
    const auto atLimit = SolutionEvaluator::evaluate(problem, solution, Policy::Strict);
    if (!check(atLimit.isFeasible() && atLimit.exceptionCount == 2 && atLimit.legs.size() == 3,
               "A route may use exactly its exception budget"))
    {
        return false;
    }

    problem.maxExceptions = 1;
    const auto overLimit = SolutionEvaluator::evaluate(problem, solution, Policy::Strict);
    problem.maxExceptions = 0;
    const auto noBudget = SolutionEvaluator::evaluate(problem, solution, Policy::Strict);
    return check(hasFailure(overLimit, Failure::ExceptionBudgetExceeded, 1) && overLimit.exceptionCount == 2 &&
                 overLimit.legs.size() == 3 && overLimit.legs[2].classification == Classification::Normal &&
                 overLimit.totalDeltaVMps == atLimit.totalDeltaVMps &&
                 overLimit.missionCompletionDays == atLimit.missionCompletionDays,
                 "A route over budget must still report all legs and the scheduled completion") &&
           check(hasFailure(noBudget, Failure::ExceptionBudgetExceeded, 0) && noBudget.exceptionCount == 2,
                 "A later exception must not overwrite the first budget failure");
}

bool testPolicies(Problem problem)
{
    problem.tomatoes = {problem.tomatoes[0], problem.tomatoes[1]};
    // Phase 3 nominal_high_offset_hi is above the strict ceiling and inside the scorer's allowance.
    Solution solution{{0, 1}, {0.125}, {1.8478637404934113}};
    const auto strict = SolutionEvaluator::evaluate(problem, solution, Policy::Strict);
    const auto compatible = SolutionEvaluator::evaluate(problem, solution, Policy::PublishedEvaluator);
    // Phase 3 published_high_offset_hi is above both ceilings.
    solution.timesOfFlight[0] = 1.8478637401696303;
    const auto beyondAllowance = SolutionEvaluator::evaluate(problem, solution, Policy::PublishedEvaluator);
    return check(hasFailure(strict, Failure::DeltaVLimitExceeded, 0) && strict.legs.size() == 1 &&
                 strict.legs[0].classification == Classification::Infeasible &&
                 strict.legs[0].transferStatus == TransferStatus::Ok && strict.totalDeltaVMps == 0.0,
                 "Strict policy must reject the over-ceiling leg and retain its raw cost") &&
           check(compatible.isFeasible() && compatible.legs.size() == 1 && compatible.exceptionCount == 1 &&
                 compatible.legs[0].classification == Classification::Exception &&
                 compatible.totalDeltaVMps == strict.legs[0].deltaVMps,
                 "Published policy must accept the same leg as an exception") &&
           check(hasFailure(beyondAllowance, Failure::DeltaVLimitExceeded, 0),
                 "Published policy must reject a leg above its exception ceiling");
}

bool testSolverFailure()
{
    Problem problem = circularProblem();
    problem.minTimeOfFlight = 0.0;
    problem.tomatoes.push_back(problem.tomatoes.front());
    Solution route{{0, 1, 2, 3}, {2.0, 2.0, 2.75}, {0.0, 1e-18, 0.25}};
    const auto invalid = SolutionEvaluator::evaluate(problem, route, Policy::Strict);
    route.timesOfFlight[0] = 1e-18;
    const auto noCandidate = SolutionEvaluator::evaluate(problem, route, Policy::Strict);

    problem.startTime = 0.0;
    problem.maxTotalTime = 1e21;
    problem.tomatoes = {{14968611.0, 0.01, 1.0, 0.0, 0.0, 0.0},
                        {14978277.26, 0.01, 1.0, 0.0, 0.0, 0.4}};
    const Solution extreme{{0, 1}, {0.0}, {1e20}};
    const auto numerical = SolutionEvaluator::evaluate(problem, extreme, Policy::Strict);

    return check(hasFailure(invalid, Failure::InvalidTransferInput, 0) && invalid.legs.size() == 3 &&
                 invalid.legs[0].transferStatus == TransferStatus::InvalidInput &&
                 invalid.legs[0].classification == Classification::Infeasible &&
                 invalid.legs[0].deltaVMps == std::numeric_limits<double>::infinity() &&
                 invalid.legs[1].transferStatus == TransferStatus::NoFiniteCandidate &&
                 invalid.legs[2].classification == Classification::Normal &&
                 invalid.totalDeltaVMps == invalid.legs[2].deltaVMps && invalid.missionCompletionDays == 3.0,
                 "A solver failure must preserve its reason and allow reporting of later legs") &&
           check(hasFailure(noCandidate, Failure::NoLambertCandidate, 0),
                 "Coincident endpoints must report that there is no Lambert candidate") &&
           check(hasFailure(numerical, Failure::NumericalFailure, 0) && numerical.legs.size() == 1 &&
                 numerical.legs[0].transferStatus == TransferStatus::NumericalFailure,
                 "TransferSolver numerical failures must be reported by the evaluator");
}

bool testSingleton()
{
    Problem problem = circularProblem();
    problem.tomatoes.resize(1);
    problem.maxTotalTime = problem.startTime;
    const Solution solution{{0}, {}, {}};
    const auto result = SolutionEvaluator::evaluate(problem, solution, Policy::Strict);
    return check(result.isFeasible() && result.missionCompletionDays == problem.startTime &&
                 result.legs.empty() && result.totalDeltaVMps == 0.0 && result.exceptionCount == 0,
                 "A one-tomato route must finish at the start time with zero transfers");
}
}

int main(int argc, char **argv)
{
    if (argc != 3)
    {
        std::cerr << "Expected a test case and the ESA easy instance path\n";
        return 1;
    }
    const std::string testCase = argv[1];
    if (testCase == "structure") return testStructure() ? 0 : 1;
    if (testCase == "chronology") return testChronology() ? 0 : 1;
    if (testCase == "normal") return testNormal() ? 0 : 1;
    if (testCase == "solver_failure") return testSolverFailure() ? 0 : 1;
    if (testCase == "singleton") return testSingleton() ? 0 : 1;

    Problem easy;
    if (!check(easy.loadFromFile(argv[2]) && easy.tomatoes.size() == 49, "Could not load the ESA easy instance"))
    {
        return 1;
    }
    if (testCase == "exception") return testException(easy) ? 0 : 1;
    if (testCase == "budget") return testBudget(easy) ? 0 : 1;
    if (testCase == "policies") return testPolicies(easy) ? 0 : 1;

    std::cerr << "Unknown evaluator test case\n";
    return 1;
}
