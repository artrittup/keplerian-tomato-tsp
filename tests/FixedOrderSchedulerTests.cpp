#include "FixedOrderDecode.hpp"
#include "SolutionEvaluator.hpp"

#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace
{
using fixed_order_detail::TransferTable;
using Class = DeltaVClassification;

void require(bool condition, const char *message)
{
    if (!condition) { throw std::runtime_error(message); }
}

TransferOpportunity edge(double departure, double arrival, Class classification = Class::Normal)
{
    return {{0, 1, departure, arrival - departure}, arrival, classification == Class::Normal ? 50.0 : 300.0,
            classification};
}

Problem circular(std::size_t count)
{
    Problem problem;
    problem.tomatoes.assign(count, Tomato{10000000.0, 0, 0, 0, 0, 0});
    problem.startTime = 2.0;
    problem.minTimeOfFlight = 0.125;
    problem.maxTotalTime = 4.0;
    problem.maxDeltaV = 0.01;
    problem.maxExceptionDeltaV = 1.0;
    problem.maxExceptions = 1;
    return problem;
}

void chronology()
{
    TransferTable table{{edge(0, 2), edge(0, 1)}, {edge(0.5, 0.75), edge(1, 3)}};
    for (bool greedy : {true, false})
    {
        const auto result = fixed_order_detail::decode(table, 0, 1, greedy);
        require(result.prefix.size() == 2 && result.prefix[0].arrivalDays == 1 &&
                result.prefix[1].query.departureDays == 1 && result.depth[1].chronologicalNormal == 1,
                "An earlier arrival must dominate and a departure at arrival time must be accepted");
    }
    table[1].pop_back();
    const auto blocked = fixed_order_detail::decode(table, 0, 1, false);
    require(blocked.stop == FixedOrderStop::NoChronologicalTransfer && blocked.prefix.size() == 1,
            "A transfer departing before every incoming arrival is unavailable");
}

void exceptions()
{
    const TransferTable table{
        {edge(0, 5), edge(0, 1, Class::Exception)},
        {edge(2, 3), edge(5, 6, Class::Exception)},
        {edge(3, 4)}
    };
    const auto greedy = fixed_order_detail::decode(table, 0, 1, true);
    const auto earliest = fixed_order_detail::decode(table, 0, 1, false);
    require(greedy.prefix.size() == 2 && greedy.stop == FixedOrderStop::NoChronologicalTransfer,
            "Normal-first should lose the useful early exception in this fixture");
    require(earliest.prefix.size() == 3 && earliest.exceptionsByDepth == std::vector<std::size_t>{0, 1, 1, 1} &&
            earliest.depth[0].outgoingLabels == 2, "The frontier must keep different exception states");
    const TransferTable twoExceptions{{edge(0, 1, Class::Exception)}, {edge(1, 2, Class::Exception)}};
    const auto blocked = fixed_order_detail::decode(twoExceptions, 0, 1, false);
    require(blocked.stop == FixedOrderStop::ExceptionBudget && blocked.depth.back().chronologicalException == 1,
            "An on-time transfer still needs capacity for its exception");
}

void fixedOrder()
{
    auto problem = circular(3);
    problem.maxTotalTime = 2.25;
    const std::vector<int> order{2, 0, 1};
    FixedOrderConfig config;
    config.maxOrbitalCalls = 4;
    const auto result = FixedOrderScheduler::compare(problem, order, config);
    require(result.discovery.solverCalls == 2, "The two methods must share two actual discovery calls");
    for (const auto *method : {&result.greedy, &result.earliest})
    {
        require(method->stop == FixedOrderStop::Feasible && method->solution &&
                method->solution->visitingOrder == order && method->validationCalls == 2 &&
                method->completionDays == 2.25, "The full fixed order needs independent validation within the deadline");
        require(SolutionEvaluator::evaluate(problem, *method->solution, DeltaVPolicy::Strict).isFeasible(),
                "The exported route must be independently feasible");
    }
}

void failure()
{
    auto problem = circular(3);
    problem.tomatoes[0].a = 0;
    FixedOrderConfig config;
    config.maxOrbitalCalls = 26;
    config.departureBands = 2;
    const auto result = FixedOrderScheduler::compare(problem, {0, 1, 2}, config);
    require(result.discovery.solverCalls == 24 && result.coverage[0].solverFailures == 12 &&
            !result.coverage[0].lowestDeltaVMps && result.earliest.stop == FixedOrderStop::NoTransferFound &&
            result.earliest.prefix.empty() && result.earliest.validationCalls == 0 && !result.earliest.solution,
            "Failed transfers charge actual calls and cannot enter a prefix");
}

void budget()
{
    auto problem = circular(3);
    problem.maxTotalTime = 2.25;
    FixedOrderConfig config;
    config.maxOrbitalCalls = 2;
    const auto empty = FixedOrderScheduler::compare(problem, {0, 2, 1}, config);
    require(empty.greedy.stop == FixedOrderStop::EvaluationBudget && empty.discovery.solverCalls == 0 &&
            empty.earliest.validationCalls == 0, "Do not spend the independent-validation reserve on discovery");
    config.maxOrbitalCalls = 3;
    const auto limited = FixedOrderScheduler::compare(problem, {0, 2, 1}, config);
    require(limited.discovery.solverCalls == 1 && limited.earliest.prefix.size() == 1 &&
            !limited.earliest.solution && limited.earliest.validationCalls == 0,
            "No method may report a partial schedule as validated");
    config.maxOrbitalCalls = 34;
    config.departureBands = 3;
    const auto cached = FixedOrderScheduler::compare(problem, {0, 2, 1}, config);
    require(cached.discovery.solverCalls == 2 && cached.discovery.hits > 0 &&
            cached.earliest.validationCalls == 2 && cached.earliest.solution,
            "Identical proposals are reusable and the full validator still calls the solver");
}

void replay()
{
    const auto problem = circular(4);
    FixedOrderConfig config;
    config.maxOrbitalCalls = 100;
    config.seed = 7;
    config.departureBands = 4;
    config.flightScaleDays = 0.5;
    const auto a = FixedOrderScheduler::compare(problem, {3, 1, 2, 0}, config);
    const auto b = FixedOrderScheduler::compare(problem, {3, 1, 2, 0}, config);
    require(a.discovery.solverCalls == b.discovery.solverCalls && a.discovery.hits == b.discovery.hits &&
            a.discovery.solverCalls <= config.maxOrbitalCalls - 3, "The precomputation must replay within the cap");
    const auto check = [](const FixedOrderOutcome &x, const FixedOrderOutcome &y)
    {
        require(x.solution && y.solution && x.solution->departureTimes == y.solution->departureTimes &&
                x.solution->timesOfFlight == y.solution->timesOfFlight && x.completionDays == y.completionDays &&
                x.exceptionsByDepth == y.exceptionsByDepth && x.stop == y.stop, "Seeded schedules must replay exactly");
    };
    check(a.greedy, b.greedy);
    check(a.earliest, b.earliest);
}

void orderInput()
{
    std::istringstream valid("2 0 1\n");
    std::istringstream duplicate("2 0 0\n");
    std::istringstream schedule("0.0 1.0 1.25 2 0 1\n");
    std::istringstream extra("2 0 1 2\n");
    require(readFixedOrder(valid, 3) == std::optional{std::vector<int>{2, 0, 1}} &&
            !readFixedOrder(duplicate, 3) && !readFixedOrder(schedule, 3) && !readFixedOrder(extra, 3),
            "Accept only one complete permutation of integer indices");
    const auto result = FixedOrderScheduler::compare(circular(3), {2, 2, 1});
    require(result.earliest.stop == FixedOrderStop::InvalidInput && result.discovery.solverCalls == 0,
            "Reject malformed permutations before orbital work");
}

void seed0Reference(const char *instancePath, const char *orderPath, const char *schedulePath)
{
    constexpr std::size_t tomatoCount = 49;
    constexpr std::size_t legCount = tomatoCount - 1;
    constexpr double expectedCompletionDays = 176.88137704611736;
    constexpr double completionToleranceDays = 1e-9;

    Problem problem;
    require(problem.loadFromFile(instancePath) && problem.tomatoes.size() == tomatoCount,
            "Expected the original 49-tomato ESA easy instance");
    std::ifstream orderFile(orderPath);
    const auto expectedOrder = readFixedOrder(orderFile, tomatoCount);
    require(expectedOrder.has_value(), "Could not read the ESA example visit order");

    std::ifstream schedule(schedulePath);
    require(schedule.is_open(), "Could not open the seed-0 schedule");
    Solution solution;
    solution.departureTimes.resize(legCount);
    solution.timesOfFlight.resize(legCount);
    solution.visitingOrder.resize(tomatoCount);
    for (auto &departure : solution.departureTimes)
    {
        require(static_cast<bool>(schedule >> departure), "Invalid departure time in the seed-0 schedule");
    }
    for (auto &duration : solution.timesOfFlight)
    {
        require(static_cast<bool>(schedule >> duration), "Invalid flight duration in the seed-0 schedule");
    }
    for (auto &tomato : solution.visitingOrder)
    {
        require(static_cast<bool>(schedule >> tomato), "Invalid tomato index in the seed-0 schedule");
    }
    schedule >> std::ws;
    require(schedule.eof() && !schedule.bad(), "Unexpected trailing data in the seed-0 schedule");
    require(solution.visitingOrder == *expectedOrder, "The schedule must preserve the ESA example visit order");

    const auto result = SolutionEvaluator::evaluate(problem, solution, DeltaVPolicy::Strict);
    require(result.isFeasible() && result.legs.size() == legCount,
            "All 48 transfers must pass strict independent evaluation");
    require(result.missionCompletionDays &&
                std::abs(*result.missionCompletionDays - expectedCompletionDays) <= completionToleranceDays,
            "The evaluated mission completion time changed");

    std::vector<std::size_t> exceptionLegs;
    for (std::size_t i = 0; i < result.legs.size(); ++i)
    {
        const auto &leg = result.legs[i];
        require(leg.transferStatus == TransferStatus::Ok, "A reference transfer failed orbital evaluation");
        if (leg.classification == Class::Exception) { exceptionLegs.push_back(i + 1); }
        else { require(leg.classification == Class::Normal, "Unexpected reference transfer classification"); }
    }
    require(result.exceptionCount == 5 && exceptionLegs == std::vector<std::size_t>{3, 12, 24, 27, 46},
            "The evaluated exception count or exception legs changed");

    std::cout << std::setprecision(17) << *result.missionCompletionDays << " days, " << legCount
              << " strict transfers verified, " << result.exceptionCount << " exceptions\n";
}

struct TestCase
{
    std::string_view name;
    void (*run)();
};

constexpr TestCase cases[] = {
    {"chronology", chronology}, {"exceptions", exceptions}, {"fixed_order", fixedOrder}, {"failure", failure},
    {"budget", budget}, {"replay", replay}, {"order_input", orderInput}
};
}

int main(int argc, char *argv[])
{
    try
    {
        if (argc == 5 && std::string_view(argv[1]) == "seed0_reference")
        {
            seed0Reference(argv[2], argv[3], argv[4]);
            return 0;
        }
        if (argc == 2)
        {
            for (const auto &test : cases)
            {
                if (test.name != argv[1]) { continue; }
                test.run();
                return 0;
            }
        }
        std::cerr << "Unknown fixed-order scheduling test.\n";
    }
    catch (const std::exception &error)
    {
        std::cerr << argv[1] << ": " << error.what() << '\n';
    }
    return 1;
}
