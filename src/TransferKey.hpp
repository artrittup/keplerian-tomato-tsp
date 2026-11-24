#pragma once

#include <cstdint>
#include <cstring>
#include <limits>

namespace transfer_detail
{
inline std::uint64_t timeBits(double value) noexcept
{
    static_assert(sizeof(double) == sizeof(std::uint64_t) &&
                  std::numeric_limits<double>::is_iec559 && std::numeric_limits<double>::digits == 53,
                  "Transfer keys require IEEE binary64 time values");
    std::uint64_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}
}
