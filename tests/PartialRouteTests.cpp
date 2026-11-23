#include "PartialRoute.hpp"
#include "Problem.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace
{
using Status = RouteExtensionStatus;
using Policy = DeltaVPolicy;

void require(bool condition, const char *message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

struct CallCounts
{
    std::size_t extensions = 0;
    std::size_t fullValidation = 0;

    RouteExtensionResult extend(const PartialRoute &parent, int destination, LegTiming timing)
    {
        auto result = parent.extend(destination, timing);
        extensions += result.transfer.has_value();
        return result;
    }

    std::optional<CompletedRoute> finish(const PartialRoute &route)
    {
        auto result = route.finish();
        if (result)
        {
            fullValidation += result->evaluation.legs.size();
        }
        return result;
    }
};

Problem circularProblem()
{
    Problem problem;
    problem.tomatoes.assign(3, Tomato{10000000.0, 0.0, 0.0, 0.0, 0.0, 0.0});
    problem.startTime = 2.0;
    problem.minTimeOfFlight = 0.125;
    problem.maxTotalTime = 3.0;
    problem.maxDeltaV = 0.01;
    problem.maxExceptionDeltaV = 1.0;
    return problem;
}

Problem easyPair(const char *path)
{
    Problem problem;
    require(problem.loadFromFile(path) && problem.tomatoes.size() == 49, "Could not load ESA easy instance");
    problem.tomatoes.resize(2);
    return problem;
}

PartialRoute start(const Problem &problem, int first = 0, Policy policy = Policy::Strict)
{
    auto route = PartialRoute::start(problem, first, policy);
    require(route.has_value(), "Could not start a route");
    return std::move(*route);
}

PartialRoute accept(CallCounts &calls, const PartialRoute &parent, int destination, LegTiming timing)
{
    auto result = calls.extend(parent, destination, timing);
    require(result.status == Status::Ok && result.route && result.transfer &&
            result.transfer->status == TransferStatus::Ok, "Expected an accepted orbital transfer");
    return std::move(*result.route);
}

void expectRejection(const RouteExtensionResult &result, Status reason, bool solverCalled)
{
    require(result.status == reason && !result.route && result.transfer.has_value() == solverCalled,
            "Unexpected extension rejection or solver call");
}

void testInitial(CallCounts &calls, const char *)
{
    const auto problem = circularProblem();
    const auto route = start(problem, 1);
    require(route.prefix().visitingOrder == std::vector<int>{1} && route.hasVisited(1) &&
            !route.hasVisited(0) && !route.hasVisited(-1) && route.remainingLegs() == 2,
            "Initial route must contain exactly the selected tomato");
    require(route.prefix().departureTimes.empty() && route.prefix().timesOfFlight.empty() &&
            route.arrivalDays() == problem.startTime && route.exceptionsUsed() == 0 && !calls.finish(route),
            "Initial visit starts at the mission epoch and requires no transfer");
    require(!PartialRoute::start(problem, -1, Policy::Strict) && !PartialRoute::start(problem, 3, Policy::Strict),
            "A first tomato index must be in range");
    const Problem empty;
    require(!PartialRoute::start(empty, 0, Policy::Strict), "An empty problem has no first tomato");
}

void testExtension(CallCounts &calls, const char *)
{
    const auto problem = circularProblem();
    const auto parent = start(problem, 2);
    const auto child = accept(calls, parent, 0, {2.25, 0.25});
    require(child.prefix().visitingOrder == std::vector<int>{2, 0} &&
            child.prefix().departureTimes == std::vector<double>{2.25} &&
            child.prefix().timesOfFlight == std::vector<double>{0.25}, "Child must record the new leg");
    require(child.hasVisited(2) && child.hasVisited(0) && child.remainingLegs() == 1 &&
            child.arrivalDays() == 2.5 && child.exceptionsUsed() == 0, "Child metadata must reflect the new leg");
    require(!parent.hasVisited(0) && parent.remainingLegs() == 2 && parent.prefix().departureTimes.empty() &&
            parent.arrivalDays() == 2.0, "The accepted extension must preserve the parent");
}

void testDuplicate(CallCounts &calls, const char *)
{
    const auto problem = circularProblem();
    const auto parent = start(problem, 1);
    expectRejection(calls.extend(parent, 1, {2.0, 0.25}), Status::AlreadyVisited, false);
    expectRejection(calls.extend(parent, -1, {2.0, 0.25}), Status::InvalidDestination, false);
    expectRejection(calls.extend(parent, 3, {2.0, 0.25}), Status::InvalidDestination, false);
    require(parent.prefix().visitingOrder == std::vector<int>{1}, "Rejected visits must preserve the parent");
}

void testTiming(CallCounts &calls, const char *)
{
    const auto problem = circularProblem();
    const auto parent = start(problem);
    const double infinity = std::numeric_limits<double>::infinity();
    const double nan = std::numeric_limits<double>::quiet_NaN();
    expectRejection(calls.extend(parent, 1, {std::nextafter(2.0, 0.0), 0.125}), Status::InvalidTiming, false);
    expectRejection(calls.extend(parent, 1, {2.0, std::nextafter(0.125, 0.0)}), Status::InvalidTiming, false);
    expectRejection(calls.extend(parent, 1, {2.0, 0.0}), Status::InvalidTiming, false);
    expectRejection(calls.extend(parent, 1, {infinity, 0.25}), Status::InvalidTiming, false);
    expectRejection(calls.extend(parent, 1, {2.0, nan}), Status::InvalidTiming, false);
    expectRejection(calls.extend(parent, 1, {3.0, 0.125}), Status::DeadlineExceeded, false);
    expectRejection(calls.extend(parent, 1, {std::nextafter(2.75, infinity), 0.125}),
                    Status::DeadlineExceeded, false);
    const auto child = accept(calls, parent, 1, {2.75, 0.125});
    require(child.arrivalDays() == 2.875, "Exact minimum-duration reservation must be allowed");
    expectRejection(calls.extend(child, 2, {std::nextafter(2.875, 0.0), 0.125}), Status::InvalidTiming, false);
}

void testException(CallCounts &calls, const char *path)
{
    const auto problem = easyPair(path);
    const auto parent = start(problem);
    // Phase 3 easy_multirev costs about 417 m/s.
    const auto child = accept(calls, parent, 1, {0.125, 5.0});
    require(parent.exceptionsUsed() == 0 && child.exceptionsUsed() == 1,
            "The accepted exception must be counted only in the child");
}

void testBudget(CallCounts &calls, const char *path)
{
    auto problem = easyPair(path);
    problem.tomatoes = {problem.tomatoes[0], problem.tomatoes[1], problem.tomatoes[0], problem.tomatoes[1]};
    problem.maxExceptions = 1;
    problem.maxExceptionDeltaV = 2000.0; // The return leg costs about 1564 m/s.
    const auto first = accept(calls, start(problem), 1, {0.125, 5.0});
    expectRejection(calls.extend(first, 2, {5.125, 5.0}), Status::ExceptionBudgetExceeded, true);
    const auto normal = accept(calls, first, 3, {5.125, 0.25});
    require(first.exceptionsUsed() == 1 && !first.hasVisited(2) && !first.hasVisited(3) &&
            normal.exceptionsUsed() == 1, "An exhausted budget must still allow a normal transfer");
}

void testPolicies(CallCounts &calls, const char *path)
{
    const auto problem = easyPair(path);
    const LegTiming timing{0.125, 1.8478637404934113}; // Phase 3 nominal_high_offset_hi.
    const auto strict = calls.extend(start(problem), 1, timing);
    expectRejection(strict, Status::DeltaVLimitExceeded, true);
    const auto published = calls.extend(start(problem, 0, Policy::PublishedEvaluator), 1, timing);
    require(published.status == Status::Ok && published.route && published.route->exceptionsUsed() == 1 &&
            published.transfer && published.transfer->deltaVMps == strict.transfer->deltaVMps,
            "The published policy must accept the same raw cost as an exception");
    const auto finished = calls.finish(*published.route);
    require(finished && finished->evaluation.isFeasible() && finished->evaluation.exceptionCount == 1,
            "Complete validation must use the route's policy");
}

void testTransferFailure(CallCounts &calls, const char *)
{
    auto malformed = circularProblem();
    malformed.tomatoes[1].a = 0.0;
    const auto badInput = calls.extend(start(malformed), 1, {2.0, 0.25});
    expectRejection(badInput, Status::TransferFailed, true);
    require(badInput.transfer->status == TransferStatus::InvalidInput, "Expected invalid orbital elements");

    auto coincident = circularProblem();
    coincident.minTimeOfFlight = 0.0;
    const auto noLambert = calls.extend(start(coincident), 1, {2.0, 1e-18});
    expectRejection(noLambert, Status::TransferFailed, true);
    require(noLambert.transfer->status == TransferStatus::NoFiniteCandidate, "Expected coincident Lambert endpoints");

    auto extreme = circularProblem();
    extreme.startTime = 0.0;
    extreme.maxTotalTime = 1e21;
    extreme.tomatoes = {{14968611.0, 0.01, 1.0, 0.0, 0.0, 0.0},
                        {14978277.26, 0.01, 1.0, 0.0, 0.0, 0.4}};
    const auto numerical = calls.extend(start(extreme), 1, {0.0, 1e20});
    expectRejection(numerical, Status::TransferFailed, true);
    require(numerical.transfer->status == TransferStatus::NumericalFailure, "Expected numerical failure to propagate");
}

void testSampling(CallCounts &, const char *)
{
    const auto problem = circularProblem();
    const auto route = start(problem);
    TimingSampler sampler(42), replay(42), different(43);
    bool distinct = false;
    for (int i = 0; i < 128; ++i)
    {
        const auto a = sampler.sample(route);
        const auto b = replay.sample(route);
        const auto c = different.sample(route);
        require(a && b && c, "The schedule has available slack");
        require(a->departureDays == b->departureDays && a->flightDays == b->flightDays,
                "The same seed must replay the timing pairs");
        require(a->departureDays >= route.arrivalDays() && a->flightDays >= problem.minTimeOfFlight &&
                a->departureDays + a->flightDays <= 2.875, "Samples must preserve time for the last leg");
        distinct = distinct || a->departureDays != c->departureDays || a->flightDays != c->flightDays;
    }
    require(distinct, "Different seeds should explore different timings");

    auto tight = circularProblem();
    tight.maxTotalTime = 2.25;
    const auto onlyPair = sampler.sample(start(tight));
    require(onlyPair && onlyPair->departureDays == 2.0 && onlyPair->flightDays == 0.125,
            "Zero slack must return the unique immediate minimum-duration pair");
    auto insufficient = circularProblem();
    insufficient.maxTotalTime = 2.125;
    require(!sampler.sample(start(insufficient)), "Insufficient time must return no sample");
    auto zeroMinimum = circularProblem();
    zeroMinimum.minTimeOfFlight = 0.0;
    const auto positive = sampler.sample(start(zeroMinimum));
    require(positive && positive->flightDays > 0.0, "Samples must have positive flight even at a zero configured minimum");
}

void testCompletion(CallCounts &calls, const char *)
{
    const auto problem = circularProblem();
    const auto first = accept(calls, start(problem, 2), 0, {2.0, 0.25});
    require(!calls.finish(first), "A prefix with unvisited tomatoes cannot be finalized");
    const auto last = accept(calls, first, 1, {2.75, 0.25});
    const auto result = calls.finish(last);
    require(result && last.remainingLegs() == 0 && result->solution.visitingOrder == std::vector<int>{2, 0, 1} &&
            result->solution.departureTimes == std::vector<double>{2.0, 2.75} &&
            result->solution.timesOfFlight == std::vector<double>{0.25, 0.25},
            "Finalization must copy all fields into the existing Solution");
    require(result->evaluation.isFeasible() && result->evaluation.legs.size() == 2 &&
            result->evaluation.missionCompletionDays == problem.maxTotalTime,
            "Final validation must check both legs and accept the inclusive deadline");
}

void testSingleton(CallCounts &calls, const char *)
{
    auto problem = circularProblem();
    problem.tomatoes.resize(1);
    problem.maxTotalTime = problem.startTime;
    const auto route = start(problem);
    const auto result = calls.finish(route);
    TimingSampler sampler(0);
    require(result && route.remainingLegs() == 0 && !sampler.sample(route) &&
            result->solution.visitingOrder == std::vector<int>{0} &&
            result->solution.departureTimes.empty() && result->solution.timesOfFlight.empty() &&
            result->evaluation.isFeasible() && result->evaluation.legs.empty() &&
            result->evaluation.missionCompletionDays == problem.startTime,
            "A one-tomato mission must finish at the start epoch without a transfer");
}

struct TestCase
{
    std::string_view name;
    void (*run)(CallCounts &, const char *);
    std::size_t extensionCalls;
    std::size_t validationCalls;
};

constexpr TestCase cases[] = {
    {"initial", testInitial, 0, 0},
    {"extension", testExtension, 1, 0},
    {"duplicate", testDuplicate, 0, 0},
    {"timing", testTiming, 1, 0},
    {"exception", testException, 1, 0},
    {"budget", testBudget, 3, 0},
    {"policies", testPolicies, 2, 1},
    {"transfer_failure", testTransferFailure, 3, 0},
    {"sampling", testSampling, 0, 0},
    {"completion", testCompletion, 2, 2},
    {"singleton", testSingleton, 0, 0}
};
}

int main(int argc, char **argv)
{
    if (argc != 3)
    {
        std::cerr << "Expected a partial route test name and the ESA easy instance path\n";
        return 1;
    }
    for (const auto &test : cases)
    {
        if (test.name != argv[1])
        {
            continue;
        }
        CallCounts calls;
        try
        {
            test.run(calls, argv[2]);
            require(calls.extensions == test.extensionCalls && calls.fullValidation == test.validationCalls,
                    "Unexpected number of TransferSolver evaluations");
            std::cout << test.name << " TransferSolver calls: " << calls.extensions << " for extensions, "
                      << calls.fullValidation << " for full validation\n";
            return 0;
        }
        catch (const std::exception &error)
        {
            std::cerr << test.name << ": " << error.what() << '\n';
            return 1;
        }
    }
    std::cerr << "Unknown partial route test\n";
    return 1;
}
