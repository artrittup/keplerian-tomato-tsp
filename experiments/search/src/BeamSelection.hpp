#pragma once

#include <cstddef>
#include <vector>

namespace beam_detail
{
struct ArrivalCandidate
{
    int endpoint;
    double arrivalDays;
};

// Candidates are already ranked best first. Returns indices into that sequence.
std::vector<std::size_t> selectTemporal(const std::vector<ArrivalCandidate> &ranked,
                                        std::size_t width, double latestArrival, double maxBinDays);
}
