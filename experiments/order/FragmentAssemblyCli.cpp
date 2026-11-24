#include "FixedOrderScheduler.hpp"
#include "FragmentAssembly.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace
{
using Clock = std::chrono::steady_clock;
using Rank = decltype(PermutationScore{}.rank());

template <class T>
bool parse(std::string_view text, T &value)
{
    const auto end = text.data() + text.size();
    const auto result = std::from_chars(text.data(), end, value);
    return result.ec == std::errc{} && result.ptr == end;
}

void printRank(const Rank &rank)
{
    const auto [unsupported, excess, conflicts, rejected, exceptions] = rank;
    std::cout << unsupported << ',' << excess << ',' << conflicts << ',' << rejected << ',' << exceptions;
}

struct Summary
{
    void add(const PermutationScore &score)
    {
        ++count;
        normal += score.normalEdges;
        exceptions += score.exceptionOnlyEdges;
        rejected += score.rejectedOnlyEdges;
        unknown += score.unknownEdges;
        conflicts += score.incompatibleJoins;
        if (!best || score.rank() < *best) { best = score.rank(); }
    }

    void print(const char *group) const
    {
        std::cout << group << ',' << count;
        if (count > 0)
        {
            for (const auto sum : {normal, exceptions, rejected, unknown, conflicts})
            {
                std::cout << ',' << static_cast<double>(sum) / count;
            }
            std::cout << ',';
            printRank(*best);
        }
        std::cout << '\n';
    }

    std::size_t count = 0, normal = 0, exceptions = 0, rejected = 0, unknown = 0, conflicts = 0;
    std::optional<Rank> best;
};

void compare(const TransferEvidenceMatrix &matrix, const std::optional<std::vector<int>> &reference,
             std::size_t count, const FragmentAssemblyConfig &config, Summary &randomOrders, Summary &ref,
             Summary &swap, Summary &insertion, Summary &reversal)
{
    std::mt19937_64 random(config.seed), neighbor(config.seed);
    if (reference) { ref.add(matrix.score(*reference)); }
    for (std::size_t i = 0; i < config.attempts; ++i)
    {
        randomOrders.add(matrix.score(randomTomatoPermutation(count, random)));
        if (!reference) { continue; }
        swap.add(matrix.score(sampleTomatoNeighbor(*reference, PermutationMove::Swap, neighbor)));
        insertion.add(matrix.score(sampleTomatoNeighbor(*reference, PermutationMove::Insert, neighbor)));
        reversal.add(matrix.score(sampleTomatoNeighbor(*reference, PermutationMove::Reverse, neighbor)));
    }
}
}

int main(int argc, char *argv[])
{
    TransferEvidenceConfig evidenceConfig;
    FragmentAssemblyConfig assemblyConfig;
    if (argc < 8 || argc > 10 || !parse(argv[3], assemblyConfig.seed) || !parse(argv[4], evidenceConfig.maxOrbitalCalls) ||
        !parse(argv[5], assemblyConfig.attempts) || assemblyConfig.attempts == 0 || assemblyConfig.attempts > 10000 ||
        !parse(argv[6], assemblyConfig.repairsPerAttempt) || assemblyConfig.repairsPerAttempt > 64 ||
        !parse(argv[7], assemblyConfig.maxUnknownEdges))
    {
        std::cerr << "Usage: fragment_assemble INSTANCE REFERENCE_ORDER|- SEED MATRIX_CALLS ATTEMPTS REPAIRS UNKNOWN"
                     " [FLIGHT_CAP_DAYS|full [SHORTLIST]]\n";
        return 1;
    }
    std::size_t shortlist = 0;
    if (argc == 10 && (!parse(argv[9], shortlist) || shortlist > 20))
    {
        std::cerr << "Expected a shortlist size between 0 and 20.\n";
        return 1;
    }
    TransferWindowBounds bounds;
    if (argc < 9 || std::string_view(argv[8]) != "full")
    {
        double flightCap = 8.0;
        if ((argc >= 9 && !parse(argv[8], flightCap)) || !std::isfinite(flightCap) || flightCap <= 0.0)
        {
            std::cerr << "Expected a positive flight cap in days or 'full'.\n";
            return 1;
        }
        bounds.maximumFlight = flightCap;
    }
    Problem problem;
    if (!problem.loadFromFile(argv[1])) { return 1; }
    const auto count = problem.tomatoes.size();
    std::optional<std::vector<int>> reference;
    if (std::string_view(argv[2]) != "-")
    {
        std::ifstream file(argv[2]);
        reference = readFixedOrder(file, count);
        if (!reference) { std::cerr << "Expected an order-only comparison file.\n"; return 1; }
    }
    try
    {
        const auto start = Clock::now();
        TransferEvidenceMatrix matrix(problem, evidenceConfig);
        matrix.sampleAll(bounds);
        const auto sampled = Clock::now();
        const auto result = assembleFragments(fragmentEvidence(matrix, count),
                                              static_cast<std::size_t>(problem.maxExceptions), assemblyConfig);
        const auto assembled = Clock::now();

        Summary fragments, randomOrders, ref, swap, insertion, reversal;
        std::vector<std::pair<Rank, std::size_t>> ranked;
        for (std::size_t i = 0; i < result.orders.size(); ++i)
        {
            const auto score = matrix.score(result.orders[i]);
            fragments.add(score);
            ranked.emplace_back(score.rank(), i);
        }
        compare(matrix, reference, count, assemblyConfig, randomOrders, ref, swap, insertion, reversal);
        std::sort(ranked.begin(), ranked.end());
        const auto scored = Clock::now();
        const auto stats = matrix.stats();
        std::cout << std::setprecision(9) << "Policy: strict\nSeed: " << assemblyConfig.seed
                  << "\nAttempts: " << assemblyConfig.attempts << "\nRepairs per attempt: " << assemblyConfig.repairsPerAttempt
                  << "\nUnknown edge cap: " << assemblyConfig.maxUnknownEdges
                  << "\nMatrix call cap: " << evidenceConfig.maxOrbitalCalls << "\nFlight cap days: ";
        if (bounds.maximumFlight) { std::cout << *bounds.maximumFlight; }
        else { std::cout << "full"; }
        std::cout << "\nMatrix calls: " << stats.solverCalls << "\nMatrix cache hits: " << stats.hits
                  << "\nComplete attempts: " << result.completedAttempts << "\nUnique orders: " << result.orders.size()
                  << "\nDuplicate completions: " << result.completedAttempts - result.orders.size()
                  << "\nConstruction failures: " << result.failedAttempts << "\nRepair splits: " << result.repairs
                  << "\nInternal insertions: " << result.insertions
                  << "\nSampling seconds: " << std::chrono::duration<double>(sampled - start).count()
                  << "\nAssembly seconds: " << std::chrono::duration<double>(assembled - sampled).count()
                  << "\nScoring seconds: " << std::chrono::duration<double>(scored - assembled).count() << '\n'
                  << "group,count,mean_normal,mean_exception_only,mean_rejected_only,mean_unknown,mean_incompatible,"
                     "best_unsupported,best_exception_excess,best_incompatible,best_rejected,best_exception_only\n";
        fragments.print("assembled");
        randomOrders.print("random");
        if (reference)
        {
            ref.print("reference_order");
            swap.print("reference_swap");
            insertion.print("reference_insertion");
            reversal.print("reference_reversal");
        }
        for (std::size_t i = 0; i < std::min(shortlist, ranked.size()); ++i)
        {
            std::cout << "order," << (i + 1) << ',';
            const auto &order = result.orders[ranked[i].second];
            for (std::size_t j = 0; j < order.size(); ++j) { std::cout << (j ? " " : "") << order[j]; }
            std::cout << '\n';
        }
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
