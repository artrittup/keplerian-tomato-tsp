#include "TransferCache.hpp"
#include "OrbitalDiagnostics.hpp"
#include "TransferKey.hpp"

#include <cmath>
#include <utility>

std::size_t TransferCache::KeyHash::operator()(const Key &key) const noexcept
{
    const auto [from, to, departure, flight] = key;
    auto mixed = departure ^ ((flight << 23) | (flight >> 41));
    mixed ^= static_cast<std::uint64_t>(from) * 0x9e3779b97f4a7c15ULL;
    mixed ^= static_cast<std::uint64_t>(to) * 0xbf58476d1ce4e5b9ULL;
    // Avalanche the correlated binary64 time bits before selecting a bucket.
    mixed = (mixed ^ (mixed >> 30)) * 0xbf58476d1ce4e5b9ULL;
    mixed = (mixed ^ (mixed >> 27)) * 0x94d049bb133111ebULL;
    return static_cast<std::size_t>(mixed ^ (mixed >> 31));
}

TransferCache::TransferCache(Problem problem, std::size_t capacity, TransferCacheIndex index)
    : problem_(std::move(problem)), capacity_(capacity)
{
    if (index == TransferCacheIndex::Hash) { index_.emplace<HashIndex>(); }
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
    return std::visit([&](auto &index) { return evaluateIndexed(query, maxSolverCalls, index); }, index_);
}

template <class Lookup>
std::optional<TransferResult> TransferCache::evaluateIndexed(const TransferQuery &query, std::size_t maxSolverCalls, Lookup &index)
{
    OrbitalTimer bookkeeping(OrbitalDiagnostics::active, OrbitalMetric::Cache);
    if (!std::isfinite(query.departureDays) || !std::isfinite(query.flightDays))
    {
        ++counts_.nonfiniteRequests;
        return TransferResult{TransferStatus::InvalidInput, std::numeric_limits<double>::infinity()};
    }

    const Key key{query.source, query.destination, transfer_detail::timeBits(query.departureDays),
                  transfer_detail::timeBits(query.flightDays)};
    const auto found = index.find(key);
    if (found != index.end())
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
    bookkeeping.pause();
    const auto result = TransferSolver::evaluate(problem_, query.source, query.destination,
                                                  query.departureDays, query.flightDays);
    bookkeeping.resume();
    if (capacity_ == 0)
    {
        return result;
    }

    if (entries_.size() == capacity_)
    {
        index.erase(entries_.back().first);
        entries_.pop_back();
        ++counts_.evictions;
    }
    entries_.emplace_front(key, result);
    try
    {
        index.emplace(key, entries_.begin());
    }
    catch (...)
    {
        entries_.pop_front();
        throw;
    }
    return result;
}
