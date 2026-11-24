#pragma once

#include "BeamSearch.hpp"
#include "PartialRoute.hpp"
#include "TransferWindowExplorer.hpp"

#include <utility>

namespace beam_detail
{
struct HybridProposal
{
    LegTiming timing;
    RouteExtensionResult extension;
    bool paidBroadCall;
};

struct HybridExpansion
{
    std::vector<HybridProposal> proposals;
    std::size_t broadCalls = 0;
    std::size_t refinementCalls = 0;
    std::size_t refinedRegions = 0;
    std::size_t duplicateProposals = 0;
    std::size_t destinationsWithoutTiming = 0;
    std::size_t refinementSolverFailures = 0;
    std::size_t refinementAboveCeiling = 0;
};

class HybridTiming
{
public:
    static HybridExpansion expand(TransferCache &cache, const PartialRoute &parent, const std::vector<int> &destinations,
                                  TimingSampler &sampler, const BeamSearchConfig &config, double localSlack,
                                  std::size_t availableCalls, const std::optional<std::pair<int, LegTiming>> &witness);
    // Absolute allowance for the shared cache, including calls already paid there.
    static RouteExtensionResult evaluate(TransferCache &cache, const PartialRoute &parent, int destination,
                                         LegTiming timing, std::size_t cacheLimit);
};
}
