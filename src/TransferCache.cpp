#include "TransferCache.hpp"
#include "TransferKey.hpp"

#include <cmath>
#include <utility>

TransferCache::TransferCache(Problem problem, std::size_t capacity)
    : problem_(std::move(problem)), capacity_(capacity)
{
}

TransferCacheStats TransferCache::stats() const noexcept
{
    auto stats = counts_;
    stats.entries = entries_.size();
    stats.capacity = capacity_;
    stats.entryPayloadBytes = stats.entries * (sizeof(Entry) + sizeof(Index::value_type));
    stats.orbitStorageBytes = problem_.tomatoes.capacity() * sizeof(Tomato);
    return stats;
}

std::optional<TransferResult> TransferCache::evaluate(const TransferQuery &query, std::size_t maxSolverCalls)
{
    if (!std::isfinite(query.departureDays) || !std::isfinite(query.flightDays))
    {
        ++counts_.nonfiniteRequests;
        return TransferResult{TransferStatus::InvalidInput, std::numeric_limits<double>::infinity()};
    }

    const Key key{query.source, query.destination, transfer_detail::timeBits(query.departureDays),
                  transfer_detail::timeBits(query.flightDays)};
    const auto found = index_.find(key);
    if (found != index_.end())
    {
        ++counts_.hits;
        entries_.splice(entries_.begin(), entries_, found->second);
        return found->second->second;
    }

    ++counts_.misses;
    if (counts_.solverCalls >= maxSolverCalls)
    {
        ++counts_.budgetDenials;
        return std::nullopt;
    }
    ++counts_.solverCalls;
    const auto result = TransferSolver::evaluate(problem_, query.source, query.destination,
                                                  query.departureDays, query.flightDays);
    if (capacity_ == 0)
    {
        return result;
    }

    if (entries_.size() == capacity_)
    {
        index_.erase(entries_.back().first);
        entries_.pop_back();
        ++counts_.evictions;
    }
    entries_.emplace_front(key, result);
    try
    {
        index_.emplace(key, entries_.begin());
    }
    catch (...)
    {
        entries_.pop_front();
        throw;
    }
    return result;
}
