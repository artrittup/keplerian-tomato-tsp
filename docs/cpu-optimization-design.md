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

### Constraint-guided one-step lookahead

The earlier searches accepted many individual transfers and then ran out of sampled continuations. An accepted leg fixes the next endpoint and its absolute arrival epoch. It also removes a destination from the remaining set and may spend an exception. The previous score measured arrival and exceptions but contained no evidence about the next feasible transfer. This matters when acceptable transfers lie in narrow windows. A sampled terminal frontier with no admissible legs gives no proof about untested destinations, timings, or routes.

Enable the new selector with `--selection lookahead`. Baseline and temporal selection and both timing modes remain available. At an incomplete level the search keeps a shortlist of up to twice the beam width, with one prefix per endpoint under the original rank. If no selection is necessary because the shortlist fits in the beam, it makes no probes. Otherwise it visits candidates in rank order. For each candidate it checks up to `--lookahead-probes` proposals (default 8) across shuffled unvisited destinations, using the selected timing sampler with a separate seeded stream. It uses `PartialRoute::extend` for the actual solver call, time constraints, and Δv classification.

Stop probing a candidate on its first accepted continuation. Prefer witnessed candidates and fill any remaining slots in the original rank order. A failed probe is inconclusive. Stop probing the level once the beam can be filled with witnessed candidates, the shared budget stops the search, or the level has spent one probe call per ten primary calls. The next level reuses each retained witness as a candidate at its exact timing and skips a duplicate of that exact leg if sampled again on the same prefix. All witnesses are checked legs. A complete route must still pass `SolutionEvaluator`. Probe success gives evidence for one next leg only.

Let `B` be beam width, `N` tomato count, `K` the per-prefix probe cap, and `A` the number of primary orbital calls made to generate a level. The shortlist and witnesses use `O(BN)` space. There are at most `min(2BK, floor(A/10), remaining construction budget)` extra orbital calls at the level. Candidate destination enumeration is `O(BN)`, rank sorting is `O(B log B)`, and successful probes can each copy a route in `O(N)`. The existing allowance for full-route validation remains reserved. A reused witness counts as an extension and adds zero new calls. The CLI reports accepted and rejected orbital calls from construction and probes, with validation calls counted separately.

#### ESA easy experiment

The Windows x64 Release runs used the strongest previous baseline for this instance, which was stratified timing with baseline selection. Both selectors used width 12, 24 destinations, 16 timing samples, strict Δv, and seeds 0, 1 and 7. The fixed cap was 48,048 calls, including a 48-call reserve, so each run spent exactly 48,000 actual calls and reached 12 tomatoes. The baseline accepted 21,165 transfers and rejected 122,835 across the three runs. Lookahead accepted 21,181 and rejected 122,819, including 3,139 probe calls. The fixed runs stopped at the evaluation allowance.

The longer single-attempt runs used a 200,000-call cap. Even a full-width pass through all depths with these settings would make at most 168,192 primary calls. The 10% probe allowance and up to 576 validation calls still fit this cap. Each search actually stopped with an empty frontier well below the cap. The later matched checkpoints reran each pair at the earlier stopping call count plus 48 reserved calls.

| Seed | Matched calls per selector | Baseline accepted / rejected | Lookahead accepted / rejected | Probe calls | Baseline / lookahead depth | Baseline / lookahead seconds |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 0 | 69,648 | 8,164 / 61,484 | 8,875 / 60,773 | 1,296 | 16 / 17 | 0.353 / 0.343 |
| 1 | 61,569 | 6,694 / 54,875 | 5,807 / 55,762 | 1,281 | 14 / 16 | 0.296 / 0.283 |
| 7 | 66,420 | 8,205 / 58,215 | 7,834 / 58,586 | 1,140 | 16 / 16 | 0.331 / 0.323 |

The matched baselines reached their allowances. Lookahead exhausted its frontiers at those counts. In the unrestricted pairs, baseline depths were 18, 15, and 17, using 73,344, 62,592, and 70,656 calls. Lookahead depths were 17, 16, and 16, using 69,648, 61,569, and 66,420 calls. A three-attempt seed-1 run with a 600,000-call cap reached 18 in both modes. Baseline used 195,840 calls (0.954 s) and lookahead used 203,612 (0.992 s), including 4,316 probes. Both exhausted their frontiers. No run completed a route, so there was no feasible completion time.

Across the longer single-attempt lookahead runs, 332 of 3,717 probes succeeded (8.93%) and all 332 witnesses were reused. The probes promoted 91 candidates past the original beam cutoff. Among failed probes, 3,204 exceeded the Δv ceiling and 181 exceeded the exception budget. The six terminal levels had 2,304 primary calls, of which 2,194 exceeded the ceiling and 110 exceeded the remaining exception budget. Lookahead improved two late matched checkpoints by one and two visits and tied the third. It gave no gain at the fixed checkpoint or in the multi-start run. Many probes found no witness, and evidence for a single option could not preserve connectivity to the rest of the unvisited set.

The next construction experiment should reuse transfer work across routes. Build a sparse, time-expanded graph of exactly evaluated feasible arcs, indexed by source, destination, and departure window. Keep the original departure and arrival epochs on each arc. Allow a connection only if the next exact departure follows the previous arrival, the destination is still unvisited, and the exception budget fits. Use bounded backtracking with reachability checks on that sampled graph to choose and revise order and timing together. Verify complete routes with the existing evaluator. Disconnected sampled windows identify where to spend a separate refinement budget. They cannot certify global infeasibility.

To reproduce the fixed pair, run from the repository root and change only `--selection`.

```powershell
./build/transfer-x64/keplerian_tsp.exe --beam-search --beam-width 12 --destinations 24 --samples 16 --seed 0 --max-evaluations 48048 --restarts 0 --policy strict --timing stratified --selection lookahead --lookahead-probes 8 --diagnostics
```

### Phase 5G graph and cache foundations

`TransferCache` owns an immutable copy of one `Problem`. Its orbits and reference epoch, together with the fixed `TransferSolver` implementation, define the context for every lookup. Each key contains the directed source and destination indices and the exact IEEE binary64 representations of departure and flight duration. The implementation copies the bits with `memcpy`. Signed zeros have separate keys. NaN and infinities return invalid input before lookup, with a separate counter and no solver call. Finite queries cache the returned raw status and Δv, including determinate failures. An exception thrown by the solver is never inserted. The cache is in memory and intended for single-threaded use with a fixed numerical environment.

Capacity is an explicit number of entries. On insertion into a full cache, evict the least recently used entry. A hit refreshes its recency. Capacity zero evaluates each funded miss and stores nothing. The cumulative call limit is supplied with each request. A hit can return at the limit, and a denied miss returns no result and creates no entry. This counter covers calls made through the cache. A future search budget must also account for separately invoked validation. The finite counters reconcile as `misses = solverCalls + budgetDenials`. Report live entry payload bytes and allocated orbit vector bytes separately. The payload estimate includes the values stored in the list and index and excludes container links, allocator overhead, and object headers.

`TransferGraph` shares that cache and applies its own explicit Δv policy to raw results. It stores only orbital legs accepted individually under that policy. Each leg records the exact departure, flight duration, arrival, Δv and classification. The source node represents the tomato ready at the departure epoch. The destination node represents the arrival epoch. A sorted departure index answers which stored legs leave a given arrival node's tomato at or after its epoch, so waiting needs neither extra arcs nor duplicate transfers. Edge IDs stay valid when cache entries are evicted. A separate edge cap bounds the graph, with at most `N + 2E` nodes for `N` possible mission starts and `E` stored edges.

Two prefixes can reach the same tomato at the same epoch with different visited sets. A destination already used by one prefix may still be available to the other. The prefixes can also have different remaining exception budgets. Neither property belongs in a node key or a raw transfer cache key. A future graph search must carry both in its state, as the existing PartialRoute does. The supplied-path conversion checks visits, chronology and the cumulative exception count through the existing solution structure and stored leg classes. `SolutionEvaluator` remains available for independent final validation. The foundations run separately from beam search.

With capacity `C`, a cache lookup or insertion costs `O(log(C+1))` index work plus constant-time recency updates. Only a funded miss adds the orbital computation. Cache storage is `O(N+C)` for the owned orbits and entries. Graph insertion adds `O(log(V+1) + log(E+1))` index work and amortized constant-time storage append. Looking up the `k` eligible departures costs `O(log(E+1) + k)`. Reconstructing a supplied full route costs `O(N)` time and space, excluding optional independent orbital validation.

### Phase 5H bounded graph search

`--graph-search` runs a separate, opt-in depth-first search. The existing parser and beam modes keep their behavior. The graph shares one exact transfer cache over the immutable problem snapshot. A stack frame stores the current node, arrival epoch, remaining mission time, exceptions used, and an ordered list of alternative edges. A shared visited bit mask and an edge-ID path describe the live prefix. Moving down sets one bit and appends an edge. Moving back undoes that leg and advances the parent's next-edge cursor. Starting tomatoes are shuffled with the seed unless a single root is requested. Each root starts at the mission epoch without an orbital call.

At a prefix, use the graph's departure index to find legs from its current tomato that depart at or after arrival. Reject visited targets and legs that exceed the remaining exception count. Reject arrivals that would leave less than the sum of minimum durations for the unfinished legs. When there are fewer eligible stored edges than the alternative cap, sample up to `--destinations` shuffled unvisited targets with up to `--samples` timings each. Use the existing timing sampler. Three out of every four proposals stratify extra flight time over the current per-leg slack share, which is remaining days divided by remaining legs minus the minimum flight time. Every fourth proposal samples the full available wait and flight window. The graph admits a new edge only after the exact cached solver result passes the selected policy and the leg's timing checks. Existing edge IDs can be followed without a cache lookup or an orbital call. An individually accepted edge may still be unusable in the current history.

Rank eligible edges by normal class first, then earliest arrival, then edge ID. Within `--alternatives`, first offer up to half the slots rounded up to distinct destinations, then fill from the sorted list. This leaves room for several timings to the same tomato. When a state has no alternatives left, undo the last leg and resume the parent. The alternatives are fixed when a frame opens. Later frames can see graph insertions made by other routes. A large set of stored alternatives can suppress fresh sampling at a state, and depth-first traversal can spend many states close to one dead end.

```text
seed the random streams and create the capped cache and graph
for each permitted root in seeded order
    push the root and mark it visited
    while the stack has a state and the limits permit
        if this state is a complete path
            reconstruct Solution and run independent validation
            retain the fastest independently feasible route found
        else on first opening the state
            gather eligible stored edges and sample if needed
            rank and save bounded alternatives
        if another alternative exists, take it and push a child
        otherwise pop the state and undo its incoming leg, if any
```

`--max-evaluations` counts actual cache misses that call the solver and every independent validation call. With at least twice the number of mission legs in the total allowance, construction leaves one full validation allowance in reserve. At smaller caps, construction can use the full allowance and a complete path is reported only if enough calls remain to validate it. Once construction runs out of orbital calls, search can still follow stored edges. Every reported complete route is reconstructed with exact recorded departures and durations, then checked directly by `SolutionEvaluator` under the same policy. The objective among verified routes is earliest final arrival. The state cap counts every opened prefix, including starts and complete paths. The backtrack cap counts undone legs over the run. Node and edge caps prevent construction from paying for a transfer it cannot store, and the cache has a separate LRU capacity. All counts are cumulative. Seeded replay is checked with the same executable and runtime environment.

For `N` tomatoes, a visited-mask check or stack transition takes constant time, with only `O(N)` live frames. Let `R` be the number of stored edges returned by the departure index at a state, `P` the maximum number of timing proposals at that state, and `V`, `E`, and `C` the live graph-node, edge, and cache-entry counts. A state needs up to `O(N + log(E+1) + (R+P) log(R+P+1) + P log(V+E+C+1))` non-orbital work, including enumeration, duplicate checks, and ranking. Each funded miss has the cost of one orbital solve, and independent validation costs up to `N-1` more solves per complete path. Multiply the state work by the explored state count for a run bound. Space is `O(N A + V + E + C)` for `A` saved alternatives per frame, with `O(N + R + P)` temporary candidate storage at a single state. The search has no optimality or completeness guarantee.

#### Matched ESA easy experiment

Windows x64 Release, UCRT64 GCC 16.1.0, PyKEP C++ v2.6, strict Δv, 24 destinations, and 16 timing proposals. The beam comparator used Phase 5F's baseline ranking, stratified timing, width 12, and no restarts. Graph search retained up to 12 alternatives per state and allowed 10,000 states and 10,000 undone legs. Each pair used the same seed and configured orbital cap. The cap included 48 reserved calls for independent validation, resulting in exactly the same number of actual calls per pair. The algorithms use separate proposal streams. No run reached a complete path, so independent validation calls were zero and no run had a feasible completion time.

| Actual calls each | Seed | Depth beam / graph | Beam accepted / rejected calls | Graph nodes / edges | Stored-edge traversals | Graph backtracks | Seconds beam / graph |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 48,000 | 0 | 12 / 11 | 6,989 / 41,011 | 3,073 / 1,536 | 9,881 | 9,993 | 0.229 / 0.126 |
| 48,000 | 1 | 12 / 16 | 6,592 / 41,408 | 5,031 / 2,515 | 9,931 | 9,985 | 0.237 / 0.136 |
| 48,000 | 7 | 12 / 16 | 7,584 / 40,416 | 3,573 / 1,786 | 9,861 | 9,987 | 0.226 / 0.119 |
| 60,000 | 0 | 14 / 11 | 8,065 / 51,935 | 3,415 / 1,707 | 9,875 | 9,993 | 0.285 / 0.147 |
| 60,000 | 1 | 14 / 16 | 6,694 / 53,306 | 5,723 / 2,861 | 9,931 | 9,985 | 0.278 / 0.167 |
| 60,000 | 7 | 14 / 16 | 8,143 / 51,857 | 3,829 / 1,914 | 9,861 | 9,989 | 0.280 / 0.140 |

Every beam run stopped at its construction allowance. Every graph run used its full construction allowance, then continued on stored edges until the 10,000-state limit. Each graph run recorded one cache miss per actual call, zero exact-query cache hits, and zero storage denials or cache evictions. Random timings rarely repeat bit-for-bit. Stored-edge traversals are the source of reuse here, while the deterministic reuse test shows that a repeated failed query in another history does hit the cache. Graph edges are unique, individually admissible transfers. Beam acceptance counts whole-prefix extension checks and measures a different event. Phase 5F beam diagnostics for seeds 0, 1, and 7 replayed identically apart from runtime. The Phase 5H graph seed 7 run at 60,000 calls did the same.

Graph search reached deeper on seeds 1 and 7 at both budgets and fell behind on seed 0. It stayed at the same depths after 12,000 additional calls per seed. At 60,000 actual calls the graph runs rejected 314,408, 3,956,826, and 534,036 stored-edge encounters on exception budget, respectively. The same stored edge can fail this history check repeatedly without an additional solver call. The results support graph reuse under these budgets and leave full-route construction unresolved. They do not exceed the 18-tomato prefix reached in a longer Phase 5F beam run. A follow-up could measure whether reserving some sampling work for states with low usable graph connectivity reduces repeated traversal of the same dead ends.

From the repository root with the backend-enabled build on the UCRT64 runtime PATH, run either command for each seed. Use `60048` instead of `48048` for the second checkpoint.

```powershell
$phase5hExe = './build/transfer-x64/keplerian_tsp.exe'
foreach ($seed in 0, 1, 7) {
    & $phase5hExe --beam-search --input data/easy.kttsp --seed $seed --max-evaluations 48048 --policy strict --beam-width 12 --destinations 24 --samples 16 --timing stratified --selection baseline --restarts 0
    & $phase5hExe --graph-search --input data/easy.kttsp --seed $seed --max-evaluations 48048 --policy strict --destinations 24 --samples 16 --alternatives 12 --max-states 10000 --max-backtracks 10000 --max-edges 60048 --max-nodes 120145 --cache-capacity 60048
}
```

### Phase 5I benchmark and bottlenecks

The standalone benchmark under `benchmarks/phase5i` uses the existing search and orbital implementations. It builds measurement-only copies of the solver and graph source in the build directory to count physical outcomes and graph filters. Production files and the CLI keep their previous code. Every search uses the same 49-tomato ESA easy data and strict policy, with seeds 0, 1, and 7. Each configuration runs three times with identical seeds. The harness checks that the orbital call count matches the search's own counter and that repeated runs reproduce the result and main counters. Times are medians of the three repeats. The Release toolchain was Windows x64 UCRT64 GCC 16.1.0, CMake 4.4.4, Ninja 1.13.2, PyKEP C++ v2.6, and Boost 1.74.0. The machine used an Intel Core i7-13650HX.

#### Matched actual orbital budgets

The beam baseline uses width 12, stratified timing, baseline ranking, and no restarts. Graph search saves up to 12 alternatives per state with caps of 10,000 opened states and 10,000 undone legs. Both use at most 24 destinations and 16 timing proposals per destination. Add 48 calls to each construction allowance for independent validation. Each pair below spent exactly the listed actual calls. Raw acceptance is the fraction of solver calls that returned a finite normal or exception leg under the strict policy before checking the route's remaining exceptions. The normal rate is reported separately because a complete easy route needs at least 43 normal legs out of 48. Rates pool the three seeds.

| Actual calls per run | Depth, beam (0 / 1 / 7) | Depth, graph (0 / 1 / 7) | Raw acceptance, beam / graph | Normal rate, beam / graph |
| ---: | ---: | ---: | ---: | ---: |
| 8,000 | 3 / 3 / 3 | 10 / 15 / 13 | 17.07% / 8.31% | 0.979% / 0.417% |
| 24,000 | 7 / 7 / 7 | 11 / 16 / 15 | 15.47% / 5.59% | 0.672% / 0.194% |
| 48,000 | 12 / 12 / 12 | 11 / 16 / 16 | 15.05% / 4.05% | 0.539% / 0.140% |
| 60,000 | 14 / 14 / 14 | 11 / 16 / 16 | 14.51% / 3.60% | 0.508% / 0.113% |

The low-budget beam expands nearly whole levels before moving deeper. Graph search can extend a single prefix sooner. At the two largest budgets, graph search leads for two seeds and trails for seed 0. Extra graph calls and extra backtracking did little for depth after 24,000. Across 71 configurations, the deepest beam prefix reached 18 and the deepest graph prefix reached 16. No search found a complete route or a feasible mission time.

#### Beam allocation and diversity

The rows below use 24,000 actual calls on each seed unless noted. The percentage counts solver calls that produced an extension admissible in the current route history, including successful lookahead calls when enabled.

| Change from beam baseline | Depth (0 / 1 / 7) | Route-admissible calls |
| --- | ---: | ---: |
| Baseline width 12, 24 destinations, 16 timings | 7 / 7 / 7 | 15.47% |
| Width 6 | 12 / 12 / 12 | 14.30% |
| Width 24 | 4 / 4 / 4 | 14.05% |
| 12 destinations | 12 / 12 / 12 | 12.54% |
| 36 destinations | 5 / 5 / 5 | 15.26% |
| 8 timings per destination | 12 / 12 / 12 | 14.16% |
| 24 timings per destination | 5 / 5 / 5 | 16.65% |
| Uniform timing | 7 / 7 / 7 | 13.44% |
| Temporal selection | 7 / 7 / 7 | 15.80% |
| One-step lookahead | 7 / 7 / 7 | 16.26% |

With 12 destinations, seed 1 emptied its frontier at 23,232 calls. At that matched checkpoint, width 12 with 12 destinations reached depth 12 and the baseline reached depth 7. The differences show how much wider levels cost at a fixed allowance. They do not establish that the narrow beam would reach the best complete route. Stratification raised acceptance relative to uniform sampling. The baseline and temporal selector retained `[26,42,75,73,0]` and `[26,43,77,70,0]` arrivals in the five diagnostic bins, pooled across levels and seeds. At this checkpoint the additional selector changed those totals slightly and depth stayed the same.

#### Limits, filters, and costs

- **Orbital allowance.** The matched beam runs stopped at the construction allowance. Graph search reached the same orbital allowance and continued traversing stored arcs until the state cap. With a 60,000-call allowance and only 50 states, each graph seed used 19,200 calls and reached depths 10/16/15. With 200 states they all used 60,000 calls and reached 10/16/16. With 2,000, 10,000, or 30,000 states the depths were 11/16/16 at those same 60,000 calls.
- **No sampled continuation.** With a 200,000-call construction allowance, the three beam runs exhausted their frontiers at 73,344, 62,592, and 70,656 calls. The last frontier had one remaining prefix per seed and tried 384 transfers. The ceiling rejected 370/370/341 and exhausted exceptions rejected the remaining 14/14/43. Each terminal prefix had already used five exceptions.
- **Deadline.** The deadline and minimum-duration reserve pruned 1,585 graph-edge encounters across the sweep. No graph expansion reported an unavailable timing window or an empty state with an observed deadline block in the 27 graph runs. The three exhausted beam frontiers had zero deadline rejections and zero missing timing windows. Deadline pressure was observable as a filter and did not explain the measured stalls.
- **Exception allowance.** At 60,000 actual calls and 10,000 states, graph seeds 0/1/7 reached their fifth exception in 8,920/9,990/9,802 opened states. The respective numbers of empty states were 8,854/8,526/7,652. Of these, 4,732/8,353/5,293 had an exception rejection and 8,734/8,419/7,542 were blocked from fresh sampling by the orbital allowance. These categories overlap. The filter counters describe attempted continuations and can count the same physical edge against many routes. They cannot establish that a missing candidate would have succeeded if the limit were higher.

At 60,000 calls the graph traversed 9,875/9,931/9,861 previously stored edges. The raw cache had one miss per call, zero hits, and no evictions. The edge index gives a previously accepted arc directly to the next path. An exact-cache hit instead requires a second evaluation request for the same pair and the same binary64 departure and duration. Random resampling rarely produces both identical timestamps. Disabling memoization gave the same calls, depths, and reused-edge counts under these settings. The exact cache still has a role when a generator deliberately revisits a fixed set of queries.

At the matched 60,000-call checkpoint, the median across the three per-seed median runtimes was 582 ms for beam and 318 ms for graph. Median time inside `TransferSolver` was 567 ms and 233 ms, respectively. The remainder was about 3% of beam runtime and 26–44% for individual graph seeds. On graph seed 1, increasing the state cap from 200 to 30,000 at the same calls and depth raised the median remainder from 82 ms to 284 ms. With graph caching disabled, the median remainder across seeds was 40 ms at 10,000 states, compared with 84 ms with caching. These wall-clock measurements include timing and classification instrumentation. The remainder also includes sampling, route copying or rollback, indexing, pruning, candidate ranking, and allocation. It is the per-run total minus time inside the solver, aggregated separately. Wall time varies across seeds and runs. Equal call counts control query quantity while different timing inputs can change the work per Lambert solve. An uninstrumented run of each of the 71 CLI configurations reproduced the call count, depth, feasibility and stop reason. At 60,000 calls those native runs took 310–339 ms for beam and 151–205 ms for graph.

#### Timing-window probe

The search accuracy tests cover numerical parity. This probe measures whether the proposal distribution actually reaches useful time windows. It evaluates 12 directed pairs at controlled openings of days 0, 25, and 75. Two pairs are known Phase 3 reference pairs and ten come from a fixed shuffle of the other directed pairs with seed 5. With 48 legs for the local-slack calculation, each case uses an 8,385-point wait/extra-flight triangle with 128 intervals per axis, about 0.020–0.033 days apart. The separate sampler replay uses 64 seeds per case and 16 calls per trial, with 12 local and four broad-window proposals. It compares uniform and stratified local sampling under the same timing geometry. The grid uses 301,860 calls and the sampler replays use 73,728 calls. These are separate diagnostics and contribute zero search-budget calls.

The local grid found admissible transfers in 25 of 36 cases and normal transfers in three. Over 2,304 trials per sampler, uniform found any admissible transfer in 1,192 trials and a normal transfer in 270. Stratified found an admissible transfer in 1,208 and a normal transfer in 283. Pair 18→13 had one admissible grid point at each opening and neither sampler found it in any 16-call trial. For 1→48, the local grid found no normal points, while the sampler with broad-window draws found normal transfers at all three openings across multiple trials. The finite grid can miss a narrow interval or an interval outside its local triangle. These probes show dependence on the pair and epoch. Neither sampler provides reliable coverage of rare useful timing windows.

#### Next phase

Test an exact, pair-directed time-window discovery pass before changing global search. Give each selected pair an explicit coarse discovery budget, retain the exact departure, flight, and cost of normal and exception arcs, and refine promising regions at multiple resolutions. Reserve part of the budget for pairs and epochs with few usable normal continuations. Feed those arcs to the current beam or graph traversal with visit-once, chronology, and exception constraints and keep final independent validation. At matched total call budgets measure coverage of normal outgoing options by tomato and epoch, the exception count versus depth, deepest prefix, and success at constructing independently feasible routes. Report discovery, route search, and final validation calls separately within the same budget. Save representative narrow-window fixtures from the probe.

This hybrid targets the observed rarity of normal legs and the repeated exploration of saturated histories. Constraint-guided construction can use discovered normal outgoing options to choose the next destination without repeatedly paying for isolated probes. More broad lookahead or arrival-bin tuning alone had little effect in this sweep. Simulated Annealing and Genetic Algorithms remain useful candidate improvements after construction produces feasible seeds that supply meaningful neighborhoods or parents.

#### Reproduce Phase 5I

Run from the repository root with the same PyKEP, Boost, and UCRT64 environment variables as the Windows x64 instructions in [README](../README.md#windows-x64-build-with-transfersolver). Use a separate build folder.

```powershell
cmake -S benchmarks/phase5i -B build/phase5i/benchmark @ucrtToolchain @boostOptions `
    "-DCMAKE_PREFIX_PATH:PATH=$pykepPrefix"
cmake --build build/phase5i/benchmark --target phase5i_benchmark
(Get-FileHash data/easy.kttsp -Algorithm SHA256).Hash
./build/phase5i/benchmark/phase5i_benchmark.exe data/easy.kttsp build/phase5i/results
```

The ESA easy SHA-256 is `175256AC98BA2F557BCF889C0462A1DDF2316CB7452A6673E2313444DAB55D53`. The run writes `runs.csv`, `beam-depth.csv`, `beam-timing.csv`, and `windows.csv`. `runs.csv` records actual calls and the reason the search loop stopped. The beam depth table covers primary expansions. The graph `empty_with_*` columns indicate at least one corresponding filter or denied sample during that empty expansion. Phase 5I leaves numerical tolerances and feasibility rules unchanged.

### Phase 5J transfer window discovery

`TransferWindowExplorer::explore` takes a directed tomato pair, a caller-owned `TransferCache`, optional timing bounds, and search limits. Indices and orbital epochs use the existing ESA conventions. Bounds can narrow the absolute departure epoch, positive flight duration, and latest arrival. The explorer intersects them with the mission start, minimum flight time, and deadline. An empty intersection needs no calls. Invalid indices, non-finite bounds, and malformed configurations return an explicit status.

The search maps a unit square into the feasible timing domain. The horizontal coordinate interpolates between the earliest and latest possible departure. At each departure, the vertical coordinate interpolates from the allowed minimum flight time to the smaller of the flight cap and remaining time before the deadline. Each actual query is checked against the deadline after binary64 arithmetic. The search evaluates the centre and boundary anchors first, then evaluates one centre per coarse cell, with an 8-by-8 grid by default. It visits coarse cells along successive diagonals to spread the first probes over both axes.

Each subdivision evaluates the four child centres. Three out of every four subdivisions take the available cell with the lowest evaluated centre Delta-V. The fourth takes the shallowest available cell. Absolute Delta-V ranks normal transfers first, followed by exceptions and then finite over-ceiling values. Over-ceiling centres can therefore guide refinement toward a usable region. Failures have infinite ranking cost and stay eligible for the shallow-cell pass. Ties follow the deterministic insertion order. The second priority queue preserves some broad coverage when many nearby low-cost samples otherwise dominate.

Each opportunity reports the exact evaluated departure and duration, arrival, raw Delta-V, and classification under the chosen existing policy. Unique accepted timings are sorted by increasing Delta-V. The lowest successful solve is also returned when every sampled leg exceeds the ceiling. Along a route, the exception budget and the compatibility of consecutive legs still need to be checked by route construction and independent validation. The explorer makes no full-route decision.

A request's orbital allowance is the number of additional solver calls permitted on the cache, including failed solves. Cache requests use the existing cumulative cap and policy-independent exact keys. Within a single exploration repeated generated points are deduplicated by their departure and flight bits. A cache hit can still succeed at the orbital cap. The first unfunded miss terminates with the evaluation-limit status. A separate work cap counts all generated points, including duplicates and hits, so a warm cache or collapsed domain cannot create unbounded work. The maximum refinement depth also bounds resolution. The default limits are 512 actual calls, 4,096 point attempts, and eight refinement levels.

For `S` generated points, cache capacity `C`, and `B` solver calls, the work outside the solver is `O(S (log(S+1) + log(C+1)))` and extra space is `O(S)` in addition to the cache. Each funded miss adds the cost of one propagation and Lambert evaluation. Both queues hold at most a constant number of entries per generated cell. Point counts measure accepted samples. Several points from one basin can all appear as opportunities.

#### Matched ESA easy pair experiment

The Release benchmark used GCC 16.1.0 on Windows x64 UCRT64, PyKEP C++ v2.6, and the exact ESA easy input. It used seven pairs from Phase 5I (0→1, 1→48, 11→4, 18→13, 24→9, 36→10, and 39→33), three opening epochs (days 0, 25, and 75), and two domains. The local deadline was the opening plus (200 − opening) / 48. The full deadline was day 200. Uniform and flight-stratified sampling used the existing `TimingSampler` over the same triangle as the explorer, with seeds 0, 1, and 7. The explorer is deterministic without a seed. Each run used a fresh cache, the strict policy, and exactly 128 or 512 actual calls. The benchmark rejects a run if the actual calls differ from the requested comparison count. Both benchmark executions matched in every field except timing.

| Domain | Calls per run | Method | Runs | Normal / exception points | Runs with any normal | Runs with any admissible | Median ms |
| --- | ---: | --- | ---: | ---: | ---: | ---: | ---: |
| Local | 128 | Explorer | 21 | 163 / 663 | 3 | 13 | 0.278 |
| Local | 128 | Uniform | 63 | 410 / 1,426 | 9 | 35 | 0.217 |
| Local | 128 | Stratified | 63 | 516 / 1,616 | 9 | 33 | 0.252 |
| Local | 512 | Explorer | 21 | 1,096 / 3,757 | 3 | 14 | 1.224 |
| Local | 512 | Uniform | 63 | 1,701 / 5,640 | 9 | 39 | 0.899 |
| Local | 512 | Stratified | 63 | 2,075 / 6,436 | 9 | 39 | 1.017 |
| Full | 128 | Explorer | 21 | 237 / 546 | 6 | 15 | 1.385 |
| Full | 128 | Uniform | 63 | 241 / 1,499 | 18 | 45 | 1.502 |
| Full | 128 | Stratified | 63 | 145 / 1,278 | 17 | 44 | 1.620 |
| Full | 512 | Explorer | 21 | 1,746 / 2,599 | 6 | 15 | 5.800 |
| Full | 512 | Uniform | 63 | 1,001 / 5,923 | 18 | 45 | 6.132 |
| Full | 512 | Stratified | 63 | 596 / 5,154 | 18 | 45 | 6.446 |

- **An opportunity found by refinement.** On 18→13 in the day-0 local window, the initial coarse grid had no admissible point. With 512 calls, refinement returned 38 exception points. The best was 590.729512 m/s at departure day 1.633316 and flight duration 2.435668 days. The best of all six matched random runs was 628.480402 m/s. At 128 calls the explorer's best was 627.857762 m/s.
- **An opportunity missed by refinement.** On 36→10 in the day-0 local window at 512 calls, the explorer's best was 642.284997 m/s. Five of the six random runs found an exception transfer, with a best of 582.978947 m/s. The search also missed the day-75 local 18→13 opportunity indicated by the finer Phase 5I lattice.
- **Normal coverage and caching.** No pair/epoch/domain gained a normal transfer exclusive to the explorer at either allowance. With 512 full-window calls it produced about 83 normal points per run, compared with about 16 for uniform and 9 for stratified. This measured denser sampling of known usable regions. Across the 588 runs there were 188,160 actual calls and zero cache hits because each run started cold and the explorer deduplicated within runs. The explorer made 26,880 calls and 26,964 misses, including one denied miss per run. Each random method made 80,640 calls and misses. A cache replay test reproduced 128 exact evaluations with 128 hits and zero new calls.

The measurements support using refinement as a targeted source of verified arcs alongside continued broad exploration. Prioritize coverage of distinct departure and arrival regions and normal arcs for under-covered pairs when deciding what to carry forward. Low cost at evaluated points provides guidance, and a coarse or refined search that returns no opportunity makes no claim about unsampled points or global optimality.

#### Reproduce Phase 5J

Build from the repository root using the existing backend-enabled Windows x64 configuration and UCRT64 runtime PATH from [README](../README.md#windows-x64-build-with-transfersolver).

```powershell
cmake --build build/transfer-x64 --target transfer_window_benchmark
New-Item -ItemType Directory -Force build/phase5j | Out-Null
./build/transfer-x64/window-benchmark/transfer_window_benchmark.exe data/easy.kttsp build/phase5j/comparison.csv
ctest --test-dir build/transfer-x64 --output-on-failure
```

The CSV records actual calls, hits, misses, budget denials, classification counts, minimum Delta-V and best accepted timing for every case. On non-Windows builds use the benchmark executable in the build root. The benchmark and explorer targets are enabled by `KTTSP_ENABLE_TRANSFER_SOLVER`.

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
