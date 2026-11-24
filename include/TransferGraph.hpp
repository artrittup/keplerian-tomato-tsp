#pragma once

#include "Solution.hpp"
#include "TransferCache.hpp"

#include <vector>

using TransferNodeId = std::size_t;
using TransferEdgeId = std::size_t;

struct TransferGraphNode
{
    std::size_t tomato;
    double arrivalDays;
};

struct TransferGraphEdge
{
    TransferNodeId from; // Source tomato ready at departureDays.
    TransferNodeId to;
    double departureDays;
    double arrivalDays;
    double flightDays;
    double deltaVMps;
    DeltaVClassification classification;
};

class TransferGraph
{
public:
    // The cache must outlive this graph. Edge classifications use this graph's policy.
    TransferGraph(TransferCache &cache, DeltaVPolicy policy, std::size_t maxEdges,
                  std::size_t maxNodes = std::numeric_limits<std::size_t>::max());

    std::optional<TransferNodeId> addStart(std::size_t tomato);
    // Adds an evaluated, individually admissible leg. An already stored leg reuses its edge ID.
    std::optional<TransferEdgeId> addTransfer(const TransferQuery &query,
        std::size_t maxSolverCalls = std::numeric_limits<std::size_t>::max());

    const std::vector<TransferGraphNode> &nodes() const noexcept { return nodes_; }
    const std::vector<TransferGraphEdge> &edges() const noexcept { return edges_; }
    std::size_t capacityDenials() const noexcept { return capacityDenials_; }

    // Waiting is implicit. Return edges departing from the node's tomato at or after its arrival.
    std::vector<TransferEdgeId> departuresAfter(TransferNodeId node) const;

    // Convert a supplied complete path, checking structure and the route-wide exception count.
    // SolutionEvaluator can independently re-evaluate the resulting Solution.
    std::optional<Solution> toSolution(TransferNodeId start, const std::vector<TransferEdgeId> &path) const;

private:
    TransferNodeId indexNode(std::size_t tomato, double arrivalDays);

    TransferCache &cache_;
    const DeltaVPolicy policy_;
    const std::size_t maxEdges_;
    const std::size_t maxNodes_;
    std::size_t capacityDenials_ = 0;
    std::vector<TransferGraphNode> nodes_;
    std::vector<TransferGraphEdge> edges_;
    std::map<std::pair<std::size_t, std::uint64_t>, TransferNodeId> nodeIndex_;
    // Ordered by source and numeric departure, with exact query bits for duplicate detection.
    using DepartureKey = std::tuple<std::size_t, double, std::size_t, std::uint64_t, std::uint64_t>;
    std::map<DepartureKey, TransferEdgeId> departures_;
};
