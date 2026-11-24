#include "TransferWindowExplorer.hpp"
#include "TransferKey.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <queue>
#include <tuple>
#include <utility>

namespace
{
constexpr double infinity = std::numeric_limits<double>::infinity();

bool validBound(const std::optional<double> &value)
{
    return !value || (std::isfinite(*value) && *value >= 0.0);
}

struct SearchDomain
{
    double opening;
    double lastDeparture;
    double minimumFlight;
    double maximumFlight;
    double deadline;

    std::optional<TransferQuery> queryAt(std::size_t from, std::size_t to, double u, double v) const
    {
        const double departure = std::min(lastDeparture, opening + u * (lastDeparture - opening));
        if (departure + minimumFlight > deadline)
        {
            return std::nullopt;
        }
        const double upper = std::max(minimumFlight, std::min(maximumFlight, deadline - departure));
        double flight = std::min(upper, minimumFlight + v * (upper - minimumFlight));
        if (departure + flight > deadline)
        {
            flight = std::nextafter(flight, minimumFlight);
        }
        if (departure + flight > deadline)
        {
            return std::nullopt;
        }
        return TransferQuery{from, to, departure, flight};
    }
};

struct Cell
{
    double u;
    double v;
    double halfWidth;
    unsigned depth;
    bool pending = true;
};

class Search
{
public:
    Search(TransferCache &cache, std::size_t from, std::size_t to, const TransferWindowConfig &config,
           SearchDomain domain)
        : cache_(cache), from_(from), to_(to), config_(config), domain_(domain), before_(cache.stats()),
          limit_(before_.solverCalls + std::min(config.maxEvaluations,
                   std::numeric_limits<std::size_t>::max() - before_.solverCalls))
    {
    }

    TransferWindowResult run()
    {
        coarse();
        for (std::size_t split = 0; !finished(); ++split)
        {
            const auto next = split % 4 == 3 ? takeCell(shallowest_) : takeCell(best_);
            if (!next)
            {
                break;
            }
            const Cell parent = cells_[*next];
            cells_[*next].pending = false;
            const double offset = parent.halfWidth / 2.0;
            for (const double u : {parent.u - offset, parent.u + offset})
            {
                for (const double v : {parent.v - offset, parent.v + offset})
                {
                    if (finished())
                    {
                        break;
                    }
                    visitCell(u, v, offset, parent.depth + 1);
                }
            }
        }

        const auto after = cache_.stats();
        result_.stats.solverCalls = after.solverCalls - before_.solverCalls;
        result_.stats.cacheHits = after.hits - before_.hits;
        result_.stats.cacheMisses = after.misses - before_.misses;
        result_.stats.budgetDenials = after.budgetDenials - before_.budgetDenials;
        std::sort(result_.opportunities.begin(), result_.opportunities.end(), [](const auto &a, const auto &b)
        {
            return std::tie(a.deltaVMps, a.query.departureDays, a.query.flightDays) <
                   std::tie(b.deltaVMps, b.query.departureDays, b.query.flightDays);
        });
        return std::move(result_);
    }

private:
    bool finished()
    {
        if (result_.stop == TransferWindowStop::EvaluationLimit)
        {
            return true;
        }
        if (result_.stats.coarseSamples + result_.stats.refinementSamples == config_.maxSamples)
        {
            result_.stop = TransferWindowStop::SampleLimit;
            return true;
        }
        return false;
    }

    double sample(double u, double v, unsigned depth)
    {
        auto &stats = result_.stats;
        ++(depth == 0 ? stats.coarseSamples : stats.refinementSamples);
        const auto query = domain_.queryAt(from_, to_, u, v);
        if (!query)
        {
            return infinity;
        }
        const auto key = std::make_pair(transfer_detail::timeBits(query->departureDays),
                                        transfer_detail::timeBits(query->flightDays));
        const auto previous = scores_.find(key);
        if (previous != scores_.end())
        {
            ++stats.duplicateSamples;
            return previous->second;
        }
        const auto transfer = cache_.evaluate(*query, limit_);
        if (!transfer)
        {
            result_.stop = TransferWindowStop::EvaluationLimit;
            return infinity;
        }
        if (transfer->status != TransferStatus::Ok || !std::isfinite(transfer->deltaVMps))
        {
            ++stats.solverFailures;
            scores_.emplace(key, infinity);
            return infinity;
        }

        const double cost = transfer->deltaVMps;
        scores_.emplace(key, cost);
        if (!result_.lowestDeltaVMps || cost < *result_.lowestDeltaVMps)
        {
            result_.lowestDeltaVMps = cost;
        }
        const auto cls = classifyDeltaV(cache_.problem(), cost, config_.policy);
        if (cls != DeltaVClassification::Infeasible)
        {
            result_.opportunities.push_back({*query, query->departureDays + query->flightDays, cost, cls});
        }
        return cost;
    }

    void visitCell(double u, double v, double halfWidth, unsigned depth)
    {
        const double score = sample(u, v, depth);
        if (depth < config_.refinementLevels)
        {
            const auto id = cells_.size();
            cells_.push_back({u, v, halfWidth, depth});
            best_.emplace(score, id);
            shallowest_.emplace(depth, id);
        }
    }

    void coarse()
    {
        // Try the middle and the boundaries explicitly, including minimum-duration transfers.
        constexpr std::pair<double, double> anchors[] = {
            {0.5, 0.5}, {0.0, 0.0}, {0.0, 1.0}, {1.0, 0.0}, {1.0, 1.0},
            {0.5, 0.0}, {0.5, 1.0}, {0.0, 0.5}, {1.0, 0.5}
        };
        for (const auto &anchor : anchors)
        {
            if (finished())
            {
                return;
            }
            sample(anchor.first, anchor.second, 0);
        }

        // If every coordinate maps to one exact timing, further cells cannot add any results.
        if (domain_.opening == domain_.lastDeparture && domain_.minimumFlight == domain_.maximumFlight)
        {
            return;
        }
        const unsigned divisions = config_.coarseDivisions;
        const double width = 1.0 / divisions;
        for (unsigned diagonal = 0; diagonal < divisions; ++diagonal)
        {
            for (unsigned row = 0; row < divisions; ++row)
            {
                if (finished())
                {
                    return;
                }
                const unsigned column = (row + diagonal) % divisions;
                visitCell((row + 0.5) * width, (column + 0.5) * width, width / 2.0, 0);
            }
        }
    }

    template <class Queue>
    std::optional<std::size_t> takeCell(Queue &queue)
    {
        while (!queue.empty())
        {
            const auto id = queue.top().second;
            queue.pop();
            if (cells_[id].pending)
            {
                return id;
            }
        }
        return std::nullopt;
    }

    template <class Rank>
    using CellQueue = std::priority_queue<std::pair<Rank, std::size_t>,
                                          std::vector<std::pair<Rank, std::size_t>>, std::greater<>>;

    TransferCache &cache_;
    std::size_t from_;
    std::size_t to_;
    const TransferWindowConfig &config_;
    SearchDomain domain_;
    TransferCacheStats before_;
    std::size_t limit_;
    TransferWindowResult result_;
    std::map<std::pair<std::uint64_t, std::uint64_t>, double> scores_;
    std::vector<Cell> cells_;
    CellQueue<double> best_;
    CellQueue<unsigned> shallowest_;
};
}

TransferWindowResult TransferWindowExplorer::explore(TransferCache &cache, std::size_t source,
                                                      std::size_t destination, const TransferWindowConfig &config,
                                                      const TransferWindowBounds &bounds)
{
    const auto &problem = cache.problem();
    TransferWindowResult result;
    if (source >= problem.tomatoes.size() || destination >= problem.tomatoes.size() || source == destination ||
        !std::isfinite(problem.startTime) || problem.startTime < 0.0 ||
        !std::isfinite(problem.minTimeOfFlight) || problem.minTimeOfFlight < 0.0 ||
        !std::isfinite(problem.maxTotalTime) || problem.maxTotalTime < problem.startTime ||
        classifyDeltaV(problem, 0.0, config.policy) != DeltaVClassification::Normal ||
        !validBound(bounds.earliestDeparture) || !validBound(bounds.latestDeparture) ||
        !validBound(bounds.minimumFlight) || !validBound(bounds.maximumFlight) ||
        !validBound(bounds.latestArrival) ||
        (bounds.earliestDeparture && bounds.latestDeparture && *bounds.earliestDeparture > *bounds.latestDeparture) ||
        (bounds.minimumFlight && bounds.maximumFlight && *bounds.minimumFlight > *bounds.maximumFlight) ||
        config.coarseDivisions == 0 || config.coarseDivisions > 64 || config.refinementLevels > 24)
    {
        result.stop = TransferWindowStop::InvalidInput;
        return result;
    }

    const double opening = std::max(problem.startTime, bounds.earliestDeparture.value_or(problem.startTime));
    const double lastDeparture = bounds.latestDeparture.value_or(problem.maxTotalTime);
    const double minimum = std::max({problem.minTimeOfFlight, bounds.minimumFlight.value_or(0.0),
                                     std::numeric_limits<double>::min()});
    const double maximum = bounds.maximumFlight.value_or(problem.maxTotalTime);
    const double deadline = std::min(problem.maxTotalTime, bounds.latestArrival.value_or(problem.maxTotalTime));
    if (opening > lastDeparture || minimum > maximum || opening + minimum > deadline)
    {
        return result;
    }
    const double last = std::min(lastDeparture, std::max(opening, deadline - minimum));
    const double longest = std::max(minimum, std::min(maximum, deadline - opening));
    return Search(cache, source, destination, config, {opening, last, minimum, longest, deadline}).run();
}
