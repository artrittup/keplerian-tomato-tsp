#include "TransferGraph.hpp"
#include "TransferKey.hpp"

#include <cmath>

namespace
{
bool validMissionTimes(const Problem &problem)
{
    return std::isfinite(problem.startTime) && problem.startTime >= 0.0 &&
           std::isfinite(problem.minTimeOfFlight) && problem.minTimeOfFlight >= 0.0 &&
           std::isfinite(problem.maxTotalTime) && problem.maxTotalTime >= problem.startTime;
}
}

TransferGraph::TransferGraph(TransferCache &cache, DeltaVPolicy policy, std::size_t maxEdges, std::size_t maxNodes)
    : cache_(cache), policy_(policy), maxEdges_(maxEdges), maxNodes_(maxNodes)
{
}

TransferNodeId TransferGraph::indexNode(std::size_t tomato, double arrivalDays)
{
    const auto [it, inserted] = nodeIndex_.emplace(std::make_pair(tomato, transfer_detail::timeBits(arrivalDays)),
                                                   nodes_.size());
    if (inserted)
    {
        try
        {
            nodes_.push_back({tomato, arrivalDays});
        }
        catch (...)
        {
            nodeIndex_.erase(it);
            throw;
        }
    }
    return it->second;
}

std::optional<TransferNodeId> TransferGraph::addStart(std::size_t tomato)
{
    const auto &problem = cache_.problem();
    if (tomato >= problem.tomatoes.size() || !validMissionTimes(problem))
    {
        return std::nullopt;
    }
    if (nodes_.size() == maxNodes_ && nodeIndex_.count({tomato, transfer_detail::timeBits(problem.startTime)}) == 0)
    {
        ++capacityDenials_;
        return std::nullopt;
    }
    return indexNode(tomato, problem.startTime);
}

std::optional<TransferEdgeId> TransferGraph::addTransfer(const TransferQuery &query, std::size_t maxSolverCalls)
{
    const auto &problem = cache_.problem();
    if (query.source >= problem.tomatoes.size() || query.destination >= problem.tomatoes.size() ||
        query.source == query.destination || !validMissionTimes(problem) ||
        !std::isfinite(query.departureDays) || query.departureDays < problem.startTime ||
        !std::isfinite(query.flightDays) || query.flightDays <= 0.0 ||
        query.flightDays < problem.minTimeOfFlight)
    {
        return std::nullopt;
    }
    const double arrival = query.departureDays + query.flightDays;
    if (!std::isfinite(arrival) || arrival > problem.maxTotalTime)
    {
        return std::nullopt;
    }
    const DepartureKey key{query.source, query.departureDays, query.destination,
                           transfer_detail::timeBits(query.departureDays), transfer_detail::timeBits(query.flightDays)};
    const auto found = departures_.find(key);
    if (found != departures_.end())
    {
        return found->second;
    }
    if (edges_.size() == maxEdges_)
    {
        ++capacityDenials_;
        return std::nullopt;
    }
    const std::size_t newNodes = (nodeIndex_.count({query.source, transfer_detail::timeBits(query.departureDays)}) == 0) +
                                 (nodeIndex_.count({query.destination, transfer_detail::timeBits(arrival)}) == 0);
    if (newNodes > maxNodes_ - nodes_.size())
    {
        ++capacityDenials_;
        return std::nullopt;
    }
    const auto transfer = cache_.evaluate(query, maxSolverCalls);
    if (!transfer || transfer->status != TransferStatus::Ok)
    {
        return std::nullopt;
    }
    const auto classification = classifyDeltaV(problem, transfer->deltaVMps, policy_);
    if (classification == DeltaVClassification::Infeasible)
    {
        return std::nullopt;
    }

    const auto previousNodeCount = nodes_.size();
    const auto edge = edges_.size();
    try
    {
        const auto from = indexNode(query.source, query.departureDays);
        const auto to = indexNode(query.destination, arrival);
        edges_.push_back({from, to, query.departureDays, arrival, query.flightDays, transfer->deltaVMps,
                          classification});
        departures_.emplace(key, edge);
    }
    catch (...)
    {
        if (edges_.size() > edge)
        {
            edges_.pop_back();
        }
        while (nodes_.size() > previousNodeCount)
        {
            const auto &node = nodes_.back();
            nodeIndex_.erase({node.tomato, transfer_detail::timeBits(node.arrivalDays)});
            nodes_.pop_back();
        }
        throw;
    }
    return edge;
}

std::vector<TransferEdgeId> TransferGraph::departuresAfter(TransferNodeId id) const
{
    std::vector<TransferEdgeId> departures;
    if (id >= nodes_.size())
    {
        return departures;
    }
    const auto &node = nodes_[id];
    auto it = departures_.lower_bound({node.tomato, node.arrivalDays, 0, 0, 0});
    for (; it != departures_.end() && std::get<0>(it->first) == node.tomato; ++it)
    {
        departures.push_back(it->second);
    }
    return departures;
}

std::optional<Solution> TransferGraph::toSolution(TransferNodeId start, const std::vector<TransferEdgeId> &path) const
{
    const auto &problem = cache_.problem();
    if (start >= nodes_.size() || nodes_[start].arrivalDays != problem.startTime ||
        problem.tomatoes.empty() || path.size() != problem.tomatoes.size() - 1 ||
        path.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) || problem.maxExceptions < 0)
    {
        return std::nullopt;
    }

    Solution solution;
    solution.visitingOrder.reserve(path.size() + 1);
    solution.departureTimes.reserve(path.size());
    solution.timesOfFlight.reserve(path.size());
    auto currentTomato = nodes_[start].tomato;
    solution.visitingOrder.push_back(static_cast<int>(currentTomato));
    std::size_t exceptions = 0;
    for (const auto id : path)
    {
        if (id >= edges_.size() || nodes_[edges_[id].from].tomato != currentTomato)
        {
            return std::nullopt;
        }
        const auto &edge = edges_[id];
        exceptions += edge.classification == DeltaVClassification::Exception;
        if (exceptions > static_cast<std::size_t>(problem.maxExceptions))
        {
            return std::nullopt;
        }
        currentTomato = nodes_[edge.to].tomato;
        solution.visitingOrder.push_back(static_cast<int>(currentTomato));
        solution.departureTimes.push_back(edge.departureDays);
        solution.timesOfFlight.push_back(edge.flightDays);
    }
    if (!solution.isStructurallyValid(problem))
    {
        return std::nullopt;
    }
    return solution;
}
