#include "PartialRoute.hpp"
#include "TransferWindowExplorer.hpp"

#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace
{
using Clock = std::chrono::steady_clock;

struct Counts
{
    std::size_t normal = 0;
    std::size_t exception = 0;
    std::optional<double> minimum;
    std::optional<LegTiming> bestAccepted;
};

void record(Counts &counts, LegTiming timing, double cost, DeltaVClassification classification)
{
    if (classification != DeltaVClassification::Infeasible &&
        (!counts.bestAccepted || !counts.minimum || cost < *counts.minimum))
    {
        counts.bestAccepted = timing;
    }
    if (!counts.minimum || cost < *counts.minimum)
    {
        counts.minimum = cost;
    }
    counts.normal += classification == DeltaVClassification::Normal;
    counts.exception += classification == DeltaVClassification::Exception;
}

void writeRun(std::ostream &out, const Problem &problem, const char *method, std::size_t source,
              std::size_t destination, double opening, double deadline, const char *domain,
              std::size_t budget, std::optional<std::uint64_t> seed)
{
    TransferCache cache(problem, budget);
    Counts counts;
    const auto start = Clock::now();
    if (!seed)
    {
        TransferWindowConfig config;
        config.maxEvaluations = budget;
        config.maxSamples = budget * 4;
        TransferWindowBounds bounds;
        bounds.earliestDeparture = opening;
        bounds.latestArrival = deadline;
        const auto result = TransferWindowExplorer::explore(cache, source, destination, config, bounds);
        counts.minimum = result.lowestDeltaVMps;
        for (const auto &opportunity : result.opportunities)
        {
            counts.normal += opportunity.classification == DeltaVClassification::Normal;
            counts.exception += opportunity.classification == DeltaVClassification::Exception;
        }
        if (!result.opportunities.empty())
        {
            const auto &best = result.opportunities.front().query;
            counts.bestAccepted = LegTiming{best.departureDays, best.flightDays};
        }
    }
    else
    {
        const TimingWindow window{opening, deadline, problem.minTimeOfFlight};
        TimingSampler sampler(*seed);
        for (std::size_t i = 0; i < budget; ++i)
        {
            constexpr double fullSlack = std::numeric_limits<double>::infinity();
            const auto timing = std::string_view(method) == "uniform"
                ? sampler.sample(window, fullSlack) : sampler.sampleStratified(window, fullSlack, i, budget);
            if (!timing)
            {
                throw std::runtime_error("A benchmark timing proposal is missing");
            }
            const auto transfer = cache.evaluate({source, destination, timing->departureDays, timing->flightDays}, budget);
            if (transfer && transfer->status == TransferStatus::Ok)
            {
                record(counts, *timing, transfer->deltaVMps,
                       classifyDeltaV(problem, transfer->deltaVMps, DeltaVPolicy::Strict));
            }
        }
    }
    const double elapsed = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    const auto stats = cache.stats();
    if (stats.solverCalls != budget || stats.misses != stats.solverCalls + stats.budgetDenials)
    {
        throw std::runtime_error("Benchmark run did not spend the matched actual orbital-call budget");
    }
    out << source << ',' << destination << ',' << opening << ',' << domain << ',' << deadline
        << ',' << budget << ',' << method << ',';
    if (seed) { out << *seed; }
    out << ',' << counts.normal << ',' << counts.exception << ',';
    if (counts.minimum) { out << *counts.minimum; }
    out << ',';
    if (counts.bestAccepted) { out << counts.bestAccepted->departureDays; }
    out << ',';
    if (counts.bestAccepted) { out << counts.bestAccepted->flightDays; }
    out << ',' << stats.solverCalls << ',' << stats.hits << ',' << stats.misses << ',' << stats.budgetDenials
        << ',' << elapsed << '\n';
}
}

int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        std::cerr << "Usage: transfer_window_benchmark ESA-easy.kttsp output.csv\n";
        return 1;
    }
    try
    {
        Problem problem;
        if (!problem.loadFromFile(argv[1]) || problem.tomatoes.size() != 49)
        {
            throw std::runtime_error("Expected the 49-tomato ESA easy instance");
        }
        std::ofstream out;
        out.exceptions(std::ios::badbit | std::ios::failbit);
        out.open(argv[2]);
        out << std::setprecision(17)
            << "source,destination,opening_days,domain,deadline_days,budget,method,seed,normal,exception,lowest_dv,"
               "best_accepted_departure,best_accepted_flight,calls,cache_hits,cache_misses,budget_denials,runtime_ms\n";
        // Every pair appeared in the Phase 5I probe. The sample includes normal-rich, rare and zero-yield controls.
        constexpr std::pair<std::size_t, std::size_t> pairs[] = {
            {0, 1}, {1, 48}, {11, 4}, {18, 13}, {24, 9}, {36, 10}, {39, 33}
        };
        for (const auto &[source, destination] : pairs)
        {
            for (const double opening : {0.0, 25.0, 75.0})
            {
                const double localDeadline = opening + (problem.maxTotalTime - opening) /
                                                       static_cast<double>(problem.tomatoes.size() - 1);
                for (const bool local : {true, false})
                {
                    const auto deadline = local ? localDeadline : problem.maxTotalTime;
                    const char *domain = local ? "local" : "full";
                    for (const std::size_t budget : {128, 512})
                    {
                        writeRun(out, problem, "explorer", source, destination, opening, deadline, domain, budget, {});
                        for (const std::uint64_t seed : {0, 1, 7})
                        {
                            writeRun(out, problem, "uniform", source, destination, opening, deadline, domain, budget, seed);
                            writeRun(out, problem, "stratified", source, destination, opening, deadline, domain, budget, seed);
                        }
                    }
                }
            }
        }
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
