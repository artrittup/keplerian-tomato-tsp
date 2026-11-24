#include "BeamSearch.hpp"
#include "BeamHybrid.hpp"
#include "BeamSelection.hpp"
#include "PartialRoute.hpp"
#include "Problem.hpp"
#include "SolutionEvaluator.hpp"

#include <algorithm>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string_view>
#include <tuple>

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

void testLookaheadBudget()
{
    auto config = singlePath();
    config.selectionStrategy = BeamSelectionStrategy::Lookahead;
    config.destinationsPerRoute = 2;
    config.samplesPerDestination = 6;
    config.maxEvaluations = 15;

    const auto problem = circularProblem(3);
    const auto result = BeamSearch::run(problem, config);
    require(result.bestSolution && result.stats.evaluationsAttempted == 15 &&
            result.stats.lookaheadEvaluations == 1 && result.stats.lookaheadAccepted == 1 &&
            result.stats.lookaheadReused == 1 && result.stats.validationEvaluations == 2 &&
            result.stats.completeFeasibleRoutes == 1,
            "Reuse an exact witness at the construction limit and still reserve full validation");

    config.maxEvaluations = 14;
    const auto limited = BeamSearch::run(problem, config);
    require(!limited.bestSolution && limited.stop == BeamSearchStop::EvaluationLimit &&
            limited.stats.evaluationsAttempted == 12 && limited.stats.lookaheadEvaluations == 0 &&
            limited.stats.validationEvaluations == 0, "Lookahead cannot spend the validation reserve");

    auto blocked = problem;
    for (auto &tomato : blocked.tomatoes)
    {
        tomato.a = 0.0;
    }
    config.maxEvaluations = 100;
    const auto deadEnd = BeamSearch::run(blocked, config);
    require(deadEnd.stats.evaluationsAttempted == 12 && deadEnd.stats.rejectedExtensions == 12 &&
            deadEnd.stats.lookaheadEvaluations == 0 && deadEnd.stop == BeamSearchStop::AttemptsExhausted,
            "An empty shortlist must spend no lookahead calls");
}

void testLookaheadReplay()
{
    auto config = singlePath();
    config.beamWidth = 2;
    config.destinationsPerRoute = 4;
    config.samplesPerDestination = 8;
    config.seed = 7;
    config.maxEvaluations = 350;
    config.selectionStrategy = BeamSelectionStrategy::Lookahead;
    config.timingStrategy = BeamTimingStrategy::Stratified;
    const auto problem = circularProblem();
    const auto a = BeamSearch::run(problem, config);
    const auto b = BeamSearch::run(problem, config);
    require(a.bestSolution && b.bestSolution &&
            a.bestSolution->visitingOrder == b.bestSolution->visitingOrder &&
            a.bestSolution->departureTimes == b.bestSolution->departureTimes &&
            a.bestSolution->timesOfFlight == b.bestSolution->timesOfFlight &&
            a.stats.evaluationsAttempted == b.stats.evaluationsAttempted &&
            a.stats.lookaheadEvaluations > 0 && a.stats.lookaheadEvaluations == b.stats.lookaheadEvaluations &&
            a.stats.lookaheadReused > 0 && a.stats.lookaheadReused == b.stats.lookaheadReused &&
            a.stats.peakBeamSize <= config.beamWidth, "Fixed seeds must reproduce funded lookahead and its route");

    std::size_t primary = 0, probes = 0;
    for (const auto &depth : a.stats.byDepth)
    {
        require(depth.lookaheadEvaluations <= depth.evaluations / 10 &&
                depth.accepted == depth.retained + depth.sameEndpointPruned + depth.widthPruned,
                "The per-level probe quota and pruning accounting must hold");
        primary += depth.evaluations;
        probes += depth.lookaheadEvaluations;
    }
    require(primary + probes + a.stats.validationEvaluations == a.stats.evaluationsAttempted &&
            probes == a.stats.lookaheadAccepted + a.stats.lookaheadRejected &&
            a.stats.lookaheadAccepted == a.stats.lookaheadReused,
            "Probe calls, promoted witnesses and validation must reconcile without a second call");
}

void testLookaheadPromotion()
{
    auto problem = circularProblem();
    for (std::size_t i = 2; i < problem.tomatoes.size(); ++i)
    {
        problem.tomatoes[i].a = 20000000.0;
    }
    // The two-tomato orbit can produce an inexpensive prefix with no inexpensive next move.
    auto config = singlePath();
    config.beamWidth = 2;
    config.destinationsPerRoute = 5;
    config.samplesPerDestination = 8;
    config.lookaheadProbes = 3;
    config.maxEvaluations = 500;
    config.selectionStrategy = BeamSelectionStrategy::Lookahead;
    config.timingStrategy = BeamTimingStrategy::Stratified;
    for (std::uint64_t seed = 0; seed < 16; ++seed)
    {
        config.seed = seed;
        const auto result = BeamSearch::run(problem, config);
        if (result.stats.lookaheadPromoted != 0)
        {
            require(result.stats.lookaheadAccepted > 0 && result.stats.lookaheadRejected > 0 &&
                    result.stats.lookaheadReused > 0 && result.stats.deepestPrefix == 4 &&
                    !result.bestSolution, "A sampled continuation can promote a prefix without making the route feasible");
            return;
        }
    }
    require(false, "Lookahead should promote a witnessed prefix over a higher-ranked unconfirmed prefix");
}

void testHybridSampling()
{
    const auto problem = circularProblem(4);
    TransferCache cache(problem, 512);
    const auto parent = PartialRoute::start(cache.problem(), 0, DeltaVPolicy::Strict);
    require(parent.has_value(), "Expected a valid cache-owned prefix");
    auto config = singlePath();
    config.samplesPerDestination = 32;
    config.hybridRefinementPercent = 50;
    config.hybridRefinementRegions = 3;
    config.hybridExplorationPercent = 50;
    TimingSampler sampler(7);
    const auto batch = beam_detail::HybridTiming::expand(cache, *parent, {1, 2, 3}, sampler, config, 0.5, 96, {});
    require(batch.broadCalls == 48 && batch.refinementCalls > 0 && batch.refinementCalls <= 48 &&
            batch.refinedRegions == 3, "Broad and refined work must share the allowance across multiple regions");
    std::set<std::tuple<int, double, double>> checked;
    for (const auto &proposal : batch.proposals)
    {
        if (!proposal.extension.route) { continue; }
        const auto &child = *proposal.extension.route;
        const auto &timing = proposal.timing;
        require(checked.emplace(child.prefix().visitingOrder.back(), timing.departureDays, timing.flightDays).second &&
                timing.departureDays >= parent->arrivalDays() && timing.flightDays >= problem.minTimeOfFlight &&
                child.arrivalDays() <= problem.maxTotalTime - 2 * problem.minTimeOfFlight,
                "Only unique, chronological legs with enough time left may be offered");
    }
    require(checked.size() > 48, "Refinement should supply additional validated options on the circular fixture");

    config.hybridRefinementPercent = 0;
    TimingSampler broadOnlySampler(7);
    const auto broadOnly = beam_detail::HybridTiming::expand(cache, *parent, {1, 2, 3}, broadOnlySampler,
                                                           config, 0.5, 96, {});
    require(broadOnly.refinedRegions == 0 && broadOnly.refinementCalls == 0, "A zero refinement share stays broad");
}

void testHybridBudget()
{
    auto config = singlePath();
    config.timingStrategy = BeamTimingStrategy::Hybrid;
    config.maxEvaluations = 4;
    const auto problem = circularProblem(3);
    const auto complete = BeamSearch::run(problem, config);
    require(complete.bestSolution && complete.stats.hybridBroadEvaluations == 2 &&
            complete.stats.validationEvaluations == 2 && complete.stats.evaluationsAttempted == 4 &&
            SolutionEvaluator::evaluate(problem, *complete.bestSolution, config.policy).isFeasible(),
            "Only the independent evaluator may confirm a completed route");

    config.maxEvaluations = 3;
    const auto unverified = BeamSearch::run(problem, config);
    require(!unverified.bestSolution && unverified.stats.completeRoutesFound == 1 &&
            unverified.stats.validationEvaluations == 0 && unverified.stop == BeamSearchStop::EvaluationLimit,
            "A complete prefix without enough validation budget must not be reported as feasible");

    auto invalid = problem;
    for (auto &tomato : invalid.tomatoes) { tomato.a = 0.0; }
    config.maxEvaluations = 1;
    const auto failed = BeamSearch::run(invalid, config);
    require(failed.stats.evaluationsAttempted == 1 && failed.stats.transferFailures == 1 &&
            failed.stats.hybridCacheMisses == 1, "A failed cached solve must still be charged");
}

void testHybridCache()
{
    auto problem = circularProblem(2);
    problem.maxTotalTime = problem.startTime + problem.minTimeOfFlight;
    auto config = singlePath();
    config.timingStrategy = BeamTimingStrategy::Hybrid;
    config.samplesPerDestination = 8;
    config.restartCount = 2;
    config.maxEvaluations = 20;
    const auto result = BeamSearch::run(problem, config);
    const auto &stats = result.stats;
    require(result.bestSolution && stats.completeFeasibleRoutes == 3 && stats.validationEvaluations == 3 &&
            stats.hybridBroadEvaluations == 2 && stats.hybridRefinementEvaluations == 0 &&
            stats.hybridRefinedRegions == 3 && stats.hybridCacheHits >= 4 && stats.hybridCacheMisses == 2 &&
            stats.evaluationsAttempted == 5 && stats.successfulExtensions == 3 && stats.hybridDuplicateProposals > 0,
            "Known exact timings are free on replay, unique per parent, and independently validated");

    config.hybridCacheCapacity = 0;
    const auto disabled = BeamSearch::run(problem, config);
    require(disabled.bestSolution && disabled.stats.hybridCacheHits == 0 &&
            disabled.stats.evaluationsAttempted > stats.evaluationsAttempted &&
            disabled.stats.successfulExtensions == stats.successfulExtensions,
            "With storage disabled, refinement outputs still reach the route checks without a second solve");
}

void testHybridClassification()
{
    auto problem = circularProblem(2);
    problem.tomatoes[1].i = 0.2;
    problem.maxTotalTime = problem.startTime + problem.minTimeOfFlight;
    auto config = singlePath();
    config.timingStrategy = BeamTimingStrategy::Hybrid;
    const auto initial = BeamSearch::run(problem, config);
    const int source = initial.stats.startingTomatoes.front();
    const auto raw = TransferSolver::evaluate(problem, source, 1 - source, problem.startTime, problem.minTimeOfFlight);
    require(raw.status == TransferStatus::Ok && raw.deltaVMps > 1e-6, "Expected a nonzero finite reference cost");

    problem.maxDeltaV = raw.deltaVMps;
    problem.maxExceptionDeltaV = raw.deltaVMps;
    const auto normal = BeamSearch::run(problem, config);
    require(normal.bestSolution && normal.stats.byDepth.front().normalExtensions == 1 &&
            normal.stats.byDepth.front().retainedExceptionsUsed == 0, "The exact normal ceiling is inclusive");

    problem.maxDeltaV = 0.0;
    problem.maxExceptions = 1;
    problem.maxExceptionDeltaV = raw.deltaVMps - 5e-7;
    const auto strict = BeamSearch::run(problem, config);
    config.policy = DeltaVPolicy::PublishedEvaluator;
    const auto published = BeamSearch::run(problem, config);
    require(!strict.bestSolution && strict.stats.deltaVRejections == 1 && published.bestSolution &&
            published.stats.byDepth.front().exceptionExtensions == 1 &&
            published.stats.byDepth.front().retainedExceptionsUsed == 1,
            "Policy tolerance should change the exception classification only under the published evaluator");
    problem.maxExceptions = 0;
    const auto exceeded = BeamSearch::run(problem, config);
    require(!exceeded.bestSolution && exceeded.stats.exceptionBudgetRejections == 1,
            "An accepted physical exception still requires a route allowance");
}

void testHybridReplay()
{
    const auto problem = circularProblem();
    auto config = singlePath();
    config.timingStrategy = BeamTimingStrategy::Hybrid;
    config.selectionStrategy = BeamSelectionStrategy::Lookahead;
    config.seed = 7;
    config.beamWidth = 3;
    config.destinationsPerRoute = 4;
    config.samplesPerDestination = 16;
    config.maxEvaluations = 1000;
    const auto first = BeamSearch::run(problem, config);
    const auto second = BeamSearch::run(problem, config);
    const auto &s = first.stats;
    require(first.bestSolution && second.bestSolution &&
            first.bestSolution->visitingOrder == second.bestSolution->visitingOrder &&
            first.bestSolution->departureTimes == second.bestSolution->departureTimes &&
            first.bestSolution->timesOfFlight == second.bestSolution->timesOfFlight &&
            s.hybridCacheHits == second.stats.hybridCacheHits &&
            s.hybridRefinementEvaluations == second.stats.hybridRefinementEvaluations &&
            s.evaluationsAttempted == second.stats.evaluationsAttempted &&
            s.evaluationsAttempted <= config.maxEvaluations && first.stop == second.stop,
            "Fixed seed must reproduce hybrid outcomes and counters");
    const auto construction = s.hybridBroadEvaluations + s.hybridRefinementEvaluations + s.lookaheadEvaluations;
    require(s.hybridRefinementEvaluations > 0 && s.lookaheadEvaluations > 0 &&
            construction + s.validationEvaluations == s.evaluationsAttempted &&
            construction + s.hybridCacheDenials == s.hybridCacheMisses,
            "Broad sampling, refinement, lookahead and independent validation must reconcile");
    for (const auto &depth : s.byDepth)
    {
        require(depth.normalExtensions == depth.accepted && depth.exceptionExtensions == 0 &&
                depth.retainedExceptionsUsed == 0, "All extensions should be normal on the circular fixture");
    }
}

void testHybridBaseline()
{
    const auto problem = circularProblem();
    auto config = singlePath();
    require(config.timingStrategy == BeamTimingStrategy::Baseline, "Uniform timing is still the default");
    const auto original = BeamSearch::run(problem, config);
    config.hybridRefinementPercent = 100;
    config.hybridRefinementRegions = 0;
    config.hybridExplorationPercent = 101;
    const auto explicitBaseline = BeamSearch::run(problem, config);
    require(original.bestSolution && explicitBaseline.bestSolution &&
            original.bestSolution->visitingOrder == explicitBaseline.bestSolution->visitingOrder &&
            original.bestSolution->departureTimes == explicitBaseline.bestSolution->departureTimes &&
            original.bestSolution->timesOfFlight == explicitBaseline.bestSolution->timesOfFlight &&
            original.stats.evaluationsAttempted == explicitBaseline.stats.evaluationsAttempted &&
            explicitBaseline.stats.hybridCacheMisses == 0, "Hybrid-only settings must not affect legacy timing");
    config.timingStrategy = BeamTimingStrategy::Hybrid;
    require(BeamSearch::run(problem, config).stop == BeamSearchStop::InvalidConfig,
            "A hybrid search must reserve broad probes and reject an invalid exploration percentage");
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
    {"temporal_replay", testTemporalReplay},
    {"lookahead_budget", testLookaheadBudget},
    {"lookahead_replay", testLookaheadReplay},
    {"lookahead_promotion", testLookaheadPromotion},
    {"hybrid_sampling", testHybridSampling},
    {"hybrid_budget", testHybridBudget},
    {"hybrid_cache", testHybridCache},
    {"hybrid_classification", testHybridClassification},
    {"hybrid_replay", testHybridReplay},
    {"hybrid_baseline", testHybridBaseline}
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
