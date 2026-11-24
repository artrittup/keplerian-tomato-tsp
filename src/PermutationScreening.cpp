#include "PermutationScreening.hpp"

#include <algorithm>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace
{
std::size_t pairCount(std::size_t count, std::size_t maxPairs)
{
    if (count < 2) { return 0; }
    if (count > maxPairs / (count - 1)) { throw std::length_error("Transfer evidence matrix exceeds its pair limit"); }
    return count * (count - 1);
}
}

bool isTomatoPermutation(const std::vector<int> &order, std::size_t tomatoCount)
{
    if (tomatoCount == 0 || order.size() != tomatoCount) { return false; }
    std::vector<bool> seen(tomatoCount, false);
    for (const auto tomato : order)
    {
        const auto index = static_cast<std::size_t>(tomato);
        if (index >= tomatoCount || seen[index]) { return false; }
        seen[index] = true;
    }
    return true;
}

void moveTomatoes(std::vector<int> &order, PermutationMove move, std::size_t from, std::size_t to)
{
    if (from >= order.size() || to >= order.size()) { throw std::out_of_range("Invalid permutation move position"); }
    if (from == to) { return; }
    switch (move)
    {
    case PermutationMove::Swap:
        std::swap(order[from], order[to]);
        break;
    case PermutationMove::Insert:
        if (from < to) { std::rotate(order.begin() + from, order.begin() + from + 1, order.begin() + to + 1); }
        else { std::rotate(order.begin() + to, order.begin() + from, order.begin() + from + 1); }
        break;
    case PermutationMove::Reverse:
        std::reverse(order.begin() + std::min(from, to), order.begin() + std::max(from, to) + 1);
        break;
    }
}

std::vector<int> randomTomatoPermutation(std::size_t tomatoCount, std::mt19937_64 &random)
{
    if (tomatoCount > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    {
        throw std::length_error("Too many indices for a tomato permutation");
    }
    std::vector<int> order(tomatoCount);
    std::iota(order.begin(), order.end(), 0);
    std::shuffle(order.begin(), order.end(), random);
    return order;
}

std::vector<int> sampleTomatoNeighbor(const std::vector<int> &order, PermutationMove move, std::mt19937_64 &random)
{
    auto neighbor = order;
    if (order.size() < 2) { return neighbor; }
    const auto from = std::uniform_int_distribution<std::size_t>(0, order.size() - 1)(random);
    const auto other = std::uniform_int_distribution<std::size_t>(0, order.size() - 2)(random);
    moveTomatoes(neighbor, move, from, other + (other >= from));
    return neighbor;
}

PairEvidenceKind PairEvidence::kind() const noexcept
{
    if (normal) { return PairEvidenceKind::NormalObserved; }
    if (exceptions) { return PairEvidenceKind::ExceptionObserved; }
    if (rejected) { return PairEvidenceKind::RejectedSamples; }
    return PairEvidenceKind::Unknown;
}

TransferEvidenceMatrix::TransferEvidenceMatrix(const Problem &problem, const TransferEvidenceConfig &config)
    : maxOrbitalCalls_(config.maxOrbitalCalls), cache_(problem, config.cacheCapacity),
      pairs_(pairCount(problem.tomatoes.size(), config.maxPairs))
{
    if (problem.maxExceptions < 0) { throw std::invalid_argument("Invalid exception budget"); }
}

std::size_t TransferEvidenceMatrix::index(std::size_t source, std::size_t destination) const
{
    const auto count = cache_.problem().tomatoes.size();
    if (source >= count || destination >= count || source == destination)
    {
        throw std::out_of_range("Expected a valid directed non-self tomato pair");
    }
    return source * (count - 1) + destination - (destination > source);
}

const PairEvidence &TransferEvidenceMatrix::at(std::size_t source, std::size_t destination) const
{
    return pairs_[index(source, destination)];
}

TransferWindowStats TransferEvidenceMatrix::samplePair(std::size_t source, std::size_t destination,
                                                        std::size_t callAllowance, const TransferWindowBounds &bounds)
{
    auto &evidence = pairs_[index(source, destination)];
    TransferWindowConfig window;
    window.policy = DeltaVPolicy::Strict;
    window.maxEvaluations = std::min(callAllowance, maxOrbitalCalls_ - stats().solverCalls);
    if (window.maxEvaluations == 0) { return {}; }
    // Leave room for a small number of duplicate grid points without unbounded cache-hit work.
    window.maxSamples = window.maxEvaluations + std::min<std::size_t>(16,
        std::numeric_limits<std::size_t>::max() - window.maxEvaluations);
    const auto result = TransferWindowExplorer::explore(cache_, source, destination, window, bounds);
    if (result.stop == TransferWindowStop::InvalidInput) { throw std::invalid_argument("Invalid transfer sampling domain"); }

    evidence.solverFailures += result.stats.solverFailures;
    evidence.rejected += result.stats.solverCalls + result.stats.cacheHits - result.stats.solverFailures
                         - result.opportunities.size();
    for (const auto &opportunity : result.opportunities)
    {
        evidence.normal += opportunity.classification == DeltaVClassification::Normal;
        evidence.exceptions += opportunity.classification == DeltaVClassification::Exception;
        evidence.earliestArrival = std::min(evidence.earliestArrival.value_or(opportunity.arrivalDays),
                                            opportunity.arrivalDays);
        evidence.latestDeparture = std::max(evidence.latestDeparture.value_or(opportunity.query.departureDays),
                                            opportunity.query.departureDays);
    }
    return result.stats;
}

void TransferEvidenceMatrix::sampleAll(const TransferWindowBounds &bounds)
{
    const auto remaining = maxOrbitalCalls_ - stats().solverCalls;
    if (pairs_.empty() || remaining == 0) { return; }
    const auto quota = remaining / pairs_.size();
    auto extra = remaining % pairs_.size();
    const auto count = cache_.problem().tomatoes.size();
    // Cycle through sources before changing destination offset, even with a partial budget.
    for (std::size_t offset = 1; offset < count; ++offset)
    {
        for (std::size_t source = 0; source < count; ++source)
        {
            if (quota == 0 && extra == 0) { return; }
            samplePair(source, (source + offset) % count, quota + (extra > 0), bounds);
            if (extra > 0) { --extra; }
        }
    }
}

PermutationScore TransferEvidenceMatrix::score(const std::vector<int> &order) const
{
    const auto &problem = cache_.problem();
    if (!isTomatoPermutation(order, problem.tomatoes.size())) { throw std::invalid_argument("Invalid tomato permutation"); }

    PermutationScore result;
    const PairEvidence *previous = nullptr;
    for (std::size_t i = 1; i < order.size(); ++i)
    {
        const auto &pair = at(static_cast<std::size_t>(order[i - 1]), static_cast<std::size_t>(order[i]));
        switch (pair.kind())
        {
        case PairEvidenceKind::NormalObserved: ++result.normalEdges; break;
        case PairEvidenceKind::ExceptionObserved: ++result.exceptionOnlyEdges; break;
        case PairEvidenceKind::RejectedSamples: ++result.rejectedOnlyEdges; break;
        case PairEvidenceKind::Unknown: ++result.unknownEdges; break;
        }
        if (previous)
        {
            if (!previous->earliestArrival || !pair.latestDeparture) { ++result.unassessedJoins; }
            else if (*previous->earliestArrival <= *pair.latestDeparture) { ++result.compatibleJoins; }
            else { ++result.incompatibleJoins; }
        }
        previous = &pair;
    }
    const auto limit = static_cast<std::size_t>(problem.maxExceptions);
    result.exceptionsOverBudget = result.exceptionOnlyEdges - std::min(result.exceptionOnlyEdges, limit);
    return result;
}
