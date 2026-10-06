# src/gpu — OpenCL, CUDA and Metal backends

GPU implementations of `cel::engine::Backend` (see `src/engine/Engine.h`). They execute **any**
`engine::Plan` generically: every kernel has the uniform ABI of `src/kernels/KernelABI.h`, and the
per-API entry points are generated from the X-macro list `src/kernels/KernelList.h` with one macro
per API. The same portable kernel sources run on the CPU, so CPU/GPU parity is testable pass by pass.

## CMake integration

```cmake
# top level (before add_subdirectory, optional overrides):
#   CEL_ENABLE_OPENCL (ON)  CEL_ENABLE_CUDA (OFF)  CEL_ENABLE_METAL (ON on Apple, else OFF)
# With CUDA on, call enable_language(CUDA) (or project(... CUDA)) at the top level.
add_subdirectory(src/gpu)                     # -> static library target `celgpu`
target_link_libraries(<plugin> PRIVATE celgpu)
add_subdirectory(tests/gpu)                   # optional: tests (see below)
```

| Item | Name |
|---|---|
| static library | `celgpu` (alias `Celluloid::gpu`), PIC, hidden visibility, C++17 |
| public include dir | `src/` (`#include "gpu/GpuBackends.h"`) |
| flattened sources target | `celgpu_kernel_sources` → `<build>/src/gpu/generated/celluloid_production.{cl,metal}` |
| cache variables (read-only) | `CEL_GPU_OPENCL_FLAT_SOURCE`, `CEL_GPU_METAL_FLAT_SOURCE`, `CEL_GPU_OPENCL_INCLUDE_DIR` |
| options | `CEL_ENABLE_OPENCL`, `CEL_ENABLE_CUDA`, `CEL_ENABLE_METAL`, `CEL_CUDA_ARCHITECTURES`, `CEL_OPENCL_HEADERS_DIR`, `CEL_GPU_KERNEL_INCLUDE_DIRS` (extra kernel header dirs; empty for product builds) |
| compile definitions (private) | `CEL_GPU_HAVE_OPENCL/CUDA/METAL=1` for the compiled-in APIs |

`engine::openclBackend()/cudaBackend()/metalBackend()` are always defined by `celgpu` and return
`nullptr` for APIs that are not compiled in.

* **OpenCL**: the runtime is loaded dynamically (`OpenCL.dll`, `libOpenCL.so.1`,
  `OpenCL.framework`; override with env `CEL_OPENCL_LIBRARY`), so the plugin never fails to load on
  machines without OpenCL. Headers: `CEL_OPENCL_HEADERS_DIR`, else system / CUDA toolkit
  (`CUDA_PATH`) `CL/cl.h`, else Khronos OpenCL-Headers `v2024.10.24` via FetchContent
  (Apache-2.0, header only). Links only `${CMAKE_DL_LIBS}`.
* **CUDA**: `cuda/CudaKernels.cu` (nvcc) + host code in `cuda/CudaBackend.cpp` (C++ compiler),
  static CUDA runtime (`CUDA::cudart_static`; needs only the NVIDIA driver at run time and loads
  it lazily, so the plugin also loads without NVIDIA drivers). Architectures
  (`CEL_CUDA_ARCHITECTURES`, set on the `celgpu` target, overriding `CMAKE_CUDA_ARCHITECTURES`):
  CUDA 12.8–12.x: SASS sm_52 60 61 70 75 80 86 89 90 120 + PTX compute_120;
  CUDA 11.8–12.7: SASS sm_52…sm_90 + PTX compute_90; CUDA ≥ 13 (dropped Maxwell–Volta): SASS
  sm_75…sm_120 + PTX. No fast math. Verified locally with nvcc 12.9 (fatbin listing in the gpu
  report). Windows CI: install the toolkit (e.g. Jimver/cuda-toolkit), configure with
  `-DCEL_ENABLE_CUDA=ON`; with the static MSVC runtime set `CMAKE_MSVC_RUNTIME_LIBRARY` globally
  (it applies to the CUDA host compile too).
* **Metal**: `metal/MetalBackend.mm`, **ARC** (`-fobjc-arc` set on the file; a `#error` enforces it),
  links `Metal` + `Foundation`. Library compiled at run time from the embedded MSL with
  `languageVersion = 2.3` (macOS 11 baseline), `mathMode = safe` +
  `mathFloatingPointFunctions = precise` on macOS 15+, `fastMathEnabled = NO` before.

## Kernel sources and entry points

* `cmake/FlattenKernels.cmake` flattens a root file (`kernels/ProductionOpenCL.cl`,
  `kernels/ProductionMetal.metal`) by resolving local `#include "…"` textually (guarded headers
  once, X-macro lists at every include, `#line` markers with repo-relative paths, missing include /
  cycle = build error) and embeds it as a byte array (`cel::gpu::embedded::<symbol>`, with
  length and SHA-256). Outputs are rewritten only when they change.
* It also fails the build when a device function is named like a generated entry point:
  **rule: kernel helpers must never be named `cel_<kernelname>`** (entry points are `cel_<name>`,
  bodies `cel_k_<name>`).
* Entry macros: `kernels/EntryOpenCL.h`, `kernels/EntryMetal.h`, `kernels/EntryCuda.h`
  (`CEL_GPU_ENTRY(name)` → `cel_<name>` with a bounds check, calling `cel_k_<name>`).
  The production roots apply it to `KernelList.h`; tests apply it to their own lists.
* `kernels/Probe.h`: self-test kernel `cel_gpu_probe`, appended to every kernel set at index
  `KernelSet::count`; it echoes the `CelPassArgs` layout, the state buffer and an input, and
  evaluates every `cel_*` math wrapper; the self test compares it with the C++ build.

ABI per API (set generically by the backends):

| | args | out, in0..in3, state | unused inputs |
|---|---|---|---|
| OpenCL | arg 0: `__constant CelPassArgs*` = all passes of the frame (one buffer), arg 1: pass index | args 2, 3–6, 7 (`__global`, `restrict`) | `NULL` |
| CUDA | `CelPassArgs` by value (param space; no local-memory copy in practice: STACK 0 on all kernels) | `__restrict__` pointers | `nullptr` |
| Metal | `[[buffer(0)]]` `constant CelPassArgs*` via `setBytes` | `[[buffer(1)]]`, `[[buffer(2..5)]]`, `[[buffer(6)]]` | 16-byte dummy buffer |

Kernels must only read inputs whose `inStride != 0`.

## Execution model

* **Plan validation** (`GpuCommon.cpp::buildSchedule`): buffer/kernel indices, output never an
  input of the same pass, no read before write, dst written, host/plan sizes agree, src ≠ dst,
  row stride ≥ width·4, 32-bit kernel index range. Errors are descriptive and returned via `err`.
* **Internal buffers** are assigned to physical slots by liveness (buffers with disjoint lifetimes
  share memory, exact size classes) and taken from a per-queue pool that is reused across frames
  and trims buffers unused for 4 frames.
* **Lanes** (per host queue/stream: pool, state buffer, OpenCL `cl_kernel`s, CUDA staging) that
  have not rendered for 10 s are released completely by the next frame on another lane (all three
  APIs; env `CEL_GPU_LANE_IDLE_MS` shortens it for tests). Lookups are retire-safe: a thread that
  took a lane just before it was retired gets a fresh one. CUDA releases a lane only after its
  last frame completed (event), only on the current device (no context switch on the host's
  thread), and orders the frees on the live stream of the rendering lane, because the host may
  have destroyed the idle lane's stream. `BackendStats::lanes` reports the live lanes.
* **State buffer**: per queue/stream, re-uploaded only when its content key changes: `stateVersion`
  + data pointer + size + a checksum of ~600 sampled floats (so even a version number reused by
  another `core::Pipeline` instance cannot alias). Uploads never read host memory asynchronously:
  OpenCL copies into a staging block freed when the write's event completes (polled, no driver
  callbacks); CUDA uses reusable pinned staging blocks + events; Metal blits from a shared staging
  buffer retained by the command buffer into a private buffer.
* **Ordering**: everything is enqueued on the host's queue/stream; the render thread never waits
  (OFX GPU rule), except: CUDA without a host stream (OFX then requires completion → the default
  stream is synchronised), and the debug-capture test hook. OpenCL out-of-order queues are
  supported (leading barrier, event chain, trailing barrier). Metal: one command buffer per frame,
  one serial compute encoder, one dispatch per pass; command-buffer errors are reported by a
  completion handler and fail the next `execute()` on that queue.
* **Caches**: OpenCL program per (`cl_context`, `cl_device_id`) with build options
  `-cl-std=CL1.2` (if supported) `-cl-fp32-correctly-rounded-divide-sqrt` (if the device reports
  `CL_FP_CORRECTLY_ROUNDED_DIVIDE_SQRT`), never fast-relaxed math, extra options via env
  `CEL_OPENCL_BUILD_OPTIONS`; a failed build is remembered with its log. `cl_kernel` objects per
  queue (clSetKernelArg is not thread-safe per kernel object). Metal library + pipeline states per
  `MTLDevice`. CUDA: per-kernel launch config from `cudaFuncGetAttributes` (32 × N ≤ 256 threads).
  Local/threadgroup sizes from the kernel limits; grids rounded up (entry points bounds-check).
* **Threading**: `execute()` is thread-safe. Different queues/streams render concurrently (one lock
  per queue/stream); calls on the same queue are serialised briefly (enqueue only).
* **Memory**: CUDA uses stream-ordered `cudaMallocAsync/cudaFreeAsync` where the device supports
  memory pools (never a device sync), else `cudaMalloc/cudaFree`. Metal internal buffers are
  `StorageModePrivate`.
* **Lifetime**: the production singletons are intentionally leaked (no driver calls during static
  destruction / DLL unload). Call `gpu::releaseAllResources()` at `kOfxActionUnload`.

## Host image conventions (OFX GPU render extension, `ofxGPURender.h`, as Resolve implements it)

| API | `HostImage::data` | queue/stream | rows |
|---|---|---|---|
| OpenCL (buffers: `kOfxImageEffectPropOpenCLRenderSupported`) | `cl_mem` from `kOfxImagePropData` | `kOfxImageEffectPropOpenCLCommandQueue` | top-down from the buffer start, stride = `rowBytes/4` floats ≥ w·4; negative strides rejected (a `cl_mem` cannot be offset) |
| CUDA | device pointer from `kOfxImagePropData` (current device = host's) | `kOfxImageEffectPropCudaStream` (may be null) | any stride incl. negative (pointer = row 0) |
| Metal | `id<MTLBuffer>` from `kOfxImagePropData` | `kOfxImageEffectPropMetalCommandQueue` | top-down from offset 0, negative rejected |

OpenCL buffer size / context and Metal buffer length / device are validated against the image
geometry; CUDA pointers are checked to be device memory of the current device.

## Public API beyond `engine::Backend` (`GpuBackends.h`)

* `gpu::selfTest(api, full)`: `full=false` is cheap (runtime loads, ≥ 1 device; safe at plugin
  describe); `full=true` builds the production kernels on a private context/queue and runs the probe
  (seconds; for diagnostics / tests).
* `gpu::cpuFallback(api, cpu, …)`: on a GPU failure, renders the frame with the CPU backend while
  the images stay in device memory (download src → CPU → upload dst, ordered on the host's
  queue/stream; blocks only this call). Recommended strategy for the OFX shell: declare GPU support
  for APIs whose quick self test passes; on `execute()` failure use `cpuFallback`; only if that
  fails too return `kOfxStatGPURenderFailed` / `kOfxStatGPUOutOfMemory`.
* `gpu::releaseAllResources()`, `gpu::backendStats()` (frames, failures, builds + build ms, state
  uploads, pool allocations/reuses, pooled bytes, live lanes, last error/device), test hooks
  `setDebugCapture` / `debugCapturedBuffer` and `setDebugPoison` (fills every internal buffer
  with NaN before the first pass of each frame: reads of memory no pass wrote become visible),
  factories `createOpenCLBackend/createCudaBackend/
  createMetalBackend(KernelSet)` for custom kernel sets.

## Tests (`tests/gpu`, standalone: `tests/gpu/build.sh [build-dir] [cmake args]`)

| ctest | what | labels |
|---|---|---|
| `gpu_flatten` | flattener fixture test (guards, X-macros, include dirs, `#line`, errors, collision rule, byte-exact embedding) | |
| `gpu_abi` | backends vs an independent reference executor on the ABI test kernel set: strides/padding, 1×1 and odd sizes, 4 inputs of different sizes, f[]/i[] layout, 4 MiB state + upload-by-key, bit-exact hashing, math parity, liveness aliasing + pool reuse, debug capture of every internal buffer, error paths, two queues concurrently, out-of-order queue, CPU fallback; plus the production full self tests | `gpu-device` (exit 77 = skipped when no device) |
| `gpu_parity` | real plans (`buildPlan`, `tex::buildTexturePlan/buildGrainPlan` + `core::Pipeline`): `cpuBackend()` vs reference vs every GPU device with NaN-poisoned internal buffers; 90 cases (131 with `CEL_PARITY_ALL=1`): all modes, every input and output space (Full and Texture Only), Lab Scan for every negative, look parameters at their limits, HDR peak/unroll, every grain format, texture extremes, render scales, 1×1 / 1×N, NaN/Inf/huge torture frames; every negative × print with `CEL_PARITY_ALL=1` (+ UHD). Asserts every kernel of `KernelList.h` is covered and frame plans never emit NaN/Inf. Tolerance 2e-4 on the encoded output. Diagnostics: an SSA copy of a failing plan (one buffer per pass) is re-run pass by pass; `CEL_PARITY_TRACE=<case substring>` forces the report | `gpu-device` |
| `gpu_math_tolerance` | `test_gpu_parity --model`: every parity case on the CPU with **worst-case GPU math** (`PerturbedKernels.cpp`: each `cel_pow/exp/exp2/log/log2/log10/sqrt/sin/cos/atan2` result moved by up to the OpenCL 1.2 full-profile ulp limits: pow 16, exp/log 3, sin/cos 4, atan2 6, sqrt 1) vs the exact CPU build; catches formulas that amplify rounding (cancellation) independently of the GPU the test machine has. No device needed (runs in CI) | |
| `gpu_opencl_syntax` | clang `-x cl -cl-std=CL1.2` syntax check of the flattened OpenCL C | |
| `gpu_metal_xcrun` | `metal_syntax/check_metal.sh`: `xcrun metal -std=metal2.3` + `metallib` of the flattened MSL (Apple) | |
| `gpu_metal_shim` | `metal_syntax/check_metal_shim.sh`: flattened MSL parsed by clang++ with an address-space shim (`device`/`constant` = distinct address spaces, no double promotion) | `linux-only` |
| `gpu_metal_objc_mock` | `metal_syntax/check_mm_mock.sh`: ARC Objective-C++ syntax/type check of the `.mm` files against mock Foundation/Metal headers | `linux-only` |

Numerics rule for kernel authors: avoid `1 - (value close to 1)` and differences of nearly equal
`pow`/`exp` results; GPU math may be up to 16 ulp off (OpenCL `pow`), and such forms amplify that
by orders of magnitude (the PQ EOTF and the Texture-Only tone map were rewritten with an accurate
`expm1` for this reason). `gpu_math_tolerance` fails when a change breaks this.

CI: macOS runs `ctest -L gpu-device` on the runner's GPU (Metal and Apple's OpenCL; SKIP without
a device), Windows runs it too (no GPU on hosted runners: proves the graceful runtime probing).
macOS CI (no device needed): `cmake --build <dir> --target celgpu_kernel_sources` then
`tests/gpu/metal_syntax/check_metal.sh <dir>/src/gpu/generated/celluloid_production.metal`
(or run `ctest -R gpu_metal_xcrun` when tests are built).
