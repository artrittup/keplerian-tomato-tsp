#include "FixedOrderScheduler.hpp"
#include "FragmentAssembly.hpp"
#include "OrbitalDiagnostics.hpp"
#include "SolutionEvaluator.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <locale>
#include <stdexcept>
#include <string_view>

namespace
{
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;
using Rank = decltype(PermutationScore{}.rank());

struct Config
{
    TransferEvidenceConfig evidence{1204224, 100000, 32768};
    FragmentAssemblyConfig assembly{7, 1000, 6, 0};
    FixedOrderConfig scheduler{4000000, 32, 50, 2.0, 0, 32768};
    std::optional<double> flightCap = 8.0;
    std::size_t shortlist = 1;
    fs::path trace;
    bool profile = false;
};

template <class T>
T number(std::string_view flag, std::string_view text)
{
    T value{};
    const auto end = text.data() + text.size();
    const auto parsed = std::from_chars(text.data(), end, value);
    if (parsed.ec != std::errc{} || parsed.ptr != end)
    {
        throw std::invalid_argument("Invalid value for " + std::string(flag));
    }
    return value;
}

Config readConfig(int argc, char *argv[])
{
    Config c;
    for (int i = 4; i < argc; i += 2)
    {
        const std::string_view flag = argv[i];
        if (i + 1 == argc) { throw std::invalid_argument("Missing value for " + std::string(flag)); }
        const std::string_view value = argv[i + 1];
        if (flag == "--assembly-seed") { c.assembly.seed = number<std::uint64_t>(flag, value); }
        else if (flag == "--timing-seed") { c.scheduler.seed = number<std::uint64_t>(flag, value); }
        else if (flag == "--matrix-calls") { c.evidence.maxOrbitalCalls = number<std::size_t>(flag, value); }
        else if (flag == "--matrix-flight-days")
        {
            if (value == "full") { c.flightCap.reset(); }
            else { c.flightCap = number<double>(flag, value); }
        }
        else if (flag == "--attempts") { c.assembly.attempts = number<std::size_t>(flag, value); }
        else if (flag == "--repairs") { c.assembly.repairsPerAttempt = number<std::size_t>(flag, value); }
        else if (flag == "--unknown") { c.assembly.maxUnknownEdges = number<std::size_t>(flag, value); }
        else if (flag == "--shortlist") { c.shortlist = number<std::size_t>(flag, value); }
        else if (flag == "--schedule-calls") { c.scheduler.maxOrbitalCalls = number<std::size_t>(flag, value); }
        else if (flag == "--bands") { c.scheduler.departureBands = number<std::size_t>(flag, value); }
        else if (flag == "--refinement-percent") { c.scheduler.refinementPercent = number<std::size_t>(flag, value); }
        else if (flag == "--flight-scale") { c.scheduler.flightScaleDays = number<double>(flag, value); }
        else if (flag == "--cache-capacity")
        {
            c.evidence.cacheCapacity = c.scheduler.cacheCapacity = number<std::size_t>(flag, value);
        }
        else if (flag == "--trace") { c.trace = value; }
        else if (flag == "--profile" && (value == "on" || value == "off")) { c.profile = value == "on"; }
        else if (flag == "--schedule-cache-index" && (value == "tree" || value == "hash"))
        {
            c.scheduler.cacheIndex = value == "hash" ? TransferCacheIndex::Hash : TransferCacheIndex::Tree;
        }
        else { throw std::invalid_argument("Unknown option " + std::string(flag)); }
    }
    if ((c.flightCap && (!std::isfinite(*c.flightCap) || *c.flightCap <= 0)) ||
        c.assembly.attempts == 0 || c.assembly.attempts > 10000 || c.assembly.repairsPerAttempt > 64 ||
        c.shortlist == 0 || c.shortlist > 20 || c.scheduler.departureBands == 0 || c.scheduler.departureBands > 4096 ||
        c.scheduler.refinementPercent >= 100 || !std::isfinite(c.scheduler.flightScaleDays) || c.scheduler.flightScaleDays <= 0)
    {
        throw std::invalid_argument("Configuration outside the supported range");
    }
    return c;
}

std::size_t totalCallCap(const Config &c, std::size_t legs)
{
    // The scheduler's per-method cap includes one replay. Reserve the other, and one for the export.
    const auto max = std::numeric_limits<std::size_t>::max();
    if (legs > max - c.evidence.maxOrbitalCalls || c.scheduler.maxOrbitalCalls > max - legs)
    {
        throw std::invalid_argument("Orbital call budget is too large");
    }
    const auto perCandidate = c.scheduler.maxOrbitalCalls + legs;
    if (perCandidate != 0 && c.shortlist > (max - c.evidence.maxOrbitalCalls - legs) / perCandidate)
    {
        throw std::invalid_argument("Orbital call budget is too large");
    }
    return c.evidence.maxOrbitalCalls + c.shortlist * perCandidate + legs;
}

double seconds(Clock::time_point start, Clock::time_point end = Clock::now())
{
    return std::chrono::duration<double>(end - start).count();
}

template <class T>
void readValues(std::istream &input, std::vector<T> &values, std::size_t count)
{
    values.resize(count);
    for (auto &value : values)
    {
        if (!(input >> value)) { throw std::runtime_error("Malformed or truncated schedule"); }
    }
}

SolutionEvaluationResult validateFile(const Problem &problem, const fs::path &path)
{
    const auto count = problem.tomatoes.size();
    std::ifstream input(path);
    input.imbue(std::locale::classic());
    if (!input) { throw std::runtime_error("Cannot open schedule for validation"); }
    Solution result;
    readValues(input, result.departureTimes, count - 1);
    readValues(input, result.timesOfFlight, count - 1);
    readValues(input, result.visitingOrder, count);
    input >> std::ws;
    if (!input.eof() || input.bad()) { throw std::runtime_error("Unexpected trailing data in schedule"); }
    return SolutionEvaluator::evaluate(problem, result, DeltaVPolicy::Strict);
}

struct PendingExport
{
    fs::path directory;
    fs::path file;

    explicit PendingExport(const fs::path &output) : directory(output)
    {
        directory += ".pending";
        file = directory / "schedule.sol";
        if (!fs::create_directory(directory)) { throw std::runtime_error("Schedule staging directory already exists"); }
    }

    ~PendingExport()
    {
        std::error_code ignored;
        fs::remove(file, ignored);
        fs::remove(directory, ignored);
    }
};

void writeSchedule(const fs::path &path, const Solution &solution)
{
    std::ofstream output(path, std::ios::binary);
    output.imbue(std::locale::classic());
    output << std::setprecision(std::numeric_limits<double>::max_digits10);
    for (const auto value : solution.departureTimes) { output << value << '\n'; }
    for (const auto value : solution.timesOfFlight) { output << value << '\n'; }
    for (const auto value : solution.visitingOrder) { output << value << '\n'; }
    output.close();
    if (!output) { throw std::runtime_error("Could not write schedule"); }
}

bool validated(const SolutionEvaluationResult &result, std::size_t legs)
{
    return result.isFeasible() && result.missionCompletionDays && result.legs.size() == legs;
}

void printValidation(const SolutionEvaluationResult &result)
{
    std::cout << "orbital.final_validation=" << result.legs.size() << '\n' << "exceptions=" << result.exceptionCount << '\n';
    if (result.missionCompletionDays) { std::cout << "completion.days=" << *result.missionCompletionDays << '\n'; }
}

void printCache(const char *stage, const TransferCacheStats &stats)
{
    std::cout << "orbital." << stage << '=' << stats.solverCalls << '\n'
              << "cache." << stage << ".hits=" << stats.hits << '\n'
              << "cache." << stage << ".misses=" << stats.misses << '\n'
              << "cache." << stage << ".budget_denials=" << stats.budgetDenials << '\n'
              << "cache." << stage << ".evictions=" << stats.evictions << '\n';
}

void addCache(TransferCacheStats &total, const TransferCacheStats &next)
{
    total.solverCalls += next.solverCalls;
    total.hits += next.hits;
    total.misses += next.misses;
    total.budgetDenials += next.budgetDenials;
    total.evictions += next.evictions;
}

void printConfig(const Config &c, std::size_t count)
{
    std::cout << "policy=strict\ntomatoes=" << count << "\nconfig.assembly_seed=" << c.assembly.seed
              << "\nconfig.timing_seed=" << c.scheduler.seed << "\nconfig.matrix_calls=" << c.evidence.maxOrbitalCalls
              << "\nconfig.matrix_flight_days=";
    if (c.flightCap) { std::cout << *c.flightCap; }
    else { std::cout << "full"; }
    std::cout << "\nconfig.max_directed_pairs=" << c.evidence.maxPairs << "\nconfig.attempts=" << c.assembly.attempts
              << "\nconfig.repairs=" << c.assembly.repairsPerAttempt << "\nconfig.unknown=" << c.assembly.maxUnknownEdges
              << "\nconfig.shortlist=" << c.shortlist << "\nconfig.schedule_calls_per_method=" << c.scheduler.maxOrbitalCalls
              << "\nconfig.bands=" << c.scheduler.departureBands << "\nconfig.refinement_percent=" << c.scheduler.refinementPercent
              << "\nconfig.flight_scale=" << c.scheduler.flightScaleDays << "\nconfig.cache_capacity=" << c.evidence.cacheCapacity
              << "\nconfig.schedule_cache_index=" << (c.scheduler.cacheIndex == TransferCacheIndex::Hash ? "hash" : "tree")
              << "\norbital.cap=" << totalCallCap(c, count - 1) << '\n';
}

int solve(const Problem &problem, const fs::path &output, const Config &config, Clock::time_point start, double parsing)
{
    if (fs::exists(fs::symlink_status(output))) { throw std::runtime_error("Output already exists. Use a fresh path"); }
    if (output.filename().empty() || (!output.parent_path().empty() && !fs::is_directory(output.parent_path())))
    {
        throw std::runtime_error("Expected an output filename in an existing directory");
    }
    const auto count = problem.tomatoes.size();
    const auto legs = count - 1;
    printConfig(config, count);
    if (!config.trace.empty() && fs::absolute(output).lexically_normal() == fs::absolute(config.trace).lexically_normal())
    {
        throw std::invalid_argument("Trace and solution require separate output paths");
    }
    OrbitalDiagnostics diagnostics(config.profile, config.trace);
    const auto beginSampling = Clock::now();
    TransferEvidenceMatrix matrix(problem, config.evidence);
    TransferWindowBounds bounds;
    bounds.maximumFlight = config.flightCap;
    matrix.sampleAll(bounds);
    const auto sampled = Clock::now();
    const auto assembly = assembleFragments(fragmentEvidence(matrix, count), static_cast<std::size_t>(problem.maxExceptions),
                                            config.assembly);
    std::vector<std::pair<Rank, std::size_t>> ranked;
    for (std::size_t i = 0; i < assembly.orders.size(); ++i)
    {
        ranked.emplace_back(matrix.score(assembly.orders[i]).rank(), i);
    }
    std::sort(ranked.begin(), ranked.end());
    ranked.resize(std::min(ranked.size(), config.shortlist));
    const auto assembled = Clock::now();
    diagnostics.stage(OrbitalStage::Schedule);

    TransferCacheStats scheduling;
    std::size_t internalValidation = 0, feasible = 0, selected = 0;
    const char *method = "";
    double bestDays = std::numeric_limits<double>::infinity();
    std::optional<Solution> best;
    for (std::size_t i = 0; i < ranked.size(); ++i)
    {
        const auto &order = assembly.orders[ranked[i].second];
        auto outcome = FixedOrderScheduler::compare(problem, order, config.scheduler);
        addCache(scheduling, outcome.discovery);
        internalValidation += outcome.greedy.validationCalls + outcome.earliest.validationCalls;
        for (auto *candidate : {&outcome.greedy, &outcome.earliest})
        {
            if (!candidate->solution) { continue; }
            ++feasible;
            if (*candidate->completionDays >= bestDays) { continue; }
            bestDays = *candidate->completionDays;
            best = std::move(candidate->solution);
            selected = i + 1;
            method = candidate == &outcome.greedy ? "greedy" : "earliest";
        }
    }
    const auto scheduled = Clock::now();
    std::cout << "assembly.completed_attempts=" << assembly.completedAttempts
              << "\nassembly.failed_attempts=" << assembly.failedAttempts << "\nassembly.unique_orders=" << assembly.orders.size()
              << "\nassembly.repairs=" << assembly.repairs << "\nshortlist.count=" << ranked.size()
              << "\nschedule.feasible_count=" << feasible << '\n';
    for (std::size_t i = 0; i < ranked.size(); ++i)
    {
        std::cout << "shortlist." << (i + 1) << ".order=";
        bool first = true;
        for (const auto tomato : assembly.orders[ranked[i].second])
        {
            if (!first) { std::cout << ' '; }
            std::cout << tomato;
            first = false;
        }
        std::cout << '\n';
    }
    const auto sampledCalls = matrix.stats();
    printCache("matrix", sampledCalls);
    printCache("schedule_discovery", scheduling);
    std::cout << "orbital.schedule_validation=" << internalValidation << '\n';

    double validationTime = 0, exportTime = 0;
    SolutionEvaluationResult validation;
    const auto status = [&]()
    {
        if (!best) { return ranked.empty() ? "no_complete_order" : "no_feasible_schedule"; }
        const auto writing = Clock::now();
        PendingExport pending(output);
        writeSchedule(pending.file, *best);
        const auto validating = Clock::now();
        diagnostics.stage(OrbitalStage::FinalValidation);
        validation = validateFile(problem, pending.file);
        const auto validatedAt = Clock::now();
        validationTime = seconds(validating, validatedAt);
        exportTime = seconds(writing, validating);
        if (!validated(validation, legs) || *validation.missionCompletionDays != bestDays) { return "validation_failed"; }
        if (fs::exists(fs::symlink_status(output))) { throw std::runtime_error("Output already exists. Use a fresh path"); }
        diagnostics.finish(std::cout);
        fs::rename(pending.file, output);
        exportTime += seconds(validatedAt);
        return "ok";
    }();
    const auto totalTime = seconds(start);
    const auto success = std::string_view(status) == "ok";
    if (!success) { diagnostics.finish(std::cout); }
    std::cout << "status=" << status << '\n';
    if (success)
    {
        std::cout << "selected.candidate=" << selected << "\nselected.method=" << method << '\n';
        printValidation(validation);
    }
    else { std::cout << "orbital.final_validation=" << validation.legs.size() << '\n'; }
    std::cout << "orbital.total=" << sampledCalls.solverCalls + scheduling.solverCalls + internalValidation + validation.legs.size()
              << "\nseconds.parse=" << parsing << "\nseconds.matrix=" << seconds(beginSampling, sampled)
              << "\nseconds.assembly=" << seconds(sampled, assembled) << "\nseconds.schedule=" << seconds(assembled, scheduled)
              << "\nseconds.final_validation=" << validationTime << "\nseconds.export=" << exportTime
              << "\nseconds.total=" << totalTime << '\n';
    return success ? 0 : 2;
}

void usage(std::ostream &out)
{
    out << "Usage: cpu_baseline solve INSTANCE OUTPUT [OPTIONS]\n"
           "       cpu_baseline validate INSTANCE SCHEDULE\n"
           "Options: --assembly-seed N --timing-seed N --matrix-calls N --matrix-flight-days DAYS|full\n"
           "         --attempts N --repairs N --unknown N --shortlist N --schedule-calls N\n"
           "         --bands N --refinement-percent N --flight-scale DAYS --cache-capacity N\n"
           "         --schedule-cache-index tree|hash --trace FILE --profile on|off\n";
}
}

int main(int argc, char *argv[])
{
    const auto start = Clock::now();
    if (argc == 2 && std::string_view(argv[1]) == "--help") { usage(std::cout); return 0; }
    if (argc < 4) { usage(std::cerr); return 1; }
    const std::string_view command = argv[1];
    if (command != "solve" && (command != "validate" || argc != 4)) { usage(std::cerr); return 1; }
    try
    {
        std::cout << std::setprecision(std::numeric_limits<double>::max_digits10);
        const auto beginParsing = Clock::now();
        Problem problem;
        if (!problem.loadFromFile(argv[2])) { return 1; }
        const auto parsing = seconds(beginParsing);
        if (command == "solve") { return solve(problem, argv[3], readConfig(argc, argv), start, parsing); }
        const auto result = validateFile(problem, argv[3]);
        const auto success = validated(result, problem.tomatoes.size() - 1);
        std::cout << "policy=strict\nstatus=" << (success ? "ok" : "validation_failed") << '\n';
        printValidation(result);
        return success ? 0 : 2;
    }
    catch (const std::exception &error)
    {
        std::cerr << "cpu_baseline: " << error.what() << '\n';
        return 1;
    }
}
