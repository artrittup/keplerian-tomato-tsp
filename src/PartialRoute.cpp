#include "PartialRoute.hpp"

#include "Problem.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace
{
bool validIndex(int tomato, std::size_t count)
{
    return tomato >= 0 && static_cast<std::size_t>(tomato) < count;
}
}

PartialRoute::PartialRoute(const Problem &problem, int firstTomato, DeltaVPolicy policy)
    : problem_(&problem), policy_(policy), prefix_{{firstTomato}, {}, {}},
      visited_(problem.tomatoes.size(), false), arrivalDays_(problem.startTime)
{
    visited_[static_cast<std::size_t>(firstTomato)] = true;
}

std::optional<PartialRoute> PartialRoute::start(const Problem &problem, int firstTomato, DeltaVPolicy policy)
{
    if (!validIndex(firstTomato, problem.tomatoes.size()) ||
        !std::isfinite(problem.startTime) || problem.startTime < 0.0 ||
        !std::isfinite(problem.minTimeOfFlight) || problem.minTimeOfFlight < 0.0 ||
        !std::isfinite(problem.maxTotalTime) || problem.maxTotalTime < problem.startTime ||
        problem.maxExceptions < 0 || classifyDeltaV(problem, 0.0, policy) != DeltaVClassification::Normal)
    {
        return std::nullopt;
    }
    return PartialRoute(problem, firstTomato, policy);
}

bool PartialRoute::hasVisited(int tomato) const
{
    return validIndex(tomato, visited_.size()) && visited_[static_cast<std::size_t>(tomato)];
}

double PartialRoute::latestLegArrivalDays() const
{
    const auto laterLegs = static_cast<double>(remainingLegs() - 1);
    return problem_->maxTotalTime - laterLegs * problem_->minTimeOfFlight;
}

RouteExtensionResult PartialRoute::extend(int destination, LegTiming timing) const
{
    if (!validIndex(destination, visited_.size()))
    {
        return {RouteExtensionStatus::InvalidDestination, std::nullopt, std::nullopt};
    }
    if (hasVisited(destination))
    {
        return {RouteExtensionStatus::AlreadyVisited, std::nullopt, std::nullopt};
    }

    const double arrival = timing.departureDays + timing.flightDays;
    if (!std::isfinite(timing.departureDays) || timing.departureDays < arrivalDays_ ||
        !std::isfinite(timing.flightDays) || timing.flightDays <= 0.0 ||
        timing.flightDays < problem_->minTimeOfFlight || !std::isfinite(arrival))
    {
        return {RouteExtensionStatus::InvalidTiming, std::nullopt, std::nullopt};
    }
    if (arrival > latestLegArrivalDays())
    {
        return {RouteExtensionStatus::DeadlineExceeded, std::nullopt, std::nullopt};
    }

    const auto sourceIndex = static_cast<std::size_t>(prefix_.visitingOrder.back());
    const auto destinationIndex = static_cast<std::size_t>(destination);
    const auto transfer = TransferSolver::evaluate(*problem_, sourceIndex, destinationIndex,
                                                    timing.departureDays, timing.flightDays);
    if (transfer.status != TransferStatus::Ok)
    {
        return {RouteExtensionStatus::TransferFailed, std::nullopt, transfer};
    }

    const auto classification = classifyDeltaV(*problem_, transfer.deltaVMps, policy_);
    if (classification == DeltaVClassification::Infeasible)
    {
        return {RouteExtensionStatus::DeltaVLimitExceeded, std::nullopt, transfer};
    }
    const auto exceptions = exceptionsUsed_ + (classification == DeltaVClassification::Exception ? 1 : 0);
    if (exceptions > static_cast<std::size_t>(problem_->maxExceptions))
    {
        return {RouteExtensionStatus::ExceptionBudgetExceeded, std::nullopt, transfer};
    }

    PartialRoute child = *this;
    child.prefix_.visitingOrder.push_back(destination);
    child.prefix_.departureTimes.push_back(timing.departureDays);
    child.prefix_.timesOfFlight.push_back(timing.flightDays);
    child.visited_[destinationIndex] = true;
    child.arrivalDays_ = arrival;
    child.exceptionsUsed_ = exceptions;
    return {RouteExtensionStatus::Ok, std::move(child), transfer};
}

std::optional<CompletedRoute> PartialRoute::finish() const
{
    if (remainingLegs() != 0)
    {
        return std::nullopt;
    }
    return CompletedRoute{prefix_, SolutionEvaluator::evaluate(*problem_, prefix_, policy_)};
}

double TimingSampler::unitSample()
{
    // The upper 53 bits map exactly to multiples of 2^-53 in [0, 1).
    return static_cast<double>(engine_() >> 11) * 0x1p-53;
}

std::optional<LegTiming> TimingSampler::sample(const PartialRoute &route)
{
    if (route.remainingLegs() == 0)
    {
        return std::nullopt;
    }
    const double opening = route.arrivalDays();
    const double latest = route.latestLegArrivalDays();
    const double minimum = std::max(route.problem_->minTimeOfFlight, std::numeric_limits<double>::min());
    if (latest <= opening || opening + minimum > latest)
    {
        return std::nullopt;
    }
    const double slack = std::max(0.0, (latest - opening) - minimum);
    if (slack == 0.0)
    {
        return LegTiming{opening, minimum};
    }

    double waitFraction = unitSample();
    double extraFraction = unitSample();
    if (waitFraction + extraFraction > 1.0)
    {
        waitFraction = 1.0 - waitFraction;
        extraFraction = 1.0 - extraFraction;
    }
    const LegTiming proposal{opening + waitFraction * slack, minimum + extraFraction * slack};
    if (proposal.departureDays + proposal.flightDays > latest)
    {
        // Rounding near the boundary can exceed the deadline. The earliest pair already fits.
        return LegTiming{opening, minimum};
    }
    return proposal;
}
