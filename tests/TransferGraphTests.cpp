#include "PartialRoute.hpp"
#include "SolutionEvaluator.hpp"
#include "TransferGraph.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace
{
void require(bool condition, const char *message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

Problem circularProblem(std::size_t count = 3)
{
    Problem problem;
    problem.tomatoes.assign(count, Tomato{10000000.0, 0.0, 0.0, 0.0, 0.0, 0.0});
    problem.startTime = 2.0;
    problem.minTimeOfFlight = 0.125;
    problem.maxTotalTime = 4.0;
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

constexpr TransferQuery firstLeg{0, 1, 2.0, 0.25};

void report(const TransferCache &cache)
{
    const auto s = cache.stats();
    std::cout << "hits=" << s.hits << " misses=" << s.misses << " solver_calls=" << s.solverCalls
              << " budget_denials=" << s.budgetDenials << " nonfinite=" << s.nonfiniteRequests
              << " evictions=" << s.evictions << " entries=" << s.entries << '/' << s.capacity
              << " entry_payload_bytes=" << s.entryPayloadBytes << " orbit_storage_bytes=" << s.orbitStorageBytes
              << '\n';
}

void testReuse(const char *)
{
    TransferCache cache(circularProblem(), 4);
    const auto initial = cache.evaluate(firstLeg);
    const auto again = cache.evaluate(firstLeg);
    const auto stats = cache.stats();
    require(initial && again && initial->status == TransferStatus::Ok && again->status == initial->status &&
            again->deltaVMps == initial->deltaVMps, "Identical queries must return the exact stored transfer");
    require(stats.hits == 1 && stats.misses == 1 && stats.solverCalls == 1 &&
            stats.entries == 1 && stats.entryPayloadBytes > 0 && stats.orbitStorageBytes >= 3 * sizeof(Tomato),
            "Reuse must charge exactly one call and report storage");
    report(cache);
}

void testTimings(const char *)
{
    TransferCache cache(circularProblem(), 4);
    require(cache.evaluate(firstLeg).has_value(), "Expected first baseline query");
    auto neighbor = firstLeg;
    neighbor.departureDays = std::nextafter(firstLeg.departureDays, 3.0);
    require(cache.evaluate(neighbor).has_value(), "Expected query at adjacent epoch");
    neighbor = firstLeg;
    neighbor.flightDays = std::nextafter(firstLeg.flightDays, 1.0);
    require(cache.evaluate(neighbor).has_value(), "Expected query with adjacent duration");
    const auto stats = cache.stats();
    require(stats.solverCalls == 3 && stats.misses == 3 && stats.hits == 0 && stats.entries == 3,
            "Adjacent binary64 epochs and durations must miss independently");
    report(cache);
}

void testPairs(const char *)
{
    TransferCache cache(circularProblem(), 3);
    require(cache.evaluate(firstLeg).has_value() && cache.evaluate({1, 0, 2.0, 0.25}).has_value() &&
            cache.evaluate({0, 2, 2.0, 0.25}).has_value(), "Expected all three orbital requests");
    require(cache.stats().solverCalls == 3 && cache.stats().hits == 0,
            "Both source and destination must be part of the directed key");
    report(cache);
}

void testPartialRoutes(const char *)
{
    TransferCache cache(circularProblem(4), 3);
    const auto a = PartialRoute::start(cache.problem(), 0, DeltaVPolicy::Strict);
    const auto b = PartialRoute::start(cache.problem(), 2, DeltaVPolicy::Strict);
    require(a && b, "Expected two separate route states");
    const auto aAtOne = a->extend(1, {2.0, 0.25});
    const auto bAtOne = b->extend(1, {2.0, 0.25});
    require(aAtOne.route && bAtOne.route && aAtOne.route->hasVisited(0) && !bAtOne.route->hasVisited(0) &&
            aAtOne.route->arrivalDays() == bAtOne.route->arrivalDays(), "The arrival epoch does not identify history");
    for (const auto *route : {&*aAtOne.route, &*bAtOne.route})
    {
        const auto transfer = cache.evaluate({static_cast<std::size_t>(route->prefix().visitingOrder.back()),
                                              3, route->arrivalDays(), 0.25});
        require(transfer && transfer->status == TransferStatus::Ok, "Both histories share the same orbital query");
    }
    require(cache.stats().solverCalls == 1 && cache.stats().hits == 1, "Route history must not affect the raw key");
    report(cache);
}

void testFailures(const char *)
{
    TransferCache cache(circularProblem(), 8);
    const TransferQuery coincident{0, 1, 2.0, 1e-18};
    const auto first = cache.evaluate(coincident, 1);
    const auto repeated = cache.evaluate(coincident, 1);
    require(first && repeated && first->status == TransferStatus::NoFiniteCandidate &&
            repeated->status == first->status && std::isinf(repeated->deltaVMps),
            "A determinate Lambert failure must be cached");
    for (const double zero : {0.0, -0.0})
    {
        const auto invalid = cache.evaluate({0, 1, 2.0, zero});
        require(invalid && invalid->status == TransferStatus::InvalidInput, "Zero-duration input must fail");
    }
    const auto before = cache.stats();
    require(before.solverCalls == 3 && before.entries == 3 && before.hits == 1,
            "Signed zero has distinct exact keys even when both inputs fail");
    const auto nan = cache.evaluate({0, 1, std::numeric_limits<double>::quiet_NaN(), 0.25}, 0);
    const auto infinity = cache.evaluate({0, 1, 2.0, std::numeric_limits<double>::infinity()}, 0);
    require(nan && infinity && nan->status == TransferStatus::InvalidInput &&
            infinity->status == TransferStatus::InvalidInput &&
            cache.stats().nonfiniteRequests == 2 && cache.stats().solverCalls == before.solverCalls &&
            cache.stats().entries == before.entries && cache.stats().misses == before.misses,
            "Reject nonfinite times before keying or invoking the solver");
    report(cache);
}

void testPolicies(const char *path)
{
    TransferCache cache(easyPair(path), 4);
    TransferGraph strict(cache, DeltaVPolicy::Strict, 1);
    TransferGraph published(cache, DeltaVPolicy::PublishedEvaluator, 1);
    const TransferQuery boundary{0, 1, 0.125, 1.8478637404934113};
    require(!strict.addTransfer(boundary, 1), "The strict graph must reject this over-ceiling ESA transfer");
    const auto accepted = published.addTransfer(boundary, 1);
    require(accepted && published.edges()[*accepted].classification == DeltaVClassification::Exception,
            "The published policy must classify the same physical result as an exception");
    const auto raw = cache.evaluate(boundary, 1);
    require(raw && raw->status == TransferStatus::Ok && raw->deltaVMps > cache.problem().maxExceptionDeltaV &&
            published.edges()[*accepted].deltaVMps == raw->deltaVMps &&
            cache.stats().solverCalls == 1 && cache.stats().hits == 2,
            "Neither acceptance policy nor graph identity belongs in the raw transfer key");
    report(cache);
}

void testIsolation(const char *)
{
    Problem original = circularProblem();
    TransferCache valid(original, 2);
    const auto a = valid.evaluate(firstLeg);
    original.tomatoes[1].a = 0.0;
    TransferCache invalid(original, 2);
    const auto b = invalid.evaluate(firstLeg);
    require(a && b && a->status == TransferStatus::Ok && b->status == TransferStatus::InvalidInput &&
            valid.problem().tomatoes[1].a > 0.0 && invalid.stats().hits == 0 &&
            valid.stats().solverCalls == 1 && invalid.stats().solverCalls == 1,
            "A cache must use its own immutable orbit snapshot");
    report(valid);
    report(invalid);
}

void testBudget(const char *)
{
    TransferCache cache(circularProblem(), 4);
    const TransferQuery secondLeg{1, 2, 2.25, 0.25};
    require(!cache.evaluate(firstLeg, 0), "Zero budget must deny a miss");
    require(cache.evaluate(firstLeg, 1).has_value() && cache.evaluate(firstLeg, 1).has_value(),
            "A hit must be usable at the cumulative call limit");
    require(!cache.evaluate(secondLeg, 1) && cache.evaluate(secondLeg, 2).has_value() &&
            cache.evaluate(secondLeg, 2).has_value(), "An unfunded miss must not create an entry");
    const auto stats = cache.stats();
    require(stats.hits == 2 && stats.misses == 4 && stats.solverCalls == 2 && stats.budgetDenials == 2,
            "Every finite miss must be a paid evaluation or a denial");
    report(cache);
}

void testEviction(const char *)
{
    TransferCache cache(circularProblem(), 2);
    const TransferQuery b{1, 2, 2.25, 0.25};
    const TransferQuery c{2, 0, 2.5, 0.25};
    require(cache.evaluate(firstLeg).has_value() && cache.evaluate(b).has_value() &&
            cache.evaluate(firstLeg).has_value() && cache.evaluate(c).has_value(), "Expected the LRU setup");
    require(cache.evaluate(firstLeg, 3).has_value() && !cache.evaluate(b, 3),
            "A hit must refresh recency so the other entry is evicted");
    require(cache.evaluate(b, 4).has_value() && cache.stats().evictions == 2 &&
            cache.stats().entries == 2 && cache.stats().solverCalls == 4, "Eviction must preserve the capacity");
    TransferCache disabled(circularProblem(), 0);
    require(disabled.evaluate(firstLeg).has_value() && disabled.evaluate(firstLeg).has_value() &&
            disabled.stats().solverCalls == 2 && disabled.stats().entryPayloadBytes == 0 &&
            disabled.stats().entries == 0, "Capacity zero must evaluate without storing entries");
    report(cache);
    report(disabled);
}

void testGraph(const char *)
{
    TransferCache cache(circularProblem(), 1);
    TransferGraph graph(cache, DeltaVPolicy::Strict, 3);
    require(!graph.addTransfer({0, 1, std::numeric_limits<double>::quiet_NaN(), 0.25}) &&
            !graph.addTransfer({0, 1, 1.75, 0.25}) && !graph.addTransfer({0, 1, 2.0, 0.1}) &&
            !graph.addTransfer({0, 1, 4.0, 0.25}) && cache.stats().solverCalls == 0,
            "The graph must reject invalid timing and deadline violations before the solver");
    const auto start = graph.addStart(0);
    const auto first = graph.addTransfer({0, 1, 2.125, 0.25});
    const auto premature = graph.addTransfer({1, 2, 2.25, 0.25});
    const auto second = graph.addTransfer({1, 2, 2.625, 0.25});
    require(start && first && premature && second && graph.addStart(0) == start,
            "The graph must hold start and exact transfer nodes");
    require(graph.addTransfer({0, 1, 2.125, 0.25}, 0) == first &&
            !graph.addTransfer({0, 2, 2.75, 0.25}) && cache.stats().solverCalls == 3,
            "Edge IDs must survive cache eviction and the graph cap must avoid new calls");
    const auto &edge = graph.edges()[*first];
    const auto &arrival = graph.nodes()[edge.to];
    require(graph.departuresAfter(*start) == std::vector<TransferEdgeId>{*first} &&
            graph.departuresAfter(edge.to) == std::vector<TransferEdgeId>{*second} &&
            arrival.tomato == 1 && arrival.arrivalDays == 2.375 && edge.arrivalDays == 2.375,
            "The departure index must allow waiting and exclude departures in the past");
    require(!graph.toSolution(*start, {*first, *premature}) &&
            !graph.toSolution(*start, {*second, *first}) && !graph.toSolution(*start, {*first}),
            "A supplied path must connect and have a valid complete chronology");
    const auto solution = graph.toSolution(*start, {*first, *second});
    require(solution && solution->visitingOrder == std::vector<int>{0, 1, 2} &&
            solution->departureTimes == std::vector<double>{2.125, 2.625} &&
            solution->timesOfFlight == std::vector<double>{0.25, 0.25} &&
            SolutionEvaluator::evaluate(cache.problem(), *solution, DeltaVPolicy::Strict).isFeasible(),
            "Recorded edges must reconstruct a solution that the existing evaluator accepts");
    std::cout << "nodes=" << graph.nodes().size() << " edges=" << graph.edges().size() << '\n';
    report(cache);
}

void testRouteConstraints(const char *path)
{
    TransferCache circular(circularProblem(), 2);
    TransferGraph revisit(circular, DeltaVPolicy::Strict, 2);
    const auto start = revisit.addStart(0);
    const auto first = revisit.addTransfer(firstLeg);
    const auto back = revisit.addTransfer({1, 0, 2.25, 0.25});
    require(start && first && back && revisit.edges()[*back].to != *start &&
            !revisit.toSolution(*start, {*first, *back}),
            "A new arrival node for an already visited tomato is still a repeated visit");

    auto problem = easyPair(path);
    problem.tomatoes.push_back(problem.tomatoes.front());
    problem.maxExceptionDeltaV = 2000.0;
    problem.maxExceptions = 1;
    TransferCache cache(std::move(problem), 2);
    TransferGraph graph(cache, DeltaVPolicy::Strict, 2);
    const auto root = graph.addStart(0);
    const auto one = graph.addTransfer({0, 1, 0.125, 5.0});
    const auto two = graph.addTransfer({1, 2, 5.125, 5.0});
    require(root && one && two && graph.edges()[*one].classification == DeltaVClassification::Exception &&
            graph.edges()[*two].classification == DeltaVClassification::Exception &&
            !graph.toSolution(*root, {*one, *two}),
            "Individually admissible exception edges can exceed the route budget when combined");
    report(cache);
}

struct TestCase
{
    std::string_view name;
    void (*run)(const char *);
};

constexpr TestCase cases[] = {
    {"reuse", testReuse}, {"timings", testTimings}, {"pairs", testPairs}, {"partial_routes", testPartialRoutes},
    {"failures", testFailures}, {"policies", testPolicies}, {"isolation", testIsolation}, {"budget", testBudget},
    {"eviction", testEviction}, {"graph", testGraph}, {"route_constraints", testRouteConstraints}
};
}

int main(int argc, char *argv[])
{
    if (argc == 3)
    {
        for (const auto &test : cases)
        {
            if (test.name == argv[1])
            {
                try
                {
                    test.run(argv[2]);
                    return 0;
                }
                catch (const std::exception &error)
                {
                    std::cerr << test.name << ": " << error.what() << '\n';
                    return 1;
                }
            }
        }
    }
    std::cerr << "Unknown transfer graph test or missing instance path\n";
    return 1;
}
