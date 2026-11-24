#include "TransferWindowExplorer.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
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

Problem loadEasy(const char *path)
{
    Problem problem;
    require(problem.loadFromFile(path) && problem.tomatoes.size() == 49, "Could not load ESA easy");
    return problem;
}

TransferWindowBounds fixedTiming(double depart, double flight)
{
    return {depart, depart, flight, flight, depart + flight};
}

void known(const char *path)
{
    TransferCache cache(loadEasy(path), 16);
    const auto result = TransferWindowExplorer::explore(cache, 0, 1, {}, fixedTiming(0.125, 5.0));
    require(result.stop == TransferWindowStop::Exhausted && result.stats.solverCalls == 1 &&
            result.opportunities.size() == 1, "Expected the known multi-revolution leg exactly once");
    const auto &leg = result.opportunities.front();
    require(leg.query.source == 0 && leg.query.destination == 1 &&
            leg.query.departureDays == 0.125 && leg.query.flightDays == 5.0 && leg.arrivalDays == 5.125 &&
            std::abs(leg.deltaVMps - 417.0047391125741) < 1e-6 &&
            leg.classification == DeltaVClassification::Exception, "Wrong transfer data for the reference leg");
}

void narrow(const char *path)
{
    // The Phase 5I local grid hit only one admissible point for this pair at mission start.
    const auto problem = loadEasy(path);
    TransferWindowBounds bounds;
    bounds.latestArrival = problem.maxTotalTime / static_cast<double>(problem.tomatoes.size() - 1);
    TransferCache coarseCache(problem, 512), refineCache(problem, 512);
    TransferWindowConfig config;
    config.refinementLevels = 0;
    const auto coarse = TransferWindowExplorer::explore(coarseCache, 18, 13, config, bounds);
    config.refinementLevels = 8;
    const auto refined = TransferWindowExplorer::explore(refineCache, 18, 13, config, bounds);
    require(coarse.lowestDeltaVMps && *coarse.lowestDeltaVMps > problem.maxExceptionDeltaV &&
            coarse.opportunities.empty(), "The initial coarse grid should miss this window");
    require(refined.stats.refinementSamples > 0 && refined.stats.solverCalls == 512 &&
            refined.lowestDeltaVMps && *refined.lowestDeltaVMps < 595.0 &&
            !refined.opportunities.empty() &&
            refined.opportunities.front().classification == DeltaVClassification::Exception,
            "Refining low measured costs above the ceiling should find a transfer");
}

void none(const char *path)
{
    TransferCache cache(loadEasy(path), 16);
    const auto result = TransferWindowExplorer::explore(cache, 0, 4, {}, fixedTiming(0.25, 0.001));
    require(result.opportunities.empty() && result.stats.solverCalls == 1 && result.stats.solverFailures == 0 &&
            result.lowestDeltaVMps && *result.lowestDeltaVMps > cache.problem().maxExceptionDeltaV,
            "An over-ceiling transfer must not appear as an opportunity");
}

void classification(const char *path)
{
    TransferCache cache(loadEasy(path), 512);
    const auto result = TransferWindowExplorer::explore(cache, 1, 48, {},
                                                         {0.125, 0.125, 4.999, 5.0, {}});
    const auto &legs = result.opportunities;
    require(!legs.empty() && legs.front().classification == DeltaVClassification::Normal &&
            legs.back().classification == DeltaVClassification::Exception &&
            std::is_sorted(legs.begin(), legs.end(), [](const auto &a, const auto &b) { return a.deltaVMps < b.deltaVMps; }),
            "Normal results should precede exception results, sorted by their evaluated cost");
}

void policies(const char *path)
{
    TransferCache cache(loadEasy(path), 16);
    const auto bounds = fixedTiming(0.125, 1.8478637404934113);
    auto config = TransferWindowConfig{};
    const auto strict = TransferWindowExplorer::explore(cache, 0, 1, config, bounds);
    config.maxEvaluations = 0;
    config.policy = DeltaVPolicy::PublishedEvaluator;
    const auto published = TransferWindowExplorer::explore(cache, 0, 1, config, bounds);
    require(strict.opportunities.empty() && published.opportunities.size() == 1 &&
            published.opportunities.front().classification == DeltaVClassification::Exception &&
            strict.lowestDeltaVMps == published.lowestDeltaVMps &&
            published.stats.solverCalls == 0 && published.stats.cacheHits == 1,
            "The same raw value must be reused and classified with the selected policy");
}

void budget(const char *path)
{
    auto problem = loadEasy(path);
    problem.tomatoes[1].a = 0.0;
    TransferCache cache(problem, 16);
    TransferWindowConfig config;
    config.maxEvaluations = 3;
    const auto result = TransferWindowExplorer::explore(cache, 0, 1, config);
    require(result.stop == TransferWindowStop::EvaluationLimit && result.stats.solverCalls == 3 &&
            result.stats.cacheMisses == 4 && result.stats.budgetDenials == 1 &&
            result.stats.solverFailures == 3 && !result.lowestDeltaVMps,
            "Failed solver invocations count toward the shared budget");
    const auto replay = TransferWindowExplorer::explore(cache, 0, 1, config);
    require(replay.stats.solverCalls == 3 && replay.stats.cacheHits == 3 &&
            replay.stats.solverFailures == 6 && cache.stats().solverCalls == 6,
            "Cached failures are free, and the next request may only add its declared budget");

    config.maxSamples = 2;
    const auto capped = TransferWindowExplorer::explore(cache, 0, 1, config);
    require(capped.stop == TransferWindowStop::SampleLimit && capped.stats.coarseSamples == 2 &&
            capped.stats.cacheHits == 2 && capped.stats.solverCalls == 0,
            "Work must be bounded even when every visited point is cached");
}

void reuseCache(const char *path)
{
    TransferCache cache(loadEasy(path), 256);
    TransferWindowBounds bounds;
    bounds.latestArrival = 4.0;
    TransferWindowConfig config;
    config.maxEvaluations = 128;
    const auto cold = TransferWindowExplorer::explore(cache, 11, 4, config, bounds);
    config.maxEvaluations = 0;
    const auto warm = TransferWindowExplorer::explore(cache, 11, 4, config, bounds);
    require(!cold.opportunities.empty() && warm.opportunities.size() == cold.opportunities.size() &&
            warm.lowestDeltaVMps == cold.lowestDeltaVMps &&
            cold.stats.solverCalls == 128 && warm.stats.solverCalls == 0 && warm.stats.cacheHits == 128 &&
            warm.stats.budgetDenials == 1, "Exact replay should reuse every visited point with no paid calls");
}

void replay(const char *path)
{
    const auto problem = loadEasy(path);
    TransferCache first(problem, 256), second(problem, 256);
    TransferWindowConfig config;
    config.maxEvaluations = 256;
    TransferWindowBounds bounds;
    bounds.latestArrival = 4.0;
    const auto a = TransferWindowExplorer::explore(first, 11, 4, config, bounds);
    const auto b = TransferWindowExplorer::explore(second, 11, 4, config, bounds);
    require(a.stop == b.stop && a.lowestDeltaVMps == b.lowestDeltaVMps &&
            a.stats.solverCalls == b.stats.solverCalls && a.stats.coarseSamples == b.stats.coarseSamples &&
            a.stats.refinementSamples == b.stats.refinementSamples &&
            a.opportunities.size() == b.opportunities.size() && !a.opportunities.empty(),
            "Fresh caches must give identical deterministic results");
    for (std::size_t i = 0; i < a.opportunities.size(); ++i)
    {
        const auto &x = a.opportunities[i];
        const auto &y = b.opportunities[i];
        require(std::tie(x.query.departureDays, x.query.flightDays, x.arrivalDays, x.deltaVMps, x.classification) ==
                std::tie(y.query.departureDays, y.query.flightDays, y.arrivalDays, y.deltaVMps, y.classification),
                "Expected an exact replay of each timing and classification");
    }
}

void bounds(const char *)
{
    Problem problem;
    problem.tomatoes.assign(2, Tomato{10000000.0, 0.0, 0.0, 0.0, 0.0, 0.0});
    problem.startTime = 2.0;
    problem.minTimeOfFlight = 0.125;
    problem.maxTotalTime = 2.5;
    problem.maxDeltaV = 0.01;
    problem.maxExceptionDeltaV = 1.0;
    TransferCache cache(problem, 32);
    TransferWindowConfig config;
    config.maxEvaluations = 30;
    const auto result = TransferWindowExplorer::explore(cache, 0, 1, config, {1.0, 4.0, 0.0, 0.25, 10.0});
    require(!result.opportunities.empty(), "Expected finite circular reference transfers");
    for (const auto &leg : result.opportunities)
    {
        require(leg.query.departureDays >= problem.startTime && leg.arrivalDays <= problem.maxTotalTime &&
                leg.query.flightDays >= problem.minTimeOfFlight && leg.query.flightDays <= 0.25,
                "A request may only tighten mission bounds");
    }
    const auto seen = cache.stats().solverCalls;
    const auto empty = TransferWindowExplorer::explore(cache, 0, 1, config, {2.4, {}, {}, {}, {}});
    require(empty.stop == TransferWindowStop::Exhausted && cache.stats().solverCalls == seen,
            "An empty mission intersection must cost no calls");
    const auto invalid = TransferWindowExplorer::explore(cache, 0, 2);
    const auto nonfinite = TransferWindowExplorer::explore(cache, 0, 1, config,
                                                           {std::numeric_limits<double>::infinity(), {}, {}, {}, {}});
    require(invalid.stop == TransferWindowStop::InvalidInput && nonfinite.stop == TransferWindowStop::InvalidInput &&
            cache.stats().solverCalls == seen, "Invalid indices and nonfinite bounds must not call the solver");
}

struct TestCase
{
    std::string_view name;
    void (*run)(const char *);
};

constexpr TestCase cases[] = {{"known", known}, {"narrow", narrow}, {"none", none}, {"classification", classification},
                              {"policies", policies}, {"budget", budget}, {"cache", reuseCache}, {"replay", replay},
                              {"bounds", bounds}};
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
    std::cerr << "Unknown transfer window test or missing instance path\n";
    return 1;
}
