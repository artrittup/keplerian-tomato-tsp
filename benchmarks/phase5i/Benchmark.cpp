#include "Measurement.hpp"

#include "BeamSearch.hpp"
#include "GraphSearch.hpp"
#include "PartialRoute.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
using Clock = std::chrono::steady_clock;
constexpr std::size_t repeats = 3;
constexpr std::array<std::uint64_t, 3> seeds{0, 1, 7};

struct OrbitalMetrics
{
    std::size_t calls = 0;
    std::size_t normal = 0;
    std::size_t exception = 0;
    std::size_t over = 0;
    std::size_t failure = 0;
    Clock::duration orbitalTime{};
};

OrbitalMetrics orbital;
}

namespace phase5i
{
GraphMetrics graphMetrics;

void recordExpansion(const GraphMetrics &before, bool exceptionRejected, bool empty)
{
    if (empty)
    {
        ++graphMetrics.emptyStates;
        graphMetrics.emptyWithException += exceptionRejected;
        graphMetrics.emptyWithDeadline +=
            graphMetrics.deadlineEdges != before.deadlineEdges || graphMetrics.noTiming != before.noTiming;
        graphMetrics.emptyWithBudget += graphMetrics.budgetBlocks != before.budgetBlocks;
    }
}
}

TransferResult TransferSolver::evaluate(const Problem &problem, std::size_t source, std::size_t destination,
                                        double departure, double flight)
{
    const auto start = Clock::now();
    const auto result = phase5i::evaluateOrbit(problem, source, destination, departure, flight);
    orbital.orbitalTime += Clock::now() - start;
    ++orbital.calls;
    if (result.status == TransferStatus::Ok)
    {
        switch (classifyDeltaV(problem, result.deltaVMps, DeltaVPolicy::Strict))
        {
        case DeltaVClassification::Normal: ++orbital.normal; break;
        case DeltaVClassification::Exception: ++orbital.exception; break;
        case DeltaVClassification::Infeasible: ++orbital.over; break;
        }
    }
    else
    {
        ++orbital.failure;
    }
    return result;
}

namespace
{
struct Settings
{
    const char *name;
    bool graph;
    std::size_t constructionCap;
    std::size_t states = 10000;
    std::size_t width = 12;
    std::size_t destinations = 24;
    std::size_t samples = 16;
    BeamTimingStrategy timing = BeamTimingStrategy::Stratified;
    BeamSelectionStrategy selection = BeamSelectionStrategy::Baseline;
    bool cacheEnabled = true;
};

struct Run
{
    OrbitalMetrics orbit;
    phase5i::GraphMetrics graph;
    double totalMs = 0.0;
    std::size_t depth = 0;
    std::size_t complete = 0;
    std::optional<double> best;
    std::size_t validation = 0;
    std::size_t expanded = 0;
    std::size_t exceptionsRejected = 0;
    std::size_t statesAtExceptionLimit = 0;
    std::size_t beamAccepted = 0;
    std::size_t graphEdges = 0;
    std::size_t reused = 0;
    std::size_t hits = 0;
    std::size_t misses = 0;
    std::vector<BeamDepthStats> byDepth;
    std::array<BeamTimingBucket, beamTimingBinEdges.size() + 1> byWait{};
    std::array<BeamTimingBucket, beamTimingBinEdges.size() + 1> byFlight{};
    const char *stop = "";
};

const char *stopName(GraphSearchStop stop)
{
    switch (stop)
    {
    case GraphSearchStop::Exhausted: return "sampled_alternatives_exhausted";
    case GraphSearchStop::EvaluationLimit: return "orbital_allowance";
    case GraphSearchStop::StateLimit: return "state_limit";
    case GraphSearchStop::BacktrackLimit: return "backtrack_limit";
    case GraphSearchStop::GraphCapacity: return "graph_capacity";
    default: throw std::runtime_error("Invalid graph benchmark result");
    }
}

Run runSearch(const Problem &problem, const Settings &settings, std::uint64_t seed)
{
    orbital = {};
    phase5i::graphMetrics = {};
    const auto cap = settings.constructionCap + problem.tomatoes.size() - 1;
    Run run;
    std::size_t reportedCalls;
    if (settings.graph)
    {
        GraphSearchConfig config;
        config.seed = seed;
        config.maxEvaluations = cap;
        config.maxStates = settings.states;
        config.maxBacktracks = settings.states;
        config.maxEdges = cap;
        config.maxNodes = 2 * cap + problem.tomatoes.size();
        config.cacheCapacity = settings.cacheEnabled ? cap : 0;
        config.alternativesPerState = settings.width;
        config.destinationsPerState = settings.destinations;
        config.samplesPerDestination = settings.samples;
        const auto result = GraphSearch::run(problem, config);
        const auto &stats = result.stats;
        run.stop = stopName(result.stop);
        run.totalMs = stats.elapsedSeconds * 1000.0;
        run.depth = stats.deepestPrefix;
        run.complete = stats.completeFeasibleRoutes;
        run.best = result.bestCompletionDays;
        run.validation = stats.validationCalls;
        run.expanded = stats.expandedStates;
        run.exceptionsRejected = stats.exceptionRejections;
        run.graphEdges = stats.graphEdges;
        run.reused = stats.reusedEdges;
        run.hits = stats.cache.hits;
        run.misses = stats.cache.misses;
        run.graph = phase5i::graphMetrics;
        run.statesAtExceptionLimit = run.graph.statesAtExceptionLimit;
        reportedCalls = stats.orbitalCalls;
        if (reportedCalls != stats.cache.solverCalls + run.validation || stats.expandedStates > config.maxStates ||
            stats.cache.misses != stats.cache.solverCalls + stats.cache.budgetDenials)
        {
            throw std::runtime_error("Graph counters do not reconcile");
        }
    }
    else
    {
        BeamSearchConfig config;
        config.seed = seed;
        config.maxEvaluations = cap;
        config.restartCount = 0;
        config.beamWidth = settings.width;
        config.destinationsPerRoute = settings.destinations;
        config.samplesPerDestination = settings.samples;
        config.timingStrategy = settings.timing;
        config.selectionStrategy = settings.selection;
        const auto result = BeamSearch::run(problem, config);
        const auto &stats = result.stats;
        if (result.stop != BeamSearchStop::EvaluationLimit && result.stop != BeamSearchStop::AttemptsExhausted)
        {
            throw std::runtime_error("Invalid beam benchmark result");
        }
        run.stop = result.stop == BeamSearchStop::EvaluationLimit ? "orbital_allowance" : "frontier_exhausted";
        run.totalMs = stats.elapsedSeconds * 1000.0;
        run.depth = stats.deepestPrefix;
        run.complete = stats.completeFeasibleRoutes;
        run.best = result.bestCompletionDays;
        run.validation = stats.validationEvaluations;
        run.exceptionsRejected = stats.exceptionBudgetRejections + stats.lookaheadExceptionBudgetRejections;
        run.beamAccepted = stats.successfulExtensions - stats.lookaheadReused + stats.lookaheadAccepted;
        run.byDepth = stats.byDepth;
        run.byWait = stats.byWait;
        run.byFlight = stats.byFlight;
        for (const auto &depth : stats.byDepth) { run.expanded += depth.prefixesExamined; }
        reportedCalls = stats.evaluationsAttempted;
    }

    if (reportedCalls != orbital.calls || reportedCalls > cap ||
        orbital.calls != orbital.normal + orbital.exception + orbital.over + orbital.failure)
    {
        throw std::runtime_error("Observed solver calls do not reconcile");
    }
    run.orbit = orbital;
    return run;
}

double milliseconds(Clock::duration duration)
{
    return std::chrono::duration<double, std::milli>(duration).count();
}

double median(std::array<double, repeats> values)
{
    std::sort(values.begin(), values.end());
    return values[repeats / 2];
}

void writeSearch(std::ostream &report, std::ostream &depths, std::ostream &timings,
                 const Problem &problem, const Settings &settings, std::uint64_t seed)
{
    const auto run = runSearch(problem, settings, seed);
    std::array<double, repeats> total{}, solver{}, other{};
    for (std::size_t i = 0; i < repeats; ++i)
    {
        const auto next = i == 0 ? run : runSearch(problem, settings, seed);
        if (next.orbit.calls != run.orbit.calls || next.orbit.normal != run.orbit.normal ||
            next.orbit.exception != run.orbit.exception || next.orbit.over != run.orbit.over ||
            next.expanded != run.expanded || next.exceptionsRejected != run.exceptionsRejected ||
            next.hits != run.hits || next.misses != run.misses || next.reused != run.reused ||
            next.depth != run.depth || next.complete != run.complete || next.best != run.best ||
            std::string(next.stop) != run.stop)
        {
            throw std::runtime_error("Repeated search changed deterministic results");
        }
        total[i] = next.totalMs;
        solver[i] = milliseconds(next.orbit.orbitalTime);
        other[i] = total[i] - solver[i];
    }

    const auto &o = run.orbit;
    const auto &g = run.graph;
    report << settings.name << ',' << seed << ',' << settings.constructionCap << ',';
    if (settings.graph) { report << settings.states; }
    report << ',';
    if (settings.graph) { report << (settings.cacheEnabled ? settings.constructionCap + problem.tomatoes.size() - 1 : 0); }
    report << ',' << settings.width << ',' << settings.destinations << ',' << settings.samples
           << ',' << o.calls << ',' << run.validation << ',' << o.normal << ',' << o.exception << ',' << o.over
           << ',' << o.failure << ',' << run.depth << ',' << run.complete << ',';
    if (run.best) { report << *run.best; }
    report << ',' << run.stop << ',' << run.expanded << ',' << run.exceptionsRejected << ',';
    if (settings.graph)
    {
        report << ',' << run.graphEdges << ',' << run.reused << ',' << run.hits << ',' << run.misses
               << ',' << run.statesAtExceptionLimit << ',' << g.deadlineEdges << ',' << g.noTiming << ','
               << g.emptyStates << ',' << g.emptyWithDeadline << ',' << g.emptyWithException << ',' << g.emptyWithBudget;
    }
    else
    {
        report << run.beamAccepted << ",,,,,,,,,,,";
    }
    report << ',' << median(total) << ',' << median(solver) << ',' << median(other) << '\n';

    const auto prefix = [&settings, seed](std::ostream &out)
    {
        out << settings.name << ',' << seed << ',' << settings.constructionCap;
    };
    for (std::size_t i = 0; i < run.byDepth.size(); ++i)
    {
        const auto &d = run.byDepth[i];
        prefix(depths);
        depths << ',' << i + 1 << ',' << d.prefixesExamined << ',' << d.evaluations << ',' << d.accepted
               << ',' << d.deltaVExceeded << ',' << d.exceptionBudgetExceeded << ',' << d.deadlineExceeded
               << ',' << d.destinationsWithoutTiming << ',' << d.retained << ',' << d.retainedAtExceptionLimit
               << ',' << d.sameEndpointPruned << ',' << d.widthPruned;
        for (const auto count : d.retainedArrivalBins) { depths << ',' << count; }
        depths << '\n';
    }
    if (!settings.graph)
    {
        for (std::size_t i = 0; i < run.byWait.size(); ++i)
        {
            prefix(timings);
            timings << ",wait," << i << ',' << run.byWait[i].evaluations << ',' << run.byWait[i].accepted << '\n';
            prefix(timings);
            timings << ",flight," << i << ',' << run.byFlight[i].evaluations << ',' << run.byFlight[i].accepted << '\n';
        }
    }
}

DeltaVClassification query(const Problem &problem, std::size_t from, std::size_t to, LegTiming timing)
{
    const auto result = phase5i::evaluateOrbit(problem, from, to, timing.departureDays, timing.flightDays);
    return result.status == TransferStatus::Ok ? classifyDeltaV(problem, result.deltaVMps, DeltaVPolicy::Strict)
                                                : DeltaVClassification::Infeasible;
}

void scanWindows(const Problem &problem, std::ostream &out)
{
    constexpr std::size_t intervals = 128;
    constexpr std::size_t trials = 64;
    constexpr std::size_t samples = 16;
    const std::size_t legs = problem.tomatoes.size() - 1;
    std::vector<std::pair<std::size_t, std::size_t>> pairs{{0, 1}, {1, 48}};
    std::vector<std::pair<std::size_t, std::size_t>> population;
    for (std::size_t from = 0; from <= legs; ++from)
    {
        for (std::size_t to = 0; to <= legs; ++to)
        {
            if (from != to && std::make_pair(from, to) != pairs[0] && std::make_pair(from, to) != pairs[1])
            {
                population.emplace_back(from, to);
            }
        }
    }
    std::mt19937_64 engine(5);
    std::shuffle(population.begin(), population.end(), engine);
    pairs.insert(pairs.end(), population.begin(), population.begin() + 10);

    out << "source,destination,opening_day,slack_step_days,grid_calls,grid_normal,grid_exception,trials,calls_per_trial,"
        << "uniform_found,uniform_normal,stratified_found,stratified_normal\n";
    for (const auto &[from, to] : pairs)
    {
        for (const double opening : {0.0, 25.0, 75.0})
        {
            const double minimum = problem.minTimeOfFlight;
            const double cap = (problem.maxTotalTime - opening) / static_cast<double>(legs) - minimum;
            const double step = cap / static_cast<double>(intervals);
            const TimingWindow window{opening, problem.maxTotalTime - static_cast<double>(legs - 1) * minimum, minimum};
            std::size_t gridNormal = 0;
            std::size_t gridException = 0;
            std::size_t gridCalls = 0;
            for (std::size_t wait = 0; wait <= intervals; ++wait)
            {
                for (std::size_t flight = 0; flight <= intervals - wait; ++flight)
                {
                    const auto cls = query(problem, from, to, {opening + wait * step, minimum + flight * step});
                    gridNormal += cls == DeltaVClassification::Normal;
                    gridException += cls == DeltaVClassification::Exception;
                    ++gridCalls;
                }
            }
            out << from << ',' << to << ',' << opening << ',' << step << ',' << gridCalls << ',' << gridNormal
                << ',' << gridException << ',' << trials << ',' << samples;
            for (const bool stratify : {false, true})
            {
                std::size_t found = 0;
                std::size_t normal = 0;
                for (std::size_t seed = 0; seed < trials; ++seed)
                {
                    TimingSampler sampler(seed);
                    bool trialFound = false;
                    bool trialNormal = false;
                    for (std::size_t i = 0; i < samples; ++i)
                    {
                        const auto timing = i % 4 == 3
                            ? sampler.sample(window, std::numeric_limits<double>::infinity())
                            : (stratify ? sampler.sampleStratified(window, cap, i - i / 4, samples - samples / 4)
                                        : sampler.sample(window, cap));
                        if (!timing) { throw std::runtime_error("Missing timing in window probe"); }
                        const auto cls = query(problem, from, to, *timing);
                        trialFound |= cls != DeltaVClassification::Infeasible;
                        trialNormal |= cls == DeltaVClassification::Normal;
                    }
                    found += trialFound;
                    normal += trialNormal;
                }
                out << ',' << found << ',' << normal;
            }
            out << '\n';
        }
    }
}

std::ofstream csv(const std::filesystem::path &path)
{
    std::ofstream file;
    file.exceptions(std::ios::badbit | std::ios::failbit);
    file.open(path);
    file << std::setprecision(10);
    return file;
}

void benchmark(const Problem &problem, const std::filesystem::path &output)
{
    auto report = csv(output / "runs.csv");
    auto depths = csv(output / "beam-depth.csv");
    auto timings = csv(output / "beam-timing.csv");
    report << "variant,seed,construction_cap,state_cap,cache_capacity,width,destinations,samples,calls,validation,normal,exception,"
           << "above_ceiling,solver_failure,depth,complete,best_days,stop,expanded,exception_rejections,beam_accepted,"
           << "graph_edges,reused_edges,cache_hits,cache_misses,states_at_exception_limit,deadline_edges,no_timing,"
           << "empty_states,empty_with_deadline,empty_with_exception,empty_with_budget,total_ms,solver_ms,other_ms\n";
    depths << "variant,seed,construction_cap,parent_depth,prefixes,calls,accepted,ceiling_reject,exception_reject,"
           << "deadline_reject,no_timing,retained,retained_at_limit,endpoint_pruned,width_pruned,lt_2,from_2_to_4,"
           << "from_4_to_8,from_8_to_16,at_least_16\n";
    timings << "variant,seed,construction_cap,axis,bucket,calls,accepted\n";

    for (const auto seed : seeds)
    {
        for (const auto cap : {8000u, 24000u, 48000u, 60000u})
        {
            writeSearch(report, depths, timings, problem, {"beam", false, cap}, seed);
            writeSearch(report, depths, timings, problem, {"graph", true, cap}, seed);
        }
        for (const auto cap : {50u, 200u, 2000u, 30000u})
        {
            writeSearch(report, depths, timings, problem, {"graph_state_sweep", true, 60000, cap}, seed);
        }
        Settings cacheOff{"graph_cache_off", true, 60000};
        cacheOff.cacheEnabled = false;
        writeSearch(report, depths, timings, problem, cacheOff, seed);
        const Settings comparisons[] = {
            {"beam_width6", false, 24000, 0, 6}, {"beam_width24", false, 24000, 0, 24},
            {"beam_dest12", false, 24000, 0, 12, 12}, {"beam_dest36", false, 24000, 0, 12, 36},
            {"beam_samples8", false, 24000, 0, 12, 24, 8}, {"beam_samples24", false, 24000, 0, 12, 24, 24},
            {"beam_uniform", false, 24000, 0, 12, 24, 16, BeamTimingStrategy::Baseline},
            {"beam_temporal", false, 24000, 0, 12, 24, 16, BeamTimingStrategy::Stratified, BeamSelectionStrategy::Temporal},
            {"beam_lookahead", false, 24000, 0, 12, 24, 16, BeamTimingStrategy::Stratified, BeamSelectionStrategy::Lookahead},
            {"beam_frontier", false, 200000}
        };
        for (const auto &settings : comparisons) { writeSearch(report, depths, timings, problem, settings, seed); }
    }
    // Seed 1's 12-destination beam emptied its frontier after 23,232 calls at the 24,000 allowance.
    writeSearch(report, depths, timings, problem, {"beam_dest12_matched", false, 23232, 0, 12, 12}, 1);
    writeSearch(report, depths, timings, problem, {"beam_matched", false, 23232}, 1);
    auto windows = csv(output / "windows.csv");
    scanWindows(problem, windows);
}
}

int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        std::cerr << "Usage: phase5i_benchmark ESA-easy.kttsp output-directory\n";
        return 1;
    }
    try
    {
        Problem problem;
        if (!problem.loadFromFile(argv[1]) || problem.tomatoes.size() != 49 || problem.startTime != 0.0 ||
            problem.maxTotalTime != 200.0 || problem.minTimeOfFlight != 0.001 || problem.maxExceptions != 5 ||
            problem.maxDeltaV != 100.0 || problem.maxExceptionDeltaV != 600.0)
        {
            throw std::runtime_error("Benchmark expects the official ESA easy instance");
        }
        std::filesystem::create_directories(argv[2]);
        benchmark(problem, argv[2]);
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
