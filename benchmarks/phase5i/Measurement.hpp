#pragma once

#include "TransferSolver.hpp"

namespace phase5i
{
TransferResult evaluateOrbit(const Problem &problem, std::size_t source, std::size_t destination,
                             double departureDays, double flightDays);

struct GraphMetrics
{
    std::size_t statesAtExceptionLimit = 0;
    std::size_t deadlineEdges = 0;
    std::size_t noTiming = 0;
    std::size_t budgetBlocks = 0;
    std::size_t emptyStates = 0;
    std::size_t emptyWithDeadline = 0;
    std::size_t emptyWithException = 0;
    std::size_t emptyWithBudget = 0;
};

extern GraphMetrics graphMetrics;
void recordExpansion(const GraphMetrics &before, bool exceptionRejected, bool empty);
}
