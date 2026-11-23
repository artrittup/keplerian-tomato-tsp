#pragma once

#include <cstddef>

class Problem;

enum class TransferStatus
{
    Ok,
    InvalidInput,
    NoFiniteCandidate, // Neither Lambert direction supplied a usable candidate.
    NumericalFailure   // The endpoint states or Lambert inputs exceeded numerical limits.
};

struct TransferResult
{
    TransferStatus status;
    double deltaVMps; // Positive infinity unless status is Ok.
};

class TransferSolver
{
public:
    // Indices follow file order. Departure is an absolute MJD2000 epoch in days.
    // Flight time is a positive duration in days. Solution checks route timing.
    static TransferResult evaluate(const Problem &problem, std::size_t source,
                                   std::size_t destination, double departureDays, double flightDays);
};

enum class DeltaVPolicy
{
    Strict,
    PublishedEvaluator
};

enum class DeltaVClassification
{
    Normal,
    Exception,
    Infeasible
};

// Classifies one leg using the instance limits. The exception count is a route constraint.
DeltaVClassification classifyDeltaV(const Problem &problem, double deltaVMps, DeltaVPolicy policy);
