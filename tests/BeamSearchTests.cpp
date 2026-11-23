#include "BeamSearch.hpp"
#include "PartialRoute.hpp"
#include "Problem.hpp"
#include "SolutionEvaluator.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace
{
void require(bool condition, const char *message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

Problem circularProblem(std::size_t count = 6)
{
    Problem problem;
    problem.tomatoes.assign(count, Tomato{10000000.0, 0.0, 0.0, 0.0, 0.0, 0.0});
    problem.startTime = 2.0;
    problem.minTimeOfFlight = 0.125;
    problem.maxTotalTime = 4.5;
    problem.maxDeltaV = 0.01;
    problem.maxExceptionDeltaV = 1.0;
    return problem;
}

BeamSearchConfig singlePath()
{
    BeamSearchConfig config;
    config.beamWidth = 1;
    config.destinationsPerRoute = 1;
    config.samplesPerDestination = 1;
    config.restartCount = 0;
    config.maxEvaluations = 100;
    return config;
}

void testDeterministic()
{
    const auto problem = circularProblem();
    BeamSearchConfig config;
    config.seed = 7;
    config.beamWidth = 3;
    config.destinationsPerRoute = 3;
    config.samplesPerDestination = 3;
    config.restartCount = 1;
    config.maxEvaluations = 400;
    const auto first = BeamSearch::run(problem, config);
    const auto repeat = BeamSearch::run(problem, config);
    require(first.bestSolution && repeat.bestSolution, "Both seeded runs should find a route");
    require(first.bestSolution->visitingOrder == repeat.bestSolution->visitingOrder &&
            first.bestSolution->departureTimes == repeat.bestSolution->departureTimes &&
            first.bestSolution->timesOfFlight == repeat.bestSolution->timesOfFlight &&
            first.bestCompletionDays == repeat.bestCompletionDays, "Fixed seed must reproduce the route");
    const auto &a = first.stats;
    const auto &b = repeat.stats;
    require(a.startingTomatoes == b.startingTomatoes && a.evaluationsAttempted == b.evaluationsAttempted &&
            a.successfulExtensions == b.successfulExtensions && a.rejectedExtensions == b.rejectedExtensions &&
            a.completeRoutesFound == b.completeRoutesFound && a.completeRoutesValidated == b.completeRoutesValidated &&
            a.deepestPrefix == b.deepestPrefix && first.stop == repeat.stop, "Fixed seed must reproduce search statistics");
    config.seed = 99;
    const auto different = BeamSearch::run(problem, config);
    require(different.stats.startingTomatoes != a.startingTomatoes && first.config.seed == 7,
            "A different seed should change the starting order");

    const auto prefix = PartialRoute::start(problem, 0, DeltaVPolicy::Strict);
    require(prefix.has_value(), "Expected a valid prefix");
    TimingSampler timing(7);
    const auto capped = timing.sample(*prefix, 0.25);
    require(capped && capped->departureDays + capped->flightDays <= 2.375,
            "Short-window timing must cap the total wait and flight extra");
}

void testWidth()
{
    const auto problem = circularProblem();
    auto config = singlePath();
    config.beamWidth = 2;
    config.destinationsPerRoute = 6;
    config.samplesPerDestination = 2;
    config.maxEvaluations = 400;
    const auto result = BeamSearch::run(problem, config);
    require(result.stats.peakBeamSize == 2 && result.stats.startingTomatoes.size() == 2 &&
            result.stats.startingTomatoes.front() != result.stats.startingTomatoes.back(),
            "Beam width must cap the frontier and admit distinct starts");
    require(result.stats.successfulExtensions > 2 && result.bestSolution,
            "The width test must generate enough children to exercise pruning");
    config.beamWidth = 1;
    const auto greedy = BeamSearch::run(problem, config);
    require(greedy.stats.peakBeamSize == 1 && greedy.bestSolution, "Width one must behave as a single-prefix search");
}

void testBudget()
{
    const auto problem = circularProblem(3);
    auto config = singlePath();
    config.maxEvaluations = 4;
    config.restartCount = 9;
    const auto exact = BeamSearch::run(problem, config);
    require(exact.bestSolution && exact.stats.evaluationsAttempted == 4 && exact.stats.validationEvaluations == 2 &&
            exact.stats.attemptsStarted == 1, "Exactly two construction and two final-validation calls must fit");

    config.maxEvaluations = 3;
    const auto unverified = BeamSearch::run(problem, config);
    require(!unverified.bestSolution && unverified.stats.completeRoutesFound == 1 &&
            unverified.stats.completeRoutesValidated == 0 && unverified.stats.evaluationsAttempted == 2 &&
            unverified.stop == BeamSearchStop::EvaluationLimit, "A complete prefix needs enough budget for validation");

    config.maxEvaluations = 0;
    const auto emptyBudget = BeamSearch::run(problem, config);
    require(emptyBudget.stats.evaluationsAttempted == 0 && emptyBudget.stats.deepestPrefix == 1 &&
            !emptyBudget.bestSolution && emptyBudget.stop == BeamSearchStop::EvaluationLimit,
            "A zero-call limit may initialize a prefix");

    auto malformed = problem;
    for (auto &tomato : malformed.tomatoes)
    {
        tomato.a = 0.0;
    }
    config.maxEvaluations = 1;
    config.destinationsPerRoute = 2;
    const auto failed = BeamSearch::run(malformed, config);
    require(failed.stats.evaluationsAttempted == 1 && failed.stats.rejectedExtensions == 1 &&
            failed.stop == BeamSearchStop::EvaluationLimit, "A failed transfer must consume one budget unit");
}

void testDuplicate()
{
    const auto problem = circularProblem();
    auto config = singlePath();
    config.destinationsPerRoute = 100;
    const auto result = BeamSearch::run(problem, config);
    require(result.bestSolution && result.bestSolution->isStructurallyValid(problem),
            "The final visit order must be a permutation");
    require(result.stats.successfulExtensions + result.stats.rejectedExtensions == 15 &&
            result.stats.evaluationsAttempted == 20, "Each level must propose only its remaining tomatoes");
}

void testFailure()
{
    auto problem = circularProblem();
    for (auto &tomato : problem.tomatoes)
    {
        tomato.a = 0.0;
    }
    auto config = singlePath();
    config.beamWidth = 2;
    config.restartCount = 2;
    const auto result = BeamSearch::run(problem, config);
    require(!result.bestSolution && !result.bestCompletionDays && result.stop == BeamSearchStop::AttemptsExhausted,
            "Exhausted starts must return no solution");
    const auto &stats = result.stats;
    require(stats.attemptsStarted == 3 && stats.startingTomatoes.size() == 6 && stats.deepestPrefix == 1 &&
            stats.evaluationsAttempted == 6 && stats.transferFailures == 6 && stats.rejectedExtensions == 6 &&
            stats.completeRoutesFound == 0, "Failed transfers and restarts must be counted");
    auto roots = stats.startingTomatoes;
    std::sort(roots.begin(), roots.end());
    require(std::adjacent_find(roots.begin(), roots.end()) == roots.end(), "Restarts should explore unused starts first");

    config.beamWidth = 0;
    require(BeamSearch::run(problem, config).stop == BeamSearchStop::InvalidConfig, "Reject zero beam width");
    config.beamWidth = 1;
    require(BeamSearch::run(Problem{}, config).stop == BeamSearchStop::InvalidProblem, "Reject an empty problem");
}

void testFeasible()
{
    const auto problem = circularProblem(4);
    auto config = singlePath();
    config.beamWidth = 3;
    config.destinationsPerRoute = 3;
    config.samplesPerDestination = 4;
    config.policy = DeltaVPolicy::PublishedEvaluator;
    config.maxEvaluations = 300;
    const auto result = BeamSearch::run(problem, config);
    require(result.bestSolution && result.bestCompletionDays && result.stats.deepestPrefix == 4 &&
            result.stats.completeRoutesFound >= result.stats.completeRoutesValidated &&
            result.stats.completeRoutesValidated >= 1 && result.stats.validationEvaluations % 3 == 0,
            "A reported route must have completed full validation");
    const auto evaluation = SolutionEvaluator::evaluate(problem, *result.bestSolution, config.policy);
    require(evaluation.isFeasible() && evaluation.missionCompletionDays == result.bestCompletionDays &&
            result.bestSolution->departureTimes.front() >= problem.startTime,
            "The returned solution must pass independent validation with the same policy");
}

void testSingleton()
{
    auto problem = circularProblem(1);
    problem.maxTotalTime = problem.startTime;
    BeamSearchConfig config;
    config.maxEvaluations = 0;
    const auto result = BeamSearch::run(problem, config);
    require(result.bestSolution && result.bestSolution->visitingOrder == std::vector<int>{0} &&
            result.bestSolution->departureTimes.empty() && result.bestSolution->timesOfFlight.empty() &&
            result.bestCompletionDays == problem.startTime && result.stats.evaluationsAttempted == 0 &&
            result.stats.completeRoutesValidated == 1 && result.stats.attemptsStarted == 1,
            "A singleton route must finish at the start epoch with zero calls");
}

struct TestCase
{
    std::string_view name;
    void (*run)();
};

constexpr TestCase cases[] = {
    {"deterministic", testDeterministic},
    {"width", testWidth},
    {"budget", testBudget},
    {"duplicate", testDuplicate},
    {"failure", testFailure},
    {"feasible", testFeasible},
    {"singleton", testSingleton}
};
}

int main(int argc, char *argv[])
{
    if (argc == 2)
    {
        for (const auto &test : cases)
        {
            if (test.name == argv[1])
            {
                try
                {
                    test.run();
                    return 0;
                }
                catch (const std::exception &error)
                {
                    std::cerr << test.name << ": " << error.what() << '\n';
                    return 1;
                }
            }
        }
    }
    std::cerr << "Unknown beam-search test\n";
    return 1;
}
