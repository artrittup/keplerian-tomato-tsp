#pragma once

#include "PermutationScreening.hpp"

using FragmentEvidence = std::vector<std::vector<PairEvidenceKind>>;

struct FragmentAssemblyConfig
{
    std::uint64_t seed = 0;
    std::size_t attempts = 1000;
    std::size_t repairsPerAttempt = 6;
    std::size_t maxUnknownEdges = 0;
};

struct FragmentAssemblyResult
{
    std::vector<std::vector<int>> orders; // Unique complete permutations, in discovery order.
    std::size_t completedAttempts = 0;
    std::size_t failedAttempts = 0;
    std::size_t repairs = 0;
    std::size_t insertions = 0;
};

// Projects Phase 6B's directed evidence onto arc kinds. The diagonal is ignored.
FragmentEvidence fragmentEvidence(const TransferEvidenceMatrix &matrix, std::size_t tomatoCount);

// exceptionLimit is the instance's budget applied to observed exception-only arcs, for screening.
// An empty orders vector means no construction succeeded within the supplied attempt/repair limits.
FragmentAssemblyResult assembleFragments(const FragmentEvidence &evidence, std::size_t exceptionLimit,
                                         const FragmentAssemblyConfig &config = {});
