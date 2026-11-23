#include "BeamSearch.hpp"
#include "BeamSelection.hpp"
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

void testStratifiedTiming()
{
    const auto problem = circularProblem();
    const auto route = PartialRoute::start(problem, 0, DeltaVPolicy::Strict);
    require(route.has_value(), "Expected a valid route for timing sampling");
    TimingSampler sampler(42), replay(42);
    require(!sampler.sampleStratified(*route, 0.5, 4, 4) &&
            !sampler.sampleStratified(*route, 0.5, 0, 0), "Reject invalid flight bands");
    for (std::size_t band = 0; band < 4; ++band)
    {
        const auto first = sampler.sampleStratified(*route, 0.5, band, 4);
        const auto again = replay.sampleStratified(*route, 0.5, band, 4);
        require(first && again && first->departureDays == again->departureDays &&
                first->flightDays == again->flightDays, "Seeded bands must reproduce exact timing pairs");
        const double minimum = problem.minTimeOfFlight + static_cast<double>(band) * 0.125;
        require(first->flightDays >= minimum && first->flightDays < minimum + 0.125 &&
                first->departureDays >= problem.startTime &&
                first->departureDays + first->flightDays <= 2.625, "Sample must fit its flight band and slack");
    }

    BeamSearchConfig config;
    config.seed = 42;
    config.beamWidth = 2;
    config.destinationsPerRoute = 3;
    config.samplesPerDestination = 4;
    config.restartCount = 1;
    config.maxEvaluations = 300;
    config.timingStrategy = BeamTimingStrategy::Stratified;
    const auto a = BeamSearch::run(problem, config);
    const auto b = BeamSearch::run(problem, config);
    require(a.bestSolution && b.bestSolution &&
            a.bestSolution->visitingOrder == b.bestSolution->visitingOrder &&
            a.bestSolution->departureTimes == b.bestSolution->departureTimes &&
            a.bestSolution->timesOfFlight == b.bestSolution->timesOfFlight &&
            a.stats.evaluationsAttempted == b.stats.evaluationsAttempted &&
            a.stats.byDepth.size() == b.stats.byDepth.size(), "Stratified search must reproduce its route and counters");
}

void testDiagnostics()
{
    BeamSearchConfig config;
    config.beamWidth = 2;
    config.destinationsPerRoute = 4;
    config.samplesPerDestination = 3;
    config.restartCount = 0;
    config.maxEvaluations = 300;
    const auto problem = circularProblem();
    const auto result = BeamSearch::run(problem, config);
    const auto &stats = result.stats;
    std::size_t calls = 0, accepted = 0, rejected = 0, endpointPrunes = 0, widthPrunes = 0;
    for (const auto &depth : stats.byDepth)
    {
        calls += depth.evaluations;
        accepted += depth.accepted;
        rejected += depth.rejected;
        endpointPrunes += depth.sameEndpointPruned;
        widthPrunes += depth.widthPruned;
        require(depth.accepted == depth.retained + depth.sameEndpointPruned + depth.widthPruned,
                "Every accepted extension must be retained or pruned once");
        require(depth.rejected == depth.invalidTiming + depth.deadlineExceeded + depth.deltaVExceeded +
                                  depth.exceptionBudgetExceeded + depth.orbitalInvalidInput +
                                  depth.noLambertCandidate + depth.numericalFailure +
                                  depth.invalidDestination + depth.duplicateDestination,
                "Rejection causes must account for every rejected extension");
        require(depth.duplicateDestination == 0 && depth.visitedDestinationsSkipped > 0,
                "Previously visited tomatoes should be filtered before extension");
    }
    require(stats.byDepth.size() == 5 && calls + stats.validationEvaluations == stats.evaluationsAttempted &&
            accepted == stats.successfulExtensions && rejected == stats.rejectedExtensions &&
            endpointPrunes > 0 && widthPrunes > 0, "Per-depth diagnostics must reconcile with totals");
    std::size_t waits = 0, flights = 0;
    for (std::size_t i = 0; i < stats.byWait.size(); ++i)
    {
        waits += stats.byWait[i].evaluations;
        flights += stats.byFlight[i].evaluations;
    }
    require(waits == calls && flights == calls, "Each orbital extension must appear in both timing histograms");

    auto malformed = problem;
    for (auto &tomato : malformed.tomatoes)
    {
        tomato.a = 0.0;
    }
    config.maxEvaluations = 1;
    const auto failed = BeamSearch::run(malformed, config);
    require(failed.stop == BeamSearchStop::EvaluationLimit && failed.stats.attemptsLimitedByBudget == 1 &&
            failed.stats.byDepth.size() == 1 && failed.stats.byDepth.front().orbitalInvalidInput == 1 &&
            failed.stats.evaluationsAttempted == 1, "A failed orbital input must be counted and consume the cap");

    auto noTime = problem;
    noTime.maxTotalTime = noTime.startTime;
    config.maxEvaluations = 300;
    const auto closed = BeamSearch::run(noTime, config);
    require(closed.stop == BeamSearchStop::AttemptsExhausted && closed.stats.byDepth.size() == 1 &&
            closed.stats.byDepth.front().destinationsWithoutTiming == 8 &&
            closed.stats.evaluationsAttempted == 0, "A closed time window must be recorded without orbital calls");
}

void testTemporalBins()
{
    using beam_detail::ArrivalCandidate;
    const std::vector<ArrivalCandidate> ranked{{0, 0.25}, {1, 19.0}, {2, 0.5}, {0, 1.5},
                                                {3, 1.75}, {4, 2.5}, {5, 3.5}};
    require(beam_detail::selectTemporal(ranked, 4, 20.0, 1.0) == std::vector<std::size_t>{0, 4, 5, 6},
            "Select distinct endpoints from occupied bins around the best-ranked prefix");
    require(beam_detail::selectTemporal(ranked, 2, 20.0, 1.0) == std::vector<std::size_t>{0, 4},
            "Bin selection must obey beam width");
    require(beam_detail::selectTemporal(ranked, 1, 20.0, 1.0) == std::vector<std::size_t>{0},
            "Width one must keep the top-ranked prefix");

    const std::vector<ArrivalCandidate> late{{0, 1.0}, {1, 1.9}, {2, 1.2}};
    require(beam_detail::selectTemporal(late, 2, 2.0, 1.0) == std::vector<std::size_t>{0, 2},
            "A tight deadline must shrink the preferred bins");
    const std::vector<ArrivalCandidate> sameEndpoint{{0, 1.0}, {0, 2.0}, {1, 2.0}};
    require(beam_detail::selectTemporal(sameEndpoint, 4, 20.0, 1.0) == std::vector<std::size_t>{0, 2},
            "Fallback must preserve unique endpoints even when the beam cannot be filled");
}

void testTemporalReplay()
{
    auto config = singlePath();
    config.beamWidth = 3;
    config.destinationsPerRoute = 3;
    config.samplesPerDestination = 4;
    config.maxEvaluations = 400;
    config.seed = 7;
    config.selectionStrategy = BeamSelectionStrategy::Temporal;
    config.arrivalBinDays = 0.125;
    const auto problem = circularProblem();
    for (const auto timing : {BeamTimingStrategy::Baseline, BeamTimingStrategy::Stratified})
    {
        config.timingStrategy = timing;
        const auto a = BeamSearch::run(problem, config);
        const auto b = BeamSearch::run(problem, config);
        require(a.bestSolution && b.bestSolution &&
                a.bestSolution->visitingOrder == b.bestSolution->visitingOrder &&
                a.bestSolution->departureTimes == b.bestSolution->departureTimes &&
                a.bestSolution->timesOfFlight == b.bestSolution->timesOfFlight &&
                a.stats.evaluationsAttempted == b.stats.evaluationsAttempted &&
                a.stats.evaluationsAttempted <= config.maxEvaluations &&
                a.stats.peakBeamSize <= config.beamWidth && a.stats.byDepth.size() == b.stats.byDepth.size(),
                "Temporal selection must reproduce a bounded feasible search under either timing mode");
        for (std::size_t i = 0; i < a.stats.byDepth.size(); ++i)
        {
            const auto &depth = a.stats.byDepth[i];
            std::size_t count = 0;
            for (const auto bin : depth.retainedArrivalBins)
            {
                count += bin;
            }
            require(count == depth.retained &&
                    depth.retainedArrivalBins == b.stats.byDepth[i].retainedArrivalBins &&
                    depth.accepted == depth.retained + depth.sameEndpointPruned + depth.widthPruned,
                    "Arrival histograms and pruning counts must reconcile and repeat");
        }
    }
    config.arrivalBinDays = 0.0;
    require(BeamSearch::run(problem, config).stop == BeamSearchStop::InvalidConfig, "Bin width must be positive");
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
    {"singleton", testSingleton},
    {"stratified", testStratifiedTiming},
    {"diagnostics", testDiagnostics},
    {"temporal_bins", testTemporalBins},
    {"temporal_replay", testTemporalReplay}
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
