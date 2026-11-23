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

### Phase 5C beam search

Each attempt starts up to `beamWidth` distinct tomatoes at the mission epoch. The seed shuffles the starting indices, and a restart takes the next slice of that order, wrapping when every start has been tried. At each depth the search shuffles the unvisited destinations for each prefix and takes up to the configured limit. It uses a separately seeded `TimingSampler`. Three of each four proposals draw from a short window whose wait plus extra flight is capped at the remaining time divided by remaining legs, less the minimum flight. Every fourth proposal samples the full available slack. Repeated runs with the same build, input and seed reproduce the decisions and counters. Runtime varies.

At each depth a lower score wins. With `H = deadline - t0`, the score is `(arrival - t0 + remainingLegs * minimumFlight) / H + exceptionsUsed / (N - 1)`. The first term estimates normalized completion with minimum remaining flights. The second penalizes spending an exception early. Ties prefer earlier arrival, fewer exceptions, then proposal order. Each retained prefix ends at a different tomato. If two candidates share an endpoint, keep the one with the better rank. This improves endpoint diversity at the cost of discarding alternative schedules for the same endpoint. The last level ranks by final arrival alone, with the same stable tie criteria. The heuristic can miss feasible or better routes.

The attempt count is capped by the requested restarts and the available budget for construction and revalidation. If the budget is smaller than two full passes, one attempt can still report partial progress. Each attempt receives a share of the remaining calls and reserves `N - 1` calls for final validation when its share covers two full passes. Unused calls carry to later attempts. Each call recorded by `PartialRoute::extend`, including failed transfers, consumes one unit. `finish()` runs only when its full validation fits within the attempt's allowance. Only a feasible result from `finish()` can become the incumbent. Complete-route statistics count accepted full prefixes before beam pruning, with a separate count for those revalidated. A run may stop below the overall budget when every attempt has an empty frontier or has used its allocation for expansion.

The implementation stores two bounded frontiers. Each holds up to `B` prefixes, each with up to `N` visits and a visited mask, for `O(BN)` frontier space. For `A` attempts, scanning and shuffling candidate lists costs up to `O(ABN²)`. With destination cap `D` and timing sample cap `K`, there are up to `O(ABNDK)` proposals, and actual orbital calls are bounded by the configured budget. Each accepted proposal copies the prefix in `O(N)` and updates the bounded frontier in `O(B)`. Sorting a level costs `O(B log B)`. The run also records up to `AB` starting indices.

### Phase 5D timing diagnostics

Use `--diagnostics` for CSV rows grouped by parent depth, the number of tomatoes visited before proposing a leg. The sum of per-depth orbital evaluations plus full-validation evaluations equals the total call count. Every extension is accepted or rejected. Accepted extensions are retained or removed by the endpoint or width rule. A replacement counts as one prune of the displaced candidate. Visited destinations and candidates clipped by the destination cap are filtered before proposing a transfer. A closed timing window also costs no orbital call. The rejection columns distinguish timing, deadline, Δv, exception budget, orbital input, no Lambert candidate, and numerical failure. Numerical failures can also arise during propagation. `Attempts limited by budget` counts each attempt where its allowance prevented additional work, even if the overall search had calls left because of a reserve.

Baseline timing is unchanged. With `--timing stratified`, split the local extra-flight interval into equal-width bands, one per local sample for a destination. Draw flight time uniformly inside its band and wait uniformly within the slack left by that draw. Each fourth sample still uses the baseline's full window. Both modes use the same beam ranking and strict feasibility rules in the experiment. The local baseline's triangular distribution favors shorter flight durations marginally. The stratified proposal distributes local samples evenly across the interval.

#### Measured on ESA easy

The Release Windows x64 build used the validated PyKEP v2.6 backend. The paired runs used seeds 0, 1 and 7, width 6, 16 destinations, 12 samples, zero restarts and the strict policy. Caps of 2,048 and 8,048 left 48 calls reserved for final validation and gave exactly 2,000 and 8,000 construction calls per run. At 2,000 calls, both modes reached 3 tomatoes for every seed. The baseline accepted 863 of 6,000 calls (14.38%) and stratified accepted 990 (16.50%).

| Seed | Baseline accepted / rejected at 8,000 calls | Stratified accepted / rejected at 8,000 calls | Baseline / stratified deepest prefix | Baseline / stratified seconds |
| --- | ---: | ---: | ---: | ---: |
| 0 | 1,193 / 6,807 | 1,300 / 6,700 | 8 / 8 | 0.0378 / 0.0401 |
| 1 | 899 / 7,101 | 1,098 / 6,902 | 8 / 8 | 0.0378 / 0.0411 |
| 7 | 1,192 / 6,808 | 1,334 / 6,666 | 8 / 8 | 0.0414 / 0.0423 |

At 8,000 calls the aggregate acceptance rate increased from 13.68% to 15.55%. All matched-run rejections were above the Δv ceiling. There were zero complete routes in both modes, so neither had a feasible completion time. The Phase 5C default baseline reproduced its starts and counters. Replaying the stratified 8,000-call seed 7 run reproduced every diagnostic row apart from runtime.

At the common 50,000-call cap, each of these single-attempt runs emptied its frontier and used fewer actual calls than the cap. The depths are consequently a secondary comparison with unequal work.

| Seed | Mode | Calls | Accepted / rejected | Above ceiling / exception budget | Deepest prefix | Seconds |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| 0 | Baseline | 13,056 | 1,507 / 11,549 | 11,215 / 334 | 13 | 0.0608 |
| 0 | Stratified | 13,248 | 1,620 / 11,628 | 11,412 / 216 | 12 | 0.0656 |
| 1 | Baseline | 9,792 | 912 / 8,880 | 8,707 / 173 | 10 | 0.0457 |
| 1 | Stratified | 13,056 | 1,397 / 11,659 | 11,205 / 454 | 14 | 0.0628 |
| 7 | Baseline | 12,864 | 1,410 / 11,454 | 11,201 / 253 | 12 | 0.0586 |
| 7 | Stratified | 11,328 | 1,514 / 9,814 | 9,516 / 298 | 11 | 0.0604 |

There were zero invalid timings, deadline violations, solver failures, and duplicate extension attempts in these runs. Across the six terminal levels, 1,571 of 1,728 calls exceeded the ceiling and the other 157 exhausted the exception budget. Both strategies found zero complete routes and no feasible completion time. Stratification improved the number of usable extensions per call in the equal-work runs. The measured depth gains were inconsistent.

#### Timing and beam observations

- At 8,000 calls per seed, the baseline used 2,002 calls on flights shorter than 0.25 days and accepted only 13 (0.65%). Flights of at least one day produced 2,804 acceptances in 16,596 calls (16.90%). Stratification reduced sub-day flight calls from 7,404 to 4,234 across the same 24,000-call total. These are conditional rates for this search and mix timing, orbital pairs and surviving prefixes.
- A controlled check held orbit pairs fixed. The existing x64 transfer probe sampled 16 departures from day 0 to 1.875 in steps of 0.125 days and flight durations of 0.125, 0.25, 0.5, 1, 2, 3, 4 and 5 days for each of two easy-instance pairs. For tomato 1 → 48 at a fixed 5-day flight, changing departure changed Δv from 82.06 to 557.85 m/s, with 2 of 16 normal. At a fixed departure of 0.125 days, varying flight time changed Δv from 99.26 to 5,110.12 m/s. The 256-query grid shows sensitivity to both variables for those pairs.
- For seed 0 in the longer baseline run, 654 accepted prefixes were removed by endpoint collisions and 791 by width, leaving 62 retained across depths. At the first expansion the latest accepted arrival was day 198.907, while the latest retained arrival was day 1.8132. The ranking strongly favors early arrivals in this example. The diagnostics establish that later feasible prefixes were discarded. They do not establish that any discarded prefix could have finished the mission. Wider timing coverage alone has yet to demonstrate more reliable complete-route construction.

Reproduce a matched run from the project root. Change only `--timing` for the paired comparison.

```powershell
./build/transfer-x64/keplerian_tsp.exe --beam-search --beam-width 6 --destinations 16 --samples 12 --seed 0 --restarts 0 --max-evaluations 8048 --policy strict --timing stratified --diagnostics
```

### Diversity-aware beam selection

The default selector still uses the Phase 5C rank. Enable the experimental selector with `--selection temporal`. It ranks all accepted candidates at an incomplete level, keeps the best one, then offers one slot per other occupied arrival bin in chronological order. It uses the best-ranked candidate in each bin whose endpoint is unused. Any open slots are filled by the original rank. Endpoints stay unique and the retained frontier never exceeds beam width. Width one and the complete level use the original selector.

The four bins start at the earliest accepted arrival at that level. Each is at most `--arrival-bin-days` wide (default 1 day). Let `latest` be the mission deadline less all remaining minimum flight durations. The bin width is also capped at `(latest - earliest) / 16`, so preferred bins cover at most one quarter of the spare time after the earliest arrival. A later candidate outside those bins can still win an ordinary rank slot. The selector makes no orbital calls. It sorts and temporarily holds up to `P` accepted prefixes for a level, taking `O(P log P)` selection time and `O(PN)` pool storage. The two retained frontiers still take `O(BN)` space.

#### Matched experiments on ESA easy

The Windows x64 runs used strict Δv, width 6, 16 destinations, 12 samples, no restarts, and seeds 0, 1 and 7. Each timing mode was tested with both selectors. With a configured 8,048-call limit, all twelve runs made exactly 8,000 orbital calls, leaving 48 reserved for validation. Each row below totals three seeds. The arrival bins are elapsed mission days and count retained children across all expanded depths. Pruned candidates are accepted children excluded from the final frontier.

| Timing | Selector | Calls | Accepted | Rejected | Pruned | Retained arrivals by bin: <2 / 2–4 / 4–8 / 8–16 / ≥16 d | Runtime per run (s) |
| --- | --- | ---: | ---: | ---: | ---: | --- | ---: |
| Uniform | Baseline | 24,000 | 3,284 | 20,716 | 3,158 | 16 / 14 / 41 / 54 / 1 | 0.036–0.039 |
| Uniform | Temporal | 24,000 | 3,193 | 20,807 | 3,067 | 11 / 17 / 43 / 55 / 0 | 0.035–0.036 |
| Stratified | Baseline | 24,000 | 3,732 | 20,268 | 3,606 | 14 / 14 / 27 / 64 / 7 | 0.038–0.039 |
| Stratified | Temporal | 24,000 | 3,691 | 20,309 | 3,565 | 12 / 17 / 36 / 58 / 3 | 0.037–0.040 |

All fixed-budget runs reached 8 of 49 tomatoes. The time distribution did change. With uniform timing and seed 0, for example, the first baseline frontier retained six arrivals before day 2. The temporal frontier retained four before day 2 and two between days 2 and 4. Across depths there was no systematic shift toward very late arrivals.

All runs at the larger 50,000-call cap exhausted their frontiers before reaching the cap. To compare equal work at a later checkpoint, both strategies were rerun for each seed and timing mode with the smaller of their two actual call counts, plus the same 48-call reserve. This gave the following matched depths.

| Timing | Seed | Calls per selector | Baseline depth | Temporal depth |
| --- | ---: | ---: | ---: | ---: |
| Uniform | 0 | 10,752 | 11 | 10 |
| Uniform | 1 | 9,792 | 10 | 10 |
| Uniform | 7 | 10,560 | 11 | 10 |
| Stratified | 0 | 12,672 | 12 | 12 |
| Stratified | 1 | 12,096 | 12 | 11 |
| Stratified | 7 | 11,328 | 11 | 11 |

The temporal selector tied three later checkpoints and lost one visit at the other three. The highest unmatched depth was 14 for baseline selection and 13 for temporal selection, with different call counts. None of the 36 runs found a complete route or a feasible mission completion time. This binning rule broadened some early beams and did not improve depth under matched work.

To reproduce a fixed-budget pair, run the following from the project root and change only `--selection`.

```powershell
./build/transfer-x64/keplerian_tsp.exe --beam-search --beam-width 6 --destinations 16 --samples 12 --seed 0 --restarts 0 --max-evaluations 8048 --policy strict --timing baseline --selection temporal --arrival-bin-days 1 --diagnostics
```

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
