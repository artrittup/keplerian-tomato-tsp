#pragma once

#include <vector>

class Problem;

struct Solution
{
    // Leg i goes from visitingOrder[i] to visitingOrder[i + 1]. All times are in days.
    std::vector<int> visitingOrder;
    std::vector<double> departureTimes;
    std::vector<double> timesOfFlight;

    // Checks the visit order and schedule only. Delta-V is evaluated separately.
    bool isStructurallyValid(const Problem &problem) const;

    // Returns the final arrival time, or the start time for a one-tomato mission.
    // Throws std::invalid_argument if the solution fails structural validation.
    double missionCompletionTime(const Problem &problem) const;
};
