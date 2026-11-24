#pragma once

#include "Problem.hpp"
#include "TransferSolver.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <list>
#include <map>
#include <optional>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <variant>

struct TransferQuery
{
    std::size_t source;
    std::size_t destination;
    double departureDays;
    double flightDays;
};

struct TransferCacheStats
{
    std::size_t hits = 0;
    std::size_t misses = 0;
    std::size_t solverCalls = 0;
    std::size_t budgetDenials = 0;
    std::size_t nonfiniteRequests = 0;
    std::size_t evictions = 0;
    std::size_t entries = 0;
    std::size_t capacity = 0;
    std::size_t entryPayloadBytes = 0; // List and index values, excluding node links and allocator overhead.
    std::size_t orbitStorageBytes = 0; // Allocated orbit vector storage in the owned snapshot.
};

enum class TransferCacheIndex { Tree, Hash };

class TransferCache
{
public:
    // This instance owns a fixed problem snapshot. Changing the original does not change the cache.
    explicit TransferCache(Problem problem, std::size_t capacity, TransferCacheIndex index = TransferCacheIndex::Tree);
    TransferCache(const TransferCache &) = delete;
    TransferCache &operator=(const TransferCache &) = delete;

    const Problem &problem() const noexcept { return problem_; }
    TransferCacheStats stats() const noexcept;

    // maxSolverCalls is a cumulative cap for this cache. A hit is available even at the cap.
    // A finite miss at the cap returns nullopt. Nonfinite timings are rejected without a call.
    std::optional<TransferResult> evaluate(const TransferQuery &query,
        std::size_t maxSolverCalls = std::numeric_limits<std::size_t>::max());

private:
    using Key = std::tuple<std::size_t, std::size_t, std::uint64_t, std::uint64_t>;
    using Entry = std::pair<Key, TransferResult>;
    using Entries = std::list<Entry>;
    using Index = std::map<Key, Entries::iterator>;
    struct KeyHash
    {
        std::size_t operator()(const Key &key) const noexcept;
    };
    using HashIndex = std::unordered_map<Key, Entries::iterator, KeyHash>;

    template <class Lookup>
    std::optional<TransferResult> evaluateIndexed(const TransferQuery &query, std::size_t maxSolverCalls, Lookup &index);

    const Problem problem_;
    const std::size_t capacity_;
    Entries entries_; // Most recently used at the front.
    std::variant<Index, HashIndex> index_;
    TransferCacheStats counts_;
};
