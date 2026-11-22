# Phase 3A orbital transfer design

Use ESA’s PyKEP v2.6 C++ core as the first CPU backend and the matching Python API as a reference for numerical tests. Wrap both orbit propagation and Lambert solving. Matching just the Lambert solver would leave the initial orbital phase and propagation convention unverified. [ESA challenge][spec] · [PyKEP v2.6 source][v2]

## How ESA computes one transfer

1. **Initialize the two-body orbits.** Each file row supplies the semi-major axis in meters, eccentricity, and four angles in radians. The code creates a Keplerian orbit around the Moon with a reference epoch of `t0` in MJD2000 days. It uses `μ = 4.9048695 × 10¹² m³/s²` for propagation and Lambert solving. PyKEP v2.6 reads the sixth element as initial mean anomaly `M0`. For an elapsed time `Δt = (epochDays − t0) × 86400` seconds, `M = M0 + sqrt(μ/a³) × Δt`. The element-based path solves Kepler’s equation `M = E − e sin(E)` for eccentric anomaly `E` and converts the elements to Cartesian position and velocity. The v2.6 implementation instead propagates the initial Cartesian state with its Lagrangian routine when `e ≤ 0.001` or `i ≤ 0.001`, using the same two-body model. [Challenge utility loader][utility-load] · [PyKEP v2.6 propagation][v2-orbit]
2. **Compute the endpoint states.** Evaluate the departure tomato at `departureDays` to obtain `(rFrom, vFrom)` and the destination at `departureDays + flightDays` to obtain `(rTo, vTo)`. Positions are in meters and velocities in meters per second in the common Moon-centered inertial frame. Pass the two positions, `flightDays × 86400` seconds, and `μ` to Lambert. Lambert’s problem finds the transfer velocities at the two endpoints for the prescribed flight time. [Challenge utility transfer][utility-transfer] · [Izzo’s Lambert paper][izzo]
3. **Search both directions and all returned branches.** The utility calls Lambert with `cw=false` and `cw=true`. These request prograde and retrograde motion relative to the reference frame’s positive z-axis. The default cap is 20 complete revolutions per direction. PyKEP determines how many are possible for the geometry and duration and returns one zero-revolution solution and two branches per available positive revolution count up to the cap. A failure in one direction is skipped. [Challenge utility transfer][utility-transfer] · [PyKEP v2.6 Lambert solver][v2-lambert]
4. **Minimize the sum of two velocity changes.** For each paired Lambert result `(vDepart, vArrive)`, compute `‖vDepart − vFrom‖₂ + ‖vTo − vArrive‖₂` in m/s. The first burn leaves the departure orbit and the second matches the arrival tomato’s velocity. Return the lowest sum across both directions and all branches. The utility’s initial value stays at positive infinity if no candidate improves it. [Challenge utility transfer][utility-transfer] · [Published Large evaluator][evaluator]

## Differences in the published files

| Item | Finding and design decision |
| --- | --- |
| Header and units | The statement, the three instance files, and the published Large evaluator use eight tokens, including the normal and exception Δv limits and the exception count. Their minimum flight duration is in days. The utility expects six tokens, leaves out the exception fields, and divides its minimum by 86400 as if the field were seconds. Its loader therefore rejects the official headers. Keep the project’s Phase 1 parser and use the evaluator’s units. [Specification][spec] · [Official instances][instances] · [Utility loader][utility-load] · [Large evaluator][evaluator] |
| Anomaly and epoch | The README calls the last field true anomaly at time zero. The utility and Large evaluator name it accordingly, then pass the raw number to the v2 Keplerian constructor at `t0`. That constructor uses mean anomaly. For scorer parity, pass the raw field through as `M0` at `problem.startTime` and document this convention in the adapter. All three published instances have `t0 = 0`, so test a synthetic nonzero epoch too. [Specification][spec] · [Official instances][instances] · [Large evaluator][evaluator] · [v2.6 constructor][v2-orbit] |
| Limits and scoring | The README’s sample maximum is 150 days. The released easy file uses 200. The Large evaluator bounds each departure and flight duration, with a departure lower bound of zero. It scores the final arrival without a separate deadline check in `fitness()`. It allows a `1e-6` day chronology margin and a `1e-6` m/s margin at the exception ceiling. The utility’s `find_transfer()` uses a strict comparison to a single threshold. Keep Phase 2’s start, chronology, and deadline checks. Keep Δv exception policy at the route level. [Specification][spec] · [Easy instance][easy] · [Utility helper][utility-transfer] · [Large evaluator][evaluator] |
| Physical scope | The inspected transfer code uses a point-mass Moon and two instantaneous burns. It applies no lunar-surface clearance check to the Lambert arcs. Capture the scoring behavior in parity tests and document any additional physical constraints if a later phase introduces them. [Challenge utility transfer][utility-transfer] · [Large evaluator][evaluator] |

The challenge README requests PyKEP 2.x. The published Large evaluator uses the v2 API and does not specify the exact patch version of the grader’s runtime. Use the v2.6 tag as a reproducible local reference and record the actual reference build used to generate fixtures. [Challenge utilities guidance][spec] · [ESA competition discussion][version-discussion] · [PyKEP v2.6 tag][v2]

## C++ backend options

| Option | Numerical compatibility | Windows, dependencies, and license | Effort and later CUDA use |
| --- | --- | --- | --- |
| **PyKEP v2.6 C++ core** | Closest match for both propagation and Izzo’s multi-revolution solver. Verify against Python fixtures before assuming agreement across compilers. | C++11 baseline with MinGW-specific build rules. The C++ target links Boost.DateTime and Boost.Serialization. Python bindings and SPICE can be disabled. Use a matching compiler and architecture for linked libraries. The tag ships GPLv3 license text. [CMake build][v2-cmake] · [Boost discovery][v2-boost] · [License][v2-license] | Small adapter after the build is working. Use the host implementation as a numerical reference for any future GPU backend. |
| **kep3 / PyKEP 3.x** | Also uses Izzo’s approach but changes the orbit API and propagation path. It supports explicit mean-anomaly input through `KEP_M`. Its host Lambert endpoint getters are `get_v0()` and `get_v1()`, compared with v2’s `get_v1()` and `get_v2()`. Numerical parity needs separate tests. [Orbit API][v3-orbit] · [Lambert source][v3-lambert] | The inspected C++ target requires C++23 and uses Boost, fmt, heyoka, spdlog, xtensor and xtensor-blas. Windows CI uses Visual Studio 2022 x64 with conda dependencies. The Lambert source is MPL-2.0. [Build][v3-cmake] · [Windows CI][v3-ci] · [Source license notice][v3-lambert] | More integration work for the current C++17 project. Useful as an independent CPU comparison after establishing v2 parity. |
| **Basilisk C++ Lambert module** | Provides Gooding and Izzo for a specified revolution count. Its validity rules and near-collinear rejection require separate parity analysis. [Module documentation][basilisk-doc] | ISC-licensed module coupled to Basilisk messaging and Eigen. A MinGW integration would require adapting that interface and verifying the build. [C++ module][basilisk-source] | Useful as a cross-check. Branch and direction enumeration, propagation, and adapter work are still needed. |

Phase 3B linked PyKEP v2.6 to a C++17 probe with the existing 32-bit MinGW GCC 6.3.0. CMake 4 needed the policy override for the legacy minimum, and the bundled SGP4/TLE sources needed to be omitted because this MinGW lacks `clock_gettime`. The two-body propagation and Lambert sources stayed identical to the pinned upstream archive. Keep license notices with any redistributed upstream code. [PyKEP build][v2-cmake] · [PyKEP license][v2-license] · [CMake policy override][cmake-policy]

## Proposed C++17 interface

```cpp
#include <cstddef>

class Problem;

enum class TransferStatus { Ok, InvalidInput, NoFiniteCandidate, NumericalFailure };

struct TransferResult
{
    TransferStatus status;
    double deltaVMps;
};

TransferResult evaluateTransfer(const Problem &problem,
                                std::size_t from, std::size_t to,
                                double departureDays, double flightDays);
```

Indices follow file order. Check index bounds, finite departure and arrival times, positive flight duration, and supported elliptic orbit inputs. Return a finite cost on success and positive infinity with an explicit status otherwise. Use `NumericalFailure` when propagation fails and `NoFiniteCandidate` when the Lambert search yields no usable result. Use fixed-size `std::array<double, 3>` vectors in SI to test propagation, Lambert solving, and Δv reduction independently.

## Tests and implementation order

1. **Make a reference fixture set.** Use the pinned v2.6 Python API with the published evaluator’s loader or the same transfer loop fed by correctly loaded data. Record the instance hash, backend version, endpoint states, branch results for selected cases, and final Δv. Include real easy-instance transfers and synthetic orbits with a nonzero `t0` and an eccentric case that distinguishes mean from true anomaly.
2. **Verify the Windows build and wrap the CPU core.** In a separate build, set `PYKEP_BUILD_KEP_TOOLBOX=ON`, `PYKEP_BUILD_PYKEP=OFF`, and `PYKEP_BUILD_SPICE=OFF`. Keep PyKEP behind the C++ adapter and use Python and NumPy for fixture generation. Compare positions and velocities at the reference epoch and at later times, including the near-circular and low-inclination path. Preserve the existing parser and solution tests. [PyKEP build flags][v2-cmake]
3. **Validate the branch search.** Cover a case where each direction wins, a positive-revolution winner, the revolution cap, near-collinear and degenerate geometry, invalid orbit data, invalid indices or durations, and the no-finite-result path. Independently propagate selected Lambert arcs to verify the arrival position. Set absolute and relative comparison tolerances from measured double-precision differences and check classification around 100 and 600 m/s independently of solver error. Document numerical differences before calling the backend reference-compatible.
4. **Profile before planning a GPU port.** Benchmark batches of independent leg queries and measure propagation, Lambert solving, and branch reduction separately. If transfers dominate, preserve the CPU result as the reference and introduce a batched backend later. Fixed-size vectors give that backend a simple data interface. Check FP64 results and branch coverage against the same fixtures before considering faster math or reducing the search.

## Verified Windows environment

| Component | Finding |
| --- | --- |
| OS and GPU | Windows 11 Home x64 (build 26200). `nvidia-smi` reported an NVIDIA GeForce RTX 4050 Laptop GPU, driver 610.60, compute capability 8.9, and 6141 MiB of VRAM. [NVIDIA compute capabilities][cuda-gpus] |
| CUDA and MSVC | The driver reports CUDA UMD 13.3. No `nvcc`, CUDA Toolkit installation, or usable Visual Studio/MSVC C++ installation was found on PATH or in the Windows installation locations checked. The reported CUDA version describes the driver. |
| CPU compilers and build | The default is MinGW GCC 6.3.0 (x86). RubyInstaller also ships MSYS2 UCRT64 GCC 16.1.0 at `C:/Ruby33-x64/msys64/ucrt64/bin/g++.exe`. A separate CMake 4.4.4/Ninja 1.13.2 build produced and loaded a 64-bit PyKEP v2.6 DLL with Boost 1.74.0, using the same SGP4/TLE omission as the x86 build. |
| OpenGL | A standalone UCRT64 C++17 probe linked and loaded `OPENGL32.dll`. OpenGL context creation and rendering remain untested. |

For a shared native Windows build with CUDA and OpenGL, use MSVC x64, a Windows SDK, and a matched CUDA Toolkit release. CUDA 13.3 is a candidate for this driver and GPU. Its support table lists x64 MSVC 19.3x with Visual Studio 2022 and x64 MSVC 19.5x with Visual Studio 2026. Confirm the exact host compiler against the selected Toolkit before installing it. Use a separate CMake build directory, set CUDA architecture `89` for this GPU, and build all C++ dependencies for the selected compiler ABI. An OpenGL target can link against CMake’s `OpenGL::GL`. The tested UCRT64 compiler is available for CPU and OpenGL work. NVIDIA’s native Windows CUDA support table lists MSVC host compilers. [NVIDIA Windows guide][cuda-windows] · [CUDA release notes][cuda-release] · [MSVC Build Tools][msvc-tools] · [CMake OpenGL][cmake-gl]

## Numerical verification

The standalone probe used the same PyKEP v2.6 sources (`23060c3`), Moon constant and day-to-second conversion on both Windows architectures. Nine deterministic queries exercised the ESA minimum flight duration, a five-day multi-revolution transfer and the same query capped at zero revolutions, a reversed ESA pair, a synthetic eccentric orbit with `t0=7.25`, analytic circular transfers in both directions, a 30-second near-collinear transfer, and zero duration. The inputs are in `build/phase3c/inputs.txt` with SHA-256 `e3e64dd7f7696d005dffe62cf9020685cd4178457ce7fd97847b5d020934fa5e`.

### Python scorer parity

Micromamba 2.3.0 created a conda-forge win-64 environment under the ignored `build/phase3d/py` prefix. The system Python was untouched. The environment used CPython **3.11.18** (`hb12b558_0_cpython`), PyKEP **2.6.4** (`py311hd1aeffc_3`), NumPy **1.26.4** (`py311h0b4df5a_0`), SciPy **1.17.1**, Boost and Boost.Python **1.86.0**, OpenBLAS **0.3.34**, and mpmath **1.3.0**. An initial MKL download stalled, so the working environment pins OpenBLAS. The 80-package explicit lock passed an offline Micromamba dry run. [Micromamba installation][micromamba] · [PyKEP 2.6.4 package metadata][pykep-conda]

The generator imported the unmodified published Large evaluator, using the pinned official Large instance for its import-time initialization. It then fed two-element case files from the nine saved inputs into that same evaluator class. This preserved the raw sixth orbital element as mean anomaly at the file epoch, the Moon’s `μ = 4.9048695e12 m³/s²`, the day-to-second conversion, and the search over both Lambert directions. The minimum of the captured candidate costs matched the evaluator’s own `compute_transfer()` exactly for every case. Two runs produced the same fixture SHA-256 `0c552f12291887999161687283bdd12bd122093093296f8d61e3fa8e508f1a12`. The imported evaluator hash was `b89fb126c3987659a7bf4b3c3e9c55266e752018b920ffe333a4c5cba128e535`. [Published ESA evaluator][evaluator] · [Official Large instance][hard]

Both C++ probes reproduced their original output, then matched Python on all **101 records per build**. This covered 18 endpoint states, 18 direction outcomes, 28 pairs of Lambert velocities and their 28 Δv costs, and nine minima. The component-wise comparison used the larger of the absolute tolerance (`1e-4 m` for position or `1e-6 m/s` for velocity and Δv) and `1e-11` times the larger magnitude. Branch counts, identities, and winners had to agree exactly.

| Maximum absolute difference | MinGW x86 vs Python | UCRT64 x64 vs Python |
| --- | ---: | ---: |
| Propagated position components | 2.4680e-8 m | 0 m |
| Propagated velocity components | 1.0516e-12 m/s | 0 m/s |
| Lambert endpoint velocity components | 7.0809e-8 m/s | 0 m/s |
| Candidate and minimum Δv | 6.4479e-8 m/s | 0 m/s |

The source comparison found identical Keplerian propagation in tags 2.6 and 2.6.4, plus Lambert changes limited to sizing casts and loop-index typing. The observed x86 discrepancies are consistent with floating-point rounding and did not alter any branch decision. The comparison covers these nine cases and the pinned local package. The published evaluator does not identify the deployed grader’s exact PyKEP patch build. [Upstream source comparison][v2-comparison] · [Published ESA evaluator][evaluator]

The analytic circular cases matched within the set bounds of `1e-5 m` and `1e-8 m/s` in Euclidean norm and produced near-zero Δv in the expected directions. Zero duration yielded no candidate in either direction. The five-day ESA query produced five candidates per direction and a minimum of **417.004739 m/s**. A zero-revolution cap raised the minimum to **635.968260 m/s**, above the 600 m/s exception ceiling for this instance.

Repropagating each finite candidate with the upstream Lagrangian routine was a separate closure diagnostic. Both builds passed **26 of 28** candidates at `0.1 m` position and `1e-4 m/s` velocity. The same two nonwinning retrograde candidates missed those limits:

| Case | x86 residual | x64 residual |
| --- | ---: | ---: |
| ESA minimum duration | 2.42 m, 0.462 m/s | 5.54 m, 0.0769 m/s |
| Near-collinear, 30 s | 4.14 km, 337.44 m/s | 2.20 km, 270.11 m/s |

### Closure failure diagnosis

An independent two-body check used a bracketed universal-anomaly solve with Stumpff functions at 80 decimal digits. It used the exact binary64 departure state and Lambert velocity recovered from each saved probe row. It propagated to the requested arrival time and compared both the position and velocity with the Lambert endpoints. The check also verified specific energy and angular momentum conservation. Repeating the two flagged branches at 110 digits left the position and velocity residuals unchanged to within `1e-40` in SI. [NASA universal-formulation reference][universal-kepler]

| Previously flagged retrograde candidate | x86 high-precision residual | x64 and Python high-precision residual | Point-mass periapsis distance |
| --- | ---: | ---: | ---: |
| ESA minimum duration, 86.4 s | 17.3 µm, 9.95e-7 m/s | 26.1 µm, 1.51e-6 m/s | 76.9 m |
| Near-collinear, 30 s | 45.5 µm, 3.03e-6 m/s | 0.501 mm, 3.34e-5 m/s | 6.09 µm |

All 28 candidates closed in each C++ dataset and in Python at the original `0.1 m` and `1e-4 m/s` limits. Evaluating the two Python Lambert roots at high precision gave time-of-flight residuals of `+1.89e-14 s` and `−2.55e-15 s`, respectively. The large misses came from numerical sensitivity in the legacy double-precision hyperbolic repropagation check. Both flagged branches pass very close to the point-mass center. In the legacy radius expression, two large terms of opposite sign nearly cancel. Their combined magnitude relative to the resulting radius corresponds to about 35 and 42 bits of cancellation, respectively. The result is especially sensitive to rounding in the radius and in the anomaly solve. The independent check used the same gravity model and left the original candidates and winning Δv unchanged. [Legacy PyKEP repropagation code][v2-lagrangian]

Both periapsides are inside the physical Moon. They are mathematical solutions in ESA’s point-mass model, which applies no terrain or surface-clearance constraint. Keep these examples as diagnostic cases and preserve ESA’s candidate selection rule when comparing scores. [ESA evaluator][evaluator]

The ignored `build/phase3d/` directory contains `commands.txt`, the Win64 environment lock and package checksums, `reference.csv`, and the Python comparison and high-precision closure logs. Replay `reference.py`, `compare.py`, and `closure.py` through `micromamba run -p` using the pinned environment. The replay uses the recorded inputs and C++ outputs in `build/phase3c/`. Keep the two retrograde cases in future regressions and use the independent closure check for near-radial arcs.

### Δv boundary verification

The written challenge and the published Large evaluator apply two distinct rules to a finite, non-negative minimum Δv. Both count an accepted leg above 100 m/s against the route’s exception budget. The test instances allow at most five such legs. [Written challenge][spec] · [Published Large evaluator][evaluator]

| Rule | Normal leg | Uses one exception | Infeasible leg |
| --- | --- | --- | --- |
| Strict written limits | Δv ≤ 100 | 100 < Δv ≤ 600 | Δv > 600 |
| Published Large evaluator | Δv ≤ 100 | 100 < Δv ≤ `600.0 + 1e-6` | Δv > `600.0 + 1e-6` |

Apply these inequalities to the raw binary64 cost. The `1e-6 m/s` allowance comes from the published evaluator and applies only to its exception ceiling. The numerical comparison tolerances above serve only to compare Python and C++ outputs. Any future route validator should name the rule it applies. The transfer adapter should return the raw cost so either rule can be tested separately.

The threshold harness reused the pinned PyKEP 2.6.4 Python environment, unmodified ESA evaluator, and existing UCRT64 x64 probe. All cases departed at day 0.125 and used the released easy instance’s orbital elements in their original epoch and anomaly convention. For each cutoff, a binary64 flight-time search found adjacent representable flight times that straddled it and two additional cases about `1e-7 m/s` to either side. The 100 m/s cases used tomato 1 → 48, and both upper-ceiling groups used 0 → 1. [Easy instance][easy] · [Published Large evaluator][evaluator]

| Cutoff (m/s) | Minimum Δv at the adjacent flight time on the lower-cost side (m/s) | Minimum Δv at the adjacent flight time on the higher-cost side (m/s) |
| --- | ---: | ---: |
| 100 | 99.999999999999801 | 100.00000000000001 |
| 600 | 599.99999999999977 | 600.00000000000159 |
| `600.0 + 1e-6` | 600.00000099999897 | 600.00000100000057 |

**Python and C++ matched on all 348 records across 12 transfers.** The maximum observed differences were `0 m` for positions and `0 m/s` for orbital velocities, Lambert endpoint velocities, candidate Δv, and minimum Δv. Direction counts and winning branches matched, and each rule classified all 12 raw C++ minima the same way as its Python counterparts. All 144 C++ candidates also passed the earlier closure limits of `0.1 m` and `1e-4 m/s` in the existing probe.

Nine additional policy cases used each exact cutoff and its immediately adjacent binary64 values. They injected a known cost into the unchanged evaluator’s `fitness()` to isolate its classification. Exactly 100 m/s stayed normal, exactly 600 m/s used one exception under both rules, and the next representable value above 600 m/s used one exception only under the published evaluator. Equality at its extended ceiling used one exception and the next value was infeasible. An independent budget check returned an exception constraint of 0 for five exceptions and +1 for six. [Published Large evaluator][evaluator]

A second run reproduced the five input, reference, backend-output, and policy artifacts byte for byte. The files and `thresholds.py` are in the ignored `build/phase3e/` directory. Run the script using the same `micromamba run -p` environment and thread settings as Phase 3D. These cases leave no numerical blocker for integrating the pinned x64 cost backend. The route-scoring layer still needs an explicit choice of strict written limits or published scorer behavior. Keep both classifications in regression tests when validating a different compiler or backend.

[spec]: https://github.com/esa/SpOC4/blob/e4276399b51852e6f1c08c2d0c133bd13f8f967a/Challenge%202%20Keplerian%20Tomato%20Traveling%20Salesperson%20Problem/README.md
[instances]: https://github.com/esa/SpOC4/tree/e4276399b51852e6f1c08c2d0c133bd13f8f967a/Challenge%202%20Keplerian%20Tomato%20Traveling%20Salesperson%20Problem/problems
[easy]: https://github.com/esa/SpOC4/blob/e4276399b51852e6f1c08c2d0c133bd13f8f967a/Challenge%202%20Keplerian%20Tomato%20Traveling%20Salesperson%20Problem/problems/easy.kttsp
[utility-load]: https://github.com/esa/SpOC4/blob/e4276399b51852e6f1c08c2d0c133bd13f8f967a/Challenge%202%20Keplerian%20Tomato%20Traveling%20Salesperson%20Problem/utils_users.py#L6-L67
[utility-transfer]: https://github.com/esa/SpOC4/blob/e4276399b51852e6f1c08c2d0c133bd13f8f967a/Challenge%202%20Keplerian%20Tomato%20Traveling%20Salesperson%20Problem/utils_users.py#L101-L151
[evaluator]: https://api.optimize.esa.int/media/problems/spoc-4-keplerian-tomato-traveling-salesperson-large-1782204009564.py
[version-discussion]: https://github.com/esa/SpOC4/discussions/3
[izzo]: https://www.esa.int/gsp/ACT/doc/MAD/pub/ACT-RPR-MAD-2014-RevisitingLambertProblem.pdf
[v2]: https://github.com/esa/pykep/tree/v2.6
[v2-orbit]: https://github.com/esa/pykep/blob/v2.6/src/planet/keplerian.cpp#L44-L115
[v2-lambert]: https://github.com/esa/pykep/blob/v2.6/src/lambert_problem.cpp
[v2-cmake]: https://github.com/esa/pykep/blob/v2.6/CMakeLists.txt
[v2-boost]: https://github.com/esa/pykep/blob/v2.6/cmake_modules/PykepFindBoost.cmake
[v2-license]: https://github.com/esa/pykep/blob/v2.6/LICENSE.txt
[v3-orbit]: https://github.com/esa/pykep/blob/d7ed41b791c6d1cca099e74392bee1f3d001db73/src/udpla/keplerian.cpp#L55-L108
[v3-lambert]: https://github.com/esa/pykep/blob/d7ed41b791c6d1cca099e74392bee1f3d001db73/src/lambert_problem.cpp
[v3-cmake]: https://github.com/esa/pykep/blob/d7ed41b791c6d1cca099e74392bee1f3d001db73/CMakeLists.txt
[v3-ci]: https://github.com/esa/pykep/blob/d7ed41b791c6d1cca099e74392bee1f3d001db73/.github/workflows/ci-cpp.yml#L117-L140
[basilisk-doc]: https://hanspeterschaub.info/bskOlderDocs/bsk_2_7_35/Documentation/fswAlgorithms/orbitControl/lambertSolver/lambertSolver.html
[basilisk-source]: https://github.com/AVSLab/basilisk/blob/develop/src/fswAlgorithms/orbitControl/lambertSolver/lambertSolver.h
[cmake-policy]: https://cmake.org/cmake/help/v4.0/variable/CMAKE_POLICY_VERSION_MINIMUM.html
[cuda-gpus]: https://developer.nvidia.com/cuda/gpus
[cuda-windows]: https://docs.nvidia.com/cuda/archive/13.3.0/cuda-installation-guide-microsoft-windows/index.html#system-requirements
[cuda-release]: https://docs.nvidia.com/cuda/archive/13.3.1/cuda-toolkit-release-notes/
[msvc-tools]: https://learn.microsoft.com/en-us/visualstudio/install/workload-component-id-vs-build-tools?view=visualstudio
[cmake-gl]: https://cmake.org/cmake/help/latest/module/FindOpenGL.html
[pykep-conda]: https://anaconda.org/conda-forge/pykep/files/modal/info/675ab1fbdcf1a09aba2d87e8
[universal-kepler]: https://ntrs.nasa.gov/api/citations/19670018313/downloads/19670018313.pdf
[v2-lagrangian]: https://github.com/esa/pykep/blob/v2.6/include/keplerian_toolbox/core_functions/propagate_lagrangian.hpp#L94-L122
[hard]: https://github.com/esa/SpOC4/blob/e4276399b51852e6f1c08c2d0c133bd13f8f967a/Challenge%202%20Keplerian%20Tomato%20Traveling%20Salesperson%20Problem/problems/hard.kttsp
[micromamba]: https://mamba.readthedocs.io/en/stable/installation/micromamba-installation.html
[v2-comparison]: https://github.com/esa/pykep/compare/v2.6...v2.6.4
