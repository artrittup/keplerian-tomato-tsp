# Phase 5A CPU optimization design

Start with **randomized beam construction followed by multi-start local search**. Construction searches for a complete feasible route by testing exact transfers as it grows each partial route. Local search then tries to shorten verified schedules. Compare simulated annealing and a seeded genetic algorithm only after measuring this baseline under the same transfer-evaluation budget. The optimizer will produce an existing `Solution` and use `SolutionEvaluator` as the final authority on feasibility.

## Objective and constraints

Let `N` be the number of tomatoes and `L = N - 1`. The decision variables are an open visit order `π`, `L` absolute departure epochs `d[i]`, and `L` flight durations `τ[i]`, all times in days. A route may start at any tomato and finish at any other. Its first leg departs at or after `t0`, and the craft can wait before a leg. Leg `i` goes from `π[i]` to `π[i+1]` and arrives at `a[i] = d[i] + τ[i]`. Minimize **`F = a[L-1]`**, the absolute final arrival, subject to feasibility. Waiting affects this value. If `N = 1`, the project reports `t0`. The elapsed time `F - t0` gives the same ranking within one instance. The ESA export order is departures, flight durations, then zero-based tomato indices. [ESA statement][spec] · [Published evaluator][evaluator]

| Constraint | Phase 5 acceptance rule |
| --- | --- |
| Visit order | Permutation of all `N` indices. No return leg. |
| Structure and time | Finite epochs and durations, `d[0] ≥ t0`, `τ[i] ≥ Problem::minTimeOfFlight`, `a[i] ≤ d[i+1]`, and `F ≤ Problem::maxTotalTime`. The current C++ validator treats the last field as an absolute deadline. The transfer backend requires a positive flight duration. |
| Orbital transfer | Call `TransferSolver::evaluate` at the actual departure and flight time. Every leg must return `Ok`. Use the existing Moon-centered, two-body PyKEP convention and minimum over Lambert candidates. |
| Δv and exceptions | A leg at or below `Problem::maxDeltaV` is normal. Above it, a leg at or below the selected exception ceiling uses one exception. The strict ceiling is `Problem::maxExceptionDeltaV`. The published-evaluator policy adds `1e-6 m/s` to that ceiling only. Reject any higher cost and any route with more than `Problem::maxExceptions` exceptions. |

The published **Large** evaluator returns final arrival, even though its `get_nobj()` comment refers to flight time. Its bounds give departures a lower bound of zero and independently bound departures and durations from above. Its `fitness()` accepts a `1e-6` day chronology margin and has no separate final-arrival deadline test. Its three equality constraints enforce a permutation within the variable bounds, all legs within the upper Δv ceiling, and the required chronology count. Its inequality limits the number of exceptions. The C++ optimizer will keep the existing `Solution` constraints for both policies, including exact chronology and the deadline. Use `Strict` by default and log the selected policy. The scorer-compatible mode matches the published upper Δv comparison. This review used the pinned source archived in Phase 3D. Independent service parity for the `small` endpoint remains unverified. [ESA statement][spec] · [Published Large evaluator][evaluator] · [Orbital transfer verification](orbital-transfer-design.md)

The evaluator returns per-leg costs for diagnostics and sums accepted Δv. Feasible routes are compared by completion time, with a deterministic candidate ID breaking exact ties. A lower total Δv gives no advantage in the official objective. A candidate enters the feasible archive only after `SolutionEvaluator::evaluate(problem, solution, policy)` returns feasible.

## Search space of the easy instance

The released file has **49 tomatoes, 48 transfers, 96 real timing variables, and 145 total decision entries**. There are **`49! ≈ 6.083 × 10^62`** visit orders before timing constraints. Neither cyclic rotations nor reversal offer a general reduction for this open route. Departure time, duration, and direction of traversal matter. The header specifies `t0 = 0`, a minimum flight of `0.001 d` (86.4 s), a deadline of day 200, Δv limits of 100 and 600 m/s, and five exceptions. At least 43 legs must be normal. The sum of minimum durations is `0.048 d`, leaving at most `199.952 d` for all waits and extra flight time. This is a structural bound with orbital feasibility still to be checked. [Released easy instance][easy]

The 49 semi-major axes form two groups in the released data, nine near 3.75 million meters and 40 near 15 million meters. With the project's lunar gravitational parameter the Keplerian periods span about 0.237 to 1.913 days. These scales can guide sampling and candidate ordering. A symmetric distance matrix is unsuitable because the orbital states vary with epoch, and timing a leg changes the earliest departure of every later leg. Even a directed table of 100 departure samples by 100 duration samples for every pair has about 23.5 million entries before deadline filtering. Feasible windows may be narrow or absent in the sampled range. The fraction of feasible full routes and the best time on this instance remain to be measured. [Released easy instance][easy] · [Orbital transfer verification](orbital-transfer-design.md)

## CPU approach comparison

| Approach | How it would work here | Main limitation |
| --- | --- | --- |
| Multi-start local search | Construct several schedules, then improve them by rescheduling one leg, relocating a tomato, or swapping two visits and repairing the affected suffix. Small moves preserve some exact transfer queries. | A feasible route may be isolated under small moves. Restarts and larger segment repairs give it more opportunities to escape. |
| Simulated annealing | Reuse the same moves and repairs. Allow an uphill move with probability `exp(-ΔE/T)` based on an internal search score, and keep a separate exact feasible incumbent. | Temperature and violation scales require tuning. A move that damages many downstream legs can consume as much computation as a good move. |
| Genetic algorithm | Seed a population with constructed routes and use order crossover, mutation, and schedule repair. Individual evaluations offer independent work. | A child can inherit a promising visit segment at different transfer epochs. Many children may lose feasibility, and evaluating a population spends the same scarce Lambert budget. |

Local search is the smallest useful baseline because it shares machinery with construction and lets us measure how often a change affects only part of the route. Use **beam width `B = 4` and up to `K = 16` time pairs per directed extension** as initial settings. Beam search keeps a small set of promising partial routes at each depth. Compare `B = 1` as a greedy control. Once feasible seed generation works, test SA on the same neighborhoods. A GA earns its place if seeded crossover and repair improve success rate or completion time at the same budget. [Simulated annealing paper][sa] · [Order-preserving crossover][ox]

## Chromosome and schedule decoding

Use a permutation `order` and two arrays indexed by leg position. `wait[i] ≥ 0` is the dwell before departure, and `extra[i] ≥ 0` is added to the minimum time of flight `m`. This encoding has the same number of real variables as `Solution`. For `L > 0`, its structural slack is `S = deadline - t0 - L*m`. Reject an instance with a negative available structural slack, subject to the same floating-point checks as the validator near equality. Use positive durations for orbital queries if an input permits `m = 0`. For a random control or exploratory GA seed, draw a total slack usage between zero and `S` and distribute it across waits and extras with normalized random weights. For construction, append one schedule segment at a time.

```text
arrival = t0
for i in 0 .. L-1:
    departure[i] = arrival + wait[i]
    flight[i] = m + extra[i]
    arrival = departure[i] + flight[i]
return Solution(order, departure, flight)
```

In exact arithmetic, `sum(wait + extra) ≤ S` ensures a schedule within the deadline. Retain unused slack so mutations can shorten or lengthen the mission. When repairing a suffix, keep the decoded prefix fixed and compute the suffix's available slack from its actual starting epoch. Project negative suffix values to zero and, if needed, scale only the suffix slack values down to fit. Verify the decoded binary64 schedule with `Solution::isStructurallyValid` before orbital evaluation. Check equality and one-ULP violations in tests. Projection changes the timing, so every changed transfer query must be evaluated again.

| Operator | Design |
| --- | --- |
| Order crossover (OX) | Copy a contiguous segment of parent A into the same child positions. Scan B cyclically from just past the segment and fill the unoccupied positions in that order, skipping any index already used. An index mask makes this `O(N)`. The child remains a permutation. |
| Timing inheritance | For a child leg with the same directed adjacency in a parent, use that parent's wait and extra as a starting proposal. When both contain it, select a parent with the seeded PRNG. Sample new values for new adjacencies. Re-decode and repair, because an inherited edge at a different epoch can have a different cost. |
| Order mutation | Swap two indices, relocate one index, or reverse a short section for a larger move. Each operation preserves membership. A reversal changes directed edges and their times. Rebuild or repair from the earliest affected leg. |
| Timing mutation | Change a wait or flight extra on one leg using a bounded signed step. Redistribute slack between waits, flight extras, or unused slack. In particular, shifting slack between one leg's wait and flight extra can change departure and flight while approximately preserving arrival. Reuse later evaluations only if their decoded query keys still match exactly. Use both small refinements and larger changes to probe other orbital phases. |

## Construction and local improvement

Start different runs at tomatoes in a seeded shuffled order. A partial route stores visited indices, last tomato, exact arrival epoch, exceptions used, and the steps that led there. With `r` legs still to schedule including the new leg, sample a nonnegative wait and flight extra whose sum fits within `deadline - arrival - r*m`. Use this structural bound conservatively near binary64 equality. For an initial `K = 16`, try four immediate or short-wait proposals, four proposals spread over the longer of the source and destination orbital periods, four spread over the full available slack, and up to four refinements near low-Δv samples. Clip each sampling range to the available slack and keep durations positive. Treat these settings as search limits, since transfer feasibility need not vary monotonically with duration.

Grow only prefixes whose transfers pass the selected policy and whose exception count fits the budget. Keep the earliest arrivals and reserve one slot when `B > 1` for the continuation with the fewest used exceptions. Break remaining ties by proposal ID. If the beam dies, restart from another tomato or with a broader timing range or beam. A single earliest-arrival choice can consume an exception or discard a useful alternative. If the beam repeatedly converges to similar prefixes, test keeping different endpoints or arrival windows. Use orbital group and plane similarity to order candidates if profiling justifies it. Periodically include other destinations so the heuristic cannot rule out the transfers needed to connect the two groups. Sparse samples and exhausted beams show how far the search got within its budget. They cannot establish global infeasibility.

```text
construct_until(call_limit):
    for start in seeded restart sequence while call_limit permits:
        beam = {prefix(start, t0, zero exceptions)}
        for each remaining depth while beam and call_limit permit:
            next_beam = empty bounded priority queue
            for prefix in beam and destination not visited:
                for wait, duration in bounded timing proposals:
                    skip if the minimum remaining schedule bound fails
                    cost = TransferSolver::evaluate(actual directed leg and times)
                    debit one leg call
                    if valid cost and exception budget permits extension:
                        offer extended prefix to next_beam
                        update deepest_prefix for diagnostics
            beam = retained extensions
        for complete routes in beam:
            decode and verify with SolutionEvaluator if L calls remain
            update feasible_best by earliest final arrival

search(problem, policy, seed, leg_budget):
    feasible_best = none
    deepest_prefix = none
    construct_until(floor(0.75 * leg_budget))
    if feasible_best is none:
        continue construction up to leg_budget
    while a candidate can be evaluated within leg_budget and feasible_best exists:
        propose a timing adjustment or an order move using the seeded RNG
        repair timings from the earliest affected leg
        validate the candidate and update the feasible incumbent on improvement
        on stagnation, try another seed or a bounded segment reschedule
    return feasible_best and search diagnostics
```

Give construction three quarters of the initial budget and make the remainder available to construction if there is no feasible incumbent. A repair should try alternate waits and flight durations on the first problematic leg and a limited number of later legs. Start with full exact route evaluation for local candidates. Reuse a prefix or exact leg cache only after measurement, and revalidate any reported incumbent through the existing evaluator. Cap restart attempts and local proposals so rejected decodes and empty expansions can also terminate a run. At budget exhaustion report either the best verified feasible route found or **no feasible route found within budget**, with the longest feasible prefix, late-stage rejection reasons and time-window coverage. If feasible solutions remain hard to find, compare wider timing proposals, larger beams, bounded backtracking and separately labeled warm starts before adding a larger population search.

### Phase 5B foundations

`PartialRoute::start` records the first tomato at `Problem::startTime` with zero transfers. Keep the `Problem` alive and unchanged while using its routes. Each prefix keeps its chosen Δv policy, a visited mask, the leg timings, arrival epoch, and exception count. `extend` tests the destination and timing before making at most one solver call. When it calls the solver, the result contains that transfer's raw status and cost for budget accounting. A successful extension returns a copy with the new leg. `finish` is available after every tomato has been visited and returns a `Solution` with its full `SolutionEvaluator` result.

For `r` remaining legs and minimum duration `m`, the nominal latest arrival for the proposed leg is `deadline - (r - 1)*m`. The sampler uses the available slack after reserving the new leg's minimum duration and checks its result against the same binary64 bound as `extend`. It draws two independent `[0,1)` values from the upper 53 bits of the seeded `std::mt19937_64` engine. If their sum exceeds one, replace each value by one minus itself. Scaling the pair by the available slack gives a uniform draw over the wait and extra-flight triangle in continuous arithmetic. The exact deadline and the minimum-duration schedule remain inclusive. If rounding makes a proposed pair too late, the sampler returns the immediate minimum-duration timing when it fits. At a configured minimum of zero, it uses the smallest positive normal double as the proposal floor. Sampling makes no orbital calls.

## Search guidance for infeasible candidates

Keep exploratory candidates separate from the feasible incumbent. Discard a structurally invalid decode that cannot be repaired. For structurally valid candidates with valid problem limits, use the per-leg statuses and raw costs to compute this lexicographic search key:

1. Number of legs with solver failure or `Infeasible` classification.
2. Exceptions beyond the allowed budget, `max(0, count - Problem::maxExceptions)`.
3. Sum of finite over-ceiling excesses for rejected legs, normalized by `max(1 m/s, Problem::maxExceptionDeltaV)`. Measure the excess against the nominal exception limit as a heuristic, and use the existing policy result to determine which legs were rejected. Count solver failures in item 1.
4. Scheduled completion time, then a deterministic candidate ID.

This key can prioritize repairs or rank infeasible GA offspring. For an SA control, scale feasible completion time into `[0,1]` using the mission window. Assign an infeasible candidate `1 + bad_legs + excess_exceptions + g/(1+g)`, where `g` is item 3. Use a positive temperature calibrated on observed score changes and cool by proposal count. These are internal search scores. The published or strict classification and the exact structural checks still determine eligibility for the incumbent.

## Complexity, measurement, and tests

Let `C[i]` denote the cost of transfer evaluation for leg `i`, including propagation and the two Lambert direction searches. A full route evaluation takes `O(N + Σ C[i])` time and `O(N)` route storage. One beam restart tests up to `O(B K N²)` directed timing candidates, because unvisited destinations decrease by one at each depth. On easy, the proposed `B = 4` and `K = 16` give an upper bound of 75,264 calls per restart before complete-route validation. Use a bounded heap with `O(log(B+1))` ranking per insertion and scan or copy `O(N)` prefix state only for retained children. Retain at most `O(BN)` parent records across all beam layers, with membership masks only for the active layers. With `M` sampled local proposals, full rescoring takes up to `O(M N (1+Cmax))` when `Cmax` bounds the cost per leg. Reordering and OX themselves take at most linear time. Actual runs stop at the evaluation budget or configured attempt limits.

- **Reproducibility.** Start with single-threaded Release C++17 and `std::mt19937_64`. Use seed 0 for smoke runs and published seeds 1–10 for comparisons. Log the input SHA-256, backend and compiler versions, policy, seed, proposal ranges, beam width, repair and restart limits, budgets and deterministic tie rules. Pin the toolchain for seeded replay. For cross-toolchain replay, record the exact ordered query list and avoid assuming that standard-library distributions consume the same random numbers.
- **Budgets.** Start with 10,000 exact leg calls for diagnostics, then compare fixed 100,000 and 1,000,000 call budgets. These are proposed work limits, with wall time to be measured. Count construction, repair, complete-route checks and failed backend calls. Reserve `L` calls before a planned full validation. Record proposed queries and cache hits separately if caching is introduced. Compare each method with the same seeds and instance, and separate end-to-end cold-start runs from equal-budget improvement runs on shared feasible seeds.
- **Measurements.** Report success fraction, calls and time to first feasible route, best mission time versus cumulative calls, and median and range across seeds. Report runs without a feasible route separately, with deepest prefix, statuses and rejection reasons. Measure exact leg calls per second, time in propagation and Lambert search, repair overhead and peak memory before choosing an optimization. Use a representative fixed query batch for CPU/CUDA throughput and report device transfers separately in future tests.
- **Correctness tests.** Exhaustively enumerate permutations and a fixed timing grid on a small synthetic case to check search results on that grid. Test chromosome round trips, operator preservation, fixed-prefix repair, time bounds, deterministic replay and budget accounting. Include a synthetic dead-end requiring a different beam choice, no feasible route under the sampled schedule set, and singleton completion. Replay Phase 3 Δv boundary fixtures and Phase 4 exception and chronology tests. For any reported easy route, serialize times with round-trip precision and recheck with `SolutionEvaluator` and the pinned Python reference. Keep any future CUDA comparison tied to the same FP64 queries and verify statuses, classifications and final route feasibility, especially near the exact ceilings.

## Bottlenecks and implementation sequence

Propagation and Lambert solving are expected to dominate. A timing edit early in a route can change many downstream evaluations. Measure repeated queries by an exact key of instance/backend identity, from/to indices and the binary64 departure and duration. Cache raw results only, then apply the configured policy. Rounded or grid-based costs can guide proposals. Use exact transfer computation for feasibility. Candidate extensions at one beam depth, independent starts, local proposals from the same incumbent and GA individuals can be batched. Reduce results in a fixed order and keep serial decisions between depths or accepted local moves. Parallel CPU evaluation also needs a thread-safety check of the pinned backend. Profile batch sizes and Lambert work variability before designing a CUDA backend.

1. Define the seed, budget and trace format. Test decoding, mutation and deterministic exact leg-call accounting on synthetic inputs.
2. Build the exact single-threaded constructor with greedy and beam variants. Measure easy-instance feasibility rates, first-feasible cost and where attempts fail.
3. Add first-improvement timing and route neighborhoods plus bounded suffix repair. Compare construction alone against construction plus local search.
4. Use measured data to tune wait and duration sampling, beam diversity and restart allocation. Test SA and seeded GA only under a matched comparison protocol.
5. Publish a stable CPU query batch and route fixtures for future parallel CPU or CUDA experiments.

Open decisions are empirical. Determine how broad the timing samples and beam must be to obtain a cold-start feasible easy route, how to divide calls between construction and repair, and whether SA or GA improves the baseline under the same budget. Verify the Small service evaluator before claiming complete leaderboard parity. Phase 5A introduces no optimizer or performance claims.

[spec]: https://github.com/esa/SpOC4/blob/e4276399b51852e6f1c08c2d0c133bd13f8f967a/Challenge%202%20Keplerian%20Tomato%20Traveling%20Salesperson%20Problem/README.md
[easy]: https://github.com/esa/SpOC4/blob/e4276399b51852e6f1c08c2d0c133bd13f8f967a/Challenge%202%20Keplerian%20Tomato%20Traveling%20Salesperson%20Problem/problems/easy.kttsp
[evaluator]: https://api.optimize.esa.int/media/problems/spoc-4-keplerian-tomato-traveling-salesperson-large-1782204009564.py
[sa]: https://doi.org/10.1126/science.220.4598.671
[ox]: https://ijcai.org/proceedings/1985/29
