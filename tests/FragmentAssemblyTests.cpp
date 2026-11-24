#include "FragmentAssembly.hpp"

#include <iostream>
#include <set>
#include <stdexcept>
#include <string_view>

namespace
{
using Kind = PairEvidenceKind;

void require(bool condition, const char *message)
{
    if (!condition) { throw std::runtime_error(message); }
}

FragmentEvidence matrix(std::size_t count)
{
    return FragmentEvidence(count, std::vector<Kind>(count, Kind::RejectedSamples));
}

void direction()
{
    auto edges = matrix(5);
    edges[3][0] = edges[0][4] = edges[4][1] = edges[1][2] = Kind::NormalObserved;
    const auto result = assembleFragments(edges, 0, {7, 16, 0, 0});
    require(result.orders == std::vector<std::vector<int>>{{3, 0, 4, 1, 2}} &&
            result.completedAttempts == 16 && result.failedAttempts == 0,
            "The unique directed path should be recovered and deduplicated");
    require(assembleFragments(matrix(1), 0, {0, 1, 0, 0}).orders == std::vector<std::vector<int>>{{0}},
            "One tomato requires no joins");
}

void insertion()
{
    auto edges = matrix(4);
    edges[0][1] = edges[1][2] = edges[0][2] = edges[2][3] = Kind::NormalObserved;
    const auto result = assembleFragments(edges, 0, {0, 64, 0, 0});
    require(result.orders == std::vector<std::vector<int>>{{0, 1, 2, 3}} &&
            result.failedAttempts == 0 && result.insertions > 0,
            "Restore the missing tomato by replacing the internal shortcut");
}

void repair()
{
    auto edges = matrix(4);
    edges[0][1] = edges[1][2] = edges[2][3] = edges[2][0] = Kind::NormalObserved;
    for (std::uint64_t seed = 0; seed < 64; ++seed)
    {
        const auto stalled = assembleFragments(edges, 0, {seed, 1, 0, 0});
        const auto repaired = assembleFragments(edges, 0, {seed, 1, 2, 0});
        if (stalled.orders.empty() && !repaired.orders.empty())
        {
            require(repaired.orders == std::vector<std::vector<int>>{{0, 1, 2, 3}} &&
                    repaired.repairs > 0 && repaired.repairs <= 2,
                    "Repair must split the blocking fragment and preserve every tomato");
            return;
        }
    }
    throw std::runtime_error("No seeded attempt escaped the directed-cycle trap by repairing a fragment");
}

void failure()
{
    auto edges = matrix(4);
    edges[0][1] = edges[1][2] = edges[2][0] = Kind::NormalObserved;
    const auto result = assembleFragments(edges, 0, {0, 16, 2, 0});
    require(result.orders.empty() && result.completedAttempts == 0 && result.failedAttempts == 16 &&
            result.repairs > 0 && result.repairs <= 32,
            "An isolated tomato must cause bounded construction failure, even beside a cycle");
}

void fallbackLimits()
{
    auto edges = matrix(4);
    edges[1][0] = Kind::NormalObserved;
    edges[0][3] = Kind::ExceptionObserved;
    edges[3][2] = Kind::Unknown;
    FragmentAssemblyConfig config{0, 4, 0, 1};
    require(assembleFragments(edges, 0, config).orders.empty(), "Do not overspend the observed exception budget");
    const auto both = assembleFragments(edges, 1, config);
    require(both.orders == std::vector<std::vector<int>>{{1, 0, 3, 2}}, "Allow each fallback at its configured limit");
    config.maxUnknownEdges = 0;
    require(assembleFragments(edges, 1, config).orders.empty(), "Do not spend unknown edges at a zero cap");
    edges[3][2] = Kind::RejectedSamples;
    config.maxUnknownEdges = 2;
    require(assembleFragments(edges, 2, config).orders.empty(), "Do not treat rejected samples as unmeasured edges");
}

void replay()
{
    const FragmentEvidence edges(5, std::vector<Kind>(5, Kind::NormalObserved));
    const FragmentAssemblyConfig config{11, 64, 2, 0};
    const auto first = assembleFragments(edges, 0, config);
    const auto second = assembleFragments(edges, 0, config);
    require(first.orders == second.orders && first.completedAttempts == second.completedAttempts &&
            first.failedAttempts == second.failedAttempts && first.insertions == second.insertions &&
            first.repairs == second.repairs && first.completedAttempts == 64 && first.orders.size() > 1,
            "Identical seeds should replay diverse complete orders and counters");
    const std::set<std::vector<int>> unique(first.orders.begin(), first.orders.end());
    require(unique.size() == first.orders.size(), "Every returned order should be unique");
    for (const auto &order : first.orders) { require(isTomatoPermutation(order, 5), "A join lost or duplicated a tomato"); }
}

void invalid()
{
    try { assembleFragments({{Kind::Unknown}, {Kind::Unknown}}, 0); }
    catch (const std::invalid_argument &) { return; }
    throw std::runtime_error("Expected rejection of a nonsquare matrix");
}
}

int main(int argc, char *argv[])
{
    if (argc != 2) { return 1; }
    const std::pair<std::string_view, void (*)()> tests[] = {
        {"direction", direction}, {"insertion", insertion}, {"repair", repair}, {"failure", failure},
        {"fallback", fallbackLimits}, {"replay", replay}, {"invalid", invalid}
    };
    try
    {
        for (const auto &test : tests)
        {
            if (test.first != argv[1]) { continue; }
            test.second();
            return 0;
        }
    }
    catch (const std::exception &error)
    {
        std::cerr << argv[1] << ": " << error.what() << '\n';
    }
    return 1;
}
