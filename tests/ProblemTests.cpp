#include "Problem.hpp"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace
{
const std::string validHeader = "p kttsp 0.0 0.001 200.0 100.0 600.0 5\n";
const std::string validOrbit = "10000000 0.01 1.0 0.1 0.2 0.3\n";

bool check(bool condition, const char *message)
{
    if (!condition)
    {
        std::cerr << message << '\n';
    }
    return condition;
}

bool expectEasy(const Problem &problem)
{
    return check(problem.tomatoes.size() == 49, "Expected 49 ESA tomatoes") &&
           check(problem.startTime == 0.0 && problem.minTimeOfFlight == 0.001 &&
                     problem.maxTotalTime == 200.0 && problem.maxDeltaV == 100.0 &&
                     problem.maxExceptionDeltaV == 600.0 && problem.maxExceptions == 5,
                 "ESA mission parameters differ") &&
           check(problem.tomatoes.front().a == 1.496861100e+07 &&
                     problem.tomatoes[4].a == 3.746038900e+06 &&
                     problem.tomatoes.back().a == 1.499197205e+07,
                 "ESA tomato order differs");
}

bool expectRejection(Problem &problem, const std::string &name,
                     const std::string &contents, const std::string &expectedError)
{
    const std::string filename = "parser_fixture_" + name + ".kttsp";
    std::ofstream fixture(filename);
    fixture << contents;
    fixture.close();
    if (!check(static_cast<bool>(fixture), "Could not write test fixture"))
    {
        return false;
    }

    std::ostringstream diagnostic;
    auto *previousBuffer = std::cerr.rdbuf(diagnostic.rdbuf());
    const bool loaded = problem.loadFromFile(filename);
    std::cerr.rdbuf(previousBuffer);
    std::remove(filename.c_str());

    const std::string error = diagnostic.str();
    if (loaded || error.find(filename + expectedError) == std::string::npos)
    {
        std::cerr << "Expected rejection of " << filename << " with "
                  << expectedError << "\nActual diagnostic: " << error;
        return false;
    }
    return true;
}

bool runTest(const std::string &testCase, const std::string &easyFile)
{
    Problem problem;

    if (testCase == "valid")
    {
        return problem.loadFromFile(easyFile) && expectEasy(problem);
    }
    if (testCase == "empty")
    {
        return expectRejection(problem, "empty", "", ": missing 'p kttsp' parameter header") &&
               check(problem.startTime == 0.0 && problem.minTimeOfFlight == 0.0 &&
                         problem.maxTotalTime == 0.0 && problem.maxDeltaV == 0.0 &&
                         problem.maxExceptionDeltaV == 0.0 && problem.maxExceptions == 0 &&
                         problem.tomatoes.empty(),
                     "Empty file changed the default problem");
    }
    if (testCase == "missing_header")
    {
        return expectRejection(problem, "missing_header", "  c comment\n\n" + validOrbit,
                               ":3: missing 'p kttsp' parameter header");
    }
    if (testCase == "malformed_header")
    {
        return expectRejection(problem, "wrong_type", "p tsp 0 0.001 200 100 600 5\n" + validOrbit,
                               ":1: malformed parameter header") &&
               expectRejection(problem, "missing_field", "p kttsp 0 0.001 200 100 600\n" + validOrbit,
                               ":1: malformed parameter header") &&
               expectRejection(problem, "duplicate_header", validHeader + validHeader + validOrbit,
                               ":2: duplicate parameter header");
    }
    if (testCase == "malformed_orbit")
    {
        return expectRejection(problem, "malformed_orbit", validHeader + validOrbit + "10000000 bad 1 2 3 4\n",
                               ":3: malformed orbit row") &&
               expectRejection(problem, "extra_orbit_field", validHeader + "1 2 3 4 5 6 7\n",
                               ":2: malformed orbit row") &&
               expectRejection(problem, "joined_orbit_fields", validHeader + "10000000-0.01 1 2 3 4\n",
                               ":2: malformed orbit row");
    }
    if (testCase == "repeated")
    {
        return problem.loadFromFile(easyFile) && expectEasy(problem) &&
               problem.loadFromFile(easyFile) && expectEasy(problem) &&
               expectRejection(problem, "failed_reload", "p kttsp 1 2 3 4 5 0\n" + validOrbit + "oops\n",
                               ":3: malformed orbit row") &&
               expectEasy(problem);
    }

    std::cerr << "Unknown parser test: " << testCase << '\n';
    return false;
}
}

int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        std::cerr << "Usage: problem_tests <case> <path-to-easy-instance>\n";
        return 1;
    }

    return runTest(argv[1], argv[2]) ? 0 : 1;
}
