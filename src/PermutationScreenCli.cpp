#include "FixedOrderScheduler.hpp"
#include "PermutationScreening.hpp"

#include <array>
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

template <class T>
bool parse(std::string_view text, T &value)
{
    const auto end = text.data() + text.size();
    const auto result = std::from_chars(text.data(), end, value);
    return result.ec == std::errc{} && result.ptr == end;
}

auto counts(const PermutationScore &score)
{
    return std::array{score.normalEdges, score.exceptionOnlyEdges, score.rejectedOnlyEdges, score.unknownEdges,
                      score.exceptionsOverBudget, score.compatibleJoins, score.incompatibleJoins, score.unassessedJoins};
}

struct Summary
{
    void add(const PermutationScore &score, const PermutationScore &reference)
    {
        ++trials;
        better += score.rank() < reference.rank();
        tied += score.rank() == reference.rank();
        const auto values = counts(score);
        for (std::size_t i = 0; i < values.size(); ++i) { totals[i] += values[i]; }
        if (!best || score.rank() < best->rank()) { best = score; }
    }

    void print(const char *group) const
    {
        std::cout << group << ',' << trials << ',' << better << ',' << tied;
        for (const auto sum : totals) { std::cout << ',' << static_cast<double>(sum) / trials; }
        const auto [unsupported, excess, conflicts, rejected, exceptions] = best->rank();
        std::cout << ',' << unsupported << ',' << excess << ',' << conflicts << ',' << rejected << ',' << exceptions << '\n';
    }

    std::size_t trials = 0;
    std::size_t better = 0;
    std::size_t tied = 0;
    std::array<std::size_t, 8> totals{};
    std::optional<PermutationScore> best;
};

void printEvidence(const TransferEvidenceMatrix &matrix, std::size_t tomatoCount)
{
    std::array<std::size_t, 4> pairs{};
    std::size_t solverFailures = 0;
    for (std::size_t source = 0; source < tomatoCount; ++source)
    {
        for (std::size_t destination = 0; destination < tomatoCount; ++destination)
        {
            if (source == destination) { continue; }
            const auto &pair = matrix.at(source, destination);
            ++pairs[static_cast<std::size_t>(pair.kind())];
            solverFailures += pair.solverFailures;
        }
    }
    const auto stats = matrix.stats();
    std::cout << "Normal pairs: " << pairs[static_cast<std::size_t>(PairEvidenceKind::NormalObserved)]
              << "\nException-only pairs: " << pairs[static_cast<std::size_t>(PairEvidenceKind::ExceptionObserved)]
              << "\nRejected-only pairs: " << pairs[static_cast<std::size_t>(PairEvidenceKind::RejectedSamples)]
              << "\nUnknown pairs: " << pairs[static_cast<std::size_t>(PairEvidenceKind::Unknown)]
              << "\nSolver failure samples: " << solverFailures << "\nOrbital calls: " << stats.solverCalls
              << "\nCache hits: " << stats.hits << "\nBudget denials: " << stats.budgetDenials << '\n';
}
}

int main(int argc, char *argv[])
{
    std::uint64_t seed = 0;
    std::size_t trials = 0;
    TransferEvidenceConfig config;
    if (argc < 6 || argc > 7 || !parse(argv[3], seed) || !parse(argv[4], config.maxOrbitalCalls) ||
        !parse(argv[5], trials) || trials == 0 || trials > 10000)
    {
        std::cerr << "Usage: permutation_screen INSTANCE REFERENCE_ORDER SEED MAX_CALLS TRIALS [MAX_FLIGHT_DAYS|full]\n"
                  << "TRIALS is 1 to 10000 per group. Default flight cap is 8 days.\n";
        return 1;
    }
    TransferWindowBounds bounds;
    double flightCap = 8.0;
    if (argc == 6 || std::string_view(argv[6]) != "full")
    {
        if ((argc == 7 && !parse(argv[6], flightCap)) || !std::isfinite(flightCap) || flightCap <= 0.0)
        {
            std::cerr << "Expected a positive flight cap in days or 'full'.\n";
            return 1;
        }
        bounds.maximumFlight = flightCap;
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
    try
    {
        const auto start = Clock::now();
        TransferEvidenceMatrix matrix(problem, config);
        matrix.sampleAll(bounds);
        const auto sampled = Clock::now();
        std::mt19937_64 random(seed);
        const auto reference = matrix.score(*order);
        Summary baseline, swaps, insertions, reversals, randomOrders;
        baseline.add(reference, reference);
        for (std::size_t i = 0; i < trials; ++i)
        {
            swaps.add(matrix.score(sampleTomatoNeighbor(*order, PermutationMove::Swap, random)), reference);
            insertions.add(matrix.score(sampleTomatoNeighbor(*order, PermutationMove::Insert, random)), reference);
            reversals.add(matrix.score(sampleTomatoNeighbor(*order, PermutationMove::Reverse, random)), reference);
            randomOrders.add(matrix.score(randomTomatoPermutation(order->size(), random)), reference);
        }
        const auto scored = Clock::now();
        std::cout << std::setprecision(9) << "Policy: strict\nSeed: " << seed << "\nCall cap: " << config.maxOrbitalCalls
                  << "\nFlight cap days: ";
        if (bounds.maximumFlight) { std::cout << *bounds.maximumFlight; }
        else { std::cout << "full"; }
        std::cout << '\n';
        printEvidence(matrix, order->size());
        std::cout << "Sampling seconds: " << std::chrono::duration<double>(sampled - start).count()
                  << "\nScreening seconds: " << std::chrono::duration<double>(scored - sampled).count()
                  << "\ngroup,trials,better_than_reference,tied_with_reference,mean_normal,mean_exception_only,"
                     "mean_rejected_only,mean_unknown,mean_exception_excess,mean_compatible,mean_incompatible,"
                     "mean_unassessed,best_unsupported,best_exception_excess,best_incompatible,best_rejected,best_exception_only\n";
        baseline.print("reference_order");
        swaps.print("swap");
        insertions.print("insertion");
        reversals.print("reversal");
        randomOrders.print("random");
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
