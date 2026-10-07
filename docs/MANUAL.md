# Celluloid Pro — User Manual

Celluloid Pro is an OpenFX film-emulation plug-in for DaVinci Resolve (macOS and Windows). It models
a photographic chain spectrally: the camera image becomes exposure on a colour negative, the negative
is developed, printed onto print stock (or scanned by a lab scanner) and projected — with halation,
bloom, film grain, lens acutance, gate weave and vignetting on top. All stocks, looks and the colour
science are original designs of this plug-in; none imitates a particular commercial product or film
title.

Contents: 1. Installation · 2. First steps · 3. Resolve project setups · 4. Workflow recipes ·
5. Controls reference · 6. Look presets · 7. LUT export · 8. HDR · 9. GPU, performance, threads ·
10. License · 11. Troubleshooting

## 1. Installation

- **macOS 11 or later** (Apple silicon and Intel): open the disk image and run
  `CelluloidPro-<version>-macOS.pkg`. The plug-in is installed to
  `/Library/OFX/Plugins/Celluloid.ofx.bundle`. If the installer is not notarized yet, right-click it
  and choose Open (or allow it under System Settings > Privacy & Security).
- **Windows 10/11 x64**: run `CelluloidPro-<version>-Windows-Setup.exe`. The plug-in is installed to
  `C:\Program Files\Common Files\OFX\Plugins\Celluloid.ofx.bundle`. A zipped bundle for manual
  installation is also provided.
- Restart DaVinci Resolve. The effect appears under **OpenFX > Celluloid > Celluloid Pro**.
- To uninstall, delete `Celluloid.ofx.bundle` from the OFX plug-in folder (Windows: Settings > Apps >
  Celluloid Pro). License keys are stored per user (section 10) and are not removed.

## 2. First steps

1. Add a serial node to a clip and drag **Celluloid Pro** onto it.
2. Set **Camera Log** (Input group) to the log encoding of your footage. The default is Fujifilm
   F-Log2 C / F-Gamut C. The node expects the *camera log signal*: do not convert the clip to Rec.709
   before this node.
3. Set **Output** to your delivery / monitoring space (Rec.709 Gamma 2.4 for most SDR work).
4. Pick a **Look Preset**, or choose a negative (Film Stock) and a Print Stock yourself.
5. Expose: a grey card should land at **Exposure 0**. Exposure moves the image along the film curve
   (highlights roll off, shadows build density like a real over- or under-exposed negative).
6. Balance with the **printer lights** (Print group), as in a film lab. **Print Density** brightens or
   darkens the print.

Do the primary balance (white balance, exposure matching between shots) **before** Celluloid Pro, in
the camera log space; secondary corrections and windows go before it as well, so they are
"photographed" by the film. Use Texture Only mode (section 4) when the colour is already graded.

## 3. Resolve project setups

**DaVinci YRGB (not colour managed)** — simplest and recommended. The clip arrives in its camera log.
Set Camera Log to the camera and Output to the project's output (e.g. Rec.709 Gamma 2.4). Put
Celluloid Pro as the last node (or before a final trim node). No LUT or colour space transform is
needed.

**DaVinci YRGB Color Managed (RCM)** — set the clip's Input Colour Space to the camera and the
**Timeline Colour Space** to a log space the plug-in knows, for example *DaVinci Wide Gamut / DaVinci
Intermediate*; set Camera Log = DaVinci Intermediate / DaVinci Wide Gamut. Set Resolve's Output Colour
Space to the same space as the plug-in's Output and switch off output tone and gamut mapping in
Project Settings > Color Management (the plug-in already renders a display image). Alternatively use a
Colour Space Transform before the node (timeline space → the camera log selected in the plug-in) and
one after it (the plug-in's Output → timeline space).

**ACES (ACEScct)** — set Camera Log = ACEScct / AP1 and Output to your display. The plug-in renders a
finished display image, so bypass the ACES output transform for this timeline, or add a Colour Space
Transform from the plug-in's Output back to ACEScct when the output transform must stay on.

**HDR** — set Output to Rec.2100 PQ or HLG and the project output to the same; see section 8.

## 4. Workflow recipes

- **Full look on log footage**: Mode Full, a preset, then Exposure, Print Density and the printer
  lights per shot.
- **Colour now, texture at the end**: Mode Color Only on the grade node and a second instance in
  Texture Only mode at the end of the node tree (or on the timeline), so grain and halation are added
  once to the final image.
- **Texture on an existing grade**: Texture Only adds halation, bloom, grain, acutance, gate weave and
  vignette to footage that is ALREADY display-referred in the selected Output space; colour is left
  untouched.
- **Matching two cameras**: one node per camera group, each with its own Camera Log, the same stocks
  and printer lights; both cameras are rendered through the same negative.
- **Printer-light grading**: keep the look's stocks and grade with Printer Red / Green / Blue and Print
  Density only. The numbers mean the same correction on every stock (they are relative to each stock's
  automatic grey balance).
- **On-set and in-camera monitoring**: export a LUT for the camera's log (section 7).

## 5. Controls reference

The tool tip of every control in Resolve shows the same text.

### Look

- **Look Preset** (default: Custom) — Curated starting points. Choosing a look sets the stock pair, development, print, halation, bloom, grain and lens settings in one go (input/output colour space, exposure and HDR settings are kept). Any later tweak keeps your changes; the status line below then shows "(modified)". Choose "Custom" to leave all settings as they are.  Studio Classic: The house look: balanced tungsten negative on the standard projection print - natural skin, dense mid-tones, fine Super 35 grain.  Tungsten Night Drama: High-speed tungsten negative pushed half a stop on the rich-contrast print, timed cool: deep blacks, warm practicals, moody night interiors.  Soft Daylight Pastel: Soft daylight negative on the low-contrast print, printed light and warm: airy pastel colour, open shadows, gentle bloom.  Archive 1978: Vintage negative on a faded archive print: golden warmth, lifted brown blacks, 16 mm grain, gate weave and lens fall-off.  Silver Retention Thriller: Pushed tungsten negative with partial bleach bypass on the silver-retention print: hard, metallic, desaturated contrast with crisp edges.  Neon Nocturne: Fast no-backing tungsten negative for city nights: red-orange halation around neon and practicals, coarse but soft grain, cool timing.  Golden Hour Portrait: Portrait colour negative through the lab scanner, timed warm: smooth golden skin, soft contrast, fine stills grain.  Crisp Commercial: Fine-grain daylight negative with extra colour density on the standard print: clean, saturated, sharp - restrained halation and grain.  Summer Snapshot: Consumer colour negative scanned like a lab print, timed warm: punchy holiday colour, visible stills grain, light vignette.  Slide Show Brilliance: Daylight reversal (slide) film projected directly: brilliant saturation, deep blacks, short latitude - expose carefully.  Monochrome Noir: Panchromatic black & white negative pushed one stop on the rich-contrast print: inky blacks, glowing highlights, strong vignette.  Home Movie Super 8: Reversal film in the Super 8 gauge: bold colour, big soft grain, warm halation, nervous gate weave and heavy vignetting.
- **Mode** (default: Full) — Full: complete film emulation (colour + halation, bloom, grain, lens and gate effects). Color Only: the colour pipeline alone - exactly what an exported LUT contains. Texture Only: for footage that is ALREADY graded/display-referred in the selected Output space; adds only the photographic texture (halation, bloom, grain, acutance, weave, vignette) and leaves the colour untouched. Use it after your own grade or after another film look.

### Input

- **Camera Log** (default: Fujifilm F-Log2 C / F-Gamut C) — The log encoding and gamut of the incoming footage. Set this to match your camera (default: Fujifilm F-Log2 C / F-Gamut C). In a colour-managed Resolve project, use a timeline/working space that keeps the camera log intact for this node, or convert to the matching log space before this node. Wrong settings show up as overly flat or harsh, colour-shifted images.
- **Exposure** (-4 … 4, default 0) — Camera exposure correction in stops, applied to the scene before it reaches the film. Because it acts before the negative, +/- exposure slides the image along the film's characteristic curve: highlights roll off and shadows build density the way a real over/under-exposed negative does.

### Negative

- **Film Stock** (default: CN 200T Studio Tungsten) — The camera negative (or reversal / black & white stock). It defines spectral sensitivity, tone curve, colour crosstalk, grain character and halation behaviour.  CN 50D Fine Daylight: Fine-grain daylight negative: clean, crisp and richly saturated with a neutral-cool balance.  CN 200T Studio Tungsten: Balanced tungsten studio negative: natural skin, dense mids, gently warm highlights. The reference stock.  CN 250D Soft Daylight: Low-contrast daylight negative: soft pastel colour, wide latitude, open shadows.  CN 500T High Speed: High-speed tungsten negative: visible grain, cool teal shadows, warm highlights.  CN 800T No-Backing: Fast tungsten negative without anti-halation backing: strong red halation, cyan-leaning shadows, warm compressed highlights, coarse grain.  CN Vintage 85: Vintage 1980s-style tungsten negative: golden highlights, green-teal shadows, softer saturation, denser base fog.  C41 Portrait 400: Portrait colour negative: flattering, smooth skin, low contrast, soft saturation.  C41 Consumer 200: Consumer colour negative: punchy, warm and saturated with yellow-leaning highlights.  RV 100 Slide: Daylight colour reversal (slide) film: brilliant saturation, high contrast, deep blacks, short latitude.  BW 400 Panchromatic: Classic panchromatic black & white negative: rich mid-tones, visible grain, natural tonal response to colour.  BW 100 Fine: Fine-grain black & white negative: crisp, higher contrast, slightly blue-leaning (darker reds) panchromatic response.
- **Push / Pull** (-2 … 3, default 0) — Development in stops. Push (+) raises contrast and effective speed, lifts fog and makes the grain coarser (about x1.25 per stop); pull (-) gives a softer, lower-contrast, finer-grained negative. Unlike Exposure this changes the curve shape, not the position on it.
- **Color Density** (0 … 2, default 1) — Interlayer (DIR) effect: how strongly the dye layers inhibit each other. 1 = the stock's nominal colour density; higher values give denser, more separated colours, 0 a flatter, purer-channel rendering. Neutrals stay neutral at any setting.
- **Neg Bleach Bypass** (0 … 1, default 0) — Skips (partially) the bleach step of the negative so silver stays in the film alongside the dyes. The extra silver density builds with exposure: higher contrast, harder highlights and muted colour, strongest in the bright parts of the image. 0 = normal processing.
- **Neutralize** (0 … 1, default 0) — Removes the stock's per-layer curve differences (the subtle colour crossover between shadows and highlights). 0 = the stock's natural character, 1 = perfectly neutral greys from black to white.

### Print

- **Process** (default: Print) — Print: the negative is printed onto print stock and projected - the classic cinema look with dense blacks and rich colour. Lab Scan: the negative is scanned and inverted by a lab scanner - a flatter, more neutral rendering that keeps the negative's character without print-stock contrast. Reversal (slide) stocks are direct positives and ignore this setting.
- **Print Stock** (default: P1 Standard Projection) — The print film the negative is printed on (Process = Print). It sets print contrast, black density, highlight roll-off and the colour of the projected image.
- **Printer Red** (-12 … 12, default 0) — Red printer light. More red light (+) adds cyan to the print: the image gets LESS red / more cyan. Fewer points (-) warm it towards red. Printer points as in a film lab: 1 point = 0.025 log exposure on that print layer, 0 = the stock's automatically grey-balanced light.
- **Printer Green** (-12 … 12, default 0) — Green printer light. More green light (+) adds magenta to the print: the image gets LESS green / more magenta. Fewer points (-) push it towards green. Printer points as in a film lab: 1 point = 0.025 log exposure on that print layer, 0 = the stock's automatically grey-balanced light.
- **Printer Blue** (-12 … 12, default 0) — Blue printer light. More blue light (+) adds yellow to the print: the image gets LESS blue / more yellow. Fewer points (-) cool it towards blue. Printer points as in a film lab: 1 point = 0.025 log exposure on that print layer, 0 = the stock's automatically grey-balanced light.
- **Print Density** (-12 … 12, default 0) — Overall printer light in points on all three layers. Positive values make the print BRIGHTER (less print density), negative values darker and denser. This is the lab's way to brighten or darken a print: highlights and blacks respond like real print film, not like a gain.
- **Print Bleach Bypass** (0 … 1, default 0) — Bleach bypass on the print: retained silver deepens the blacks and shadows, raises contrast and desaturates - the classic gritty, metallic release-print look. 0 = normal processing.

### Halation

- **Halation Amount** (0 … 2, default 0.5) — Strength of halation: light that passes through the emulsion, reflects off the film base and re-exposes the red layer, creating a warm glow that hugs bright edges and practicals. Scaled by the stock (no-backing stocks glow much more). 0 = off.
- **Halation Radius** (2 … 80, default 20) — Spread of the glow in pixels at 1920 px image width (automatically scaled for other resolutions and proxy render scales).
- **Halation Threshold** (0 … 6, default 3.5) — How bright a highlight must be before it halates, in stops above middle grey. The glow eases in over about a stop and a half above it, so broad bright areas (an overcast sky) barely halate while light sources and speculars glow fully. Lower values let more of the image glow.
- **Halation Hue** (-1 … 1, default 0) — Colour of the glow: -1 deep red, 0 the classic red-orange, +1 orange-yellow.

### Bloom

- **Bloom Amount** (0 … 2, default 0.15) — Gentle, neutral glow of bright areas from lens and emulsion scatter - softens harsh digital highlights without the warm tint of halation. Scaled by the stock. 0 = off.
- **Bloom Radius** (5 … 200, default 40) — Spread of the bloom in pixels at 1920 px image width (scaled automatically for other resolutions).

### Grain

- **Grain** (default: on) — Adds film grain. Grain is added to the dye densities of the negative, so it behaves like real grain: strongest in the mid-tones and shadows of the negative, varying per colour layer.
- **Grain Format** (default: Super 35) — Film gauge the image was 'shot' on. Smaller formats enlarge the grain more: Super 8 is very coarse, 16 mm clearly visible, Super 35 the cinema standard, 65 mm very fine; 35 mm Stills (36x24 full frame) is finer than Super 35.
- **Grain Amount** (0 … 3, default 1) — Grain strength. 1 = the stock's nominal granularity for the chosen format and development; push processing increases it automatically.
- **Grain Size** (0.5 … 3, default 1) — Grain size relative to the stock and format (1 = nominal). Resolution independent: it looks the same at HD, UHD and in proxy renders.
- **Grain Softness** (0 … 1, default 0.5) — 0 = crisp, sharply defined grains (like a sharp scan), 1 = soft clumps (like an optical print or a softer scan).
- **Animated Grain** (default: on) — On: a new grain pattern every frame, like real film. Off: a frozen pattern (useful for stills or for matching a static texture).
- **Grain Seed** (0 … 9999, default 0) — Changes the random grain pattern. Give different shots or layers different seeds so their grain never lines up.

### Lens & Gate

- **Acutance** (-1 … 2, default 0.3) — Edge contrast from development adjacency effects: crisp, film-like apparent sharpness without the halos of digital sharpening. Negative values soften the image.
- **Acutance Radius** (0.5 … 10, default 2) — Size of the adjacency effect in pixels at 1920 px width. Small = fine micro-contrast, larger = broader edge enhancement.
- **Gate Weave** (0 … 2, default 0) — Subtle frame-to-frame movement of the film in the camera/projector gate, mostly vertical. 1 = typical 35 mm projection; keep it low for a believable result. 0 = off.
- **Weave Speed** (0.25 … 4, default 1) — How quickly the gate weave wanders (1 = natural; lower is a slow drift, higher a nervous jitter).
- **Vignette** (0 … 1, default 0) — Natural lens light fall-off towards the corners, applied to the scene light before the film (so it darkens like an optical vignette, not a grey overlay). 0 = off.

### Output

- **Output** (default: Rec.709 (Gamma 2.4)) — The display the result is encoded for. Rec.709 Gamma 2.4 for broadcast/HD grading monitors, sRGB for web/computer displays, P3 for cinema (DCI) or Apple/P3 displays, PQ or HLG for HDR deliverables. Your timeline/output colour space should match. In Texture Only mode this is the encoding of the incoming (already graded) image.
- **HDR Peak (nits)** (300 … 4000, default 1000) — PQ output only: peak luminance of the HDR master. The film shoulder is unrolled smoothly so the brightest highlights reach this level.
- **HDR Highlight Unroll** (0 … 1, default 1) — PQ/HLG only: 0 places the SDR print look in HDR unchanged (soft film highlights), 1 expands the print shoulder to use the full HDR range while keeping mid-tones and colour consistent with SDR.

### LUT Export

- **LUT Size** (default: 33) — Lattice size of the exported 3D LUT. 33 is the usual choice; 65 is the most accurate (larger file); 17 for devices with small LUT memory.
- **Folder** — Folder the LUT is written to (created if needed). Leave empty for the default folder <Documents>/Celluloid LUTs of the user who exports. File name: Celluloid_<input>_<negative>_<print>_<output>.cube. Add this folder to Resolve's LUT list (Project Settings > Color Management > Open LUT Folder) to use the LUTs elsewhere.
- **Export LUT** — Licensed version only. Writes the current colour settings (Color Only: input space, exposure, stocks, development, print, output; no grain or other texture) as a .cube 3D LUT for on-set monitoring, cameras or other applications. The LUT expects the selected camera log as input (Linear Rec.2020: the file carries a log2 shaper).

### License

- **License Status** — Current license state. The trial includes every feature; renders carry a small watermark until a license key is activated.
- **License Key** — Paste the license key you received by e-mail (starts with CELL1-), then press Activate. The key is stored for your user account, not in the project; the field is cleared after activation.
- **Activate** — Verifies the pasted key and activates Celluloid Pro on this computer.
- **Deactivate** — Removes the license from this computer (for example before moving it to another machine). Renders return to trial mode.

### About


## 6. Look presets

Twelve curated looks are in the **Look Preset** menu: Studio Classic, Tungsten Night Drama, Soft
Daylight Pastel, Archive 1978, Silver Retention Thriller, Neon Nocturne, Golden Hour Portrait, Crisp
Commercial, Summer Snapshot, Slide Show Brilliance, Monochrome Noir and Home Movie Super 8. A preset
sets every look parameter (stocks, development, print, printer lights, halation, bloom, grain, lens and
gate); input colour space, Exposure, Mode, Output, HDR, LUT export, license and the grain seed are never
changed. After any edit the Preset Status line shows "<name> (modified)".

## 7. LUT export (licensed version)

**Export LUT** writes the Color Only pipeline of the current settings (input space, exposure, stocks,
development, print, printer lights, output — no grain, halation or other texture) as a `.cube` 3D LUT
with 17³, 33³ or 65³ points. The file is named `Celluloid_<input>_<negative>_<print>_<output>.cube`
and written to the **Folder** field, or — when the field is empty — to `<Documents>/Celluloid LUTs/`
of the user who exports. A LUT maps one camera's log code values: export one per camera. For Linear
Rec.2020 input the file carries a 1D log2 shaper (−8.5 … +8.5 stops around mid grey) in front of the 3D
LUT. A 33³ LUT is accurate to about 0.03 code values (99th percentile) against the plug-in; use 65³ for
critical work. LUT export is not available in the trial.

## 8. HDR

With Output = Rec.2100 PQ or HLG the print is shown on an HDR display: mid grey sits at about 15 nits,
and **HDR Highlight Unroll** expands the print's shoulder smoothly toward **HDR Peak** (PQ; HLG uses
the 1000-nit reference). Unroll 0 places the SDR print look unchanged in the HDR container (soft film
highlights); 1 uses the full HDR range while mid-tones and colour stay consistent with SDR.

## 9. GPU, performance, threads

Celluloid Pro renders on the GPU Resolve uses: Metal on macOS, CUDA or OpenCL on Windows. The GPU
backends are tested when the plug-in loads, and only backends that pass are offered to Resolve. If a GPU
frame fails (for example when GPU memory runs out), the plug-in renders that frame on the CPU or asks
Resolve to do so. On the CPU the plug-in uses all cores for one frame at a time. Color Only is the
fastest mode; halation, bloom and grain add several full-resolution passes. For support cases:
`CELLULOID_DISABLE_GPU=1` (or a list such as `opencl,cuda,metal`) and `CELLULOID_LOG=1` (diagnostics
on the standard error output).

## 10. License

Without a license key the plug-in runs as a **trial**: every feature except LUT export, with a small
"CELLULOID PRO — TRIAL" badge and a faint diagonal band on renders. To activate, paste your key (it
starts with `CELL1-`) into **License Key** and press **Activate**; **Deactivate** removes it from this
computer. The key is stored for your user account:

- macOS: `~/Library/Application Support/Celluloid/license.key`
- Windows: `%APPDATA%\Celluloid\license.key`

Studios can install a key machine-wide (macOS `/Library/Application Support/Celluloid/license.key`,
Windows `%PROGRAMDATA%\Celluloid\license.key`) or provide it in the environment variable
`CELLULOID_LICENSE_KEY` (render nodes). A paid key from any of these places takes precedence over an
evaluation (Trial edition) key. Keys are verified offline; no internet connection is needed.

## 11. Troubleshooting

- **Flat, harsh or colour-shifted image**: Camera Log does not match the footage, or the clip was
  converted before the node (section 3).
- **Too dark or too bright**: Exposure for the scene, Print Density for the print.
- **Grain looks wrong in a scaled viewer**: grain is resolution-aware; judge it at 100 %.
- **Halation on every bright edge**: raise Halation Threshold (stops over mid grey) or lower its Amount.
- **"LUT export is part of the licensed version"**: activate a license key first.
- **The plug-in does not appear in Resolve**: check that `Celluloid.ofx.bundle` is in the OFX plug-in
  folder and restart Resolve; on macOS confirm that the installer was allowed to run.
- **Slow renders**: check that Resolve uses the GPU (Preferences > Memory and GPU); with
  `CELLULOID_LOG=1` the plug-in logs which GPU backends passed their self test.
