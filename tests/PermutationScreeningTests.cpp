#include "PermutationScreening.hpp"

#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string_view>

namespace
{
void require(bool condition, const char *message)
{
    if (!condition) { throw std::runtime_error(message); }
}

Problem circular()
{
    Problem problem;
    problem.tomatoes.assign(4, Tomato{10000000.0, 0, 0, 0, 0, 0});
    problem.startTime = 2.0;
    problem.minTimeOfFlight = 0.125;
    problem.maxTotalTime = 4.0;
    problem.maxDeltaV = 0.01;
    problem.maxExceptionDeltaV = 1.0;
    problem.maxExceptions = 1;
    return problem;
}

TransferWindowBounds fixedTiming(double departure, double flight = 0.125)
{
    return {departure, departure, flight, flight, departure + flight};
}

void validity(const char *)
{
    require(isTomatoPermutation({2, 0, 1}, 3) && isTomatoPermutation({0}, 1), "Valid permutations rejected");
    require(!isTomatoPermutation({0, 1, 1}, 3) && !isTomatoPermutation({0, 1}, 3) &&
            !isTomatoPermutation({0, 1, -1}, 3) && !isTomatoPermutation({0, 1, 3}, 3) &&
            !isTomatoPermutation({}, 0), "Invalid or incomplete permutations accepted");
    TransferEvidenceMatrix matrix(circular());
    try { matrix.score({0, 0, 1, 2}); }
    catch (const std::invalid_argument &) { return; }
    throw std::runtime_error("Screen accepted duplicate tomato indices");
}

void operators(const char *)
{
    const std::vector<int> original{0, 1, 2, 3, 4};
    auto order = original;
    moveTomatoes(order, PermutationMove::Swap, 0, 4);
    require(order == std::vector<int>{4, 1, 2, 3, 0}, "Swap must exchange positions");
    order = original;
    moveTomatoes(order, PermutationMove::Insert, 0, 3);
    require(order == std::vector<int>{1, 2, 3, 0, 4}, "Forward insertion placed the wrong final index");
    moveTomatoes(order, PermutationMove::Insert, 3, 0);
    require(order == original, "Backward insertion must undo the move");
    moveTomatoes(order, PermutationMove::Reverse, 4, 1);
    require(order == std::vector<int>{0, 4, 3, 2, 1}, "Reversal must include both positions");
    const auto reversed = order;
    moveTomatoes(order, PermutationMove::Reverse, 2, 2);
    require(order == reversed, "A same-position move must be a no-op");

    for (const auto move : {PermutationMove::Swap, PermutationMove::Insert, PermutationMove::Reverse})
    {
        for (std::size_t from = 0; from < original.size(); ++from)
        {
            for (std::size_t to = 0; to < original.size(); ++to)
            {
                order = original;
                moveTomatoes(order, move, from, to);
                require(isTomatoPermutation(order, original.size()), "Move lost or duplicated a tomato");
            }
        }
    }
    try { moveTomatoes(order, PermutationMove::Insert, 0, order.size()); }
    catch (const std::out_of_range &) { return; }
    throw std::runtime_error("Expected an invalid position error");
}

void evidence(const char *path)
{
    Problem problem;
    require(problem.loadFromFile(path) && problem.tomatoes.size() == 49, "Expected ESA easy");
    problem.maxExceptions = 0;
    TransferEvidenceMatrix matrix(problem);
    // Independent transfer probes from the transfer-window tests.
    matrix.samplePair(0, 1, 1, fixedTiming(0.125, 5.0));
    matrix.samplePair(0, 4, 1, fixedTiming(0.25, 0.001));
    matrix.samplePair(1, 48, 128, {0.125, 0.125, 4.999, 5.0, {}});
    require(matrix.at(0, 1).kind() == PairEvidenceKind::ExceptionObserved &&
            matrix.at(0, 4).kind() == PairEvidenceKind::RejectedSamples && matrix.at(0, 4).rejected == 1 &&
            matrix.at(1, 0).kind() == PairEvidenceKind::Unknown,
            "The matrix must distinguish admissible, rejected and unsampled directed pairs");
    const auto &normal = matrix.at(1, 48);
    require(normal.kind() == PairEvidenceKind::NormalObserved && normal.normal > 0 && normal.exceptions > 0,
            "Keep distinct normal and exception counts for a mixed pair");
    std::vector<int> order(49);
    std::iota(order.begin(), order.end(), 0);
    const auto score = matrix.score(order);
    require(score.exceptionOnlyEdges == 1 && score.exceptionsOverBudget == 1 && score.unknownEdges == 47,
            "Screen for exception dependence using the instance budget and keep unknown legs separate");

    problem.tomatoes[1].a = 0;
    TransferEvidenceMatrix invalidOrbit(problem);
    invalidOrbit.samplePair(0, 1, 1, fixedTiming(0.125, 5.0));
    const auto &failed = invalidOrbit.at(0, 1);
    require(failed.kind() == PairEvidenceKind::Unknown && failed.solverFailures == 1 && failed.rejected == 0,
            "A solver failure is not evidence of Delta-V above the ceiling");
}

void chronology(const char *)
{
    TransferEvidenceMatrix matrix(circular());
    matrix.samplePair(0, 1, 1, fixedTiming(3));
    matrix.samplePair(1, 2, 1, fixedTiming(2.5));
    require(matrix.score({0, 1, 2, 3}).incompatibleJoins == 1 &&
            matrix.score({0, 1, 2, 3}).unassessedJoins == 1,
            "A missed observed join and an unassessed join must remain distinct");
    matrix.samplePair(1, 2, 1, fixedTiming(3.125));
    matrix.samplePair(2, 3, 1, fixedTiming(3));
    const auto score = matrix.score({0, 1, 2, 3});
    require(score.compatibleJoins == 2 && score.incompatibleJoins == 0 && score.unassessedJoins == 0,
            "Two local joins can both work, even using different middle witnesses");
}

void score(const char *)
{
    PermutationScore good, over, conflict, unknown, rejected;
    good.normalEdges = 3;
    over.exceptionOnlyEdges = 2;
    over.exceptionsOverBudget = 1;
    conflict.incompatibleJoins = 1;
    unknown.unknownEdges = 1;
    rejected.rejectedOnlyEdges = 1;
    require(good.rank() < conflict.rank() && conflict.rank() < over.rank() && over.rank() < unknown.rank() &&
            unknown.rank() < rejected.rank(), "Unexpected ranking priority for equal-length paths");

    auto problem = circular();
    TransferEvidenceMatrix matrix(problem);
    require(matrix.score({0, 1, 2, 3}).unknownEdges == 3 &&
            matrix.score({0, 1, 2, 3}).unassessedJoins == 2 && matrix.stats().solverCalls == 0,
            "Scoring an empty table cannot fabricate transfer evidence or call the solver");
    problem.tomatoes.resize(1);
    TransferEvidenceMatrix one(problem);
    one.sampleAll();
    require(one.score({0}).rank() == good.rank() && one.stats().solverCalls == 0,
            "A one-tomato order has no transfers or joins");
}

void limits(const char *)
{
    TransferEvidenceConfig config;
    config.maxOrbitalCalls = 2;
    TransferEvidenceMatrix matrix(circular(), config);
    const auto first = matrix.samplePair(0, 1, 10, fixedTiming(2));
    const auto replay = matrix.samplePair(0, 1, 10, fixedTiming(2));
    matrix.samplePair(1, 0, 10, fixedTiming(2));
    matrix.sampleAll();
    require(first.solverCalls == 1 && replay.solverCalls == 0 && replay.cacheHits == 1 &&
            matrix.stats().solverCalls == 2 && matrix.score({0, 1, 2, 3}).normalEdges == 1 &&
            matrix.at(1, 2).kind() == PairEvidenceKind::Unknown, "Reuse and score without exceeding the call budget");

    config.maxPairs = 11; // Four tomatoes need twelve directed cells.
    try { TransferEvidenceMatrix tooLarge(circular(), config); }
    catch (const std::length_error &) { return; }
    throw std::runtime_error("Expected the pair capacity to constrain matrix allocation");
}

void reproducibility(const char *)
{
    TransferEvidenceConfig config;
    config.maxOrbitalCalls = 4;
    TransferEvidenceMatrix first(circular(), config), second(circular(), config);
    first.sampleAll(fixedTiming(2));
    second.sampleAll(fixedTiming(2));
    require(first.stats().solverCalls == 4 && second.stats().solverCalls == 4 &&
            first.at(3, 0).kind() == PairEvidenceKind::NormalObserved &&
            first.at(0, 2).kind() == PairEvidenceKind::Unknown, "Small budgets should round-robin across sources");
    std::mt19937_64 a(7), b(7);
    const std::vector<int> reference{2, 1, 3, 0};
    for (int i = 0; i < 16; ++i)
    {
        const auto neighbor = sampleTomatoNeighbor(reference, PermutationMove::Reverse, a);
        require(neighbor != reference && neighbor == sampleTomatoNeighbor(reference, PermutationMove::Reverse, b) &&
                first.score(neighbor).rank() == second.score(neighbor).rank(), "Seeded neighbors and scores must replay");
        const auto random = randomTomatoPermutation(4, a);
        require(random == randomTomatoPermutation(4, b) && isTomatoPermutation(random, 4),
                "Seeded complete permutations must replay");
    }
}
}

int main(int argc, char *argv[])
{
    if (argc != 3) { return 1; }
    const std::pair<std::string_view, void (*)(const char *)> tests[] = {
        {"validity", validity}, {"operators", operators}, {"evidence", evidence}, {"chronology", chronology},
        {"score", score}, {"limits", limits}, {"reproducibility", reproducibility}
    };
    try
    {
        for (const auto &test : tests)
        {
            if (test.first != argv[1]) { continue; }
            test.second(argv[2]);
            return 0;
        }
        std::cerr << "Unknown permutation-screen test\n";
    }
    catch (const std::exception &error)
    {
        std::cerr << argv[1] << ": " << error.what() << '\n';
    }
    return 1;
}
