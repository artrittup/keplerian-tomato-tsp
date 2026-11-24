# Keplerian Tomato Traveling Salesperson Problem

Course: Dizajni dhe Analiza e Algoritmeve

Language: C++

This project focuses on solving the Keplerian Tomato Traveling Salesperson Problem using algorithmic and metaheuristic approaches.

CUDA acceleration and interactive OpenGL visualization are planned. The current implementation runs on the CPU.

## Build and testing

Run these commands from the repository root in PowerShell. Put `cmake`, `ctest`, and `ninja` on your `PATH`. The verified Windows builds used CMake/CTest 4.4.4 and Ninja 1.13.2.

### Dependency-free C++17 build

Use a C++17 compiler available in your shell. If you have several compilers, pass `-DCMAKE_CXX_COMPILER:FILEPATH=<path-to-compiler>` at configure time. The transfer backend is off by default. This configuration runs the **12 parser and solution tests**.

```powershell
cmake -S . -B build/core -G Ninja -DCMAKE_BUILD_TYPE=Release -DKTTSP_ENABLE_TRANSFER_SOLVER=OFF -DBUILD_TESTING=ON
cmake --build build/core
ctest --test-dir build/core --output-on-failure
```

### Windows x64 build with TransferSolver

Use the **PyKEP v2.6 C++ core** and **Boost 1.74.0 headers and static Date_Time and Serialization libraries**. Build the dependencies and this project with the same Windows x64 compiler and runtime. The verified setup used **MSYS2 UCRT64 GCC 16.1.0**. The fixtures are included in the checkout, and CTest runs them without Python. [Orbital transfer design](docs/orbital-transfer-design.md)

Replace the placeholders below. `$ucrt64` is the UCRT64 directory containing `bin/g++.exe`. Prepending its `bin` directory to the current shell's `PATH` lets Windows load the matching GCC runtime DLLs. The build copies the PyKEP DLL next to the program and each orbital test executable.

```powershell
$ucrt64 = '<path-to-MSYS2-ucrt64>'
$pykepPrefix = '<path-to-PyKEP-v2.6-install-prefix>'
$boostInclude = '<path-to-directory-containing-boost-headers>'
$boostLib = '<path-to-directory-containing-the-x64-Boost-static-libraries>'
$env:Path = "$ucrt64/bin;$env:Path"

$ucrtToolchain = @(
    '-G', 'Ninja'
    "-DCMAKE_CXX_COMPILER:FILEPATH=$ucrt64/bin/g++.exe"
    '-DCMAKE_BUILD_TYPE=Release'
)
$boostOptions = @(
    '-DCMAKE_POLICY_DEFAULT_CMP0167=OLD'
    '-DBoost_NO_BOOST_CMAKE=ON'
    '-DBoost_NO_SYSTEM_PATHS=ON'
    '-DBoost_USE_STATIC_LIBS=ON'
    "-DBOOST_INCLUDEDIR:PATH=$boostInclude"
    "-DBOOST_LIBRARYDIR:PATH=$boostLib"
)

cmake -S . -B build/transfer-x64 @ucrtToolchain @boostOptions `
    "-DCMAKE_PREFIX_PATH:PATH=$pykepPrefix" -DKTTSP_ENABLE_TRANSFER_SOLVER=ON -DBUILD_TESTING=ON
cmake --build build/transfer-x64
ctest --test-dir build/transfer-x64 --output-on-failure
```

`CMAKE_PREFIX_PATH` points to the PyKEP install prefix. You can instead set `keplerian_toolbox_DIR` to the directory containing `keplerian_toolbox-config.cmake`, typically `lib/cmake/keplerian_toolbox` under that prefix. Set `BOOST_INCLUDEDIR` to the directory that contains `boost/version.hpp` and `BOOST_LIBRARYDIR` to the directory containing the two Boost libraries. The `CMP0167` and Boost options select the legacy `FindBoost` module and the explicit static-library paths used in the verified build. This configuration runs **111 standard tests**. [CMake CMP0167](https://cmake.org/cmake/help/v4.0/policy/CMP0167.html)

With the backend enabled, call `SolutionEvaluator::evaluate(problem, solution, policy)` to validate a complete route. Choose `DeltaVPolicy::Strict` or `DeltaVPolicy::PublishedEvaluator` for the leg limits. Both use the same structural checks. The result reports the scheduled completion time, per-leg costs and classifications, total accepted Delta-V, exception count, and the first failure.

### End-to-end CPU baseline

Use the backend-enabled build and UCRT64 runtime PATH. The default `cpu_baseline solve` run builds the matrix, assembles its own visit order, schedules the top candidate, and reopens the serialized schedule for strict validation before publishing it. On ESA easy it reproduces **177.19440349732923 days**, **five exceptions**, and **5,204,320 orbital calls**, including 48 calls to validate the export. Use a fresh output path on each run.

```powershell
cmake --build build/transfer-x64 --target cpu_baseline
New-Item -ItemType Directory -Force build/cpu | Out-Null
$cpu = './build/transfer-x64/cpu-baseline/cpu_baseline.exe'
& $cpu solve data/easy.kttsp build/cpu/easy.sol
& $cpu validate data/easy.kttsp build/cpu/easy.sol
```

The standard tests include a small end-to-end replay and strict validation of the tracked fixture. Configure with `-DKTTSP_ENABLE_CPU_BASELINE_REPLAY=ON` to register the full multi-million-call replay. The commands, configuration, file checksum, and benchmarking procedure are documented separately. [CPU baseline](docs/cpu-baseline.md)

### Bounded beam-search experiment

From the repository root, run the backend-enabled executable with the same UCRT64 `PATH` as the build. Without `--beam-search`, the executable continues to parse and summarize the instance.

```powershell
./build/transfer-x64/keplerian_tsp.exe --beam-search --seed 0 --beam-width 4 --destinations 8 --samples 6 --max-evaluations 10000 --restarts 2
```

These are the defaults. The input defaults to `data/easy.kttsp` and can be set with `--input FILE`. Use `--policy strict` (default) or `--policy published` to select the existing Delta-V rules. The evaluation limit counts failed orbital calls and full validation. `--restarts` requests additional attempts when the budget permits. The report includes seed, starts, counters, runtime and the verified route if one is found. A run that finds no route within its limits still exits successfully. [CPU optimization design](docs/cpu-optimization-design.md#phase-5c-beam-search)

Use `--timing stratified` to spread the short-window proposals across flight-duration bands. The original sampler remains the default (`--timing baseline`). Add `--diagnostics` for per-depth rejection and pruning counts and timing histograms. [Phase 5D comparison](docs/cpu-optimization-design.md#phase-5d-timing-diagnostics)

Use `--timing hybrid` to combine broad sampling with deterministic refinement around low-Delta-V proposals. Set `--hybrid-refinement-percent` (default 25, range 0–99), `--hybrid-refinement-regions` (default 4 per prefix), and `--hybrid-exploration-percent` (default 25, range 0–100) to control the split. The latter sets the full-window share of the broad phase. The existing `--destinations` and `--samples` bound work per prefix, and `--hybrid-cache-capacity` caps exact cache entries (default 16,384). Add `--transfer-stats` to show the normal and exception split for either existing timing mode. Hybrid prints those counts automatically. [Phase 5K comparison and commands](docs/cpu-optimization-design.md#phase-5k-hybrid-beam-timing)

Use `--selection temporal` to try arrival-bin selection and `--arrival-bin-days DAYS` to set the maximum bin width. The original ranking remains the default. `--diagnostics` includes the retained arrival distribution per depth. [Diversity benchmark](docs/cpu-optimization-design.md#diversity-aware-beam-selection)

Use `--selection lookahead` for exact one-step continuation probes. Set `--lookahead-probes N` to cap probes per shortlisted prefix. All calls share the same orbital budget. [Lookahead experiment](docs/cpu-optimization-design.md#constraint-guided-one-step-lookahead)

The `transfer_graph` library provides an exact, capacity-limited transfer cache and sparse graph storage. It can reconstruct a supplied path. [Graph and cache design](docs/cpu-optimization-design.md#phase-5g-graph-and-cache-foundations)

### Bounded graph search

Graph search is opt-in and uses the same backend-enabled build and UCRT64 runtime. It can undo an earlier visit or timing decision, then try another stored edge. The parser remains the default when you pass no search flag.

```powershell
./build/transfer-x64/keplerian_tsp.exe --graph-search --input data/easy.kttsp --seed 7 --policy strict --max-evaluations 48048 --max-states 10000 --max-backtracks 10000 --max-edges 60048 --max-nodes 120145 --cache-capacity 60048 --destinations 24 --samples 16 --alternatives 12
```

The orbital cap includes independent validation of every reported complete route. The state and backtrack caps limit work on stored edges, which takes no orbital calls. Use `--policy published` for the existing published evaluator tolerance, `--start-tomato N` to fix a zero-based starting index, or `--graph-search --help` for all options.

At 48,000 matched actual calls on ESA easy, graph search reached 11, 16, and 16 tomatoes for seeds 0, 1, and 7. The stratified beam baseline reached 12 on each seed. Neither mode found a complete route. [Matched runs and reproduction commands](docs/cpu-optimization-design.md#matched-esa-easy-experiment)

### Fixed-order scheduling experiment

The backend-enabled build includes `fixed_order_schedule`. It accepts an instance and an integer-only visit permutation. `data/easy.order` is the order from ESA's public easy example, with all timing fields withheld. The two schedulers share one discovery table and independently validate complete schedules with the strict policy.

```powershell
cmake --build build/transfer-x64 --target fixed_order_schedule
New-Item -ItemType Directory -Force build/phase6a | Out-Null
./build/transfer-x64/fixed-order/fixed_order_schedule.exe data/easy.kttsp data/easy.order --max-evaluations 4000000 --seed 0 --bands 32 --refinement-percent 50 --flight-scale 2 --cache-capacity 32768 --output-prefix build/phase6a/seed0
```

The verified seed-0 run produced complete schedules in **188.757 days** with normal-first greedy and **176.881 days** with earliest-arrival labels. Each method used 4,000,000 actual calls including independent validation. The executable reports per-pair coverage and exceptions used at every scheduled depth. [Phase 6A design and full results](docs/optimization-literature-review.md#phase-6a-fixed-order-scheduling-experiment)

The seed-0 earliest-arrival fixture uses ESA's published visit order and timings independently generated by our scheduler. Only the regression test reads the stored schedule. It checks all 48 legs with the strict evaluator and verifies the completion time and five exceptions. [Fixture provenance and replay instructions](docs/optimization-literature-review.md#seed-0-schedule-regression)

```powershell
cmake --build build/transfer-x64 --target fixed_order_tests
ctest --test-dir build/transfer-x64 -R '^fixed_order_seed0_reference$' --output-on-failure
```

### Permutation screening experiment

`permutation_screen` builds a shared table of strict transfer evidence from the instance. It scores ESA's reference order, one-move swap, insertion, and reversal neighbors, and randomly shuffled orders. The example uses seed `0`, a shared cap of `150528` orbital calls, `1000` trials per comparison group, and an eight-day sampled flight cap. Scores compare witnessed connections and local timing. Full feasibility and mission duration require a timed solution and strict evaluation. [Method and results](docs/optimization-literature-review.md#phase-6b-permutation-screening)

```powershell
cmake --build build/transfer-x64 --target permutation_screen permutation_screen_tests
./build/transfer-x64/permutation-screen/permutation_screen.exe data/easy.kttsp data/easy.order 0 150528 1000 8
ctest --test-dir build/transfer-x64 -R '^permutation_screen_' --output-on-failure
```

### Directed fragment assembly experiment

`fragment_assemble` constructs complete orders from directed pair evidence. Its assembler starts with singleton fragments and can merge, insert, or split them within a fixed attempt and repair budget. Pass `-` in place of the reference order to omit the ESA-derived comparison groups. In this example, `16` caps repair splits per attempt, `0` disables unknown joins, `8` is the sampling flight cap in days, and `5` prints up to five unique shortlisted orders for separate scheduling. [Phase 6C design, results, and scheduling commands](docs/optimization-literature-review.md#phase-6c-directed-fragment-assembly)

```powershell
cmake --build build/transfer-x64 --target fragment_assemble fragment_assembly_tests
./build/transfer-x64/fragment-assembly/fragment_assemble.exe data/easy.kttsp data/easy.order 0 1204224 1000 16 0 8 5
ctest --test-dir build/transfer-x64 -R '^fragment_assembly_' --output-on-failure
```

<details>
<summary>Preparing a PyKEP v2.6 package from source</summary>

Before configuring TransferSolver, prepare the Boost headers and static libraries with the same UCRT64 compiler. The verified PyKEP build used tag `v2.6` at commit `23060c3574fcccc178c1db86705c7c3eff2b83e7` with Python bindings, SPICE, and upstream tests disabled. It omitted `src/planet/tle.cpp` and the seven bundled SGP4 implementation files. To reproduce that reduced build, work in a separate PyKEP source copy. Remove the `src/planet/tle.cpp` entry from its `KEP_TOOLBOX_SRC_FILES` list and remove the `LIBSGP4_SRC_FILES` list from the `keplerian_toolbox` target's sources. Keep the two-body propagation and Lambert sources unchanged. [PyKEP v2.6 build file](https://github.com/esa/pykep/blob/v2.6/CMakeLists.txt) · [Boost Windows build guide](https://www.boost.org/doc/libs/1_74_0/more/getting_started/windows.html)

Use the same PowerShell variables defined above. After installing into the local prefix, run the TransferSolver configure, build, and test commands.

```powershell
$pykepSource = '<path-to-PyKEP-v2.6-source-copy>'
$pykepOptions = @(
    "-DCMAKE_C_COMPILER:FILEPATH=$ucrt64/bin/gcc.exe"
    "-DCMAKE_INSTALL_PREFIX:PATH=$pykepPrefix"
    '-DCMAKE_POLICY_VERSION_MINIMUM=3.5'
    '-DPYKEP_BUILD_KEP_TOOLBOX=ON'
    '-DPYKEP_BUILD_PYKEP=OFF'
    '-DPYKEP_BUILD_SPICE=OFF'
    '-DPYKEP_BUILD_TESTS=OFF'
)
cmake -S $pykepSource -B build/pykep-v2.6-x64 @ucrtToolchain @boostOptions @pykepOptions
cmake --build build/pykep-v2.6-x64
cmake --install build/pykep-v2.6-x64
```

</details>
