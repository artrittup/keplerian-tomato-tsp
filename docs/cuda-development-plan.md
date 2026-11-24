# Phase 8A CUDA development plan

## Verified starting point

The repository was clean on `main` at `5987c14`. The root CMake project uses C++17 and enables only the C++ language. `KTTSP_ENABLE_TRANSFER_SOLVER=ON` builds the CPU backend against the exact PyKEP v2.6 C++ package. The independently assembled ESA easy solution remains the reference at **177.19440349732923 days**, with **five exceptions** and **5,204,320 orbital calls**. In the Phase 7C diagnostic run, Lambert work took **67.434 of 83.361 seconds** in scheduling and ephemeris propagation took **3.074 seconds**. Start with the batch evaluator and preserve fragment assembly, the scheduler, the exact cache, and the strict CPU validator. [CPU baseline](cpu-baseline.md) · [CPU profiling](cpu-benchmarking.md)

| Prerequisite | Observed on Windows |
| --- | --- |
| OS | Windows 11 Home 25H2 x64, build 26200.9974. [Microsoft release history](https://learn.microsoft.com/en-us/windows/release-health/windows11-release-information) |
| GPU and driver | RTX 4050 Laptop GPU, compute capability 8.9, 6,141 MiB, WDDM, NVIDIA driver 610.60. `nvidia-smi` reports CUDA UMD 13.3. Direct `nvcuda.dll` Driver API calls reported `cuInit(0) = 0`, driver API version `13030`, and one device. The UMD version describes the driver. [NVIDIA SMI documentation](https://docs.nvidia.com/deploy/nvidia-smi/index.html#cuda-umd-version) |
| CUDA Toolkit | `nvcc` was absent from PATH. The standard CUDA Toolkit installation directory and `CUDA_PATH` were absent. Device enumeration therefore verifies the driver only. |
| MSVC and SDK | `cl.exe` and `vswhere.exe` were absent from PATH and standard locations checked. A Visual Studio 2022 directory exists, with no usable C++ compiler or developer command script found there. Windows Kits had `UnionMetadata` but no versioned SDK headers or x64 libraries in the locations checked. |
| Existing build tools | CMake and CTest 4.4.4. Ninja 1.13.2 is used by the existing CMake builds and is outside shell PATH. The CPU Release builds use UCRT64 GCC 16.1.0 x64 with PyKEP and MinGW GCC 6.3.0 x86 for the dependency-free configuration. The Release flags are `-O3 -DNDEBUG`. |

## Minimum installation and smoke test

Use **CUDA Toolkit 13.3 Update 1 (13.3.1)** with **Visual Studio Build Tools 2026 (18.x), MSVC v14.50 x64 (`cl` 19.50.x)** and **Windows 11 SDK 10.0.26100**. NVIDIA's versioned CUDA 13.3 Windows table lists Windows 11 25H2 and native x64 Visual Studio 2026 with MSVC 19.5x and C++17. Microsoft lists 14.50/19.50 as a long-term toolset. This pins the compiler independently of the Build Tools bootstrapper's current channel version. Driver 610.60 already initializes this GPU and reports CUDA 13.3 support. Reuse it for the first smoke test and compile native `sm_89` code. The toolkit and Windows display driver have separate installers for this release. [NVIDIA Windows compiler support][windows] · [CUDA 13.3.1 release notes][release] · [NVIDIA driver compatibility][compatibility] · [Microsoft MSVC versioning][msvc-version]

After installation approval, use these selections.

1. Get the **Build Tools** bootstrapper from Microsoft's Visual Studio 2026 release history. Select the **Desktop development with C++** build-tools workload and the two pinned components below. Leave recommended and optional bundles unchecked except for those two components. Reuse the existing standalone CMake and Ninja. MFC, ATL, C++/CLI, ARM builds, the IDE, vcpkg, and .NET desktop or web workloads can be omitted. The workload still installs its required core dependencies. [Build Tools bootstrapper and channel][vs-download] · [Microsoft component IDs][vs-components] · [Pinning a specific MSVC toolset][msvc-acquire]

   ```powershell
   .\vs_BuildTools.exe --wait --passive --norestart `
       --add Microsoft.VisualStudio.Workload.VCTools `
       --add Microsoft.VisualStudio.Component.VC.14.50.18.0.x86.x64 `
       --add Microsoft.VisualStudio.Component.Windows11SDK.26100
   ```

2. Get the **Windows x86_64 CUDA 13.3.1** installer from NVIDIA's archive. Use Custom selection for the compiler and runtime development pieces. The documented core package IDs are `nvcc_13.3`, `nvvm_13.3`, `crt_13.3`, and `cudart_13.3`. Keep any dependencies the installer selects for them. The result must provide `nvcc`, its device compilation tools, CUDA headers, and the x64 CUDA runtime link library and DLL. Start with those pieces. Visual Studio integration, Nsight VSE, Nsight Compute, Nsight Systems, cuBLAS, cuFFT, cuDNN, and the other numerical libraries can be deferred. Ninja builds use the command-line tools. [Toolkit download][cuda-download] · [Windows installer package list][windows]

3. Use a separate **x64 Developer Command Prompt** for CUDA. Replace the Build Tools installation placeholder, then verify the compiler and SDK the process actually resolves. Use the existing Ninja executable or pass its absolute path to CMake through `CMAKE_MAKE_PROGRAM` if it is outside PATH. Keep the existing MinGW CPU presets and build directories for the verification oracle. [Microsoft command-line build environment][vcvars]

   ```bat
   call "<Build Tools install>\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50
   where cl.exe
   cl
   set VCToolsVersion
   set WindowsSDKVersion
   where nvcc.exe
   nvcc --version
   nvcc --list-gpu-code | findstr /c:"sm_89"
   cmake --version
   ```

4. For the smoke test, use NVIDIA CUDA Samples **v13.3** in an ignored build directory. Build and run `deviceQuery` to load the CUDA runtime and check the device properties. Build and run the upstream `vectorAdd` sample as an actual device launch and readback. The commands below assume a checkout at `build\cuda-samples` and run in the x64 environment above. Expect the RTX 4050, capability 8.9, and a pass from both programs. Save the tool versions and output. [NVIDIA deviceQuery source][device-query] · [NVIDIA vectorAdd source][vector-add] · [NVCC options][nvcc]

   ```bat
   git clone --depth 1 --branch v13.3 https://github.com/NVIDIA/cuda-samples.git build\cuda-samples
   mkdir build\phase8b
   nvcc -std=c++17 -gencode arch=compute_89,code=sm_89 -I build\cuda-samples\Common build\cuda-samples\cpp\1_Utilities\deviceQuery\deviceQuery.cpp -o build\phase8b\deviceQuery.exe
   nvcc -std=c++17 -gencode arch=compute_89,code=sm_89 -I build\cuda-samples\Common build\cuda-samples\cpp\0_Introduction\vectorAdd\vectorAdd.cu -o build\phase8b\vectorAdd.exe
   build\phase8b\deviceQuery.exe
   build\phase8b\vectorAdd.exe
   ```

The first project-specific CUDA build should also be standalone, with its own Ninja build directory. Set C++ and CUDA standards to 17 and target `CMAKE_CUDA_ARCHITECTURES=89-real` for native Ada code. With Ninja, pin NVCC and the resolved x64 `cl.exe` using `CMAKE_CUDA_COMPILER` and `CMAKE_CUDA_HOST_COMPILER` at first configuration. When CUDA is eventually wired into the root project, enable the language only behind an opt-in switch and retain the current CPU-only configurations. [CMake CUDA architectures][architectures] · [CMake CUDA host compiler][host-compiler]

The standalone GPU tools can exchange byte-defined files with the current MinGW CPU oracle. Linking the two C++ backends into one MSVC executable later requires rebuilding PyKEP v2.6 and Boost Date_Time and Serialization for the same MSVC x64 ABI and CRT configuration, then reproducing the CPU reference suite on that build. Reuse the existing PyKEP source and build notes as a numerical baseline. If that rebuild remains blocked, keep the CPU oracle in a separate executable through comparison. A DLL boundary would need a deliberately specified C ABI and memory ownership before integration. [CPU dependency setup](../README.md#windows-x64-build-with-transfersolver) · [PyKEP build research](orbital-transfer-design.md)

## Workload and numerical implementation

`TransferSolver::evaluate` propagates the source to the absolute departure epoch and the destination to `departure + flightDays`. The sixth input element is used as mean anomaly at `Problem::startTime`, as in the current evaluator. Keep the two-body Moon parameter `4.9048695e12 m³/s²`, the conversion `86400 s/day`, meters for position, and m/s for velocity and Δv. Each query minimizes the sum of the departure and arrival velocity changes over finite Lambert candidates in both directions. [Current CPU evaluator](../src/TransferSolver.cpp) · [ESA parity verification](orbital-transfer-design.md)

**Propagation first.** Upload the orbit table once for an immutable problem. Develop a small device-compatible arithmetic implementation, starting with FP64 scalars and fixed-size vectors. Compute mean motion and reference state per orbit, then propagate at the requested epochs. PyKEP v2.6 uses a mean-anomaly Kepler solve and orbital-element conversion when both eccentricity and inclination exceed `1e-3`. Otherwise its `eph_impl` takes the Lagrangian path from the reference Cartesian state and uses a Boost root solver. Preserve this behavior or demonstrate numerical equivalence for each path. The general elliptic equations alone are a useful first checkpoint. Support or route the low-eccentricity and low-inclination cases to the CPU until their device implementation passes parity. Use bounded root iterations and explicitly report nonconvergence. [PyKEP v2.6 ephemeris source][pykep-eph] · [PyKEP Lagrangian propagation][pykep-lagrange]

**Lambert in layers.** Implement a portable scalar version of Izzo's formulation from the published equations and check it on a host before compiling its math for device execution. Initially feed it PyKEP endpoint states to isolate Lambert differences. Reproduce the geometry and signed direction conventions, time-of-flight evaluation near the parabolic regime, and the root-to-velocity reconstruction. Add zero-revolution direct and retrograde first, then both roots for each positive revolution count supported by the flight time, with the current cap of 20 per direction. PyKEP searches for the feasible revolution bound and uses Halley/Householder iterations with different time-of-flight expressions in different regimes. Keep reference branch identities `(direction, revolutions, left/right)` and finite-candidate tie order. At the cap there can be 41 candidates per direction. Accumulate the minimum as branches are evaluated. Reserve detailed velocity and root telemetry for diagnostics on selected cases. Iteration bounds and residual failures must produce an explicit CPU retry signal. [Izzo's Lambert paper][izzo] · [PyKEP v2.6 Lambert source][pykep-lambert]

The upstream CPU classes use host containers, allocation, Boost, and exceptions. The development path is a separate device-compatible math implementation with scalar statuses, fixed-size state and bounded control flow. Use PyKEP as the oracle. Review licensing before carrying over upstream source. The pinned source headers contain GPL notices, and the pinned archive includes a GPL license file. Preserve upstream attribution and verify redistribution obligations if a port copies source. [Pinned source notices][pykep-lambert] · [Pinned license][pykep-license]

Start parity with `double` throughout, `--fmad=false`, and the standard precise math path. Leave fast math and mixed precision out of the correctness baseline. Separate CPU and GPU library implementations can still differ in rounding. Ada 8.9 has limited native FP64 throughput, and branch-count and iteration variability can limit speedup. Treat full FP64 speed and the need for additional specialization as measurement questions. [NVCC math options][nvcc] · [NVIDIA floating-point behavior][floating] · [Native arithmetic throughput][practices]

## Batch interface and integration

Use one conceptual interface for both the CPU reference adapter and GPU evaluator. The following signatures describe the boundary for a later C++17 implementation.

```text
prepare(immutable Problem snapshot) -> evaluator context
evaluateBatch(context, queries*, count, results*) -> batch execution status

query  = {uint32 source, uint32 destination, binary64 departureDays, binary64 flightDays}
result = {query status, diagnostic flags, binary64 deltaVMps}
```

Each output slot corresponds to the input at the same index, including duplicates. Keep the status values compatible with `TransferStatus` and distinguish a request for CPU verification from a proven physical failure. Report a batch launch or transfer error separately. Validate index and input conversion at the boundary. The file representation and any cross-toolchain transport should fix field widths, byte order, and encoding explicitly. Serialize individual fields for offline replay. The CPU reference adapter calls the existing `TransferSolver::evaluate` in request order. [Current query/cache interface](../include/TransferCache.hpp) · [Current result interface](../include/TransferSolver.hpp)

Reuse the orbit table and device buffers for multiple batches. Start with a simple one-thread-per-query kernel with sequential branch evaluation and one compact result, followed by explicit H2D and D2H copies. Store the prepared orbits once, and measure whether column arrays or packed query records are better after parity. At 65,536 queries, a 24-byte query and a 16-byte result require about 2.5 MiB combined for one set of buffers, excluding solver workspace and diagnostics. Process the trace in chunks and preserve global order. Consider pinned buffers, overlap, or grouping by expected branch count only after profiling. Any internal grouping must scatter results back to their original indices. With the Windows WDDM driver, start with short launches and reduce chunk size if execution latency grows. [NVIDIA transfer guidance][practices] · [Microsoft WDDM timeout behavior][tdr]

Start with offline trace replay. In the optimizer, the CPU retains route logic, exact cache admission/eviction, classification, call budgets, and the strict final `SolutionEvaluator`. `TransferWindowExplorer` uses a cost-priority queue. Reordering queries whose results guide later proposals can change the schedule. Batch only candidates that are already independent under the existing rules. Submit cache misses and consume results in the original request order. Count speculative work, actual GPU evaluations, and CPU fallbacks explicitly. A fallback must fit the applicable budget. Initially run GPU comparisons in shadow mode. Before authoritative use, protect class changes, ambiguous branches and any close rankings that affect adaptive sampling with CPU verification. Keep candidate entries with uncertain GPU provenance separate from certified CPU cache results. Compare the integrated query trace with the CPU fixture before claiming that behavior is equivalent. [Adaptive explorer](../src/TransferWindowExplorer.cpp) · [Scheduling discovery](../src/FixedOrderScheduler.cpp) · [Final CPU evaluator](../src/SolutionEvaluator.cpp)

## Numerical qualification and benchmarking

1. **Reference stages.** Compare both propagated endpoint positions and velocities against pinned PyKEP on the same binary64 queries. Then compare Lambert with identical CPU-provided endpoints, checking direction, legal revolution count, both roots, velocity vectors, iteration/residual behavior, branch minima, and the final raw Δv. Finally compare the combined device pipeline. Independent propagation of a selected Lambert solution should close at the target position with the reconstructed transfer arrival velocity, before the arrival burn. Include the near-degenerate and high-precision closure cases from Phase 3. [PyKEP parity cases](orbital-transfer-design.md)
2. **Cases and tolerances.** Reuse `tests/fixtures/transfer-reference.txt` plus the Phase 3 endpoint and branch probes. Add cases for nonzero start epoch, low `e`/`i` on both sides of the ephemeris switch, near-collinear and zero chord geometries, invalid indices and non-finite durations, near-parabolic branches, multi-revolution minima and the 20-cap. Start with the Phase 3 numeric comparison gates `max(1e-4 m, 1e-11 × magnitude)` for position and `max(1e-6 m/s, 1e-11 × magnitude)` for velocity and Δv. Report failures, branch/status mismatches, error tails and worst cases. Keep these as comparison gates. A solver variant that needs different bounds requires recorded evidence and review before integration. [Existing reference fixture](../tests/fixtures/transfer-reference.txt) · [Phase 3 numerical tolerances](orbital-transfer-design.md)
3. **Feasibility boundaries.** Run the neighboring representable inputs around 100 and 600 m/s and the separate published evaluator threshold. The authoritative strict route applies the exact instance ceilings, the exception budget, chronology and deadline. Use CPU reevaluation for near-threshold or poorly conditioned cases. Set any reevaluation guard band from observed error and residuals, with a conservative margin. This decides which evaluator to consult. It never increases either ceiling. Require the final route to pass the strict CPU check on all 48 legs and report its mission time and exception count. [Boundary policy and checks](orbital-transfer-design.md#δv-boundary-verification) · [CPU reference route](cpu-baseline.md#reference-and-expected-accounting)
4. **Fixed workload.** Replay the Phase 7C `KTTSPQ1` trace in original order with all **5,204,320** records, including duplicated validation work. Its SHA-256 is `11a9acc916fbd29f51dbdec6fd68f6e9cc99b84edeede704223b789d90afb8a4`. Preserve the easy-instance hash and report results by matrix, scheduling and final-validation segments. Capture the CPU statuses and raw Δv for the same trace. Begin with 256, 1,024, 4,096, 16,384 and 65,536 queries per batch. Use larger batches only while device memory and launch durations stay within measured bounds. Run the same fixed workload on serial PyKEP and the GPU. Report any optional multicore CPU baseline separately. [Trace format and provenance](cpu-benchmarking.md#exact-orbital-query-trace)
5. **Timing and profiling.** Measure cold initialization separately. Warm both paths, then collect at least five alternating timed runs at stable AC power. Report medians, ranges, throughput, CPU/GPU clocks, GPU power, and driver/toolchain versions. Use CUDA events for H2D, compute and D2H, and a host clock through synchronization for packing, launch overhead, fallback, and total replay time. Report kernel-only and transfer-inclusive speedups as separate numbers. Include disk decode time in an explicitly labeled end-to-end measurement or report it separately for in-memory replay. Add Compute Sanitizer before relying on device results. Use Nsight Systems for host/transfer/stream gaps and Nsight Compute for FP64 utilization, branch divergence, registers and spills if timings justify installing them. [NVIDIA timing and profiling guidance][practices]

## Milestones and gates

| Stage | Deliverable and gate |
| --- | --- |
| 1. CUDA toolchain smoke test | After installation approval, verify MSVC, SDK, toolkit and `sm_89`. Run both official samples. Keep the current CPU executable and build untouched. |
| 2. GPU batch interface | Ship the standalone trace reader and CPU/GPU adapters. Test record order, duplicates, empty/partial batches, malformed input and status separation with a deterministic test workload. |
| 3. GPU propagation | Check both endpoint state vectors against PyKEP on normal, low-`e`/`i`, nonzero-epoch, and edge cases. Track CPU fallback coverage. |
| 4. Lambert evaluation | Verify scalar host math first. Add device zero-revolution, then both-direction multi-revolution enumeration. Pass branch, root, velocity and closure tests. |
| 5. CPU/GPU numerical comparison | Pass the threshold suite and fixed trace comparison. Quantify every remaining mismatch and fallback before any use in the optimizer. |
| 6. Optimizer integration | Opt-in CPU/GPU selection. Verify cache accounting, seeded query ordering, strict independent validation, and output equivalence for the known baseline. Keep the CPU path available for comparison and recovery. |
| 7. End-to-end performance | Compare fixed-trace throughput and full-solve wall time separately. Report exact work, CPU fallbacks, mission days, exception count, and strict acceptance alongside speed. |

Phase 8A ends with a verified driver, a pinned installation plan, and this design. The remaining environment blocker is the missing native compiler, SDK and CUDA Toolkit. The GPU implementation should begin only after the standalone smoke test passes.

[windows]: https://docs.nvidia.com/cuda/archive/13.3.1/cuda-installation-guide-microsoft-windows/index.html
[release]: https://docs.nvidia.com/cuda/archive/13.3.1/cuda-toolkit-release-notes/index.html
[compatibility]: https://docs.nvidia.com/deploy/cuda-compatibility/latest/minor-version-compatibility.html
[cuda-download]: https://developer.nvidia.com/cuda-13-3-1-download-archive
[msvc-version]: https://learn.microsoft.com/en-us/cpp/overview/compiler-versions
[vs-download]: https://learn.microsoft.com/en-us/visualstudio/releases/2026/release-history
[vs-components]: https://learn.microsoft.com/en-us/visualstudio/install/workload-component-id-vs-build-tools?view=visualstudio
[msvc-acquire]: https://learn.microsoft.com/en-us/cpp/overview/acquire-msvc
[vcvars]: https://learn.microsoft.com/en-us/cpp/build/building-on-the-command-line
[device-query]: https://github.com/NVIDIA/cuda-samples/tree/v13.3/cpp/1_Utilities/deviceQuery
[vector-add]: https://raw.githubusercontent.com/NVIDIA/cuda-samples/v13.3/cpp/0_Introduction/vectorAdd/vectorAdd.cu
[nvcc]: https://docs.nvidia.com/cuda/archive/13.3.1/cuda-compiler-driver-nvcc/index.html
[architectures]: https://cmake.org/cmake/help/latest/prop_tgt/CUDA_ARCHITECTURES.html
[host-compiler]: https://cmake.org/cmake/help/latest/variable/CMAKE_LANG_HOST_COMPILER.html
[pykep-eph]: https://raw.githubusercontent.com/esa/pykep/v2.6/src/planet/keplerian.cpp
[pykep-lagrange]: https://github.com/esa/pykep/blob/v2.6/include/keplerian_toolbox/core_functions/propagate_lagrangian.hpp
[pykep-lambert]: https://raw.githubusercontent.com/esa/pykep/v2.6/src/lambert_problem.cpp
[pykep-license]: https://github.com/esa/pykep/blob/23060c3574fcccc178c1db86705c7c3eff2b83e7/LICENSE.txt
[izzo]: https://arxiv.org/abs/1403.2705
[floating]: https://docs.nvidia.com/cuda/archive/13.3.1/floating-point/index.html
[practices]: https://docs.nvidia.com/cuda/archive/13.3.1/cuda-c-best-practices-guide/index.html
[tdr]: https://learn.microsoft.com/en-us/windows-hardware/drivers/display/timeout-detection-and-recovery
