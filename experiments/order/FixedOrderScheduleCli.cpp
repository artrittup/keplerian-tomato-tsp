#include "FixedOrderScheduler.hpp"

#include <charconv>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <string_view>

namespace
{
template <class T>
bool parse(std::string_view text, T &value)
{
    const auto end = text.data() + text.size();
    const auto result = std::from_chars(text.data(), end, value);
    return result.ec == std::errc{} && result.ptr == end;
}

int usage(std::ostream &out, int code)
{
    out << "Usage: fixed_order_schedule INSTANCE ORDER [--max-evaluations N] [--seed N]\n"
        << "       [--bands N] [--refinement-percent N] [--flight-scale DAYS] [--cache-capacity N]\n"
        << "       [--cache-index tree|hash] [--output-prefix PATH]\n";
    return code;
}

bool option(std::string_view name, std::string_view value, FixedOrderConfig &config, std::string &output)
{
    if (name == "--max-evaluations") { return parse(value, config.maxOrbitalCalls); }
    if (name == "--seed") { return parse(value, config.seed); }
    if (name == "--bands") { return parse(value, config.departureBands); }
    if (name == "--refinement-percent") { return parse(value, config.refinementPercent); }
    if (name == "--flight-scale") { return parse(value, config.flightScaleDays); }
    if (name == "--cache-capacity") { return parse(value, config.cacheCapacity); }
    if (name == "--cache-index" && (value == "tree" || value == "hash"))
    {
        config.cacheIndex = value == "hash" ? TransferCacheIndex::Hash : TransferCacheIndex::Tree;
        return true;
    }
    if (name == "--output-prefix") { output = value; return !output.empty(); }
    return false;
}

const char *name(FixedOrderStop stop)
{
    switch (stop)
    {
    case FixedOrderStop::InvalidInput: return "invalid_input";
    case FixedOrderStop::EvaluationBudget: return "evaluation_budget";
    case FixedOrderStop::NoTransferFound: return "no_transfer_in_table";
    case FixedOrderStop::NoChronologicalTransfer: return "no_chronological_transfer";
    case FixedOrderStop::ExceptionBudget: return "exception_budget";
    case FixedOrderStop::ValidationFailed: return "validation_failed";
    case FixedOrderStop::Feasible: return "feasible";
    }
    return "unknown";
}

bool printMethod(const FixedOrderExperiment &experiment, const FixedOrderOutcome &result,
                 const char *method, const std::string &output)
{
    const auto exceptions = result.exceptionsByDepth.empty() ? 0 : result.exceptionsByDepth.back();
    const auto calls = experiment.discovery.solverCalls + result.validationCalls;
    std::cout << '\n' << method << ',' << result.prefix.size() + 1 << ',' << result.prefix.size() - exceptions
              << ',' << exceptions << ',' << calls << ',' << result.validationCalls << ','
              << experiment.discoverySeconds + result.schedulingSeconds << ',' << result.schedulingSeconds << ','
              << name(result.stop) << ',';
    if (result.completionDays) { std::cout << *result.completionDays; }
    if (!result.solution || output.empty()) { return true; }

    std::ofstream file(output + "-" + method + ".sol", std::ios::binary);
    file << std::setprecision(std::numeric_limits<double>::max_digits10);
    for (auto value : result.solution->departureTimes) { file << value << '\n'; }
    for (auto value : result.solution->timesOfFlight) { file << value << '\n'; }
    for (auto value : result.solution->visitingOrder) { file << value << '\n'; }
    return static_cast<bool>(file);
}

void printDepth(const FixedOrderOutcome &outcome, const char *method)
{
    for (std::size_t i = 0; i < outcome.depth.size(); ++i)
    {
        const auto &depth = outcome.depth[i];
        std::cout << method << ',' << (i + 1) << ',' << depth.incomingLabels << ',' << depth.chronologicalNormal
                  << ',' << depth.chronologicalException << ',' << depth.outgoingLabels << ',';
        if (i < outcome.prefix.size())
        {
            const auto &edge = outcome.prefix[i];
            std::cout << outcome.exceptionsByDepth[i + 1] << ',' << edge.arrivalDays << ',' << edge.deltaVMps;
        }
        else
        {
            std::cout << ",,";
        }
        std::cout << '\n';
    }
}
}

int main(int argc, char *argv[])
{
    if (argc == 2 && std::string_view(argv[1]) == "--help") { return usage(std::cout, 0); }
    if (argc < 3) { return usage(std::cerr, 1); }

    FixedOrderConfig config;
    std::string output;
    for (int i = 3; i < argc; i += 2)
    {
        if (i + 1 >= argc || !option(argv[i], argv[i + 1], config, output)) { return usage(std::cerr, 1); }
    }
    Problem problem;
    if (!problem.loadFromFile(argv[1])) { return 1; }
    std::ifstream orderFile(argv[2]);
    const auto order = readFixedOrder(orderFile, problem.tomatoes.size());
    if (!order)
    {
        std::cerr << "Expected an order-only file with one permutation of zero-based tomato indices.\n";
        return 1;
    }
    const auto result = FixedOrderScheduler::compare(problem, *order, config);
    if (result.greedy.stop == FixedOrderStop::InvalidInput)
    {
        std::cerr << "Invalid mission or scheduler configuration.\n";
        return 1;
    }

    std::cout << std::setprecision(std::numeric_limits<double>::max_digits10)
              << "Tomatoes: " << order->size() << "\nSeed: " << config.seed
              << "\nPolicy: strict\nMax evaluations per method: " << config.maxOrbitalCalls
              << "\nDeparture bands: " << config.departureBands << "\nRefinement percent: " << config.refinementPercent
              << "\nFlight scale (days): " << config.flightScaleDays << "\nCache capacity: " << config.cacheCapacity
              << "\nCache index: " << (config.cacheIndex == TransferCacheIndex::Hash ? "hash" : "tree")
              << "\nDiscovery calls: " << result.discovery.solverCalls << "\nCache hits: " << result.discovery.hits
              << "\nCache misses: " << result.discovery.misses << "\nCache budget denials: " << result.discovery.budgetDenials
              << "\nDiscovery seconds: " << result.discoverySeconds << '\n'
              << "\nMethod summary (CSV)\n"
              << "method,deepest_prefix,normal,exceptions,actual_calls,validation_calls,total_seconds,decode_seconds,stop,best_days";
    const bool greedyPrinted = printMethod(result, result.greedy, "greedy", output);
    const bool earliestPrinted = printMethod(result, result.earliest, "earliest", output);
    if (!greedyPrinted || !earliestPrinted)
    {
        std::cerr << "\nCould not write the independently validated solution.\n";
        return 1;
    }

    std::cout << "\n\nPair discovery (CSV)\nleg,source,target,normal,exception,solver_calls,solver_failures,lowest_delta_v\n";
    for (std::size_t i = 0; i < result.coverage.size(); ++i)
    {
        const auto &leg = result.coverage[i];
        std::cout << (i + 1) << ',' << (*order)[i] << ',' << (*order)[i + 1] << ',' << leg.normal << ','
                  << leg.exception << ',' << leg.solverCalls << ',' << leg.solverFailures << ',';
        if (leg.lowestDeltaVMps) { std::cout << *leg.lowestDeltaVMps; }
        std::cout << '\n';
    }
    std::cout << "\nSchedule depth (CSV)\nmethod,leg,incoming_labels,chronological_normal,chronological_exception,"
              << "outgoing_labels,prefix_exceptions,prefix_arrival,prefix_delta_v\n";
    printDepth(result.greedy, "greedy");
    printDepth(result.earliest, "earliest");
    return 0;
}
