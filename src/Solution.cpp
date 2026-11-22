#include "Solution.hpp"

#include "Problem.hpp"

#include <cmath>
#include <cstddef>
#include <stdexcept>

bool Solution::isStructurallyValid(const Problem &problem) const
{
    const std::size_t tomatoCount = problem.tomatoes.size();
    if (tomatoCount == 0 || visitingOrder.size() != tomatoCount ||
        !std::isfinite(problem.startTime) || problem.startTime < 0.0 ||
        !std::isfinite(problem.minTimeOfFlight) || problem.minTimeOfFlight < 0.0 ||
        !std::isfinite(problem.maxTotalTime) || problem.maxTotalTime < problem.startTime)
    {
        return false;
    }

    const std::size_t legCount = tomatoCount - 1;
    if (departureTimes.size() != legCount || timesOfFlight.size() != legCount)
    {
        return false;
    }

    std::vector<bool> visited(tomatoCount, false);
    for (const int tomato : visitingOrder)
    {
        const auto index = static_cast<std::size_t>(tomato);
        if (tomato < 0 || index >= tomatoCount || visited[index])
        {
            return false;
        }
        visited[index] = true;
    }

    double arrival = problem.startTime;
    for (std::size_t leg = 0; leg < legCount; ++leg)
    {
        const double departure = departureTimes[leg];
        const double duration = timesOfFlight[leg];
        if (!std::isfinite(departure) || departure < arrival ||
            !std::isfinite(duration) || duration < problem.minTimeOfFlight)
        {
            return false;
        }

        arrival = departure + duration;
        if (!std::isfinite(arrival))
        {
            return false;
        }
    }

    return arrival <= problem.maxTotalTime;
}

double Solution::missionCompletionTime(const Problem &problem) const
{
    if (!isStructurallyValid(problem))
    {
        throw std::invalid_argument("Cannot calculate completion time for a structurally invalid solution");
    }

    return departureTimes.empty() ? problem.startTime
                                  : departureTimes.back() + timesOfFlight.back();
}
