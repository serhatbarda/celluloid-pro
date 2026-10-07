# src/ofx — the OFX plug-in shell

The OpenFX plug-in `de.serhatbarda.CelluloidPro` ("Celluloid Pro", grouping "Celluloid", v1.0),
built with the OpenFX C++ Support library (compiled statically from the pinned OpenFX sources,
`cmake/CelOpenFX.cmake`).

| File | Purpose |
|---|---|
| `CelluloidPlugin.{h,cpp}` | factory (describe / describeInContext / unload), the effect: render orchestration, UI logic, license UI, LUT export |
| `Params.{h,cpp}` | the parameter registry: every parameter/group once (id, group, label, range, default, hint, flags). describe, presets and the self test all use it |
| `Presets.{h,cpp}` | data-driven look presets (name, description, `param = value` list) + resolver/validator |
| `Platform.{h,cpp}` | Documents folder, path helpers, UTF-8 on Windows |
| `SelfTest.cpp` | `cel_ofx_selftest` (CTest `ofx_selftest`): registry vs SPEC §7, choice lists vs core enums, presets, path helpers |
| `Version.h.in` | generated `CelVersion.h` (version, build id, date) |

## Parameters

IDs and choice-option order are permanent (stored in projects); append only. Groups:
Look, Input, Negative, Print, Halation, Bloom, Grain, Lens & Gate, Output, LUT Export, License,
About (see `Params.cpp` for every range, default and hint). Labels are unambiguous outside their
group too ("Halation Radius", "Grain Size"), because Resolve's keyframe editor shows labels only.

Derived read-only labels (`lookPresetStatus`, `licenseStatus`, `aboutVersion`) are
non-persistent and refreshed when an instance is created; `licenseStatus` evaluates on change so
activating a license re-renders (watermark removed) immediately.

## Look presets

Choosing a preset applies a complete look: listed parameters get the preset value, every other
look parameter (`ParamDef::look`) returns to its default, in one undo block. Colour spaces,
exposure, mode, HDR, grain seed, LUT and license settings are never touched. The status label
shows `<name>` or `<name> (modified)` (computed by comparing the current values with the resolved
preset, so it is also correct after reloading a project or undo). Values are strings: numbers,
`true/false`, or choice labels. `validatePresets()` runs at load (diagnostics with
`CELLULOID_LOG=1`) and in the self test.

## Rendering

`render()` builds `engine::FrameSettings` (stock scaling: halation × `halationScale`, grain size ×
`grainSizeScale`, bloom × `bloomScale`; mono grain for B&W stocks; Color Only disables grain;
`watermark = lic::needsWatermark(lic::current())`; `pixelScale = imageWidth / 1920`, which is
render-scale correct because the image width already includes the host's render scale), prepares
the instance's cached `core::Pipeline` only when the `LookParams` changed, builds the plan and
runs it on the backend the host asked for.

* **Images**: float RGBA directly; RGB or mismatched source/destination bounds are staged through
  packed RGBA buffers on the CPU path. y = 0 is the first row in memory (OFX bottom row) on every
  backend; the stride is `rowBytes / 4` (negative CPU strides are passed through).
* **Threads**: eRenderInstanceSafe, host frame threading off. One frame at a time on the CPU
  backend process-wide (it already uses every core: no oversubscription, no races on its buffer
  pool); per-API mutex for GPU enqueues.
* **GPU**: OpenCL (buffers), CUDA (+ streams) and Metal are declared for compiled-in backends that
  pass `gpu::selfTest(api, false)`. On a GPU failure the frame is re-rendered on the CPU while the
  images stay on the device (`gpu::cpuFallback`); if that fails too, `kOfxStatGPURenderFailed`
  (or `kOfxStatGPUOutOfMemory`) asks the host to retry on the CPU, and the instance switches its
  `<API>RenderSupported` property to "false".
* **Environment**: `CELLULOID_DISABLE_GPU=1|opencl,cuda,metal` (do not declare those APIs),
  `CELLULOID_LOG=1` (diagnostics on stderr).

## LUT export

`Export LUT` (licensed version only; the trial answers with a message) writes the Color Only
pipeline (no grain/texture) of the current settings as
`<folder>/Celluloid_<input>_<negative>_<print|Lab-Scan|Reversal>_<output>.cube` (17/33/65),
creating the folder (empty field = `<Documents>/Celluloid LUTs/` of the exporting user, resolved
at export time), and reports the path via `sendMessage`. The input-space tag is the part of the
input label before " / " (e.g. `Fujifilm-F-Log2-C`): a .cube maps one camera's code values.
Linear Rec.2020 input is written with a 1D log2 shaper (-8.5..+8.5 stops) in front of the 3D LUT.

## Bundle

```
Celluloid.ofx.bundle/Contents/Info.plist
Celluloid.ofx.bundle/Contents/{MacOS|Win64|Linux-x86-64}/Celluloid.ofx
Celluloid.ofx.bundle/Contents/Resources/de.serhatbarda.CelluloidPro.png   (resources/icon/make_icon.py)
Celluloid.ofx.bundle/Contents/Resources/MANUAL.md, Licenses/
```

Only `OfxGetNumberOfPlugins` and `OfxGetPlugin` are exported (hidden visibility + version script on
Linux, exported-symbols list on macOS, `OfxExport` dllexport on Windows). Installers:
`installers/macos/build_pkg.sh` (→ /Library/OFX/Plugins) and `installers/windows/` (Inno Setup →
Common Files\OFX\Plugins); CI: `.github/workflows/build.yml`.
