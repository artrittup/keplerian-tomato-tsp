#include "Problem.hpp"
#include "TransferSolver.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>

namespace
{
using Classification = DeltaVClassification;
using Policy = DeltaVPolicy;
using Status = TransferStatus;

constexpr double infinity = std::numeric_limits<double>::infinity();
constexpr double nan = std::numeric_limits<double>::quiet_NaN();

bool check(bool condition, const std::string &message)
{
    if (!condition)
    {
        std::cerr << message << '\n';
    }
    return condition;
}

bool readOrbit(std::istream &input, Tomato &orbit)
{
    return static_cast<bool>(input >> orbit.a >> orbit.e >> orbit.i >> orbit.Omega >> orbit.omega >> orbit.nu);
}

const char *classificationName(Classification classification)
{
    switch (classification)
    {
    case Classification::Normal: return "normal";
    case Classification::Exception: return "exception";
    case Classification::Infeasible: return "infeasible";
    }
    return "unknown";
}

bool testReference(const char *fixturePath, const char *easyPath)
{
    std::ifstream fixture(fixturePath);
    Problem easy;
    if (!check(fixture.is_open(), "Could not open transfer fixture") ||
        !check(easy.loadFromFile(easyPath) && easy.tomatoes.size() == 49, "Could not load the ESA easy instance"))
    {
        return false;
    }

    Problem problem;
    problem.tomatoes.resize(2);
    problem.maxDeltaV = easy.maxDeltaV;
    problem.maxExceptionDeltaV = easy.maxExceptionDeltaV;
    std::string line;
    int caseCount = 0;
    double maxDifference = 0.0;
    while (std::getline(fixture, line))
    {
        if (line.empty() || line.front() == '#')
        {
            continue;
        }
        ++caseCount;
        std::istringstream row(line);
        std::string name, expectedText, strict, published, extra;
        double departure, duration;
        if (!(row >> name >> problem.startTime >> departure >> duration >> expectedText >> strict >> published) ||
            !readOrbit(row, problem.tomatoes[0]) || !readOrbit(row, problem.tomatoes[1]) || row >> extra)
        {
            return check(false, "Malformed transfer fixture row: " + line);
        }

        // Also verify real file indices in a threshold case involving the last tomato.
        const auto result = name == "normal_adjacent_hi"
            ? TransferSolver::evaluate(easy, 1, 48, departure, duration)
            : TransferSolver::evaluate(problem, 0, 1, departure, duration);
        if (expectedText == "inf")
        {
            if (!check(result.status == Status::InvalidInput && result.deltaVMps == infinity,
                       name + " should reject zero flight time"))
            {
                return false;
            }
            continue;
        }

        const double expected = std::stod(expectedText);
        const double difference = std::abs(result.deltaVMps - expected);
        const double tolerance = std::max(1e-6, 1e-11 * std::max(std::abs(result.deltaVMps), std::abs(expected)));
        if (result.status != Status::Ok || !std::isfinite(result.deltaVMps) || difference > tolerance)
        {
            std::cerr << std::setprecision(17) << name << " expected " << expected << " m/s, got "
                      << result.deltaVMps << " m/s (status " << static_cast<int>(result.status) << ")\n";
            return false;
        }
        maxDifference = std::max(difference, maxDifference);
        if (!check(classificationName(classifyDeltaV(problem, result.deltaVMps, Policy::Strict)) == strict,
                   name + " has the wrong strict classification") ||
            !check(classificationName(classifyDeltaV(problem, result.deltaVMps, Policy::PublishedEvaluator)) == published,
                   name + " has the wrong published classification"))
        {
            return false;
        }
    }
    std::cout << "Transfer fixture cases: " << caseCount
              << ", maximum absolute difference: " << maxDifference << " m/s\n";
    return check(caseCount == 20, "Expected 20 transfer fixture cases");
}

bool testErrors()
{
    Problem problem;
    problem.tomatoes = {
        {14968611.0, 0.01, 1.0, 0.0, 0.0, 0.0},
        {14978277.26, 0.01, 1.0, 0.0, 0.0, 0.4}
    };
    const auto expectStatus = [&problem](std::size_t source, std::size_t destination,
                                        double departure, double duration, Status expected,
                                        const std::string &message)
    {
        const auto result = TransferSolver::evaluate(problem, source, destination, departure, duration);
        return check(result.status == expected && result.deltaVMps == infinity, message);
    };

    const Status invalid = Status::InvalidInput;
    if (!expectStatus(2, 1, 0.0, 1.0, invalid, "Source index out of bounds") ||
        !expectStatus(0, 2, 0.0, 1.0, invalid, "Destination index out of bounds") ||
        !expectStatus(0, 1, nan, 1.0, invalid, "NaN departure") ||
        !expectStatus(0, 1, infinity, 1.0, invalid, "Infinite departure") ||
        !expectStatus(0, 1, 0.0, infinity, invalid, "Infinite duration") ||
        !expectStatus(0, 1, 0.0, nan, invalid, "NaN duration") ||
        !expectStatus(0, 1, 0.0, 0.0, invalid, "Zero duration") ||
        !expectStatus(0, 1, 0.0, -1.0, invalid, "Negative duration") ||
        !expectStatus(0, 1, 0.0, std::numeric_limits<double>::max(), invalid, "Seconds overflow") ||
        !expectStatus(0, 1, 0.0, 1e20, Status::NumericalFailure, "Lambert scaled time out of range"))
    {
        return false;
    }

    problem.startTime = nan;
    if (!expectStatus(0, 1, 0.0, 1.0, invalid, "Nonfinite epoch"))
    {
        return false;
    }
    problem.startTime = 0.0;

    problem.tomatoes[1].e = 1.0;
    if (!expectStatus(0, 1, 0.0, 1.0, invalid, "Unsupported eccentricity"))
    {
        return false;
    }

    problem.tomatoes[0].nu = 0.4;
    problem.tomatoes[1] = problem.tomatoes[0];
    if (!expectStatus(0, 1, 1.0, 1e-18, Status::NoFiniteCandidate, "Coincident Lambert endpoints"))
    {
        return false;
    }

    // At the reference epoch, mirror one circular orbit across the xy plane.
    // PyKEP cannot choose a direction when angular momentum has zero z component.
    problem.startTime = 1.0;
    problem.tomatoes[0].e = 0.0;
    problem.tomatoes[1].e = 0.0;
    problem.tomatoes[1].i = -problem.tomatoes[0].i;
    return expectStatus(0, 1, 1.0, 1e-18, Status::NoFiniteCandidate, "Undefined Lambert direction");
}

bool testPolicies()
{
    Problem problem;
    problem.maxDeltaV = 37.5;
    problem.maxExceptionDeltaV = 83.0;
    const double normal = problem.maxDeltaV;
    const double strictHigh = problem.maxExceptionDeltaV;
    const double publishedHigh = strictHigh + 1e-6;
    const auto classify = [&problem](double cost, Policy policy) { return classifyDeltaV(problem, cost, policy); };

    for (Policy policy : {Policy::Strict, Policy::PublishedEvaluator})
    {
        if (!check(classify(0.0, policy) == Classification::Normal, "Zero cost must be normal") ||
            !check(classify(normal, policy) == Classification::Normal, "Normal limit must be inclusive") ||
            !check(classify(std::nextafter(normal, infinity), policy) == Classification::Exception,
                   "No tolerance at the normal limit") ||
            !check(classify(strictHigh, policy) == Classification::Exception, "Exception limit must be inclusive") ||
            !check(classify(nan, policy) == Classification::Infeasible &&
                   classify(infinity, policy) == Classification::Infeasible &&
                   classify(-1.0, policy) == Classification::Infeasible, "Invalid costs must be infeasible"))
        {
            return false;
        }
    }
    const double aboveStrict = std::nextafter(strictHigh, infinity);
    return check(classify(aboveStrict, Policy::Strict) == Classification::Infeasible,
                 "Strict policy has no tolerance at the exception limit") &&
           check(classify(aboveStrict, Policy::PublishedEvaluator) == Classification::Exception,
                 "Published policy applies tolerance to the exception limit") &&
           check(classify(publishedHigh, Policy::PublishedEvaluator) == Classification::Exception,
                 "Published exception limit must be inclusive") &&
           check(classify(std::nextafter(publishedHigh, infinity), Policy::PublishedEvaluator) ==
                     Classification::Infeasible, "Published policy must reject costs above its exception limit");
}
}

int main(int argc, char **argv)
{
    const std::string testCase = argc > 1 ? argv[1] : "";
    if (testCase == "reference" && argc == 4)
    {
        return testReference(argv[2], argv[3]) ? 0 : 1;
    }
    if (testCase == "errors")
    {
        return testErrors() ? 0 : 1;
    }
    if (testCase == "policies")
    {
        return testPolicies() ? 0 : 1;
    }
    std::cerr << "Unknown transfer test case\n";
    return 1;
}
