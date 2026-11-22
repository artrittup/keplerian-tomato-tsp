#include "Problem.hpp"
#include "Solution.hpp"

#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace
{
Problem makeProblem(std::size_t tomatoCount = 3)
{
    Problem problem;
    problem.tomatoes.resize(tomatoCount);
    problem.startTime = 2.0;
    problem.minTimeOfFlight = 1.0;
    problem.maxTotalTime = 8.0;
    return problem;
}

bool check(bool condition, const char *message)
{
    if (!condition)
    {
        std::cerr << message << '\n';
    }
    return condition;
}

bool expectCompletion(const Problem &problem, const Solution &solution, double expected)
{
    return check(solution.isStructurallyValid(problem), "Expected a structurally valid solution") &&
           check(solution.missionCompletionTime(problem) == expected, "Unexpected mission completion time");
}

bool runTest(const std::string &testCase)
{
    const Problem problem = makeProblem();
    const Solution valid{{2, 0, 1}, {2.0, 6.0}, {1.0, 1.0}};

    if (testCase == "valid")
    {
        // The first leg arrives at day 3, then the spacecraft waits until day 6.
        return expectCompletion(problem, valid, 7.0);
    }

    if (testCase == "permutation")
    {
        Solution candidate = valid;
        for (const auto &order : {std::vector<int>{2, 2, 1}, {2, -1, 0}, {3, 0, 1}, {2, 0}, {2, 0, 1, 2}})
        {
            candidate.visitingOrder = order;
            if (!check(!candidate.isStructurallyValid(problem), "Accepted an invalid visiting order"))
            {
                return false;
            }
        }
        return true;
    }

    if (testCase == "lengths")
    {
        const Solution shortDeparture{{2, 0, 1}, {2.0}, {1.0, 1.0}};
        const Solution longDeparture{{2, 0, 1}, {2.0, 6.0, 7.0}, {1.0, 1.0}};
        const Solution shortFlight{{2, 0, 1}, {2.0, 6.0}, {1.0}};
        const Solution longFlight{{2, 0, 1}, {2.0, 6.0}, {1.0, 1.0, 1.0}};

        return check(!shortDeparture.isStructurallyValid(problem), "Accepted too few departures") &&
               check(!longDeparture.isStructurallyValid(problem), "Accepted too many departures") &&
               check(!shortFlight.isStructurallyValid(problem), "Accepted too few flight durations") &&
               check(!longFlight.isStructurallyValid(problem), "Accepted too many flight durations");
    }

    if (testCase == "timing")
    {
        const Solution early{{2, 0, 1}, {std::nextafter(2.0, 0.0), 6.0}, {1.0, 1.0}};
        const Solution shortFlight{{2, 0, 1}, {2.0, 6.0}, {std::nextafter(1.0, 0.0), 1.0}};
        const Solution overlapping{{2, 0, 1}, {2.0, std::nextafter(3.0, 0.0)}, {1.0, 1.0}};
        const Solution late{{2, 0, 1}, {2.0, 8.0}, {1.0, 1.0}};

        return check(!early.isStructurallyValid(problem), "Accepted departure before the mission start") &&
               check(!shortFlight.isStructurallyValid(problem), "Accepted a flight shorter than the minimum") &&
               check(!overlapping.isStructurallyValid(problem), "Accepted a departure before the previous arrival") &&
               check(!late.isStructurallyValid(problem), "Accepted final arrival after the deadline");
    }

    if (testCase == "nonfinite")
    {
        const double infinity = std::numeric_limits<double>::infinity();
        const double nan = std::numeric_limits<double>::quiet_NaN();
        for (const double badTime : {infinity, -infinity, nan, -1.0})
        {
            const Solution badDeparture{{2, 0, 1}, {2.0, badTime}, {1.0, 1.0}};
            const Solution badFlight{{2, 0, 1}, {2.0, 6.0}, {1.0, badTime}};
            if (!check(!badDeparture.isStructurallyValid(problem), "Accepted an invalid departure time") ||
                !check(!badFlight.isStructurallyValid(problem), "Accepted an invalid flight duration"))
            {
                return false;
            }
        }

        Problem overflowProblem = makeProblem(2);
        overflowProblem.maxTotalTime = std::numeric_limits<double>::max();
        const Solution overflow{{0, 1}, {overflowProblem.maxTotalTime}, {overflowProblem.maxTotalTime}};
        return check(!overflow.isStructurallyValid(overflowProblem), "Accepted a non-finite arrival time");
    }

    if (testCase == "boundaries")
    {
        const Solution atLimits{{2, 0, 1}, {2.0, 3.0}, {1.0, 5.0}};
        Problem singleton = makeProblem(1);
        singleton.maxTotalTime = singleton.startTime;
        const Solution noLegs{{0}, {}, {}};
        const Solution empty;

        if (!expectCompletion(problem, atLimits, 8.0) ||
            !expectCompletion(singleton, noLegs, 2.0) ||
            !check(!empty.isStructurallyValid(makeProblem(0)), "Accepted a problem with no tomatoes"))
        {
            return false;
        }

        try
        {
            empty.missionCompletionTime(singleton);
        }
        catch (const std::invalid_argument &)
        {
            return true;
        }
        return check(false, "Expected completion-time calculation to reject an invalid solution");
    }

    std::cerr << "Unknown solution test: " << testCase << '\n';
    return false;
}
}

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        std::cerr << "Usage: solution_tests <case>\n";
        return 1;
    }

    return runTest(argv[1]) ? 0 : 1;
}
