#pragma once

#include "TransferWindowExplorer.hpp"

#include <cstddef>
#include <optional>
#include <random>
#include <tuple>
#include <vector>

enum class PermutationMove { Swap, Insert, Reverse };

bool isTomatoPermutation(const std::vector<int> &order, std::size_t tomatoCount);
// Indices are positions. Insert places the source item at the given final position.
// Reverse includes both positions. Identical positions are a no-op.
void moveTomatoes(std::vector<int> &order, PermutationMove move, std::size_t from, std::size_t to);
std::vector<int> randomTomatoPermutation(std::size_t tomatoCount, std::mt19937_64 &random);
std::vector<int> sampleTomatoNeighbor(const std::vector<int> &order, PermutationMove move, std::mt19937_64 &random);

enum class PairEvidenceKind { Unknown, RejectedSamples, ExceptionObserved, NormalObserved };

struct PairEvidence
{
    // Counts of exact samples within each window query, including cached results across queries.
    std::size_t normal = 0;
    std::size_t exceptions = 0;
    std::size_t rejected = 0; // Finite Delta-V above the strict ceiling.
    std::size_t solverFailures = 0;
    std::optional<double> earliestArrival;
    std::optional<double> latestDeparture;

    PairEvidenceKind kind() const noexcept;
};

struct PermutationScore
{
    std::size_t normalEdges = 0;
    std::size_t exceptionOnlyEdges = 0;
    std::size_t rejectedOnlyEdges = 0;
    std::size_t unknownEdges = 0;
    std::size_t exceptionsOverBudget = 0; // Based on observed exception-only pairs.
    std::size_t compatibleJoins = 0;
    std::size_t incompatibleJoins = 0;
    std::size_t unassessedJoins = 0;

    // Lower is better. A ranking key for sampled evidence, with no units of time.
    auto rank() const noexcept
    {
        return std::make_tuple(rejectedOnlyEdges + unknownEdges, exceptionsOverBudget,
                               incompatibleJoins, rejectedOnlyEdges, exceptionOnlyEdges);
    }
};

struct TransferEvidenceConfig
{
    std::size_t maxOrbitalCalls = 150000;
    std::size_t maxPairs = 100000;
    std::size_t cacheCapacity = 32768;
};

class TransferEvidenceMatrix
{
public:
    // Owns one immutable problem snapshot through TransferCache. All evidence uses strict Delta-V.
    explicit TransferEvidenceMatrix(const Problem &problem, const TransferEvidenceConfig &config = {});

    // Split the remaining shared call budget across all directed non-self pairs, independently of any order.
    void sampleAll(const TransferWindowBounds &bounds = {});
    TransferWindowStats samplePair(std::size_t source, std::size_t destination, std::size_t callAllowance,
                                   const TransferWindowBounds &bounds = {});
    const PairEvidence &at(std::size_t source, std::size_t destination) const;
    TransferCacheStats stats() const noexcept { return cache_.stats(); }
    PermutationScore score(const std::vector<int> &order) const;

private:
    std::size_t index(std::size_t source, std::size_t destination) const;

    const std::size_t maxOrbitalCalls_;
    TransferCache cache_;
    std::vector<PairEvidence> pairs_;
};
