#include "FixedOrderScheduler.hpp"

#include "FixedOrderDecode.hpp"
#include "SolutionEvaluator.hpp"
#include "TransferKey.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <istream>
#include <random>
#include <set>
#include <string>
#include <utility>

namespace
{
using Clock = std::chrono::steady_clock;
constexpr auto policy = DeltaVPolicy::Strict;

bool validOrder(const std::vector<int> &order, std::size_t count)
{
    if (count == 0 || order.size() != count) { return false; }
    std::vector<bool> visited(count, false);
    for (const auto tomato : order)
    {
        const auto index = static_cast<std::size_t>(tomato);
        if (index >= count || visited[index]) { return false; }
        visited[index] = true;
    }
    return true;
}

bool validConfig(const Problem &p, const FixedOrderConfig &c)
{
    return std::isfinite(p.startTime) && p.startTime >= 0.0 &&
           std::isfinite(p.minTimeOfFlight) && p.minTimeOfFlight >= 0.0 &&
           std::isfinite(p.maxTotalTime) && p.maxTotalTime >= p.startTime &&
           p.maxExceptions >= 0 && classifyDeltaV(p, 0.0, policy) == DeltaVClassification::Normal &&
           c.departureBands > 0 && c.departureBands <= 4096 && c.refinementPercent < 100 &&
           std::isfinite(c.flightScaleDays) && c.flightScaleDays > 0.0;
}

struct TimingBand
{
    double begin;
    double end;
    double minimumFlight;
    double deadline;
};

class LegDiscovery
{
public:
    LegDiscovery(TransferCache &cache, const FixedOrderConfig &config, std::mt19937_64 &random,
                  std::size_t source, std::size_t destination, FixedOrderLegCoverage &coverage,
                  std::vector<TransferOpportunity> &edges)
        : cache_(cache), config_(config), random_(random), source_(source), destination_(destination),
          coverage_(coverage), edges_(edges)
    {
    }

    void scan(TimingBand window, std::size_t allowance)
    {
        const auto bands = std::min(allowance, config_.departureBands);
        if (bands == 0 || window.begin > window.end || window.begin + window.minimumFlight > window.deadline)
        {
            return;
        }
        const auto before = cache_.stats().solverCalls;
        for (std::size_t i = 0; i < bands; ++i)
        {
            const auto quota = allowance / bands + (i < allowance % bands);
            const auto at = [&](std::size_t boundary)
            {
                return std::min(window.end, window.begin + (window.end - window.begin) *
                                     (static_cast<double>(boundary) / static_cast<double>(bands)));
            };
            scanBand({at(i), at(i + 1), window.minimumFlight, window.deadline}, quota);
        }
        coverage_.solverCalls = cache_.stats().solverCalls - before;
    }

private:
    double unit()
    {
        return static_cast<double>(random_() >> 11) * 0x1p-53;
    }

    void keepCost(double cost)
    {
        if (!coverage_.lowestDeltaVMps || cost < *coverage_.lowestDeltaVMps) { coverage_.lowestDeltaVMps = cost; }
    }

    void keep(TransferOpportunity edge)
    {
        const auto key = std::make_pair(transfer_detail::timeBits(edge.query.departureDays),
                                        transfer_detail::timeBits(edge.query.flightDays));
        if (!seen_.insert(key).second) { return; }
        coverage_.normal += edge.classification == DeltaVClassification::Normal;
        coverage_.exception += edge.classification == DeltaVClassification::Exception;
        edges_.push_back(edge);
    }

    void broad(TimingBand band, std::size_t attempts, std::size_t limit,
               std::size_t &proposal, std::optional<TransferQuery> &guide, double &best)
    {
        for (std::size_t i = 0; i < attempts && cache_.stats().solverCalls < limit; ++i, ++proposal)
        {
            const double departure = std::min(band.end, band.begin + unit() * (band.end - band.begin));
            const double available = band.deadline - departure;
            if (departure + band.minimumFlight > band.deadline) { continue; }
            const double preferred = proposal % 4 == 3 ? available
                : config_.flightScaleDays * (proposal % 4 == 2 ? 4.0 : 1.0);
            const double upper = std::max(band.minimumFlight, std::min(available, preferred));
            double flight = std::min(upper, band.minimumFlight + unit() * (upper - band.minimumFlight));
            if (departure + flight > band.deadline) { flight = std::nextafter(flight, band.minimumFlight); }
            if (departure + flight > band.deadline) { continue; }

            const TransferQuery query{source_, destination_, departure, flight};
            const auto result = cache_.evaluate(query, limit);
            if (!result) { break; }
            if (result->status != TransferStatus::Ok)
            {
                ++coverage_.solverFailures;
                continue;
            }
            keepCost(result->deltaVMps);
            if (result->deltaVMps < best)
            {
                best = result->deltaVMps;
                guide = query;
            }
            const auto cls = classifyDeltaV(cache_.problem(), result->deltaVMps, policy);
            if (cls != DeltaVClassification::Infeasible)
            {
                keep({query, departure + flight, result->deltaVMps, cls});
            }
        }
    }

    void scanBand(TimingBand band, std::size_t quota)
    {
        const auto before = cache_.stats().solverCalls;
        const auto refined = (quota / 100) * config_.refinementPercent +
                             (quota % 100) * config_.refinementPercent / 100;
        const auto broadQuota = quota - refined;
        std::size_t proposal = 0;
        std::optional<TransferQuery> guide;
        double best = std::numeric_limits<double>::infinity();
        broad(band, broadQuota, before + broadQuota, proposal, guide, best);
        if (guide && refined != 0)
        {
            const double departureRadius = (band.end - band.begin) / 3.0;
            const double flightRadius = config_.flightScaleDays / 2.0;
            const TransferWindowBounds bounds{
                std::max(band.begin, guide->departureDays - departureRadius),
                std::min(band.end, guide->departureDays + departureRadius),
                std::max(band.minimumFlight, guide->flightDays - flightRadius),
                std::min(band.deadline - band.begin, guide->flightDays + flightRadius),
                band.deadline
            };
            TransferWindowConfig explorer;
            explorer.maxEvaluations = refined;
            explorer.maxSamples = 13 + 4 * std::min<std::size_t>(refined, 16384);
            explorer.coarseDivisions = 2;
            const auto result = TransferWindowExplorer::explore(cache_, source_, destination_, explorer, bounds);
            coverage_.solverFailures += result.stats.solverFailures;
            if (result.lowestDeltaVMps) { keepCost(*result.lowestDeltaVMps); }
            for (const auto &edge : result.opportunities) { keep(edge); }
        }
        const auto limit = before + quota;
        broad(band, limit - cache_.stats().solverCalls, limit, proposal, guide, best);
    }

    TransferCache &cache_;
    const FixedOrderConfig &config_;
    std::mt19937_64 &random_;
    std::size_t source_;
    std::size_t destination_;
    FixedOrderLegCoverage &coverage_;
    std::vector<TransferOpportunity> &edges_;
    std::set<std::pair<std::uint64_t, std::uint64_t>> seen_;
};

struct Label
{
    double arrival;
    std::size_t previousExceptions;
    std::size_t edge;
};

FixedOrderOutcome evaluate(const Problem &problem, const std::vector<int> &order,
                          const fixed_order_detail::TransferTable &table, bool greedy)
{
    const auto start = Clock::now();
    auto result = fixed_order_detail::decode(table, problem.startTime, static_cast<std::size_t>(problem.maxExceptions),
                                             greedy);
    if (result.prefix.size() == table.size())
    {
        Solution candidate;
        candidate.visitingOrder = order;
        for (const auto &edge : result.prefix)
        {
            candidate.departureTimes.push_back(edge.query.departureDays);
            candidate.timesOfFlight.push_back(edge.query.flightDays);
        }
        const auto validation = SolutionEvaluator::evaluate(problem, candidate, policy);
        result.validationCalls = validation.legs.size();
        result.stop = FixedOrderStop::ValidationFailed;
        if (validation.isFeasible())
        {
            result.stop = FixedOrderStop::Feasible;
            result.completionDays = validation.missionCompletionDays;
            result.solution = std::move(candidate);
        }
    }
    result.schedulingSeconds = std::chrono::duration<double>(Clock::now() - start).count();
    return result;
}
}

std::optional<std::vector<int>> readFixedOrder(std::istream &input, std::size_t tomatoCount)
{
    std::vector<int> order;
    for (std::string token; input >> token;)
    {
        int value;
        const auto end = token.data() + token.size();
        const auto parsed = std::from_chars(token.data(), end, value);
        if (parsed.ec != std::errc{} || parsed.ptr != end || order.size() == tomatoCount)
        {
            return std::nullopt;
        }
        order.push_back(value);
    }
    return input.eof() && !input.bad() && validOrder(order, tomatoCount) ? std::optional{std::move(order)} : std::nullopt;
}

FixedOrderOutcome fixed_order_detail::decode(const TransferTable &table, double start, std::size_t exceptionLimit,
                                              bool greedy)
{
    const auto limit = std::min(exceptionLimit, table.size());
    using Layer = std::vector<std::optional<Label>>;
    std::vector<Layer> layers(1, Layer(limit + 1));
    layers.front()[0] = Label{start, 0, 0};
    FixedOrderOutcome result;
    for (const auto &edges : table)
    {
        auto &depth = result.depth.emplace_back();
        const auto &previous = layers.back();
        double earliest = std::numeric_limits<double>::infinity();
        for (const auto &label : previous)
        {
            if (label) { ++depth.incomingLabels; earliest = std::min(earliest, label->arrival); }
        }
        Layer next(limit + 1);
        for (std::size_t i = 0; i < edges.size(); ++i)
        {
            const auto &edge = edges[i];
            if (edge.query.departureDays < earliest) { continue; }
            const bool exception = edge.classification == DeltaVClassification::Exception;
            depth.chronologicalNormal += !exception;
            depth.chronologicalException += exception;
            for (std::size_t used = 0; used + exception <= limit; ++used)
            {
                if (!previous[used] || edge.query.departureDays < previous[used]->arrival) { continue; }
                auto &label = next[used + exception];
                if (!label || edge.arrivalDays < label->arrival)
                {
                    label = Label{edge.arrivalDays, used, i};
                }
            }
        }
        for (auto &label : next)
        {
            if (!label) { continue; }
            if (greedy && depth.outgoingLabels != 0) { label.reset(); }
            else { ++depth.outgoingLabels; }
        }
        if (depth.outgoingLabels == 0)
        {
            result.stop = edges.empty() ? FixedOrderStop::NoTransferFound
                : depth.chronologicalException == 0 ? FixedOrderStop::NoChronologicalTransfer
                : FixedOrderStop::ExceptionBudget;
            break;
        }
        layers.push_back(std::move(next));
    }

    std::size_t used = 0;
    double earliest = std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i <= limit; ++i)
    {
        if (layers.back()[i] && layers.back()[i]->arrival < earliest)
        {
            earliest = layers.back()[i]->arrival;
            used = i;
        }
    }
    result.exceptionsByDepth.resize(layers.size());
    result.prefix.resize(layers.size() - 1);
    for (std::size_t depth = result.prefix.size(); depth != 0; --depth)
    {
        const auto &label = *layers[depth][used];
        result.exceptionsByDepth[depth] = used;
        result.prefix[depth - 1] = table[depth - 1][label.edge];
        used = label.previousExceptions;
    }
    return result;
}

FixedOrderExperiment FixedOrderScheduler::compare(const Problem &problem, const std::vector<int> &order,
                                                   const FixedOrderConfig &config)
{
    FixedOrderExperiment result;
    if (!validOrder(order, problem.tomatoes.size()) || !validConfig(problem, config)) { return result; }
    const auto legs = order.size() - 1;
    if (legs != 0 && config.maxOrbitalCalls <= legs)
    {
        for (auto *outcome : {&result.greedy, &result.earliest})
        {
            outcome->stop = FixedOrderStop::EvaluationBudget;
            outcome->exceptionsByDepth = {0};
        }
        return result;
    }

    const auto start = Clock::now();
    TransferCache cache(problem, config.cacheCapacity, config.cacheIndex);
    std::mt19937_64 random(config.seed);
    fixed_order_detail::TransferTable table(legs);
    result.coverage.resize(legs);
    const auto available = config.maxOrbitalCalls - legs;
    const double positiveMinimum = std::max(problem.minTimeOfFlight, std::numeric_limits<double>::min());
    for (std::size_t i = 0; i < legs; ++i)
    {
        const auto quota = available / legs + (i < available % legs);
        const double opening = problem.startTime + static_cast<double>(i) * problem.minTimeOfFlight;
        const double deadline = problem.maxTotalTime - static_cast<double>(legs - i - 1) * problem.minTimeOfFlight;
        const double last = std::max(opening, deadline - positiveMinimum);
        LegDiscovery(cache, config, random, static_cast<std::size_t>(order[i]), static_cast<std::size_t>(order[i + 1]),
                      result.coverage[i], table[i]).scan({opening, last, positiveMinimum, deadline}, quota);
    }
    result.discovery = cache.stats();
    result.discoverySeconds = std::chrono::duration<double>(Clock::now() - start).count();
    result.greedy = evaluate(problem, order, table, true);
    result.earliest = evaluate(problem, order, table, false);
    return result;
}
