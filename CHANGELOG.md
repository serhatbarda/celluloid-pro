# Changelog

All notable changes to Celluloid Pro. Versions follow [Semantic Versioning](https://semver.org);
parameter identifiers and choice orders are permanent, so projects saved with an older version open
unchanged in a newer one.

## [1.3.0] — unreleased

### Added

- **Pipeline Stage** (Look group): run one part of the film chain per node. *Negative -> Log Scan*
  writes the developed negative as a Cineon-style printing-density scan, *Print Only* prints such a
  scan, *Negative -> Scene* gives the negative back as scene light in **Scene Encoding** (ARRI LogC4 /
  AWG4, LogC3 / AWG3, ACEScct, DaVinci Intermediate, Linear Rec.2020) for your own display transform,
  *Negative Only (clean)* is that without any texture, *Texture Only* is the existing mode. Grain,
  halation and damage act in the film layer the stage holds. Default Full Chain: existing projects
  render unchanged. Identical on CPU, OpenCL, CUDA and Metal.
- **Tone group**: control the highlights without darkening the mid-tones.
  - **Zones** (Shadows, Lights, Highlights, Specular, each with a Range, plus a shared Zone Falloff),
    in the style of an HDR wheel palette: exposure for one part of the tonal range, applied to the
    scene light before the negative. The blend is smooth and never reverses tones, and hue is kept.
  - **Filmic Contrast** with **Contrast Pivot**: contrast in log exposure about a pivot (default 18 %
    grey), with film-like saturation.
  - **Film Roll-off** (Start, Softness): reshapes the print's highlight shoulder as it approaches the
    clear base, giving it a longer, softer shoulder or a harder one.
  - **Dye Saturation** (Saturation, Richness, Saturation Balance): subtractive saturation in the film's
    dyes. More saturation adds dye, so strong colours deepen instead of turning neon, while greys,
    white and black stay put.
  - Each part has its own switch, and at the defaults projects render unchanged. Results match across
    CPU, OpenCL, CUDA and Metal.
  - The manual's HDR-wheels-style recipe explains why Exposure alone keeps a bright sky bright: the
    print shoulder squeezes the top five stops into L* 95-99.

### Fixed

- Changing only Pipeline Stage or Scene Encoding in the plug-in could keep rendering the previously
  prepared look until another colour control changed.

- **Print lab** (Print group, module switch *Enable Print Lab*), on every print stock and made for
  the data-sheet prints 2383 / 2393 / 3513DI. Physical lab parameters, not a colour twist:
  - *Projection White*: D65 adapted, Theatre Xenon unadapted, D60 Sim.
  - *Process Aim* (Nominal / Rich / Soft) and *Print Contrast*: print development changes the gamma,
    D-max and fog per layer, and the print is re-timed so grey stays put.
  - *Rendering*: Contact Print; Release Print through interpositive and internegative on Kodak 2242
    (curves from the data sheet, LAD aims); Telecine / Scan with a D-min / D-max balance and a black
    set.

  Off or at the defaults the render is unchanged.
- **Stock provenance**: every stock says where its data comes from (data sheet, partial data sheet +
  proxy, designed, or style approximation). This shows in the read-only *Stock Data* and *Print Stock
  Data* lines and in `docs/STOCKS.md` 5c.
- **More data-sheet stocks** (appended, existing indices unchanged): Kodak Vision2 500T 5218,
  Ektachrome 100D 5294 (reversal) and Double-X 5222 (B&W).
- **Close data-sheet stocks for variety** (owner decision 2026-10-11): Kodak Vision2 Expression 500T
  5229, Fujifilm Eterna Vivid 160 8543, Eterna 500 8573 and Reala 500D 8592 (negatives 22-25), and the
  prints Fujifilm Eterna-CP 3521XD (9) and a corrected Eterna-CP 3513DI (10). Some of them render close
  to an existing stock (down to 0.9 mean dE2000), which is what their data sheets say. The Fujifilm
  negatives borrow VISION3 spectral data, because Fujifilm publishes only their curves.
- The distinctness checks keep 2.0 mean dE2000 for our own designed stocks. Two data-sheet stocks need
  at least 0.7, so none is a duplicate, and the close pairs are listed in the test report.

### Changed

- **Fujifilm Eterna-CP 3513DI**: the curve used since v1.2 was read from the 3521XD figure of the
  Fujifilm data sheet, with a 10 % short exposure axis. Print 7 keeps that curve, so saved projects
  render exactly as before. It is now called *Fujifilm Eterna-CP 3513DI (v1.2 legacy curve)*. The
  corrected 3513DI is the new print 10 and is close to Kodak 2383, like the real stock. The Soft Cinema
  preset still uses print 7.

### Fixed

- **Grey ramps rise into black on every stock pair.** On some pairs (50D on P6, 500T on 2393 or
  3513DI) the print's layers reach D-max at different rates, so the last stops before black turned
  bluer and the red display channel fell by up to 3e-5 as the scene got brighter. The neutral ramp is
  now made to rise from black to grey; nothing changes where it already did (the default look moves
  by at most 6e-5 below -12 stops).

## [1.2.0] — unreleased

### Added

- **Real film stocks from the manufacturers' data sheets** (appended; existing stocks unchanged):
  Kodak Vision3 50D 5203, 250D 5207, 200T 5213 and 500T 5219, Fujifilm Eterna 250D 8563, Kodak
  Portra 400, Kodak Ektachrome E100 and a Kodachrome 64 Style reversal (designed from the E100 data);
  print stocks Kodak Vision 2383, Kodak Vision Premier 2393 and Fujifilm Eterna-CP 3513DI. Spectral
  sensitivities, characteristic curves and dye densities are digitised from the published Kodak and
  Fujifilm technical data (sources in `reference/datasheets/`), then the colour coupling (interlayer,
  masks) is calibrated against the owner's reference renderings.
- **P6 Modern Print**: 2383 spectral data with a modern tone scale - dense lower mids, true blacks,
  soft highlight roll-off.
- **Camera Emulation** (new group, on by default): "Fujifilm X-H2S -> ARRI ALEXA / AMIRA" renders
  F-Log2 C / F-Log2 / F-Log footage with the colours the ALEXA's ALEV III sensor would have recorded.
  Fitted from measured camera spectral sensitivities over ~950 reflectances under 13 illuminants: mean
  colour error to the ALEXA 1.38 -> 1.15 dE2000 (daylight 1.62 -> 1.31, tungsten 1.57 -> 1.25). Greys are
  untouched; Strength blends it in. Other cameras pass through.
- **Print Crossover** (Print group): warm highlights / cool shadows the way release prints render
  them (negative values reverse it). 0 = the print as designed.
- Presets **Studio Modern** (the new default look), **Soft Cinema** (Eterna 250D on Eterna-CP) and
  **Warm Print** (Vision3 250D on 2383 with Print Crossover).
- `tools/look/owner_footage.py`: regression suite on the owner's F-Log2 C shots (metrics, contact
  sheet, baseline check).
- **Film grain, rebuilt.** Grain is now made of dye clouds developed from each colour layer's own
  exposure: a fast (coarse) and a slow (fine) emulsion per layer, calibrated on the published
  KODAK VISION3 granularity curves. Coarse, sparse clumps sit in the shadows, the grain peaks a stop
  under grey and gets finer towards the highlights; the film curves shape it, so it lives inside the
  picture instead of on top of it. Layers stay highly correlated (low chroma noise).
- **Film Resolution** (new group, on by default): the camera negative's own sharpness for the chosen
  gauge (per colour layer, from the VISION3 MTF curves) and **Integrate**, the print/scan resolution
  that picture and grain share (default 0.5).
- **Physical halation** (Halation Threshold 0, the new default): no threshold - light comes back from
  the film base as a thin warm ring around bright edges, added to the film's exposure so the film
  renders it (never magenta). A smooth highlight emphasis gives practicals and speculars their full
  share while a bright overcast sky does not tint the dark window in front of it. Thresholds above 0
  keep the v1.1 behaviour.
- Grain follows the pixel's film area: a proxy (render scale 0.5) shows the grain of the downsampled
  full-resolution frame; 1920 px is the calibration point.

### Changed

- **New default look**: Kodak Vision3 250D 5207 on P6 Modern Print with camera emulation, tuned toward
  the ARRI LogC3 -> Rec.709 rendering (weight 0.6) and the Referent density (0.4). On the owner's eight
  shots the mean colour difference is 2.5 dE2000 to the ARRI rendering and 2.7 to the Referent.
- Hue hold: half strength, no chroma boost, and only beyond one stop from grey: highlight colours
  now desaturate and shift like film and the references do (they were held colorimetric before).
  The designed stocks render slightly differently in strong highlights.
- **Gauge.** Grain Format is now **Gauge** in the Look group (same parameter, same choices) and sets
  the whole texture ladder: grain amplitude 16 mm : Super 35 : 65 mm = 1.7 : 1 : 0.67 and size
  1.25 : 1 : 0.5 (Super 8 coarser still), the film resolution, and the halation and acutance radii
  (a fixed distance on the film, so smaller gauges get larger rings).
- Acutance default 0.3 -> 0.35 (adjacency effect at the gauge's radius). Presets use physical halation
  (their thresholds are gone); Archive 1978 gate weave 0.5 -> 0.4 (≤ 0.3 px rms at 1920).
- Faster texture passes: acutance luminance, glow prefilter, grain noise generation and grain finish
  are fused into neighbouring passes, and the colour pass no longer re-reads the source for alpha.
- Projects saved with 1.1 render with the new grain, halation and film resolution.

- **Personal build by default.** Celluloid Pro is now built for the owner's personal, non-commercial
  use: no license check, no trial watermark, and LUT export is always available. The License group
  is hidden; its parameters still exist, so projects saved with earlier versions (licensed or trial)
  open unchanged. About › Version reads "… - Personal build". Renders are pixel-identical to a
  licensed render of the same settings.
- The licensed variant (offline keys, trial watermark, licensed-only LUT export) is still in the
  source and builds with the CMake option `-DCEL_LICENSING=ON`; its tests pass unchanged.
  `CEL_DEV_LICENSE=ON` now requires `CEL_LICENSING=ON`.
- CI builds the personal build by default (macOS `.pkg` + `.dmg`, Windows installer); the release
  licence gate applies only to licensed builds. Installer texts follow the build variant.
- Documentation: the repository is private (corrected "public" wording).

## [1.1.0] — unreleased

### Added

- **Module switches.** Every module has an Enable checkbox at the top of its controls: Exposure,
  Development (push/pull, colour density, neg bleach bypass, neutralize), Print Timing (printer
  lights, print density, print bleach bypass), Halation, Bloom, Grain, Acutance, Gate Weave, Vignette and
  HDR Highlight Unroll. Unticking a module switches it off without losing its values; a switched-off
  module renders exactly like its neutral setting and costs no render time. Its controls are dimmed.
  The film stocks, Process and Mode always stay active (the stock pair is the look).
- **Film Damage** (new group, off by default): procedural **dust** (white specks from dirt on the
  negative, dark specks and lint from dirt on the print; new every frame), **scratches** (vertical,
  persisting for frames to seconds and wandering; neutral base scratches, coloured emulsion scratches:
  green / yellow / white on the print, yellow / red on the negative) and the occasional **hair** (one
  frame on the film, or caught in the gate for seconds). The damage is applied inside the film model,
  so it prints in the print's own colour and grain. Controls: Dust Amount / Size / White Dust Mix,
  Scratch Amount / Persistence / Emulsion Scratches / White Scratch Mix, Hair Amount, Animated Damage,
  Damage Seed, with Enable switches for the whole module and for dust, scratches and hair.
- **Film Blur** (new group, off by default): now and then the projected picture goes soft and comes
  back (eased soft moments plus gentle focus breathing, optional chromatic softness). Controls: Blur
  Radius, Blur Frequency, Blur Duration, Focus Breathing, Chromatic Softness, Blur Seed.
- Two look presets (appended): **Worn Release Print** and **Found Footage 16**.
- Damage and blur are deterministic per frame and seed, resolution independent, rendered on CPU,
  OpenCL, CUDA and Metal from the same kernels (CPU / OpenCL parity within 2e-4), never in Color Only
  mode or exported LUTs. Validation images on real F-Log2 C footage in `docs/validation/`.

### Changed

- The grain checkbox is labelled "Enable Grain" (identifier `grainEnable` unchanged).

### Compatibility

- New parameters only; no identifier or choice order changed. The new switches default to on, so
  projects saved with 1.0 open and render unchanged, and a look preset applied in 1.0 still shows its
  name without "(modified)".
- Film Damage and Film Blur default to off, so 1.0 projects stay clean. The two new presets are
  appended to the Look Preset menu (existing preset indices unchanged).

## [1.0.0] — unreleased

First release of Celluloid Pro, the spectral film-emulation OpenFX plug-in for DaVinci Resolve.

### Platforms

- macOS 11 or later, universal binary (Apple silicon and Intel), Metal GPU rendering; OpenCL offered
  to hosts that use it.
- Windows 10/11 x64, CUDA (NVIDIA, Maxwell and newer) and OpenCL (AMD, Intel) GPU rendering.
- Multithreaded CPU fallback on both platforms, producing the same image as the GPU paths.
- Installers: macOS `.pkg` (in a `.dmg`) to `/Library/OFX/Plugins`; Windows Inno Setup installer to
  `Common Files\OFX\Plugins` (English and German) plus a zipped bundle for manual installs.
  Signing and notarization switch on when the signing certificates are configured in CI.

### Colour science

- Spectral negative/print model (380–780 nm, 5 nm): smooth spectral upsampling of the scene,
  designed layer sensitivities, characteristic curves with toe, shoulder, D-min and orange mask,
  dye absorption spectra with secondary absorptions and masking couplers.
- Development: push/pull (−2 … +3 stops), interlayer (DIR) colour density, negative bleach bypass,
  neutralize.
- Print with printer lights in lab points and automatic grey balance, print bleach bypass,
  per-pixel spectral projection; or Lab Scan (narrow-band scanner, balanced inversion); reversal
  and black & white paths.
- Inputs: Fujifilm F-Log2 C / F-Gamut C (default), F-Log2, F-Log, DaVinci Intermediate, ARRI LogC3
  and LogC4, Sony S-Log3, Panasonic V-Log, Canon Log 3, Blackmagic Film Gen 5, ACEScct, Linear
  Rec.2020.
- Outputs: Rec.709 Gamma 2.4, Rec.709 sRGB, P3-D65, DCI-P3 (D65 simulated), Rec.2100 PQ and HLG with
  smooth gamut compression and an HDR highlight unroll (peak 300–4000 nits).

### Film stocks and looks

- 11 negative stocks: CN 50D Fine Daylight, CN 200T Studio Tungsten, CN 250D Soft Daylight,
  CN 500T High Speed, CN 800T No-Backing, CN Vintage 85, C41 Portrait 400, C41 Consumer 200,
  RV 100 Slide (reversal), BW 400 Panchromatic, BW 100 Fine.
- 5 print stocks: P1 Standard Projection, P2 Low Contrast, P3 Rich Contrast, P4 Faded Archive,
  P5 Silver Retention.
- 12 look presets: Studio Classic, Tungsten Night Drama, Soft Daylight Pastel, Archive 1978, Silver
  Retention Thriller, Neon Nocturne, Golden Hour Portrait, Crisp Commercial, Summer Snapshot, Slide
  Show Brilliance, Monochrome Noir, Home Movie Super 8 — with a "(modified)" status after edits.
- All stocks and looks are original designs.

### Texture

- Halation with a soft-knee threshold in stops, multi-scale long-tail glow and hue control; neutral
  bloom; development acutance; gate weave; optical vignette.
- Format-aware film grain (Super 8, 16 mm, Super 35, 65 mm, 35 mm stills) in the negative's density
  domain: clumpy, band-limited, layer-correlated, animated or frozen, seedable; monochrome for black &
  white stocks.
- Resolution independent: radii and grain size scale with the image width, so proxies and render
  scales match the final render.
- Modes: Full, Color Only and Texture Only (texture on an already graded image).

### Workflow

- `.cube` LUT export (17³/33³/65³) of the Color Only pipeline, named after input, stocks and output;
  Linear Rec.2020 exports include a 1D log2 shaper. Licensed version only.
- Offline license keys (Ed25519): activation in the License group, per-user, machine-wide or
  environment-variable keys; no internet connection required. Without a key: full image features with
  a small trial watermark; LUT export disabled.
- User manual inside the plug-in bundle.

### Known limitations

- Not yet verified inside DaVinci Resolve (testing in progress), on NVIDIA hardware (CUDA compiled but
  not executed) or with real-GPU timings; see the manual, section 13.
- The production license public key is provisional until the owner's key ceremony; release tags are
  blocked until then.
