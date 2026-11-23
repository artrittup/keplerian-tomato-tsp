#include "BeamSearch.hpp"
#include "Problem.hpp"

#include <charconv>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>

namespace
{
template <class Unsigned>
bool readUnsigned(std::string_view text, Unsigned &value)
{
    const auto end = text.data() + text.size();
    const auto parsed = std::from_chars(text.data(), end, value);
    return parsed.ec == std::errc{} && parsed.ptr == end;
}

bool readOption(std::string_view name, std::string_view value, BeamSearchConfig &config, std::string &input)
{
    if (name == "--input")
    {
        input = value;
        return !input.empty();
    }
    if (name == "--policy")
    {
        if (value == "strict")
        {
            config.policy = DeltaVPolicy::Strict;
            return true;
        }
        if (value == "published")
        {
            config.policy = DeltaVPolicy::PublishedEvaluator;
            return true;
        }
        return false;
    }
    if (name == "--seed")
    {
        return readUnsigned(value, config.seed);
    }

    struct SizeOption
    {
        std::string_view name;
        std::size_t BeamSearchConfig::*member;
    };
    constexpr SizeOption sizeOptions[] = {
        {"--beam-width", &BeamSearchConfig::beamWidth},
        {"--destinations", &BeamSearchConfig::destinationsPerRoute},
        {"--samples", &BeamSearchConfig::samplesPerDestination},
        {"--max-evaluations", &BeamSearchConfig::maxEvaluations},
        {"--restarts", &BeamSearchConfig::restartCount}
    };
    for (const auto &option : sizeOptions)
    {
        if (name == option.name)
        {
            return readUnsigned(value, config.*option.member);
        }
    }
    return false;
}

void printUsage(std::ostream &out)
{
    out << "Usage: keplerian_tsp --beam-search [--input FILE] [--beam-width N] [--destinations N]\n"
        << "       [--samples N] [--seed N] [--max-evaluations N] [--restarts N] [--policy strict|published]\n";
}

void printResult(const BeamSearchResult &result)
{
    const auto &config = result.config;
    const auto &stats = result.stats;
    using std::cout;
    cout << "Seed: " << config.seed << "\n"
         << "Beam width: " << config.beamWidth << "\n"
         << "Destinations per prefix: " << config.destinationsPerRoute << "\n"
         << "Timing samples per destination: " << config.samplesPerDestination << "\n"
         << "Requested restarts: " << config.restartCount << "\n"
         << "Delta-V policy: " << (config.policy == DeltaVPolicy::Strict ? "strict" : "published") << "\n"
         << "Attempts started: " << stats.attemptsStarted << "\n"
         << "Starting tomatoes:";
    for (const int tomato : stats.startingTomatoes)
    {
        cout << ' ' << tomato;
    }
    cout << "\nOrbital evaluations: " << stats.evaluationsAttempted << " / " << config.maxEvaluations << "\n"
         << "Full-validation evaluations: " << stats.validationEvaluations << "\n"
         << "Successful extensions: " << stats.successfulExtensions << "\n"
         << "Rejected extensions: " << stats.rejectedExtensions << "\n"
         << "Solver failures: " << stats.transferFailures << "\n"
         << "Delta-V rejections: " << stats.deltaVRejections << "\n"
         << "Exception-budget rejections: " << stats.exceptionBudgetRejections << "\n"
         << "Deepest prefix (tomatoes): " << stats.deepestPrefix << "\n"
         << "Peak beam size: " << stats.peakBeamSize << "\n"
         << "Complete routes found: " << stats.completeRoutesFound << "\n"
         << "Complete routes validated: " << stats.completeRoutesValidated << "\n"
         << "Stop reason: " << (result.stop == BeamSearchStop::EvaluationLimit
                                  ? "evaluation allowance reached" : "attempts exhausted") << "\n"
         << "Runtime (s): " << stats.elapsedSeconds << "\n";

    if (!result.bestSolution)
    {
        cout << "No feasible route found within these search limits.\n";
        return;
    }
    cout << std::setprecision(std::numeric_limits<double>::max_digits10)
         << "Best feasible completion (days): " << *result.bestCompletionDays << "\n";
    const auto &solution = *result.bestSolution;
    const auto printValues = [](const char *label, const auto &values)
    {
        std::cout << label;
        for (const auto value : values)
        {
            std::cout << ' ' << value;
        }
        std::cout << '\n';
    };
    printValues("Visit order:", solution.visitingOrder);
    printValues("Departure days:", solution.departureTimes);
    printValues("Flight days:", solution.timesOfFlight);
}
}

int runBeamSearchExperiment(int argc, char *argv[])
{
    if (argc == 1 && std::string_view(argv[0]) == "--help")
    {
        printUsage(std::cout);
        return 0;
    }
    BeamSearchConfig config;
    std::string input = "data/easy.kttsp";
    for (int i = 0; i < argc; i += 2)
    {
        if (i + 1 == argc || !readOption(argv[i], argv[i + 1], config, input))
        {
            std::cerr << "Invalid beam-search option or value for " << argv[i] << "\n";
            printUsage(std::cerr);
            return 1;
        }
    }
    Problem problem;
    if (!problem.loadFromFile(input))
    {
        return 1;
    }
    const auto result = BeamSearch::run(problem, config);
    if (result.stop == BeamSearchStop::InvalidConfig || result.stop == BeamSearchStop::InvalidProblem)
    {
        std::cerr << (result.stop == BeamSearchStop::InvalidConfig
                          ? "Invalid beam-search configuration.\n"
                          : "Problem cannot initialize a partial route.\n");
        return 1;
    }
    std::cout << "Instance: " << input << " (" << problem.tomatoes.size() << " tomatoes)\n";
    printResult(result);
    return 0;
}
