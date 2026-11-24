#include "BeamHybrid.hpp"
#include "TransferKey.hpp"

#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>

namespace beam_detail
{
namespace
{
using ProposalKey = std::tuple<int, std::uint64_t, std::uint64_t>;
constexpr auto departureBins = beamTimingBinEdges.size() + 1;

ProposalKey key(int destination, LegTiming timing)
{
    return {destination, transfer_detail::timeBits(timing.departureDays), transfer_detail::timeBits(timing.flightDays)};
}

std::size_t percentOf(std::size_t count, std::size_t percent)
{
    return (count / 100) * percent + (count % 100) * percent / 100;
}

std::optional<LegTiming> sampleBroad(TimingSampler &sampler, const PartialRoute &parent, double slack,
                                     std::size_t sample, std::size_t count, std::size_t explorationPercent)
{
    const auto priorWide = percentOf(sample, explorationPercent);
    if (percentOf(sample + 1, explorationPercent) > priorWide)
    {
        return sampler.sample(parent);
    }
    return sampler.sampleStratified(parent, slack, sample - priorWide, count - percentOf(count, explorationPercent));
}

struct Guide
{
    int destination;
    std::size_t departureBin;
    LegTiming timing;
    double cost;
};

std::vector<Guide> selectRegions(const std::map<std::pair<int, std::size_t>, Guide> &guides, std::size_t count)
{
    std::vector<Guide> ranked;
    for (const auto &entry : guides)
    {
        ranked.push_back(entry.second);
    }
    std::sort(ranked.begin(), ranked.end(), [](const auto &a, const auto &b)
    {
        return std::tie(a.cost, a.timing.departureDays, a.destination, a.timing.flightDays) <
               std::tie(b.cost, b.timing.departureDays, b.destination, b.timing.flightDays);
    });

    std::set<int> usedDestinations;
    std::array<bool, departureBins> usedBins{};
    std::vector<bool> chosen(ranked.size(), false);
    std::vector<Guide> result;
    for (int pass = 0; pass < 3 && result.size() < count; ++pass)
    {
        for (std::size_t i = 0; i < ranked.size() && result.size() < count; ++i)
        {
            const auto &guide = ranked[i];
            if (chosen[i] || (pass < 2 && usedDestinations.count(guide.destination)) ||
                (pass == 0 && usedBins[guide.departureBin]))
            {
                continue;
            }
            result.push_back(guide);
            chosen[i] = true;
            usedBins[guide.departureBin] = true;
            usedDestinations.insert(guide.destination);
        }
    }
    return result;
}
}

RouteExtensionResult HybridTiming::evaluate(TransferCache &cache, const PartialRoute &parent, int destination,
                                            LegTiming timing, std::size_t cacheLimit)
{
    if (&cache.problem() != parent.problem_)
    {
        throw std::logic_error("Hybrid timing must use the partial route's problem snapshot");
    }
    const auto status = parent.checkExtension(destination, timing);
    if (status != RouteExtensionStatus::Ok)
    {
        return {status, std::nullopt, std::nullopt};
    }
    const auto &order = parent.prefix().visitingOrder;
    const auto raw = cache.evaluate({static_cast<std::size_t>(order.back()), static_cast<std::size_t>(destination),
                                     timing.departureDays, timing.flightDays}, cacheLimit);
    return raw ? parent.appendEvaluated(destination, timing, *raw)
               : RouteExtensionResult{RouteExtensionStatus::Ok, std::nullopt, std::nullopt};
}

HybridExpansion HybridTiming::expand(TransferCache &cache, const PartialRoute &parent,
                                     const std::vector<int> &destinations, TimingSampler &sampler,
                                     const BeamSearchConfig &config, double localSlack, std::size_t availableCalls,
                                     const std::optional<std::pair<int, LegTiming>> &witness)
{
    HybridExpansion result;
    const auto targets = destinations.size();
    if (targets == 0 || availableCalls == 0)
    {
        return result;
    }
    const auto allocated = config.samplesPerDestination > availableCalls / targets
        ? availableCalls : config.samplesPerDestination * targets;
    const auto refineQuota = percentOf(allocated, config.hybridRefinementPercent);
    const auto broadQuota = allocated - refineQuota;
    const auto beforeBroad = cache.stats().solverCalls;

    std::set<ProposalKey> seen;
    if (witness)
    {
        seen.insert(key(witness->first, witness->second));
    }
    std::map<std::pair<int, std::size_t>, Guide> guides;
    std::vector<bool> withoutTiming(targets, false);
    for (std::size_t i = 0; i < broadQuota; ++i)
    {
        const auto slot = i % targets;
        if (withoutTiming[slot])
        {
            continue;
        }
        const int destination = destinations[slot];
        const auto count = broadQuota / targets + (slot < broadQuota % targets);
        const auto timing = sampleBroad(sampler, parent, localSlack, i / targets, count,
                                        config.hybridExplorationPercent);
        if (!timing)
        {
            withoutTiming[slot] = true;
            ++result.destinationsWithoutTiming;
            continue;
        }
        if (!seen.insert(key(destination, *timing)).second)
        {
            ++result.duplicateProposals;
            continue;
        }

        const auto before = cache.stats().solverCalls;
        auto extension = evaluate(cache, parent, destination, *timing, beforeBroad + broadQuota);
        const bool paid = before != cache.stats().solverCalls;
        if (refineQuota != 0 && extension.transfer && extension.transfer->status == TransferStatus::Ok)
        {
            const auto wait = timing->departureDays - parent.arrivalDays();
            const auto bin = static_cast<std::size_t>(std::upper_bound(beamTimingBinEdges.begin(),
                beamTimingBinEdges.end(), wait) - beamTimingBinEdges.begin());
            const auto guideKey = std::make_pair(destination, bin);
            const auto found = guides.find(guideKey);
            const double cost = extension.transfer->deltaVMps;
            if (found == guides.end() || cost < found->second.cost)
            {
                guides.insert_or_assign(guideKey, Guide{destination, bin, *timing, cost});
            }
        }
        result.proposals.push_back({*timing, std::move(extension), paid});
    }
    result.broadCalls = cache.stats().solverCalls - beforeBroad;
    if (guides.empty())
    {
        return result;
    }

    const auto slots = std::min(config.hybridRefinementRegions, refineQuota);
    const auto regions = selectRegions(guides, slots);
    const double deadline = parent.latestLegArrivalDays();
    const auto &problem = cache.problem();
    const double radius = std::max(problem.minTimeOfFlight, localSlack / 4.0);
    const auto source = static_cast<std::size_t>(parent.prefix().visitingOrder.back());
    for (std::size_t i = 0; i < regions.size(); ++i)
    {
        const auto &guide = regions[i];
        const auto &centre = guide.timing;
        // Split the quota across planned slots even when fewer regions qualified.
        const auto share = refineQuota / slots + (i < refineQuota % slots);
        const TransferWindowBounds bounds{
            std::max(parent.arrivalDays(), centre.departureDays - radius),
            std::min(deadline, centre.departureDays + radius),
            std::max(problem.minTimeOfFlight, centre.flightDays - radius),
            std::min(deadline - parent.arrivalDays(), centre.flightDays + radius),
            deadline
        };
        TransferWindowConfig window;
        window.policy = config.policy;
        window.coarseDivisions = 2;
        window.maxEvaluations = share;
        window.maxSamples = 13 + 4 * std::min<std::size_t>(share, 1024);
        const auto refined = TransferWindowExplorer::explore(cache, source,
            static_cast<std::size_t>(guide.destination), window, bounds);
        ++result.refinedRegions;
        result.refinementCalls += refined.stats.solverCalls;
        result.refinementSolverFailures += refined.stats.solverFailures;
        result.refinementAboveCeiling += refined.stats.solverCalls + refined.stats.cacheHits -
                                        refined.stats.solverFailures - refined.opportunities.size();
        for (const auto &opportunity : refined.opportunities)
        {
            const LegTiming timing{opportunity.query.departureDays, opportunity.query.flightDays};
            if (!seen.insert(key(guide.destination, timing)).second)
            {
                ++result.duplicateProposals;
                continue;
            }
            const auto status = parent.checkExtension(guide.destination, timing);
            auto extension = status == RouteExtensionStatus::Ok
                ? parent.appendEvaluated(guide.destination, timing, {TransferStatus::Ok, opportunity.deltaVMps})
                : RouteExtensionResult{status, std::nullopt, std::nullopt};
            result.proposals.push_back({timing, std::move(extension), false});
        }
    }
    return result;
}
}
