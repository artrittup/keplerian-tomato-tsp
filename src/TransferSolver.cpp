#include "TransferSolver.hpp"

#include "OrbitalDiagnostics.hpp"
#include "Problem.hpp"

#include <keplerian_toolbox/exceptions.hpp>
#include <keplerian_toolbox/lambert_problem.hpp>
#include <keplerian_toolbox/planet/keplerian.hpp>

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <new>

namespace
{
constexpr double moonMu = 4.9048695e12;
constexpr double secondsPerDay = 86400.0;
constexpr int maxRevolutions = 20;
constexpr double pi = 3.14159265358979323846;
constexpr double evaluatorExceptionTolerance = 1e-6;
constexpr double infinity = std::numeric_limits<double>::infinity();

using kep_toolbox::array3D;
using kep_toolbox::array6D;

template <class Array>
bool allFinite(const Array &values)
{
    return std::all_of(values.begin(), values.end(), [](double value) { return std::isfinite(value); });
}

array6D elements(const Tomato &tomato)
{
    // ESA interprets the sixth column (Problem's nu) as mean anomaly at the file epoch.
    return {tomato.a, tomato.e, tomato.i, tomato.Omega, tomato.omega, tomato.nu};
}

bool validOrbit(const array6D &orbit)
{
    return allFinite(orbit) && orbit[0] > 0.0 && orbit[1] >= 0.0 && orbit[1] < 1.0;
}

double distance(const array3D &a, const array3D &b)
{
    const double x = a[0] - b[0];
    const double y = a[1] - b[1];
    const double z = a[2] - b[2];
    return std::sqrt(x * x + y * y + z * z);
}
}

TransferResult TransferSolver::evaluate(const Problem &problem, std::size_t source,
                                        std::size_t destination, double departureDays, double flightDays)
{
    auto *const diagnostics = OrbitalDiagnostics::active;
    if (diagnostics) { diagnostics->query(source, destination, departureDays, flightDays); }
    OrbitalTimer total(diagnostics, OrbitalMetric::Solver);
    const double arrivalDays = departureDays + flightDays;
    const double flightSeconds = flightDays * secondsPerDay;
    if (source >= problem.tomatoes.size() || destination >= problem.tomatoes.size() ||
        !std::isfinite(problem.startTime) || !std::isfinite(departureDays) ||
        !std::isfinite(arrivalDays) || !std::isfinite(flightSeconds) || flightSeconds <= 0.0 ||
        !std::isfinite((departureDays - problem.startTime) * secondsPerDay) ||
        !std::isfinite((arrivalDays - problem.startTime) * secondsPerDay))
    {
        return {TransferStatus::InvalidInput, infinity};
    }

    const auto fromElements = elements(problem.tomatoes[source]);
    const auto toElements = elements(problem.tomatoes[destination]);
    if (!validOrbit(fromElements) || !validOrbit(toElements))
    {
        return {TransferStatus::InvalidInput, infinity};
    }

    array3D rFrom{}, vFrom{}, rTo{}, vTo{};
    try
    {
        OrbitalTimer setup(diagnostics, OrbitalMetric::PlanetSetup);
        const kep_toolbox::epoch epoch(problem.startTime);
        const kep_toolbox::planet::keplerian from(epoch, fromElements, moonMu, 0.0, 0.0, 0.0);
        const kep_toolbox::planet::keplerian to(epoch, toElements, moonMu, 0.0, 0.0, 0.0);
        setup.pause();
        OrbitalTimer propagation(diagnostics, OrbitalMetric::Propagation);
        from.eph(departureDays, rFrom, vFrom);
        to.eph(arrivalDays, rTo, vTo);
    }
    catch (const std::bad_alloc &)
    {
        throw;
    }
    catch (const std::exception &)
    {
        // PyKEP's low-eccentricity propagation can also throw from Boost.Math.
        return {TransferStatus::NumericalFailure, infinity};
    }

    const array3D origin{};
    const double fromRadius = distance(rFrom, origin);
    const double toRadius = distance(rTo, origin);
    if (!std::isfinite(fromRadius) || fromRadius <= 0.0 ||
        !std::isfinite(toRadius) || toRadius <= 0.0 ||
        !allFinite(vFrom) || !allFinite(vTo))
    {
        return {TransferStatus::NumericalFailure, infinity};
    }

    const double chord = distance(rFrom, rTo);
    if (chord == 0.0)
    {
        return {TransferStatus::NoFiniteCandidate, infinity};
    }

    // PyKEP v2.6 converts the dimensionless time divided by pi to int
    // before applying the 20-revolution cap. Guard that conversion.
    const double semiperimeter = (chord + fromRadius + toRadius) / 2.0;
    const double scaledTime = std::sqrt(2.0 * moonMu / semiperimeter / semiperimeter / semiperimeter)
                              * flightSeconds;
    if (!std::isfinite(scaledTime) || scaledTime <= 0.0 ||
        scaledTime / pi >= static_cast<double>(std::numeric_limits<int>::max()) + 1.0)
    {
        return {TransferStatus::NumericalFailure, infinity};
    }

    OrbitalTimer lambert(diagnostics, OrbitalMetric::Lambert);
    double best = infinity;
    for (int clockwise : {0, 1})
    {
        try
        {
            const kep_toolbox::lambert_problem transfer(rFrom, rTo, flightSeconds,
                                                      moonMu, clockwise, maxRevolutions);
            const auto &launch = transfer.get_v1();
            const auto &arrival = transfer.get_v2();
            if (launch.size() != arrival.size())
            {
                continue;
            }
            for (std::size_t branch = 0; branch < launch.size(); ++branch)
            {
                const double deltaV = distance(launch[branch], vFrom) + distance(arrival[branch], vTo);
                if (std::isfinite(deltaV) && deltaV < best)
                {
                    best = deltaV;
                }
            }
        }
        catch (const kep_toolbox_error &)
        {
            // The other direction can still supply a finite candidate.
        }
    }
    return {std::isfinite(best) ? TransferStatus::Ok : TransferStatus::NoFiniteCandidate, best};
}

DeltaVClassification classifyDeltaV(const Problem &problem, double deltaVMps, DeltaVPolicy policy)
{
    if (!std::isfinite(deltaVMps) || deltaVMps < 0.0 ||
        !std::isfinite(problem.maxDeltaV) || problem.maxDeltaV < 0.0 ||
        !std::isfinite(problem.maxExceptionDeltaV) || problem.maxExceptionDeltaV < problem.maxDeltaV)
    {
        return DeltaVClassification::Infeasible;
    }
    if (deltaVMps <= problem.maxDeltaV)
    {
        return DeltaVClassification::Normal;
    }
    const double exceptionCeiling = policy == DeltaVPolicy::PublishedEvaluator
        ? problem.maxExceptionDeltaV + evaluatorExceptionTolerance
        : problem.maxExceptionDeltaV;
    return deltaVMps <= exceptionCeiling ? DeltaVClassification::Exception : DeltaVClassification::Infeasible;
}
