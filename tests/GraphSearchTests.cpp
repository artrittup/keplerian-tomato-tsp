#include "GraphSearch.hpp"
#include "SolutionEvaluator.hpp"

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

Problem circularProblem(std::size_t count = 3)
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

GraphSearchConfig seededConfig()
{
    GraphSearchConfig config;
    config.samplesPerDestination = 0;
    config.startTomato = 0;
    config.maxEvaluations = 100;
    return config;
}

void testBacktrack(const char *path)
{
    const auto problem = circularProblem();
    const std::vector<TransferQuery> arcs{
        {0, 1, 2.0, 0.125}, {0, 2, 2.125, 0.125}, {2, 1, 2.25, 0.125}
    };
    auto config = seededConfig();
    config.maxBacktracks = 0;
    const auto first = GraphSearch::run(problem, config, arcs);
    require(!first.bestSolution && first.stop == GraphSearchStop::BacktrackLimit &&
            first.stats.backtracks == 0 && first.stats.deepestPrefix == 2,
            "The earliest edge should lead to a dead end");

    config.maxBacktracks = 1;
    const auto second = GraphSearch::run(problem, config, arcs);
    require(second.bestSolution && second.bestSolution->visitingOrder == std::vector<int>{0, 2, 1} &&
            second.stats.backtracks == 1 && second.stats.completeFeasibleRoutes == 1 &&
            second.stats.validationCalls == 2 && second.stats.orbitalCalls == 5,
            "One undo should restore the root and try its other destination");

    Problem timingProblem;
    require(timingProblem.loadFromFile(path) && timingProblem.tomatoes.size() == 49, "Could not load ESA easy");
    // ESA easy indices 1 and 48 are the pair used by the Phase 3 normal-limit fixtures.
    const auto source = timingProblem.tomatoes[1];
    const auto target = timingProblem.tomatoes[48];
    timingProblem.tomatoes = {source, target, target};
    timingProblem.maxExceptions = 1;
    const double departure = 0.125;
    const double earlyFlight = 4.9994;
    const std::vector<TransferQuery> timings{
        {0, 1, departure, 4.9996}, {0, 1, departure, earlyFlight}, {1, 2, departure + earlyFlight, 0.125}
    };
    // The later normal leg ranks first, but misses the only onward departure. The earlier leg costs an exception.
    config.maxBacktracks = 0;
    const auto late = GraphSearch::run(timingProblem, config, timings);
    require(!late.bestSolution && late.stop == GraphSearchStop::BacktrackLimit && late.stats.deepestPrefix == 2,
            "The first timing choice must miss the onward transfer");
    config.maxBacktracks = 1;
    const auto early = GraphSearch::run(timingProblem, config, timings);
    require(early.bestSolution && early.stats.backtracks == 1 && early.stats.validationCalls == 2 &&
            early.bestSolution->visitingOrder == std::vector<int>{0, 1, 2} &&
            early.bestSolution->timesOfFlight.front() == earlyFlight,
            "Backtracking must also recover a different timing for the same destination");
}

void testReuse(const char *)
{
    auto problem = circularProblem(4);
    problem.minTimeOfFlight = 0.25;
    problem.maxTotalTime = 2.75;
    const std::vector<TransferQuery> arcs{{0, 1, 2.0, 0.25}, {1, 0, 2.0, 0.25},
                                         {1, 2, 2.25, 0.25}, {0, 2, 2.25, 0.25}, {2, 3, 2.5, 0.25}};
    auto config = seededConfig();
    config.startTomato.reset();
    const auto result = GraphSearch::run(problem, config, arcs);
    require(result.stats.completeFeasibleRoutes == 2 && result.stats.cache.solverCalls == arcs.size() &&
            result.stats.validationCalls == 6 && result.stats.reusedEdges >= 6,
            "Two histories must share the stored last leg and pay only for independent validation again");

    problem.tomatoes[3].a = 0.0;
    config.samplesPerDestination = 1;
    // Every state samples at most one query per target. A repeat of a failed query must come from another history.
    const auto failures = GraphSearch::run(problem, config, {{0, 1, 2.0, 0.25}, {2, 1, 2.0, 0.25}});
    require(!failures.bestSolution && failures.stats.cache.hits > 0 &&
            failures.stats.cache.solverCalls == failures.stats.cache.misses &&
            failures.stats.orbitalCalls == failures.stats.cache.solverCalls,
            "Convergent histories must reuse cached failures without spending another orbital call");
}

void testVisitOnce(const char *)
{
    const auto result = GraphSearch::run(circularProblem(), seededConfig(),
                                        {{0, 1, 2.0, 0.25}, {1, 0, 2.25, 0.25}});
    require(!result.bestSolution && result.stats.visitedRejections == 1 && result.stats.deepestPrefix == 2 &&
            result.stats.validationCalls == 0, "A later node for a previously visited tomato must be rejected");
}

void testExceptions(const char *path)
{
    Problem problem;
    require(problem.loadFromFile(path) && problem.tomatoes.size() == 49, "Could not load ESA easy");
    problem.tomatoes.resize(2);
    const TransferQuery boundary{0, 1, 0.125, 1.8478637404934113};
    auto config = seededConfig();
    const auto strict = GraphSearch::run(problem, config, {boundary});
    config.policy = DeltaVPolicy::PublishedEvaluator;
    const auto published = GraphSearch::run(problem, config, {boundary});
    require(!strict.bestSolution && published.bestSolution && published.stats.validationCalls == 1,
            "The policy must agree during graph insertion and full validation");

    config.policy = DeltaVPolicy::Strict;
    problem.tomatoes.push_back(problem.tomatoes.front());
    problem.maxExceptionDeltaV = 2000.0;
    problem.maxExceptions = 1;
    const std::vector<TransferQuery> arcs{{0, 1, 0.125, 5.0}, {1, 2, 5.125, 5.0}};
    const auto over = GraphSearch::run(problem, config, arcs);
    require(over.stats.graphEdges == 2 && over.stats.exceptionRejections == 1 &&
            over.stats.deepestPrefix == 2 && !over.bestSolution,
            "A locally admissible exception must still fit the current route budget");
    problem.maxExceptions = 2;
    const auto exact = GraphSearch::run(problem, config, arcs);
    require(exact.bestSolution && exact.stats.completeFeasibleRoutes == 1 && exact.stats.validationCalls == 2,
            "A route may use exactly its exception budget");
}

void testReplay(const char *)
{
    const auto problem = circularProblem(4);
    GraphSearchConfig config;
    config.seed = 19;
    config.maxEvaluations = 300;
    config.maxStates = 200;
    config.samplesPerDestination = 4;
    const auto a = GraphSearch::run(problem, config);
    const auto b = GraphSearch::run(problem, config);
    require(a.bestSolution && b.bestSolution && a.bestCompletionDays == b.bestCompletionDays &&
            a.bestSolution->visitingOrder == b.bestSolution->visitingOrder &&
            a.bestSolution->departureTimes == b.bestSolution->departureTimes &&
            a.bestSolution->timesOfFlight == b.bestSolution->timesOfFlight &&
            a.stats.orbitalCalls == b.stats.orbitalCalls && a.stats.cache.hits == b.stats.cache.hits &&
            a.stats.graphEdges == b.stats.graphEdges && a.stats.graphNodes == b.stats.graphNodes &&
            a.stats.expandedStates == b.stats.expandedStates && a.stats.backtracks == b.stats.backtracks &&
            a.stats.completeFeasibleRoutes == b.stats.completeFeasibleRoutes && a.stop == b.stop,
            "Fixed seeds must reproduce graph construction and route choices");
}

void testLimits(const char *)
{
    const auto problem = circularProblem();
    const std::vector<TransferQuery> arcs{{0, 1, 2.0, 0.25}, {1, 2, 2.25, 0.25}};
    auto config = seededConfig();
    config.maxEvaluations = 3;
    const auto unverified = GraphSearch::run(problem, config, arcs);
    require(!unverified.bestSolution && unverified.stats.deepestPrefix == 3 &&
            unverified.stats.orbitalCalls == 2 && unverified.stats.validationCalls == 0 &&
            unverified.stop == GraphSearchStop::EvaluationLimit, "A complete path needs its full validation budget");
    config.maxEvaluations = 4;
    config.cacheCapacity = 1;
    const auto exact = GraphSearch::run(problem, config, arcs);
    require(exact.bestSolution && exact.stats.orbitalCalls == 4 && exact.stats.validationCalls == 2 &&
            exact.stats.cache.entries == 1 && exact.stats.cache.evictions == 1,
            "The budget must include validation and the graph must retain evicted transfers");

    config.maxStates = 1;
    const auto limited = GraphSearch::run(problem, config, arcs);
    require(limited.stop == GraphSearchStop::StateLimit && limited.stats.expandedStates == 1 &&
            !limited.bestSolution, "Opening a child must not exceed the state limit");

    config = seededConfig();
    config.maxNodes = 2;
    const auto nodes = GraphSearch::run(problem, config, arcs);
    require(nodes.stop == GraphSearchStop::GraphCapacity && nodes.stats.graphNodes == 2 &&
            nodes.stats.graphEdges == 1 && nodes.stats.orbitalCalls == 1 && nodes.stats.deepestPrefix == 2,
            "The node cap must deny the second edge while leaving the existing edge traversable");
    config = seededConfig();
    config.maxEdges = 1;
    const auto edges = GraphSearch::run(problem, config, arcs);
    require(edges.stop == GraphSearchStop::GraphCapacity && edges.stats.graphEdges == 1 &&
            edges.stats.orbitalCalls == 1 && edges.stats.deepestPrefix == 2,
            "The edge cap must prevent an orbital evaluation when no edge can be stored");

    config = seededConfig();
    config.maxEvaluations = 1;
    config.samplesPerDestination = 4;
    const auto sampled = GraphSearch::run(problem, config);
    require(sampled.stats.orbitalCalls == 1 && sampled.stats.cache.solverCalls == 1 &&
            sampled.stats.validationCalls == 0 && sampled.stop == GraphSearchStop::EvaluationLimit,
            "Fresh queries must stop when the actual-call budget is used");
}

void testValidation(const char *)
{
    const auto problem = circularProblem();
    const auto config = seededConfig();
    const auto result = GraphSearch::run(problem, config, {{0, 1, 2.125, 0.25}, {1, 2, 2.625, 0.25}});
    require(result.bestSolution && result.bestSolution->visitingOrder == std::vector<int>{0, 1, 2} &&
            result.bestSolution->departureTimes == std::vector<double>{2.125, 2.625} &&
            result.bestSolution->timesOfFlight == std::vector<double>{0.25, 0.25} &&
            result.bestCompletionDays == 2.875 && result.stats.validationCalls == 2,
            "Reconstruct departure and duration exactly, including the wait");
    require(SolutionEvaluator::evaluate(problem, *result.bestSolution, config.policy).isFeasible(),
            "The returned path must satisfy independent feasibility validation");

    auto singleton = circularProblem(1);
    singleton.maxTotalTime = singleton.startTime;
    auto zero = config;
    zero.maxEvaluations = 0;
    const auto one = GraphSearch::run(singleton, zero);
    require(one.bestSolution && one.bestCompletionDays == singleton.startTime && one.stats.orbitalCalls == 0,
            "A one-tomato mission has no orbital calls");
}

struct TestCase
{
    std::string_view name;
    void (*run)(const char *);
};

constexpr TestCase cases[] = {
    {"backtrack", testBacktrack}, {"reuse", testReuse}, {"visit_once", testVisitOnce},
    {"exceptions", testExceptions}, {"replay", testReplay}, {"limits", testLimits},
    {"validation", testValidation}
};
}

int main(int argc, char *argv[])
{
    if (argc == 3)
    {
        for (const auto &test : cases)
        {
            if (test.name == argv[1])
            {
                try
                {
                    test.run(argv[2]);
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
    std::cerr << "Unknown graph search test or missing instance path\n";
    return 1;
}
