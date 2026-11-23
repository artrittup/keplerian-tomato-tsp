#pragma once

#include "Solution.hpp"
#include "SolutionEvaluator.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <random>
#include <vector>

struct LegTiming
{
    double departureDays;
    double flightDays;
};

enum class RouteExtensionStatus
{
    Ok,
    InvalidDestination,
    AlreadyVisited,
    InvalidTiming,
    DeadlineExceeded,
    TransferFailed,
    DeltaVLimitExceeded,
    ExceptionBudgetExceeded
};

struct RouteExtensionResult;

struct CompletedRoute
{
    Solution solution;
    SolutionEvaluationResult evaluation;
};

class PartialRoute
{
public:
    // The problem must outlive the route and remain unchanged. The first visit is at its start time.
    static std::optional<PartialRoute> start(const Problem &problem, int firstTomato, DeltaVPolicy policy);
    static std::optional<PartialRoute> start(const Problem &&, int, DeltaVPolicy) = delete;

    const Solution &prefix() const { return prefix_; }
    double arrivalDays() const { return arrivalDays_; }
    std::size_t exceptionsUsed() const { return exceptionsUsed_; }
    std::size_t remainingLegs() const { return visited_.size() - prefix_.visitingOrder.size(); }
    bool hasVisited(int tomato) const;

    RouteExtensionResult extend(int destination, LegTiming timing) const;

    // A complete visit sequence is returned with its full evaluation, which determines feasibility.
    std::optional<CompletedRoute> finish() const;

private:
    PartialRoute(const Problem &problem, int firstTomato, DeltaVPolicy policy);
    double latestLegArrivalDays() const; // Requires at least one remaining leg.

    friend class TimingSampler;

    const Problem *problem_;
    DeltaVPolicy policy_;
    Solution prefix_;
    std::vector<bool> visited_;
    double arrivalDays_;
    std::size_t exceptionsUsed_ = 0;
};

struct RouteExtensionResult
{
    RouteExtensionStatus status;
    std::optional<PartialRoute> route;
    std::optional<TransferResult> transfer; // Present when the extension made one solver call.
};

class TimingSampler
{
public:
    explicit TimingSampler(std::uint64_t seed) : engine_(seed) {}

    // Uniform over nonnegative wait and extra flight time whose sum fits the available slack.
    // Uses a fixed binary64 mapping of mt19937_64 outputs. No orbital transfer is evaluated.
    std::optional<LegTiming> sample(const PartialRoute &route);
    // Optionally cap the combined wait and extra flight time for a shorter timing proposal.
    std::optional<LegTiming> sample(const PartialRoute &route, double maxSlackDays);
    // Restrict extra flight time to one of the equal-width bands of the slack interval.
    std::optional<LegTiming> sampleStratified(const PartialRoute &route, double maxSlackDays,
                                             std::size_t band, std::size_t bands);

private:
    std::optional<LegTiming> sampleWindow(const PartialRoute &route, double maxSlackDays,
                                         std::size_t band, std::size_t bands);
    double unitSample();

    std::mt19937_64 engine_;
};
