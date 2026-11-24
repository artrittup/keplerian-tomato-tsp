#include "FragmentAssembly.hpp"

#include <algorithm>
#include <limits>
#include <set>
#include <stdexcept>

namespace
{
struct Fallback
{
    int unknown = 0;
    int exceptions = 0;

    Fallback &operator+=(Fallback other)
    {
        unknown += other.unknown;
        exceptions += other.exceptions;
        return *this;
    }

    auto rank() const { return std::tie(unknown, exceptions); }
};

Fallback fallback(PairEvidenceKind kind)
{
    return {kind == PairEvidenceKind::Unknown, kind == PairEvidenceKind::ExceptionObserved};
}

struct Join
{
    std::size_t host;
    std::size_t guest;
    std::size_t position;
    Fallback delta;
};

class AssemblyAttempt
{
public:
    AssemblyAttempt(const FragmentEvidence &evidence, int exceptionLimit, int unknownLimit,
                    std::mt19937_64 &random, bool preferScarce)
        : evidence_(evidence), exceptionLimit_(exceptionLimit), unknownLimit_(unknownLimit), random_(random),
          preferScarce_(preferScarce), cut_(evidence.size(), std::vector<bool>(evidence.size())),
          normalOut_(evidence.size()), normalIn_(evidence.size())
    {
        for (std::size_t from = 0; from < evidence.size(); ++from)
        {
            paths_.push_back({static_cast<int>(from)});
            for (std::size_t to = 0; to < evidence.size(); ++to)
            {
                if (from != to && evidence[from][to] == PairEvidenceKind::NormalObserved)
                {
                    ++normalOut_[from];
                    ++normalIn_[to];
                }
            }
        }
    }

    std::vector<int> run(std::size_t repairLimit, FragmentAssemblyResult &counts)
    {
        while (paths_.size() > 1)
        {
            const auto next = chooseJoin();
            if (next)
            {
                merge(*next, counts);
                continue;
            }
            if (repairLimit == 0 || !split()) { return {}; }
            --repairLimit;
            ++counts.repairs;
        }
        return std::move(paths_.front());
    }

private:
    bool addArc(int from, int to, Fallback &delta, std::size_t &scarcity) const
    {
        const auto kind = evidence_[from][to];
        if (cut_[from][to] || kind == PairEvidenceKind::RejectedSamples) { return false; }
        delta += fallback(kind);
        if (preferScarce_) { scarcity += normalOut_[from] + normalIn_[to]; }
        return true;
    }

    std::optional<Join> chooseJoin()
    {
        std::optional<Join> best;
        std::tuple<int, int, std::size_t> bestRank{};
        std::size_t ties = 0;
        for (std::size_t host = 0; host < paths_.size(); ++host)
        {
            const auto &path = paths_[host];
            for (std::size_t guest = 0; guest < paths_.size(); ++guest)
            {
                if (host == guest) { continue; }
                const auto &insert = paths_[guest];
                // Prepending is already represented by appending with the host and guest exchanged.
                for (std::size_t position = 1; position <= path.size(); ++position)
                {
                    Fallback delta;
                    std::size_t scarcity = 0;
                    if (!addArc(path[position - 1], insert.front(), delta, scarcity)) { continue; }
                    if (position != path.size() && !addArc(insert.back(), path[position], delta, scarcity)) { continue; }
                    if (position != path.size())
                    {
                        const auto removed = fallback(evidence_[path[position - 1]][path[position]]);
                        delta += {-removed.unknown, -removed.exceptions};
                    }
                    if (used_.unknown + delta.unknown > unknownLimit_ ||
                        used_.exceptions + delta.exceptions > exceptionLimit_) { continue; }
                    const auto rank = std::tuple_cat(delta.rank(), std::make_tuple(scarcity));
                    if (!best || rank < bestRank)
                    {
                        bestRank = rank;
                        best = Join{host, guest, position, delta};
                        ties = 1;
                    }
                    else if (rank == bestRank && std::uniform_int_distribution<std::size_t>(1, ++ties)(random_) == 1)
                    {
                        best = Join{host, guest, position, delta};
                    }
                }
            }
        }
        return best;
    }

    void merge(const Join &next, FragmentAssemblyResult &counts)
    {
        auto &host = paths_[next.host];
        const auto &guest = paths_[next.guest];
        counts.insertions += next.position != host.size();
        host.insert(host.begin() + next.position, guest.begin(), guest.end());
        paths_.erase(paths_.begin() + next.guest);
        used_ += next.delta;
    }

    bool split()
    {
        const auto edgeCount = evidence_.size() - paths_.size();
        if (edgeCount == 0) { return false; }
        auto chosen = std::uniform_int_distribution<std::size_t>(0, edgeCount - 1)(random_);
        for (auto &path : paths_)
        {
            const auto edges = path.size() - 1;
            if (chosen >= edges)
            {
                chosen -= edges;
                continue;
            }
            const auto from = path[chosen], to = path[chosen + 1];
            cut_[from][to] = true;
            const auto removed = fallback(evidence_[from][to]);
            used_ += {-removed.unknown, -removed.exceptions};
            std::vector<int> suffix(path.begin() + chosen + 1, path.end());
            path.erase(path.begin() + chosen + 1, path.end());
            paths_.push_back(std::move(suffix));
            return true;
        }
        return false;
    }

    const FragmentEvidence &evidence_;
    const int exceptionLimit_;
    const int unknownLimit_;
    std::mt19937_64 &random_;
    const bool preferScarce_;
    std::vector<std::vector<bool>> cut_;
    std::vector<std::size_t> normalOut_, normalIn_;
    std::vector<std::vector<int>> paths_;
    Fallback used_;
};
}

FragmentEvidence fragmentEvidence(const TransferEvidenceMatrix &matrix, std::size_t tomatoCount)
{
    FragmentEvidence result(tomatoCount, std::vector<PairEvidenceKind>(tomatoCount, PairEvidenceKind::Unknown));
    for (std::size_t from = 0; from < tomatoCount; ++from)
    {
        for (std::size_t to = 0; to < tomatoCount; ++to)
        {
            if (from != to) { result[from][to] = matrix.at(from, to).kind(); }
        }
    }
    return result;
}

FragmentAssemblyResult assembleFragments(const FragmentEvidence &evidence, std::size_t exceptionLimit,
                                         const FragmentAssemblyConfig &config)
{
    const auto count = evidence.size();
    if (count == 0 || count > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        std::any_of(evidence.begin(), evidence.end(), [count](const auto &row) { return row.size() != count; }))
    {
        throw std::invalid_argument("Expected a nonempty square directed evidence matrix");
    }
    const auto exceptions = static_cast<int>(std::min(count - 1, exceptionLimit));
    const auto unknown = static_cast<int>(std::min(count - 1, config.maxUnknownEdges));
    FragmentAssemblyResult result;
    std::set<std::vector<int>> unique;
    std::mt19937_64 random(config.seed);
    for (std::size_t i = 0; i < config.attempts; ++i)
    {
        auto order = AssemblyAttempt(evidence, exceptions, unknown, random, i % 2 != 0)
                         .run(config.repairsPerAttempt, result);
        if (order.empty()) { ++result.failedAttempts; continue; }
        ++result.completedAttempts;
        if (unique.insert(order).second) { result.orders.push_back(std::move(order)); }
    }
    return result;
}
