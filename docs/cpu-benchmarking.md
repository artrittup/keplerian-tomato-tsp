# Phase 7C CPU benchmarking

The CPU baseline uses the original ESA easy instance and independently assembles the visit permutation. The reference completes in 177.19440349732923 mission days with five exceptions and 5,204,320 orbital calls, including all 48 strict checks of the export. The existing baseline documentation gives the dataset and solution hashes, dependency versions, and complete solve command. [CPU baseline](cpu-baseline.md)

## Host and build controls

Both preserved x64 executables were Release builds using MSYS2 UCRT64 GCC 16.1.0, `-O3 -DNDEBUG`, CMake 4.4.4, Ninja 1.13.2, PyKEP C++ v2.6, and Boost 1.74.0. The host is an Intel Core i7-13650HX with 14 cores and 20 logical processors. The two pre-instrumentation builds had identical production object files and PyKEP DLLs. The comparison used a fixed, independently assembled order from the Phase 7A fixture to isolate scheduling. The order extraction is for this benchmark only. The end-to-end CPU solver continues to generate its own order.

The prior Phase 7A end-to-end median was 75.213 seconds. Both Phase 7B full controls were much slower during the same verification session, at 187.513 seconds after cleanup and 207.183 seconds before cleanup. Their exports and orbital accounting matched. Phase 7C ran shorter paired scheduling probes in the order old, new, new, old, old, new. Use trace and profiling options only for diagnostics. Use fresh process invocations with empty application caches for performance comparisons.

| Preserved executable | Median external seconds | Minimum and maximum | Scheduling discovery calls per run |
| --- | ---: | --- | ---: |
| Before cleanup | 6.881 | 6.740 to 7.439 | 399,952 |
| After cleanup | 6.732 | 6.681 to 6.756 | 399,952 |

Every probe had 414 exact cache hits and the same prefix depths and failure reasons. These shorter probes exercised discovery and the decoder within a common budget. The full easy schedule is tested separately. The small difference between medians is inside the observed run-to-run spread.

Windows reported the Balanced plan with a 5% minimum and 100% maximum processor state for both AC and DC. Power status was observed offline earlier in the session and online later, around the faster measurements. Across the initial short probes, sampled host-wide processor frequency ranged from 2,050 to 2,545 MHz. These counters include idle cores, so they give host context rather than the solver thread's effective clock. Windows did not expose ACPI thermal-zone readings on this host. A performance-limit snapshot reported 100% and no limit flags. That snapshot cannot establish past thermal throttling. The large timing variation also affected the original executable, and the available data cannot assign it to a specific cause. Future comparisons should record AC state at each run boundary and keep it fixed within each pair.

To reproduce the host probe, use the runtime PATH from the README and substitute the paths to the two preserved Windows x64 executables. Test on a stable power source, record the active power plan and background workload, and alternate runs before interpreting an improvement. `Get-Counter` can read `\Processor Information(_Total)\Processor Frequency` and `% Processor Utility` when the counters are available.

```powershell
New-Item -ItemType Directory -Force build/phase7c | Out-Null
Get-Content tests/fixtures/cpu-easy-fragment-seed7-earliest.sol | Select-Object -Last 49 |
    Set-Content -Encoding ascii build/phase7c/independent.order
powercfg /getactivescheme
powercfg /query SCHEME_CURRENT SUB_PROCESSOR
$before = '<path-to-pre-cleanup-fixed_order_schedule.exe>'
$after = '<path-to-post-cleanup-fixed_order_schedule.exe>'
$timing = @('--max-evaluations', '400000', '--seed', '0', '--bands', '32',
            '--refinement-percent', '50', '--flight-scale', '2', '--cache-capacity', '32768')
$i = 0
foreach ($exe in $before, $after, $after, $before, $before, $after) {
    $clock = [Diagnostics.Stopwatch]::StartNew()
    & $exe data/easy.kttsp build/phase7c/independent.order @timing |
        Set-Content -Encoding ascii "build/phase7c/host-$i.log"
    if ($LASTEXITCODE -ne 0) { throw "Scheduler failed on run $i" }
    $clock.Stop()
    "external.seconds=$($clock.Elapsed.TotalSeconds)" | Add-Content "build/phase7c/host-$i.log"
    $i++
}
```

## Exact orbital query trace

Tracing is opt-in at runtime through `--trace FILE`. `--profile on` adds wall-clock breakdowns. Omit both options for timing. The trace hooks record calls at `TransferSolver::evaluate` entry, including failed solver calls, calls from the independent validators, and repeated queries that reach the solver. Exact cache hits and denied proposals never reach that entry point. Recording preserves call order and duplicates. The fixture is used only by the comparison and validation scripts.

The eight-byte `KTTSPQ1` header is the ASCII text `KTTSPQ1` and one LF. Every record has 24 bytes. Fields are unsigned 32-bit source index, unsigned 32-bit destination index, and two unsigned 64-bit words containing the unchanged IEEE 754 binary64 bits for the departure epoch and flight duration. All fields are little-endian. Indices are zero-based and epochs and durations are in days. There is no padding or footer. Compare the number of complete records with the `trace.records` and `orbital.total` fields from the run log. Retain the input hash, executable configuration, trace hash, and stage counts alongside the binary. With the default shortlist, the recorded stages are matrix discovery, one scheduling discovery and its two strict checks, then strict export validation.

```powershell
$cpu = './build/transfer-x64/cpu-baseline/cpu_baseline.exe'
& $cpu solve data/easy.kttsp build/phase7c/traced.sol --trace build/phase7c/easy.kttspq --profile on |
    Set-Content -Encoding ascii build/phase7c/trace.log
Get-FileHash data/easy.kttsp,build/phase7c/easy.kttspq,build/phase7c/traced.sol
```

Read the magic before reading records. Python's `struct` layout `<IIQQ` reads exact integer payloads. Layout `<IIdd` decodes the same record as indices and two double-precision times. Keep the original byte stream for workload replay, especially if it contains signed zero or non-finite test inputs. The full trace is approximately 119 MiB and stays in an ignored build directory.

The Phase 7C default trace has **5,204,320 records**, occupies 124,903,688 bytes, and has SHA-256 `11a9acc916fbd29f51dbdec6fd68f6e9cc99b84edeede704223b789d90afb8a4`. It uses the Phase 7A configuration and dataset hashes. Its first record is `0 → 1`, departure 99.9995 and flight duration 4.0005 days. The final 48 records repeat the preceding selected schedule validation records byte for byte. The repeated check is part of the workload. [Dataset and solution provenance](cpu-baseline.md#reference-and-expected-accounting)

## Scheduling profile and cache experiment

One diagnostic tree run measured 83.361 seconds in scheduling, including 4,000,048 solver entries. The solver timer includes its subphases. The cache timer excludes the orbital call. Remaining time includes timing proposal work, window exploration, decoding, and measurement overhead. These are diagnostic measurements with tracing and profiling enabled.

| Scheduling component | Seconds |
| --- | ---: |
| Total scheduling | 83.361 |
| Orbital solver, inclusive | 74.700 |
| Planet construction within solver | 2.664 |
| Ephemeris propagation within solver | 3.074 |
| Lambert work within solver | 67.434 |
| Exact cache bookkeeping outside solver | 4.255 |

Lambert work dominated the run. The measured cache cost was large enough to test a smaller change with no orbital math changes. Phase 7C adds an optional hash index for the scheduling LRU. Both indexes use the same exact 4-field key and a common implementation for cache hit handling, eviction order, capacity, and budget accounting. Matrix discovery keeps the tree index. The default scheduler mode is `tree`.

For short comparisons, use the same scheduler executable, timing options, and independently assembled order from the host probe. Change `--cache-index tree` to `--cache-index hash` and alternate the order within pairs. For full comparisons, run the same CPU CLI and use `--schedule-cache-index` with either mode. Keep profiling and tracing disabled for wall measurements and enable them separately for numerical and query-order comparisons.

```powershell
$fixed = './build/transfer-x64/fixed-order/fixed_order_schedule.exe'
& $fixed data/easy.kttsp build/phase7c/independent.order @timing --cache-index tree
& $fixed data/easy.kttsp build/phase7c/independent.order @timing --cache-index hash
& $cpu solve data/easy.kttsp build/phase7c/full-tree.sol --schedule-cache-index tree
& $cpu solve data/easy.kttsp build/phase7c/full-hash.sol --schedule-cache-index hash
& $cpu validate data/easy.kttsp build/phase7c/full-hash.sol
```

The short cache experiment used six pairs with alternating run order and 399,952 actual discovery calls per run. Both modes recorded 414 hits and identical per-leg discovery results and decoded prefixes. The hashed index was slower in five of the six pairs. The measured median wall time rose by about 1.9%.

| Cache index | Median external seconds | Minimum and maximum | Median discovery seconds |
| --- | ---: | ---: | ---: |
| Tree | 3.248 | 3.227 to 3.391 | 3.231 |
| Hash | 3.309 | 3.283 to 3.339 | 3.286 |

Two full pairs then used the default CPU configuration, including the same independently assembled shortlist and strict policy. Both pairs ran without profiling and tracing. The second pair had AC power online before and after both runs. Its reported processor frequency snapshots ranged from 2,271 to 2,544 MHz. These snapshots leave frequency and load during each solve unresolved.

| Full-run order | Tree scheduling / total seconds | Hash scheduling / total seconds |
| --- | ---: | ---: |
| Tree then hash | 34.913 / 40.468 | 33.991 / 39.604 |
| Hash then tree | 41.883 / 47.380 | 87.582 / 97.643 |
| Median of the two observations | 38.398 / 43.924 | 60.787 / 68.623 |

The first full pair showed a small improvement for hashing and the reversed pair did not reproduce it. The ranges and the short-run results do not support a reliable improvement. The tree index remains the default. The two profiled traces were used for numerical and workload parity. Their wall-clock durations include instrumentation and were measured in separate conditions.

Every full run matched the Phase 7A export byte for byte, with **177.19440349732923 days**, **five exceptions**, and **5,204,320 orbital calls**. The discovery work and cache accounting also matched, including 419 scheduling cache hits, 4,001,488 misses, and 1,536 denials. The trace files from both modes had identical bytes, including repeated queries and the final 48 evaluations. A separate strict validation of the hashed run recomputed all 48 legs and passed. The trace and the export remain reproducible with the Phase 7A input and seeds. [Baseline provenance and accounting](cpu-baseline.md#reference-and-expected-accounting)

Standard CTest checks exact key and eviction parity, including signed zero, failed solves and budget limits. The CPU smoke test also compares trace hashes, independent validation counts and solution bytes between the two cache modes on a small instance. The full ESA reference replay remains opt-in. For future CUDA throughput tests, use a fixed trace workload and pair it with separate full-solve benchmarks recorded on stable power with continuous frequency and utilization measurements.
