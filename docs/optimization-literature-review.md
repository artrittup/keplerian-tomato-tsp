# Optimization literature review for ESA SpOC4 Challenge 2

Reviewed on 24 November 2026 against project revision `bc3d531`. The Phase 5K search reached 22 of 49 tomatoes and produced no independently verified full route. This review found and verified a full **reference** route derived from ESA's own public example. Use it to test scheduling separately from finding the visit order. [Project benchmark](cpu-optimization-design.md#phase-5k-hybrid-beam-timing) · [ESA Small example](https://api.optimize.esa.int/data/spoc4/kttsp/examples/easy.sol)

## The public easy route and independent replay

ESA calls the 49-object `easy.kttsp` instance **Small** on Optimise. Its 145 decision entries are 48 absolute departure epochs, 48 flight durations, and 49 zero-based tomato indices. The route starts at a freely chosen tomato at epoch zero, may wait, visits each tomato once, and ends at the last arrival. The written limits are a 200-day deadline, a 0.001-day minimum flight, normal transfers at or below 100 m/s, and up to five exceptions at or below 600 m/s. The official GitHub input matches the local input byte for byte. The hosted input has the same tokens. [Challenge specification and utility](https://github.com/esa/SpOC4/tree/e4276399b51852e6f1c08c2d0c133bd13f8f967a/Challenge%202%20Keplerian%20Tomato%20Traveling%20Salesperson%20Problem) · [Small evaluator source](https://api.optimize.esa.int/media/problems/spoc-4-keplerian-tomato-traveling-salesperson-small-1782203910047.py) · [Hosted Small input](https://api.optimize.esa.int/data/spoc4/kttsp/problems/easy.kttsp)

The Small evaluator's `example()` loads `data/spoc4/kttsp/examples/easy.sol`, which ESA serves publicly at the corresponding data URL. Using the published evaluator with the existing Python 3.11.18 and PyKEP 2.6.4 environment, all decision bounds held and its fitness was `[183.5136514048605, 0, 0, 0, -1]`. The first entry is completion in days, the next three are equality constraints, and the last is exceptions used minus the limit. The source allows up to `1e-6` day of inter-leg overlap as well as `1e-6 m/s` above the exception ceiling. The project's `PublishedEvaluator` policy implements the Delta-V allowance while both project policies require exact chronology. The raw example has 21 negative inter-leg gaps, with a minimum of about `-9.1e-8` day. The C++ structural check therefore rejected it before any Lambert calls. [Public example](https://api.optimize.esa.int/data/spoc4/kttsp/examples/easy.sol) · [Small evaluator](https://api.optimize.esa.int/media/problems/spoc-4-keplerian-tomato-traveling-salesperson-small-1782203910047.py) · [C++ structural check](../src/Solution.cpp)

On a separate copy, keep the visit order and durations. In increasing leg order, replace each departure after the first by the greater of its original value and the previous leg's updated arrival. Write the values with 17 significant digits so the reloaded binary64 values preserve exact chronology. This shifted 28 departures, by at most 0.007863 seconds, and changed the completion time by about 0.00089 seconds. The repaired route passed ESA's evaluator and the existing C++ `SolutionEvaluator` under **both** policies. The C++ evaluator recomputed all 48 legs. It found 44 normal legs, four exceptions at zero-based leg indices 2, 23, 26, and 45, a maximum normal cost of 99.9332924960 m/s, and a maximum transfer cost of 597.8335237576 m/s. It recorded **183.5136514151605 days**. This is a verified reference derived from the public example. Its source is independent of the project's search algorithms. [C++ evaluator](../src/SolutionEvaluator.cpp) · [C++ feasibility policies](orbital-transfer-design.md)

| Replay artifact | SHA-256 |
| --- | --- |
| Local and ESA GitHub `easy.kttsp` | `175256ac98ba2f557bcf889c0462a1ddf2316cb7452a6673e2313444dab55d53` |
| ESA `easy.sol` as served | `cd1774d0fc497bd69754e538116cdaff9ae919d7efd57e1bc689a3afcfdc1eab` |
| Repaired copy, one number per LF line | `267d7ddca663f33ac11d69b3be1713954a50a6982bf018805abbe781077332ba` |

The original, repaired copy, and separate Python and C++ replay logs are in the ignored `build/literature-review/` directory. The public links and procedure above suffice to recreate them. The replay used a temporary input adapter linked to the existing x64 libraries.

## Published methods

### Bannach, Acciarini, and Izzo

Their KTSP formulation minimizes cumulative Delta-V for a mission that starts and returns to a designated body at fixed endpoint epochs and visits each target at least once. The cost of a directed transfer depends on both departure and arrival. A fixed time grid gives an acyclic network of body–epoch nodes, with explicit waiting arcs and exact transfer costs at the sampled times. Binary arc choices, flow conservation, target coverage, and a single vehicle define the integer program. A connected path supplies an entire timed trajectory. Their public benchmark data and precomputed costs cover asteroid-belt and Jovian-moon instances. Challenge 2 needs its own table, an open path with exact visit-once constraints, a final-arrival objective, class cutoffs, and an exception counter. [Paper §§1–2, 5](https://arxiv.org/html/2605.00010v1) · [Public benchmark files](https://zenodo.org/records/14850862)

Their interval-based Dynamic Discretization Discovery starts from a lower-bound relaxation for that model. Interval minima may combine departures that precede arrivals or allow disconnected subtours. They split conflicting intervals and add subtour cuts, then re-solve. The paper reports smaller formulations and harder solves in some cases, with no overall advantage over the direct encoding in its experiments. Their constructive heuristic inserts bodies and epochs into a depot-to-depot path. SWAN alternates order swaps at fixed epochs and one-grid-step timing nudges at fixed order. B-SWAN searches alternatives in this improvement space. These are useful precedents for graph construction or improving a complete schedule. The paper's guarantees concern its own formulation and discretization. [Paper §§3–4, 6.3–6.4 and appendices](https://arxiv.org/html/2605.00010v1)

### Weise, Abdipoor, Li, and Wu

The GECCO Companion paper applies directly to Challenge 2. It preprocesses an ordered-pair matrix with three categories. Code 2 means a normal transfer was found at some epoch. Code 1 means an exception was found and no normal witness was found. Code 0 means the bounded heuristic found no admissible transfer. The reported Small averages are **2.8 normal successors** and **16.76 exception-only successors** per tomato. A successful sample is evidence for that epoch and timing. A missing sample leaves the continuous pair unresolved. [Full paper, §2](https://thomasweise.github.io/assets/papers/WALW2026STS4C2WAMSPEFTKTS.pdf) · [Authors' slide source](https://raw.githubusercontent.com/thomasweise/talks/2ecf3a43599c4116d48245a8626520845b27b5a7/slides/gecco2026_spoc4_keplerian_tsp/gecco2026_spoc4_keplerian_tsp.tex)

The search variable is a full permutation. The decoder first splits it into normal-arc fragments and tries to join or insert fragments. It then permits exception connections and finally concatenates whatever remains. A cheap matrix pass screens orders with missing pairs or too many expected exceptions. For an order that passes, propagate the calendar from the first visit. Each leg searches for an earliest feasible arrival from the current epoch, tries the normal ceiling first, and then tries the exception ceiling. The timing subproblem uses departure and arrival. Its score is Delta-V above the selected ceiling and negative reciprocal arrival time once feasible, so it first seeks feasibility and then favors earlier arrival. The paper uses several local methods, including Nelder–Mead and Powell, and retries against tighter arrival bounds. A spare exception can be tried on an earlier leg followed by rebuilding the suffix. [Full paper, §§2–3](https://thomasweise.github.io/assets/papers/WALW2026STS4C2WAMSPEFTKTS.pdf) · [Authors' slide source](https://raw.githubusercontent.com/thomasweise/talks/2ecf3a43599c4116d48245a8626520845b27b5a7/slides/gecco2026_spoc4_keplerian_tsp/gecco2026_spoc4_keplerian_tsp.tex)

The outer search is randomized local search with geometrically distributed swaps, restarts, sometimes Frequency Fitness Assignment, and more expensive Bi-Pop CMA-ES timing on selected improvements. Its penalties distinguish matrix violations from failures while scheduling. The authors report feasible results on all challenge instances and an early Large feasibility run of 187 seconds, with another run taking 3.8 hours. These are author-reported results, and the runtime figures refer to Large. The public paper and supplements describe the method. I found no released Challenge 2 solver, Small transfer matrix, or Small submission vector accompanying them. Grid budgets, seeds, and tie-breaking for a faithful rerun remain unspecified by the materials inspected. [Full paper, §4](https://thomasweise.github.io/assets/papers/WALW2026STS4C2WAMSPEFTKTS.pdf) · [Author's competition report](https://thomasweise.github.io/posts/2026-07-02-spoc4/)

### ESA results and the fcmaes transfer-table implementation

The July 1 final announcement ranks Team HRI, AC_TUWien, and TGMA first through third overall. It lists fcmaes fourth and ScholORs_HFUU+Sunway ninth. The Challenge 2 column aggregates points across instances. Small's separate public leaderboard reports completion days and includes post-deadline entries. Its public metadata includes, for example, a June 30 entry labelled SINTEF at 111.7561054687 days. Wolz reports third place on Small for fcmaes and repeated search convergence around 110.878421 days, with experiments involving CP-SAT, exact methods, and timing corridors. Those reported results provide performance context. The inspected public leaderboard fields expose scores and descriptions, and the vector that can be replayed here is ESA's separate example. [ESA final discussion](https://github.com/esa/SpOC4/discussions/13) · [Official rules on late entries](https://github.com/esa/SpOC4#timeline) · [Small leaderboard](https://optimise.esa.int/challenge/spoc-4-keplerian-tomato-traveling-salesperson/p/small) · [Wolz's competition debrief](https://www.linkedin.com/pulse/algorithms-ai-lunar-tomato-war-final-debrief-from-esas-wolz-qbxle)

The public `pykep-lambert` code batches real Lambert evaluations on a grid of directed pairs, departure times, and flight durations. It uses separate grids for inner, outer, and cross-cluster transfers, ranks or screens pairs with plane-angle and circular-orbit approximations, and can retain all qualifying samples or just the lowest Delta-V per departure. The grid benchmark on the bundled easy input used 474 of 2,352 non-self pairs, evaluated 10,709,178 cells, and reported 175,150 retained cells for both implementations. The reported warmed 16-worker times were 24.998 s for the original PyKEP path and 1.941 s for Numba. These counts and timings apply to table construction. [Table-building code](https://github.com/dietmarwo/pykep-lambert/blob/f50e1b1ae3bbf8593a2b5fd3143c8a6cc597036b/benchmark_numba_lambert.py) · [Published benchmark JSON](https://raw.githubusercontent.com/dietmarwo/pykep-lambert/f50e1b1ae3bbf8593a2b5fd3143c8a6cc597036b/results/benchmark_table_pykep.json)

That benchmark uses retention ceilings of 105/105/605 m/s and per-type revolution caps of 5/2/0. Our reference has a 20-revolution cap and keeps the actual challenge cutoffs. The public optimizer demo fixes one leg and optimizes wait and flight time. The inspected repository provides table code and benchmark outputs, with no reproduction of the full fcmaes route-construction pipeline. For an adaptation, use geometric estimates to prioritize sampling. Keep class and calendar information with exact transfer witnesses, since the lowest Delta-V at one departure can arrive later than another admissible transfer. [Python wrapper](https://github.com/dietmarwo/pykep-lambert/blob/f50e1b1ae3bbf8593a2b5fd3143c8a6cc597036b/numba_transfer.py) · [Benchmark source](https://github.com/dietmarwo/pykep-lambert/blob/f50e1b1ae3bbf8593a2b5fd3143c8a6cc597036b/benchmark_numba_lambert.py) · [Small evaluator](https://api.optimize.esa.int/media/problems/spoc-4-keplerian-tomato-traveling-salesperson-small-1782203910047.py)

## Recommendation for the C++ project

**Adapt Weise's order-first, schedule-second decomposition.** It directly addresses the competition instance and its scarce normal connections. Phase 5K produced many more acceptable normal timings while often keeping the same depth at equal call counts. A directed pair summary can reject or repair whole orders cheaply, and a reusable timed table can answer scheduling questions across multiple orders. The public fcmaes table is a reference for offline sampling and batching, and Bannach et al. explain how exact time-stamped arcs preserve chronology. The public ESA route gives a much stronger regression fixture than partial-route depth. [Phase 5K results](cpu-optimization-design.md#phase-5k-hybrid-beam-timing) · [Weise et al.](https://thomasweise.github.io/assets/papers/WALW2026STS4C2WAMSPEFTKTS.pdf)

| Existing component | Role in the adaptation |
| --- | --- |
| `Problem`, `TransferSolver`, and `TransferCache` | Evaluate exact transfer witnesses on the same input and cap actual calls, including failures. Keep the PyKEP backend. |
| `TransferWindowExplorer` | Build candidates for the directed table offline, then spend bounded extra work where a connection is missing. Its low-cost search should be judged by the normal or exception arrival opportunities it produces. |
| `TransferGraph` | Store exact timings and classes, query departures from an arrival epoch, and reconstruct a supplied path. A pair summary is a cheap order filter over this evidence. |
| `Solution` and `SolutionEvaluator` | Express the full schedule and independently recompute all 48 legs before reporting success. |
| Beam search and graph backtracking | Serve as existing comparison baselines for first feasible route, calls, and time. |

### Small proof of concept before a rewrite

1. **Run a positive control on the verified reference order.** In a future temporary driver, feed a table containing the 48 repaired example transfers to a fixed-order scheduler. Reconstruct and independently check the 49-visit route. Require the result above and add a negative control that removes the only compatible witness from one leg. Keep the controls labelled as reference-assisted.
2. **Test timing with the order known and the timing withheld.** Use only those 48 directed pairs and the original mission bounds. Start with a deterministic total cap of 200,000 actual calls including controls and validation, with up to 4,096 discovery calls per pair. Reserve a complete 48-call validation from that cap. Compare the paper-style normal-first greedy rule with an earliest-arrival dynamic program on the same timed table. For each leg and each exact exception count from zero to five, keep the earliest reachable arrival and its predecessor. Accept an edge only if its departure follows the state's arrival. Within one state, an earlier arrival can wait for any transfer available to a later arrival, so that dominance rule is exact for the fixed table and fixed order.
3. **Require a full strict C++ validation and an official Python replay.** Record discovery calls, cache hits, first blocking pair and incoming time for any failed decode, completion day, per-leg Delta-V, and exceptions. A full route from the withheld-timing test would justify the next experiment, where a small permutation-repair and matrix search would also have to find the order without the reference. A failed decode would identify a sampled-data or scheduling bottleneck and leave the continuous problem open.

This proposal adds an experiment after the completed replay. The review phase leaves the solver, evaluator, search code, fixtures, and prior benchmarks as they were.

## Phase 6A fixed-order scheduling experiment

### Order-only input and holdout

`data/easy.order` contains only the 49 zero-based indices from the suffix of ESA's original `easy.sol`. The extraction sought backwards through the final 49 integer records, stopping at the delimiter before them. It did not read the 96 timing entries. The scheduler receives `data/easy.kttsp`, the order file, and an explicit configuration. It rejects non-integer entries, duplicates, missing or extra indices, and out-of-range indices. There is no input for a reference timing, cost, or solution. [ESA origin of the order](https://api.optimize.esa.int/data/spoc4/kttsp/examples/easy.sol) · [Order-only file](../data/easy.order)

The original example and the chronology-repaired copy remain separate validation artifacts. Their provenance and hashes are listed above. Neither is an input to discovery, selection, or the tests. The experiments use the unchanged ESA instance, so the orbital elements, mission bounds, and Delta-V limits come from that file.

### Two decoders on the same transfer table

Leave the permutation fixed and generate candidates for each of its 48 consecutive pairs. Give each pair an equal share of the discovery allowance, with any remainder assigned to the first pairs. Compute its earliest possible departure from the mission start and the number of preceding minimum flights. Compute its latest possible arrival by reserving minimum durations for subsequent legs.

1. Split each departure interval into equal bands. The seeded `mt19937_64` generator maps its upper 53 bits to a uniform fraction. In a band, sample departures uniformly. Use two draws with a flight cap at the configured flight scale, one at four times the scale, and one over the full available flight range in each four-proposal cycle. Clip all cases to the physical time bounds.
2. Assign a configurable fraction of each band's allowance to `TransferWindowExplorer`. If the broad phase returns a finite cost, refine a box around its lowest-cost timing. Its departure radius is one third of the band width and its flight radius is half the flight scale. The box stays within the band and all mission bounds. Broad draws use any unused orbital allowance from that pass. Accept only exact evaluated normal and exception options, deduplicated by timing bits.
3. Give the same read-only table to both methods. Normal-first greedy picks the earliest chronological normal option and falls back to the earliest available exception if one remains. Earliest-arrival keeps one label per exact count of exceptions spent at each depth. At the same depth and exception count, an earlier arrival can wait for every departure available after a later arrival. This proves the dominance rule on the sampled table. It keeps different exception counts even when one arrives later. Predecessors recover the best complete schedule, or the earliest arrival at the deepest prefix when scheduling stops.
4. Independently replay every completed schedule with `SolutionEvaluator` under the strict policy. Only a successful replay can produce an exported `.sol`. For a cap of `B` actual calls, the shared discovery table can spend up to `B − 48` on easy. Each method has a separate 48-call reserve inside its own `B`. The comparison constructs the table once and attributes its cost to both methods. A partial result uses zero validation calls. Cache hits never count as orbital calls. The diagnostics report actual work and leave unused reserves visible.

For `L` fixed-order legs, `K` stored options in total, and exception limit `E`, the earliest-arrival scan takes `O((E+1)(L+K))` work and `O(L(E+1))` label storage alongside the table. Discovery pays for orbital evaluation and uses a bounded exact cache. The decoder establishes feasibility only over its stored options. It does not assess other visit orders or unsampled transfers. [Scheduler implementation](../src/FixedOrderScheduler.cpp)

### Experiment results

Both methods ran on the same generated table for each seed and configuration. Windows x64 Release used GCC 16.1.0 and the existing PyKEP C++ v2.6 backend. Every run spent exactly its listed discovery calls. The completed methods spent a further 48 calls each. The 14 tables produced four independently feasible schedules across 28 decoding outcomes.

| Call cap per method | Departure bands / flight scale | Seeds | Shared discovery calls per run | Greedy depth | Earliest-arrival depth | Cache hits by seed |
| ---: | --- | --- | ---: | --- | --- | --- |
| 200,000 | 32 / 2 days | 0 / 1 / 7 | 199,952 | 26 / 30 / 26 | 32 / 32 / 32 | 399 / 417 / 442 |
| 1,000,000 | 32 / 2 days | 0 / 1 / 7 | 999,952 | 40 / 39 / 40 | 43 / 43 / 42 | 405 / 424 / 429 |
| 1,000,000 | 128 / 1 day | 0 / 1 / 7 | 999,952 | 31 / 34 / 30 | 33 / 36 / 36 | 1,802 / 1,875 / 1,805 |
| 2,000,000 | 128 / 1 day | 0 / 1 / 7 | 1,999,952 | 36 / 36 / 36 | 38 / 38 / 39 | 1,851 / 1,869 / 1,896 |
| 4,000,000 | 32 / 2 days | 0 / 7 | 3,999,952 | 49 / 49 | 49 / 49 | 419 / 426 |

The two configurations use a 50% refinement share and a 32,768-entry cache. The second configuration changes both the departure partition and the flight scale. It reached fewer tomatoes at the matched one-million-call cap. In the 32-band seed-0 run at one million calls, earliest-arrival stopped at leg 43, from tomato 10 to 46. The table had 11,453 normal options for the pair, and none could depart after the reachable incoming arrival at day 197.139. There were 31 chronological exception options and the only incoming label had spent all five exceptions. Greedy stopped earlier at leg 40, from tomato 22 to 25, with 39 chronological exception options and no remaining exception. More broadly, incomplete outcomes stopped because chronology left no options or because the only chronological transfers required an unavailable exception. They made zero validation calls. The detailed logs distinguish pair coverage from chronological coverage.

| Seed at 4,000,000 calls | Method | Normal / exception legs | Exceptions on one-based legs | Validated completion (days) | Runtime including discovery |
| ---: | --- | --- | --- | ---: | ---: |
| 0 | Normal-first greedy | 44 / 4 | 3, 24, 27, 46 | 188.7567701690 | 34.212 s |
| 0 | Earliest arrival | 43 / 5 | 3, 12, 24, 27, 46 | **176.8813770461** | 34.214 s |
| 7 | Normal-first greedy | 44 / 4 | 3, 24, 27, 46 | 195.4733964285 | 37.455 s |
| 7 | Earliest arrival | 43 / 5 | 3, 12, 24, 27, 46 | **180.7119365235** | 37.458 s |

All four completions terminated with `feasible` and passed strict independent C++ validation. By keeping the earlier path that used an extra exception at leg 12, earliest-arrival improved the finish by 11.875 days for seed 0 and 14.761 days for seed 7. Decoding and validation took about 11–14 ms per successful method. Each runtime column includes the same shared discovery. The physical run used 3,999,952 discovery calls plus 96 total validation calls for its two schedules.

The seed-0 four-million-call replay reproduced both files byte for byte and reproduced all diagnostics apart from runtime. After all blind runs and this replay, the original and repaired reference files were copied separately to `build/phase6a/holdout/` and checked against their recorded hashes. The repaired holdout still passed strict C++ validation. ESA's published Small evaluator also accepted all four newly generated schedules with the same completion times. The repaired reference completed in 183.5136514152 days. The best independently discovered schedule finished about 6.63 days earlier. This compares schedules of one order and makes no claim about the best order or continuous optimum. [Small evaluator](https://api.optimize.esa.int/media/problems/spoc-4-keplerian-tomato-traveling-salesperson-small-1782203910047.py)

### Reproduce the blind comparison

Use the existing backend-enabled Windows x64 build and UCRT64 runtime PATH. Run from the repository root. The order file contains exactly 49 integer indices and has SHA-256 `e61e5a605a7b03787195054f34693c621efdbdcee52a40fcaa6598674632e863` with its original LF line ending. No reference solution file is needed.

```powershell
cmake --build build/transfer-x64 --target fixed_order_schedule
New-Item -ItemType Directory -Force build/phase6a/runs | Out-Null
$exe = './build/transfer-x64/fixed-order/fixed_order_schedule.exe'
foreach ($seed in 0, 7) {
    $out = "build/phase6a/runs/fixed-4000000-$seed"
    & $exe data/easy.kttsp data/easy.order --max-evaluations 4000000 --seed $seed --bands 32 --refinement-percent 50 --flight-scale 2 --cache-capacity 32768 --output-prefix $out | Tee-Object "$out.log"
}
```

Use the cap, band count, flight scale, and seeds in the results table for the partial cases. On non-Windows Ninja builds, the executable is at the build root. A run writes its method summary, per-pair discovery counts and minimum Delta-V, and one schedule row per attempted leg. In schedule rows, the first leg is numbered one and the corresponding child prefix contains two tomatoes. The optional output prefix creates distinct `-greedy.sol` and `-earliest.sol` files only for independently feasible schedules. Logs and solution outputs stay under ignored `build/` in the commands above.

### Seed-0 schedule regression

The reference fixture records our earliest-arrival result at seed 0 and 4,000,000 orbital calls, including 48 fresh validation calls. The visit permutation comes from the European Space Agency Advanced Concepts Team's published SpOC4 Challenge 2 easy example. Our scheduler independently generated all 48 departure times and 48 flight durations. ESA's SpOC4 GitHub repository lists GPL-3.0. The public example is hosted separately on ESA's Optimise service. Only the example's visit permutation is reproduced here. [ESA example](https://api.optimize.esa.int/data/spoc4/kttsp/examples/easy.sol) · [ESA challenge](https://github.com/esa/SpOC4/tree/e4276399b51852e6f1c08c2d0c133bd13f8f967a/Challenge%202%20Keplerian%20Tomato%20Traveling%20Salesperson%20Problem) · [SpOC4 repository license](https://github.com/esa/SpOC4/blob/main/LICENSE)

The fixture contains 48 departure epochs in MJD2000 days, 48 flight durations in days, and 49 zero-based tomato indices, in that order. Git preserves its LF line endings on checkout. Its SHA-256 is `633f49b11617a50cf2e0de97b682b2458d95011a4c3ded6eba2a28e060d7d731`. The scheduling CLI consumes only the instance, order, and settings. CTest passes the fixture to a test executable, which reads the original instance and calls `SolutionEvaluator` with `DeltaVPolicy::Strict`. It checks the visit order, all 48 transfers, a completion time of **176.88137704611736 days** within `1e-9` days, and five exceptions at one-based legs 3, 12, 24, 27, and 46. The time tolerance applies to the regression assertion. Feasibility uses the existing strict rules. [Seed-0 fixture](../tests/fixtures/esa-easy-fixed-order-seed0-earliest.sol)

After running the seed-0 CLI command above, use the same backend-enabled Windows x64 build to validate the stored fixture through CTest and the newly exported schedule directly. The verified Windows UCRT64 build reproduced the fixture byte for byte. On non-Windows Ninja builds, run `fixed_order_tests` from the build root.

```powershell
cmake --build build/transfer-x64 --target fixed_order_tests
ctest --test-dir build/transfer-x64 -R '^fixed_order_seed0_reference$' --output-on-failure
./build/transfer-x64/fixed-order-tests/fixed_order_tests.exe seed0_reference data/easy.kttsp data/easy.order build/phase6a/runs/fixed-4000000-0-earliest.sol
Get-FileHash tests/fixtures/esa-easy-fixed-order-seed0-earliest.sol,build/phase6a/runs/fixed-4000000-0-earliest.sol
```

### Implications for order-first search

The experiment demonstrates that a complete known permutation can be scheduled using exact transfer evidence discovered without its reference times. Retaining different exception counts improved every partial comparison and both successful completion times. The high cost was generating compatible time-stamped options. A future permutation search should reuse a persistent directed transfer table across orders, use witnessed normal and exception connectivity to screen orders cheaply, and schedule a shortlist of candidates with the exception-label decoder. A visit order found by that next stage will need to pass the same strict verification. [Weise et al.](https://thomasweise.github.io/assets/papers/WALW2026STS4C2WAMSPEFTKTS.pdf)

## Phase 6B permutation screening

### Published method and adaptation

Weise et al. use heuristic timing optimization to precompute a directed matrix. Value `2` records a normal witness, `1` an exception witness with no normal witness found, and `0` a pair for which preprocessing failed to find an admissible witness. They explicitly qualify the matrix as heuristic. Their decoder splits a permutation into normal-connected fragments, merges or inserts compatible fragments, relaxes the joins to allow exceptions, and concatenates any remaining fragments. It screens the resulting order by counts of `0` and `1` entries before searching departure and arrival times leg by leg. It starts each leg's timing search at the previous arrival and tries the normal ceiling first. Their outer randomized local search uses swaps of geometrically distributed size, restarts, occasional Frequency Fitness Assignment, and a separate more expensive timing pass for selected improvements. The paper reports an average of 2.8 normal and 16.76 exception-only successors per tomato on Small. [GECCO Companion paper, §§2–4](https://doi.org/10.1145/3795101.3815557) · [Author's copy](https://thomasweise.github.io/assets/papers/WALW2026STS4C2WAMSPEFTKTS.pdf) · [Authors' slides](https://raw.githubusercontent.com/thomasweise/talks/2ecf3a43599c4116d48245a8626520845b27b5a7/slides/gecco2026_spoc4_keplerian_tsp/gecco2026_spoc4_keplerian_tsp.tex)

Phase 6B keeps the shared directed-pair evidence and cheap scoring stages. Its trials are independently drawn one-move neighbors and uniformly shuffled complete orders. A swap exchanges two positions, an insertion moves one tomato to a specified final position, and a reversal flips an inclusive segment. The route is an open path, so the screen considers `N-1` directed edges. Reversal changes the direction of every edge inside the segment. Phase 6B stops at scoring the proposed order. The fragment decoder, randomized local search loop, and timing optimization from the paper are separate future work. [Implementation](../src/PermutationScreening.cpp)

### Evidence and score

The matrix owns a fixed `Problem` snapshot in `TransferCache`. Its cells cover all directed non-self pairs up to a configurable capacity of 100,000 pairs by default. A sweep divides a shared actual-call cap among pairs and cycles through sources at each destination offset. The discovery method is `TransferWindowExplorer` under the strict policy. It samples within the mission's epochs, minimum flight duration, and deadline, optionally with a tighter discovery flight cap. Failed orbital evaluations consume the call budget. Cache hits use exact keys and consume proposal work but no orbital calls. Each window has a proposal cap as well, so duplicate or cached samples cannot make a request run indefinitely. A sweep can leave budget unused when a pair fails to use its quota. [Transfer-window exploration](../src/TransferWindowExplorer.cpp) · [Exact cache](../src/TransferCache.cpp)

Each cell retains counts of normal, exception, over-ceiling, and solver-failure samples. Its summary is normal-observed if a normal witness exists, exception-observed if there is only an exception witness, rejected-only if finite sampled costs exceed the exception ceiling and there are no admissible witnesses, and unknown if no finite cost was measured. Solver failures alone leave a pair unknown. Repeated exact queries in different windows can contribute to the sample counts through the cache. Both unknown and rejected-only pairs remain unresolved outside the sampled timings.

The ranking key is lexicographic in ascending order. Compare unsupported legs first (`rejected-only + unknown`), then observed exception-only legs above `Problem::maxExceptions`, then incompatible joins, then rejected-only legs, then exception-only legs. For a fixed number of legs, fewer unsupported and exception-only edges implies more witnessed normal connectivity. On a tie in unsupported count and preceding keys, an unsampled leg receives a better rank than an already sampled rejected-only leg. This preference allocates attention during screening. It has no effect on ESA's feasibility thresholds.

For adjacent legs with witnesses, the checker compares the earliest sampled arrival on the first leg with the latest sampled departure on the next. If the former is later, these observed options have no chronological join. With no witness on either leg, the join is unassessed. A compatible join can rely on an exception, and neighboring joins can select different timings for their common leg. Exception-only counts are conditional on the discovery budget and timing domain. Full feasibility requires one consistent schedule and strict recomputation of all legs through `SolutionEvaluator`. The ranking key has no units of time.

### ESA easy results

Built with the existing Windows x64 UCRT64 and PyKEP v2.6 Release configuration. Each run built its table once from `data/easy.kttsp` across 2,352 directed pairs. `data/easy.order` was used only as the labelled ESA reference order and the source of one-move neighbors. The random group was generated independently of its indices' ordering. No original, repaired, or Phase 6A solution schedule was read. The seeds control the order draws. Discovery is deterministic and independent of the seed and permutation. Each comparison used 1,000 draws per operator and 1,000 random orders, with replacement. The time column measures a single local run.

| Calls per pair | Sampled flight cap | Seed | Actual calls | Normal / exception-only / rejected-only / unknown pairs | Build time | Score 4,001 orders |
| ---: | --- | ---: | ---: | --- | ---: | ---: |
| 64 | 8 days | 0 | 150,528 | 131 / 753 / 1,468 / 0 | 1.056 s | 2.166 ms |
| 512 | 8 days | 0 | 1,204,224 | 135 / 790 / 1,427 / 0 | 9.638 s | 2.430 ms |
| 512 | 8 days | 7 | 1,204,224 | 135 / 790 / 1,427 / 0 | 9.549 s | 2.263 ms |
| 512 | Mission-wide | 0 | 1,204,224 | 138 / 795 / 1,419 / 0 | 25.762 s | 2.444 ms |

The table build had zero exact cache hits and zero solver failures in these runs. Each sweep queried each directed pair once and the explorer deduplicated exact times within that query. There were 2,352 budget denials per run, one unexecuted request at each pair cap. The scoring phase made zero orbital calls. The eight-day cap is a sampling choice. `full` samples the problem's full flight domain at the same orbital-call cap.

At 512 calls per pair and an eight-day flight cap, the seed-0 scoring statistics were as follows. The normal, exception-only, rejected-only, and temporal columns are per-order means. Unknown legs were zero in these four experiments because every pair had at least one finite evaluation.

| Order group | Normal | Exception-only | Rejected-only | Incompatible joins | Unassessed joins | Lower rank key / tied with reference |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| ESA reference order | 44 | 4 | 0 | 0 | 0 | 0 / 1 |
| Swap | 40.649 | 5.044 | 2.307 | 0.003 | 3.468 | 0 / 31 |
| Insertion | 41.653 | 5.113 | 1.234 | 0.008 | 1.800 | 0 / 24 |
| Reversal | 42.310 | 4.490 | 1.200 | 0.000 | 2.339 | 0 / 26 |
| Random | 2.705 | 16.082 | 29.213 | 0.023 | 39.281 | 0 / 0 |

At 64 calls per pair the reference had 43 witnessed normal and five exception-only legs. Three insertion draws and one reversal draw scored better by showing four exception-only legs and no unsupported legs or incompatible joins. At the larger budget the reference itself showed 44 normal and four exception-only legs. No trial beat its key in the two larger-budget seeds or in the mission-wide run. The mission-wide sampling found three more normal pairs overall at the same call budget and took about 2.7 times as long as the eight-day run. The very low incompatible-join means show that local chronology was a weak discriminator in these sweeps. The screening experiment establishes neither a new feasible order nor a mission time improvement. The reference permutation still has the separate Phase 6A verified time of 176.88137704611736 days.

### Complexity and reproduction

`N` tomatoes require `N(N-1)` compact cell summaries. If `C` is cache capacity and `S` the largest per-pair proposal cap, the matrix build uses `O(N² + C + S)` storage including temporary exploration data. It spends at most the configured orbital calls, plus per-proposal explorer and cache work. Scoring takes `O(N)` time and `O(N)` temporary space for permutation validation. A swap is `O(1)` in place. Insertion and reversal cost time proportional to the modified segment. The experiment copies the reference for each trial. The matrix build can be amortized across as many permutations as the caller chooses to score.

Run from the repository root with the backend-enabled configuration and its UCRT64 runtime PATH. The non-Windows Ninja executable is at the build root.

```powershell
cmake --build build/transfer-x64 --target permutation_screen permutation_screen_tests
$screen = './build/transfer-x64/permutation-screen/permutation_screen.exe'
& $screen data/easy.kttsp data/easy.order 0 150528 1000 8
& $screen data/easy.kttsp data/easy.order 0 1204224 1000 8
& $screen data/easy.kttsp data/easy.order 7 1204224 1000 8
& $screen data/easy.kttsp data/easy.order 0 1204224 1000 full
ctest --test-dir build/transfer-x64 -R '^permutation_screen_' --output-on-failure
```

The evidence supports trying directed normal-fragment assembly before budgeting for timing. Keep a shortlist of orders with strong connectivity and resample a small number of uncertain joins. Use fixed-order scheduling and strict validation on that shortlist before comparing mission times. The best ranking for choosing which orders warrant that expense remains an open measurement question.
