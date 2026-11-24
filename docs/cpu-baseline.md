# Phase 7A CPU baseline

`cpu_baseline solve` connects the existing ESA parser, strict PyKEP transfer-evidence matrix, directed fragment assembler, fixed-order scheduler, and strict `SolutionEvaluator`. The solver takes an instance and an output path. Assembly starts from singleton fragments. Candidate orders are ranked by the existing Phase 6B key and then by discovery order. The chosen fixed-order candidates run both scheduling methods on their respective shared timing tables. Among their validated schedules, the earliest completion wins. Ties retain the first scheduled candidate and try normal-first before earliest-arrival within that candidate. [Phase 6C method and original run](optimization-literature-review.md#phase-6c-directed-fragment-assembly)

## Reproduce ESA easy

Use the C++17 Release build with `KTTSP_ENABLE_TRANSFER_SOLVER=ON` and `BUILD_TESTING=ON`. The verified environment uses Windows x64, MSYS2 UCRT64 GCC 16.1.0, CMake/CTest 4.4.4, Ninja 1.13.2, PyKEP v2.6, and Boost 1.74.0 Date_Time and Serialization. Follow the dependency and PATH setup in the README. On non-Windows Ninja builds the executable is in the build root. [Windows x64 setup](../README.md#windows-x64-build-with-transfersolver)

Run from the repository root. The options below are also the program's defaults. Use a fresh output filename in an existing directory. The `.sol` file contains `N-1` departure epochs, `N-1` flight durations, and `N` zero-based visit indices, one value per line. Epochs and durations are in days.

```powershell
cmake --build build/transfer-x64 --target cpu_baseline
New-Item -ItemType Directory -Force build/cpu | Out-Null
$cpu = './build/transfer-x64/cpu-baseline/cpu_baseline.exe'
$options = @(
    '--assembly-seed', '7', '--timing-seed', '0', '--matrix-calls', '1204224',
    '--matrix-flight-days', '8', '--attempts', '1000', '--repairs', '6', '--unknown', '0',
    '--shortlist', '1', '--schedule-calls', '4000000', '--bands', '32',
    '--refinement-percent', '50', '--flight-scale', '2', '--cache-capacity', '32768'
)
& $cpu solve data/easy.kttsp build/cpu/easy.sol @options
& $cpu validate data/easy.kttsp build/cpu/easy.sol
Get-FileHash build/cpu/easy.sol,tests/fixtures/cpu-easy-fragment-seed7-earliest.sol
```

The program serializes the selected schedule in a staging directory beside the requested output and reads it back with round-trip floating-point precision. It calls `SolutionEvaluator` with the strict policy on the values it read, recomputing every transfer using the current `Problem` limits. The output is published only after the evaluation passes and its completion time matches the scheduler's result. `validate` independently rereads any completed schedule. Successful solves return 0. A completed run with no candidate, no feasible schedule, or failed validation returns 2. Invalid input or filesystem errors return 1.

## Reference and expected accounting

The authors of this repository generated the fixture with the Phase 6C assembler at seed 7 and the fixed-order scheduler at timing seed 0. Its input was the original ESA SpOC4 Challenge 2 easy instance. The fixture and its order are independent of ESA's example permutation and schedules. The CPU solver takes only the instance as input. Tests use the fixture to verify feasibility and compare the post-run export. The earlier ESA-order fixture remains separate. [Independent fixture](../tests/fixtures/cpu-easy-fragment-seed7-earliest.sol) · [Original ESA instance](https://github.com/esa/SpOC4/blob/e4276399b51852e6f1c08c2d0c133bd13f8f967a/Challenge%202%20Keplerian%20Tomato%20Traveling%20Salesperson%20Problem/problems/easy.kttsp) · [ESA SpOC4 repository license](https://github.com/esa/SpOC4/blob/main/LICENSE)

- Input SHA-256 `175256ac98ba2f557bcf889c0462a1ddf2316cb7452a6673e2313444dab55d53`
- Output SHA-256 `68d90c0dcaa7cd9c8aa4253fc44f21feb1b0a4ea407f6462364f40013060233c`. The fixture's Git attributes preserve LF line endings.
- Assembly completed one of 1,000 attempts and shortlisted the one unique order. Both timing methods produced feasible schedules. Earliest-arrival won in **177.19440349732923 days**, with **43 normal transfers and five exceptions**.

| Phase | Actual solver calls | Cache hits |
| --- | ---: | ---: |
| Evidence matrix | 1,204,224 | 0 |
| Scheduling discovery | 3,999,952 | 419 |
| Both scheduling methods' internal strict checks | 96 | 0 |
| Final independent check of the export | 48 | 0 |
| Total | **5,204,320** | **419** |

The matrix call cap and the scheduling per-method cap are explicit. The scheduler spends one shared discovery budget of at most `schedule-calls - leg_count`. The CPU driver budgets for two scheduler validations of `leg_count` each and one final file validation. Thus the total ceiling is `matrix-calls + shortlist * (schedule-calls + leg_count) + leg_count`. The program reports actual calls and cache counters by phase. The standalone `validate` command adds a separate 48-call check.

The evidence cache recorded 1,206,576 misses, including 2,352 budget denials, and 1,171,456 evictions. The scheduling cache recorded 4,001,488 misses, including 1,536 budget denials, and 3,967,184 evictions. Each cache has capacity 32,768. Denied requests and cache hits do not consume orbital calls.

## Regression tests

Ordinary CTest runs the three-tomato smoke test and strict replay of the saved easy fixture. The smoke test checks byte-for-byte seeded replay, counters, failed construction, insufficient scheduling budget, malformed input, and protection of existing exports. The expensive test rebuilds the matrix, assembles, schedules, validates, and checks the export's SHA-256 against the fixture. Register it explicitly when needed. `KTTSP_ENABLE_CPU_BASELINE_REPLAY` defaults to `OFF`.

```powershell
ctest --test-dir build/transfer-x64 -R '^cpu_baseline_(smoke|fixture)$' --output-on-failure
cmake -S . -B build/transfer-x64 -DKTTSP_ENABLE_CPU_BASELINE_REPLAY=ON
ctest --test-dir build/transfer-x64 -R '^cpu_baseline_easy_replay$' --repeat until-fail:3 -V
cmake -S . -B build/transfer-x64 -DKTTSP_ENABLE_CPU_BASELINE_REPLAY=OFF
```

## Timing and comparison methodology

Use single-process sequential runs on the same host in the Release configuration. Pin the instance and export hashes, compiler, numerical backend, strict policy, both seeds, all settings, and orbital counts. Record cold or warm cache state explicitly. Each solve here starts with empty application caches. Report multiple repetitions, their median, and their range. The clock starts at command entry and the pipeline total ends after publishing the export. Process startup and teardown can additionally be measured outside the executable. Matrix time covers construction and sampling. Assembly time includes projection, scoring, and shortlist sorting. Scheduling time includes the existing scheduler validations. Final validation time covers reloading the written schedule and all strict orbital checks. Writing and publishing are also reported separately.

Three sequential runs on an Intel Core i7-13650HX with the UCRT64 Release build matched the Phase 6C fixture byte for byte and reproduced all counts and scores. The times below are in seconds and reflect this host's measured wall clock. Stage medians are computed independently.

| Stage | Median seconds | Minimum–maximum seconds |
| --- | ---: | ---: |
| Parse | 0.002122 | 0.001884–0.002350 |
| Build transfer matrix | 10.152831 | 9.269369–10.627162 |
| Assemble and rank | 0.611609 | 0.508334–0.685607 |
| Fixed-order scheduling | 63.963475 | 61.682397–65.995360 |
| Final file validation | 0.002621 | 0.002191–0.004509 |
| Export I/O | 0.002134 | 0.001973–0.003232 |
| Export-ready total | **75.213463** | **71.467667–76.841887** |

Measure orbital throughput on an identical captured list of exact `(from, to, departure, duration)` queries, then measure end-to-end search on identical inputs and settings. A portable exact-query trace is still needed for the fixed-work throughput comparison. Adaptive refinement can change its later query choices after even small numerical differences. Before comparison, check status, classification near both Delta-V limits, and final strict feasibility. Test close-orbit cases and long or multi-revolution Lambert candidates because call counts alone miss variation in work per transfer. Report outcome and schedule quality alongside speed, since a run that assembles no order can skip the dominant scheduling work. Measure GPU startup, transfers and synchronization separately and include them in end-to-end time. Control for Windows compiler and dependency ABI and record host compiler versions because UCRT64 is the verified CPU configuration. Seeded trajectories may differ across standard library implementations. [Numerical parity and Windows toolchain notes](orbital-transfer-design.md)
