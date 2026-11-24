#pragma once

#include "FixedOrderScheduler.hpp"

namespace fixed_order_detail
{
using TransferTable = std::vector<std::vector<TransferOpportunity>>;

FixedOrderOutcome decode(const TransferTable &table, double start, std::size_t exceptionLimit, bool greedy);
}
