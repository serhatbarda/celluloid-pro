# Reafilm Pro

**Spectral film emulation for DaVinci Resolve** — an OpenFX plug-in for macOS (universal, Metal) and
Windows (x64, CUDA and OpenCL), with a CPU fallback everywhere.

Reafilm Pro simulates the photochemical chain instead of applying a fixed LUT: camera log →
scene light → spectral exposure of a three-layer colour negative → development (push/pull,
interlayer effects, bleach bypass) → print with printer lights, or lab scan → projection →
display encoding (SDR or HDR). Halation, bloom, film grain, acutance, gate weave, vignetting and (1.1)
film damage (dust, scratches, hair) and momentary film blur are added at the stage where they
physically arise; every module has its own Enable switch. All stocks, presets and colour science are original
designs.

| | |
|---|---|
| Plug-in | `de.serhatbarda.CelluloidPro` · "Reafilm Pro" · OpenFX group "Reafilm" · version 1.12.0 |
| Stocks | 11 negatives (incl. reversal and black & white) · 5 prints · Lab Scan process |
| Looks | 14 curated presets (2 with film damage) |
| Cameras | Fujifilm F-Log2 C (primary), F-Log2, F-Log, ARRI LogC3/LogC4, Sony S-Log3, Panasonic V-Log, Canon Log 3, Blackmagic Film Gen 5, DaVinci Intermediate, ACEScct, Linear Rec.2020; Rec.709 video and sRGB (camcorder, phone, JPEG) |
| Outputs | Rec.709 (2.4 / sRGB), P3-D65, DCI-P3, Rec.2100 PQ and HLG with highlight unroll |
| Extras | .cube LUT export |
| Build | **personal build** (default, non-commercial): no licensing, no watermark. The licensed variant (offline Ed25519 keys, trial watermark, licensed-only LUT export) builds with `-DCEL_LICENSING=ON` |
| Shipping platforms | macOS 11+ (arm64 + x86_64) · Windows 10/11 x64. Linux builds are for development and tests only |

**Upgrading from Celluloid Pro.** The plug-in was renamed from Celluloid Pro to Reafilm Pro in
1.3.0, and its bundle from `Celluloid.ofx.bundle` to `ReafilmPro.ofx.bundle`. Delete the old
`Celluloid.ofx.bundle` from the OFX plug-ins folder (`/Library/OFX/Plugins` on macOS,
`C:\Program Files\Common Files\OFX\Plugins` on Windows), otherwise Resolve shows both. The
installers do this for you; after a manual (zip) install, delete it by hand. Saved projects keep
loading: the plug-in identifier and all parameters are unchanged, and license keys stay valid.

Documentation:

- [docs/MANUAL.md](docs/MANUAL.md) — user manual (installed in the plug-in bundle).
- [docs/QUICKSTART.md](docs/QUICKSTART.md) — one-page quick start (incl. Blackmagic BRAW, stills, monitoring LUTs).
- [docs/STOCKS.md](docs/STOCKS.md) — colour model and stock designs.
- [docs/LOOKS.md](docs/LOOKS.md) — look presets.
- [tools/licensegen/README.md](tools/licensegen/README.md) — license keys, signing and shop integration
  (licensed build only).
- [CHANGELOG.md](CHANGELOG.md) · [docs/EULA_TEMPLATE.md](docs/EULA_TEMPLATE.md) (template, not yet
  reviewed by a lawyer).

---

## Architecture

```
                         DaVinci Resolve (OpenFX host)
                                      │  describe / instanceChanged / render
                                      ▼
 ┌──────────────────────────── src/ofx  (plug-in shell) ─────────────────────────────┐
 │ Params (single registry) · Presets · License UI · LUT export · render orchestration │
 └───────┬─────────────────────────────┬──────────────────────────────────┬───────────┘
         │ LookParams                  │ FrameSettings                    │ key check / watermark
         ▼                             ▼                                  ▼
 ┌─ src/core ───────────────┐  ┌─ src/engine + src/texture ─────┐  ┌─ src/license ─────────┐
 │ spectral design + bakes: │  │ buildPlan(): ordered passes    │  │ Ed25519 (Monocypher)  │
 │ stocks, upsampling LUT,  │  │ over float RGBA buffers        │  │ CELL1- key format,    │
 │ printer/scanner LUTs,    │  │ (decode, weave, acutance,      │  │ storage, trial state  │
 │ grey balance, .cube      │  │  halation/bloom pyramid,       │  └───────────────────────┘
 │ → one state buffer       │  │  grain, colour, watermark)     │
 └────────────┬─────────────┘  └──────────────┬─────────────────┘
              └──────────────┬────────────────┘
                             ▼  Backend::execute(plan, state, src, dst)
     ┌──────────────┬────────┴───────┬──────────────────┬──────────────────┐
     │ CpuBackend   │ OpenCLBackend  │ CudaBackend      │ MetalBackend     │
     │ (thread pool)│ (runtime C)    │ (nvcc fatbin+PTX)│ (runtime MSL)    │
     └──────────────┴────────┬───────┴──────────────────┴──────────────────┘
                             ▼
        src/kernels: ONE portable kernel source (Portable.h dialect, uniform ABI,
        KernelList.h X-macro) compiled as C++, OpenCL C, CUDA and Metal
```

- **One source of truth for the math.** Every per-pixel computation lives in `src/kernels/*.h`,
  written in a portable dialect that compiles as C++, OpenCL C, CUDA and MSL. CPU and GPUs run the
  same code, so CPU/GPU parity is tested pass by pass (tolerance 2e-4 on the encoded output).
- **Host-side design, device-side evaluation.** `src/core` designs the stocks spectrally (380–780 nm,
  5 nm, double precision) and bakes them into a state buffer once per stock pair (cached;
  `prepare()` on a cache hit ≈ 0.5 ms). Kernels only evaluate.
- **GPU strategy.** Resolve hands over device buffers and its queue/stream; the backends work on them
  directly, with pooled internal buffers. Only backends that pass a self test at load are declared to
  the host. A failed GPU frame is re-rendered on the CPU while the images stay on the device.

Details: [src/gpu/README.md](src/gpu/README.md),
[src/ofx/README.md](src/ofx/README.md).

---

## Repository map

| Path | Contents |
|---|---|
| `src/common/` | shared types, `parallelRows` worker pool |
| `src/kernels/` | portable kernels (colour, texture, watermark), kernel ABI and list — **fixed interfaces** |
| `src/core/` | colour science: spectral model, stock data, bakes, transfer functions, .cube writer |
| `src/engine/` | frame plan builder and the CPU backend |
| `src/texture/` | texture design (halation, bloom, grain, weave, acutance, vignette, film damage / film blur) and plans |
| `src/gpu/` | OpenCL, CUDA and Metal backends, kernel flattening, self tests |
| `src/license/` | key format, verification, storage, watermark; `PublicKey.h` (production public key) |
| `src/ofx/` | the OpenFX plug-in: parameters, presets, rendering, LUT export, license UI |
| `tests/` | unit tests: `common`, `core`, `texture`, `license`, `gpu` (CTest) |
| `tools/ofxhost/` | headless OpenFX test host (CPU, OpenCL and Metal render paths) + CI smoke test |
| `tools/render_tests/` | end-to-end render tests through the real bundle (Python, NumPy, Pillow) |
| `tools/licensegen/` | key generation and signing CLI, Python reference implementation, development keys |
| `tools/docs/` | generator of the manual's controls reference |
| `reference/` | NumPy reference model of the colour science, design and gate scripts, CIE data |
| `installers/` | macOS `.pkg` builder, Windows Inno Setup script, CI helpers (version, bundle check, release gate) |
| `resources/` | Info.plist template, icon generator, third-party notices template |
| `cmake/` | OpenFX fetch, kernel flattening, versioning and helper scripts |
| `third_party/` | vendored Monocypher (Ed25519) |
| `docs/` | manual, stocks, looks, validation, EULA template, contact sheets |
| `.github/workflows/build.yml` | CI: macOS and Windows builds, installers, releases |

---

## Building

Requirements on every platform: CMake ≥ 3.20, a C++17 compiler, Git. The OpenFX SDK is fetched at
the pinned commit `e40728885390ec16276d11e00025de9b4282060c` during configure; to build offline,
clone it once and pass `-DFETCHCONTENT_SOURCE_DIR_OPENFX=<path>`:

```sh
git clone https://github.com/AcademySoftwareFoundation/openfx.git external-openfx-ref
git -C external-openfx-ref checkout e40728885390ec16276d11e00025de9b4282060c
```

Every configuration below builds the **personal build** (no license check, no trial watermark,
LUT export always on), the default of this repository. Add `-DCEL_LICENSING=ON` for the licensed
variant (see [CMake options](#cmake-options)).

The result of every build is `<build>/ReafilmPro.ofx.bundle`
(`Contents/{MacOS|Win64|Linux-x86-64}/ReafilmPro.ofx`, `Info.plist`, `Resources/` with the icon,
`MANUAL.md` and licence texts).

### macOS (universal, Metal + OpenCL)

Xcode with the Metal toolchain (CI uses the default Xcode of the macos-14 runner) (`xcodebuild -downloadComponent MetalToolchain` if
`xcrun metal` is missing).

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 \
      -DCEL_ENABLE_METAL=ON -DCEL_ENABLE_OPENCL=ON
cmake --build build --parallel
bash installers/ci/verify_bundle.sh build/ReafilmPro.ofx.bundle macos
installers/macos/build_pkg.sh --bundle build/ReafilmPro.ofx.bundle --version 1.12.0 --out dist
```

For a quick local test, copy the bundle to `/Library/OFX/Plugins/` and restart Resolve.

### Windows (x64, CUDA + OpenCL)

Visual Studio 2022 (x64 C++ tools), Ninja, optionally the CUDA Toolkit 12.x (12.9 in CI; it is
the last line that still builds every architecture from sm_52), Inno Setup ≥ 6.3 for the installer.
From an *x64 Native Tools* prompt:

```bat
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release ^
      -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl ^
      -DCEL_ENABLE_CUDA=ON -DCEL_ENABLE_OPENCL=ON
cmake --build build --parallel
powershell -File installers\windows\build_installer.ps1 -BundleDir build\ReafilmPro.ofx.bundle -Version 1.12.0 -OutDir dist
```

The plug-in DLL links the MSVC runtime and the CUDA runtime statically; OpenCL is loaded at run time
from System32, so the plug-in loads on machines without NVIDIA or OpenCL drivers.

### Linux (development and tests only)

GCC or Clang with C++17 (tested with GCC 13), Ninja; for the test suite also Python 3 with NumPy and Pillow, and POCL
(`pocl-opencl-icd ocl-icd-opencl-dev opencl-headers`) for the OpenCL parity tests.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DFETCHCONTENT_SOURCE_DIR_OPENFX=$PWD/external-openfx-ref \
      -DCEL_BUILD_TESTS=ON -DCEL_WARNINGS_AS_ERRORS=ON
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
tools/render_tests/run_all.sh build/ReafilmPro.ofx.bundle /tmp/cel-render   # end-to-end render tests
```

### CMake options

| Option | Default | Meaning |
|---|---|---|
| `CEL_BUILD_TESTS` | ON | unit tests and CTest registration |
| `CEL_BUILD_TOOLS` | ON | `licensegen`; `ofxhost` on Linux (and wherever tests are built) |
| `CEL_ENABLE_OPENCL` | ON | OpenCL backend (headers from the system, the CUDA toolkit or fetched Khronos headers) |
| `CEL_ENABLE_CUDA` | OFF | CUDA backend (needs nvcc); ON in Windows CI |
| `CEL_ENABLE_METAL` | ON on Apple | Metal backend |
| `CEL_UNIVERSAL` | ON on Apple | arm64 + x86_64 |
| `CEL_LICENSING` | OFF | OFF = **personal build**: the plug-in contains no license code, never watermarks, always exports LUTs, and keeps the License group as hidden parameters (saved projects stay compatible). ON = licensed build: offline keys, trial watermark, licensed-only LUT export, License group visible |
| `CEL_DEV_LICENSE` | OFF | licensed build only (needs `CEL_LICENSING=ON`): accept the committed **development** key pair — tests only, never ship |
| `CEL_WARNINGS_AS_ERRORS` | OFF | `-Werror` / `/WX` for our own code |
| `CEL_BUILD_ID` | git commit | build id shown in the About group |

### Tests

`ctest` labels: `gpu-device` (needs a GPU or POCL device; exits 77 = skipped without one),
`linux-only` (workbench tools), `ofx` (the real bundle in `ofxhost`), `docs` (the manual's controls
reference matches the plug-in; personal build only). The license unit tests (`cel_license_tests*`) run in both
build variants; the end-to-end smoke test (`ofx_smoke`) checks the variant it finds (personal: no
watermark, hidden License group, LUT export without a key; licensed: trial watermark and refused
LUT export) against its own reference render.

When you change a parameter's label, range, default or hint in `src/ofx/Params.cpp`, regenerate the
manual's controls reference (the `docs_manual_controls` test fails until you do):

```sh
python3 tools/docs/gen_manual_controls.py --bundle build/ReafilmPro.ofx.bundle \
        --ofxhost build/tools/ofxhost/ofxhost
```

---

## CI, releases and signing

`.github/workflows/build.yml` runs:

- on a push to `main` (macOS and Windows), but only when it touches build-relevant paths
  (`src/`, `CMakeLists.txt`, `cmake/`, `third_party/`, `tests/`, `tools/ofxhost/`,
  `tools/licensegen/`, `tools/docs/`, `resources/`, `reference/golden/`, `installers/`,
  `.github/workflows/`). Docs and look-dev tools do not trigger CI;
- on a tag `v*`: both platforms, then the release is published;
- on a manual run (*Run workflow*): both platforms.

There is no pull-request trigger. A newer run cancels an older one on the same branch. It builds
the **personal build** by default; the `licensing` input of a manual run builds the licensed variant
(`-DCEL_LICENSING=ON`):

| Job | Runner | Does |
|---|---|---|
| macOS universal (Metal) | macos-14 | universal build, offline `xcrun metal` check of the kernels, bundle verification, unit tests, GPU device tests on the runner's GPU, `.pkg` and `.dmg` |
| Windows x64 (CUDA, OpenCL) | windows-2022 | MSVC + CUDA 12.9 build, bundle verification, unit tests, GPU probes, Inno Setup installer and bundle zip |
| GitHub Release | ubuntu-latest, tags only | after both jobs succeed, publishes all installers to the release of the tag |

Every job uploads its installers as workflow artifacts
(`ReafilmPro-<version>-macOS.{pkg,dmg}`, `ReafilmPro-<version>-Windows-Setup.exe`,
`ReafilmPro-<version>-Windows-bundle.zip`). There is deliberately no Linux job.

### Signing and releases

Signing switches on automatically when the signing secrets exist in the repository's Actions
secrets; without them the macOS plug-in is ad-hoc signed and the installers are unsigned (the
manual explains how to open them). A release is a tag `vX.Y.Z` equal to the project version in
`CMakeLists.txt`.

## License keys

*Licensed build (`-DCEL_LICENSING=ON`) only; the default personal build has no license keys.*

Offline, signed keys: `CELL1-` + Base32 of a small payload and an Ed25519 signature; the plug-in
contains only the public key. See [tools/licensegen/README.md](tools/licensegen/README.md). The key
pair in `tools/licensegen/dev-keys/` is a public **development** pair, accepted only by builds
configured with `CEL_DEV_LICENSE=ON`.

---

## Status

Version 1.12.0. Built and tested on the Linux workbench (unit tests, CPU and POCL/OpenCL parity,
end-to-end render tests through the real bundle) and in CI on macOS and Windows (including Metal
execution on the macOS runner's virtual GPU). Not yet verified: CUDA execution on NVIDIA hardware,
real-GPU performance (the < 8 ms UHD target is an estimate, not a measurement), and use inside
DaVinci Resolve, which is being tested now.

## Legal

Copyright © 2026 Serhat (serhatbarda.de). The source code is published for reference; no open-source licence is granted, and all rights are reserved unless a separate
licence says otherwise. End users receive the plug-in under the end-user licence agreement (see the
[template](docs/EULA_TEMPLATE.md)). Third-party components: Monocypher (BSD-2-Clause / CC0),
Khronos OpenCL headers (Apache-2.0, compile time), the OpenFX SDK Support library (BSD-3-Clause), and
on Windows the statically linked NVIDIA CUDA runtime; their notices ship in the bundle under
`Contents/Resources/Licenses/`. DaVinci Resolve is a trademark of Blackmagic Design Pty. Ltd.;
Reafilm Pro is not affiliated with or endorsed by Blackmagic Design or any film manufacturer.
