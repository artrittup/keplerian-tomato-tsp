#include "GraphSearch.hpp"

#include <charconv>
#include <iomanip>
#include <iostream>
#include <string>
#include <string_view>

namespace
{
template <class Unsigned>
bool parseUnsigned(std::string_view value, Unsigned &out)
{
    const auto end = value.data() + value.size();
    const auto parsed = std::from_chars(value.data(), end, out);
    return parsed.ec == std::errc{} && parsed.ptr == end;
}

bool readOption(std::string_view name, std::string_view value, GraphSearchConfig &config, std::string &input)
{
    if (name == "--input")
    {
        input = value;
        return !input.empty();
    }
    if (name == "--policy")
    {
        if (value != "strict" && value != "published")
        {
            return false;
        }
        config.policy = value == "strict" ? DeltaVPolicy::Strict : DeltaVPolicy::PublishedEvaluator;
        return true;
    }
    if (name == "--seed")
    {
        return parseUnsigned(value, config.seed);
    }
    if (name == "--start-tomato")
    {
        std::size_t index;
        if (!parseUnsigned(value, index))
        {
            return false;
        }
        config.startTomato = index;
        return true;
    }
    struct SizeOption
    {
        std::string_view name;
        std::size_t GraphSearchConfig::*member;
    };
    constexpr SizeOption options[] = {
        {"--max-evaluations", &GraphSearchConfig::maxEvaluations}, {"--max-states", &GraphSearchConfig::maxStates},
        {"--max-backtracks", &GraphSearchConfig::maxBacktracks}, {"--max-edges", &GraphSearchConfig::maxEdges},
        {"--max-nodes", &GraphSearchConfig::maxNodes}, {"--cache-capacity", &GraphSearchConfig::cacheCapacity},
        {"--destinations", &GraphSearchConfig::destinationsPerState},
        {"--samples", &GraphSearchConfig::samplesPerDestination}, {"--alternatives", &GraphSearchConfig::alternativesPerState}
    };
    for (const auto &option : options)
    {
        if (name == option.name)
        {
            return parseUnsigned(value, config.*option.member);
        }
    }
    return false;
}

const char *stopReason(GraphSearchStop stop)
{
    switch (stop)
    {
    case GraphSearchStop::Exhausted: return "sampled alternatives exhausted";
    case GraphSearchStop::EvaluationLimit: return "orbital evaluation allowance reached";
    case GraphSearchStop::StateLimit: return "search state limit reached";
    case GraphSearchStop::BacktrackLimit: return "backtrack limit reached";
    case GraphSearchStop::GraphCapacity: return "graph storage limit reached";
    case GraphSearchStop::InvalidConfig: return "invalid graph-search configuration";
    case GraphSearchStop::InvalidProblem: return "invalid graph-search problem";
    }
    return "unknown";
}

void usage(std::ostream &out)
{
    out << "Usage: keplerian_tsp --graph-search [--input FILE] [--seed N] [--start-tomato N]\n"
        << "       [--max-evaluations N] [--max-states N] [--max-backtracks N]\n"
        << "       [--max-edges N] [--max-nodes N] [--cache-capacity N]\n"
        << "       [--destinations N] [--samples N] [--alternatives N] [--policy strict|published]\n";
}

void printResult(const GraphSearchConfig &config, const GraphSearchResult &result)
{
    const auto &s = result.stats;
    std::cout << "Seed: " << config.seed << "\n"
              << "Delta-V policy: " << (config.policy == DeltaVPolicy::Strict ? "strict" : "published") << "\n"
              << "Orbital evaluations: " << s.orbitalCalls << " / " << config.maxEvaluations << "\n"
              << "Independent validation calls: " << s.validationCalls << "\n"
              << "Cache solver calls: " << s.cache.solverCalls << "\n"
              << "Cache hits: " << s.cache.hits << "\n"
              << "Cache misses: " << s.cache.misses << "\n"
              << "Cache entries: " << s.cache.entries << " / " << s.cache.capacity << "\n"
              << "Cache evictions: " << s.cache.evictions << "\n"
              << "Graph nodes: " << s.graphNodes << " / " << config.maxNodes << "\n"
              << "Graph edges: " << s.graphEdges << " / " << config.maxEdges << "\n"
              << "Graph capacity denials: " << s.graphCapacityDenials << "\n"
              << "Reused edges taken: " << s.reusedEdges << "\n"
              << "Expanded states: " << s.expandedStates << " / " << config.maxStates << "\n"
              << "Backtracked legs: " << s.backtracks << " / " << config.maxBacktracks << "\n"
              << "Visited-target rejections: " << s.visitedRejections << "\n"
              << "Exception-budget rejections: " << s.exceptionRejections << "\n"
              << "Deepest prefix (tomatoes): " << s.deepestPrefix << "\n"
              << "Complete feasible routes: " << s.completeFeasibleRoutes << "\n"
              << "Stop reason: " << stopReason(result.stop) << "\n"
              << "Runtime (s): " << s.elapsedSeconds << "\n";
    if (!result.bestSolution)
    {
        std::cout << "No feasible route found within these graph-search limits.\n";
        return;
    }
    std::cout << std::setprecision(std::numeric_limits<double>::max_digits10)
              << "Best feasible completion (days): " << *result.bestCompletionDays << "\n";
    const auto print = [](const char *label, const auto &values)
    {
        std::cout << label;
        for (const auto value : values)
        {
            std::cout << ' ' << value;
        }
        std::cout << '\n';
    };
    print("Visit order:", result.bestSolution->visitingOrder);
    print("Departure days:", result.bestSolution->departureTimes);
    print("Flight days:", result.bestSolution->timesOfFlight);
}
}

int runGraphSearchExperiment(int argc, char *argv[])
{
    if (argc == 1 && std::string_view(argv[0]) == "--help")
    {
        usage(std::cout);
        return 0;
    }
    GraphSearchConfig config;
    std::string input = "data/easy.kttsp";
    for (int i = 0; i < argc; i += 2)
    {
        if (i + 1 == argc || !readOption(argv[i], argv[i + 1], config, input))
        {
            std::cerr << "Invalid graph-search option or value for " << argv[i] << "\n";
            usage(std::cerr);
            return 1;
        }
    }
    Problem problem;
    if (!problem.loadFromFile(input))
    {
        return 1;
    }
    const auto result = GraphSearch::run(problem, config);
    if (result.stop == GraphSearchStop::InvalidConfig || result.stop == GraphSearchStop::InvalidProblem)
    {
        std::cerr << stopReason(result.stop) << "\n";
        return 1;
    }
    std::cout << "Instance: " << input << " (" << problem.tomatoes.size() << " tomatoes)\n";
    printResult(config, result);
    return 0;
}
