# Celluloid Pro — User Manual

Version 1.0.0 · OpenFX plug-in for DaVinci Resolve on macOS and Windows

Celluloid Pro turns camera log footage into a photochemical film image. It does not apply a fixed
LUT: it simulates the chain a frame of film goes through. The scene exposes a colour negative
through the spectral sensitivities of its three layers; the negative is developed (with push/pull,
interlayer effects and optional bleach bypass); it is printed onto print stock with printer lights,
or scanned by a lab scanner; and the result is projected and encoded for your display. Halation,
bloom, film grain, development acutance, gate weave and lens vignetting are added at the point in
the chain where they physically happen.

All film stocks, look presets and colour science in Celluloid Pro are original designs. They are
not copies or measurements of any manufacturer's product, and their names deliberately do not
refer to real film stocks.

**Contents**

1. [Requirements and installation](#1-requirements-and-installation)
2. [First steps](#2-first-steps)
3. [Setting up Resolve: YRGB, RCM and ACES](#3-setting-up-resolve-yrgb-rcm-and-aces)
4. [Thinking like a film lab](#4-thinking-like-a-film-lab)
5. [Controls reference](#5-controls-reference)
6. [Film stocks and look presets](#6-film-stocks-and-look-presets)
7. [Workflow recipes](#7-workflow-recipes)
8. [LUT export](#8-lut-export)
9. [HDR](#9-hdr)
10. [Personal build and licensing](#10-personal-build-and-licensing)
11. [Performance](#11-performance)
12. [Troubleshooting](#12-troubleshooting)
13. [Known limitations](#13-known-limitations)

---

## 1. Requirements and installation

### Requirements

| | macOS | Windows |
|---|---|---|
| Operating system | macOS 11 Big Sur or later | Windows 10 or 11, 64-bit (x64) |
| Processor | Apple silicon or Intel (one universal binary) | x64 |
| GPU rendering | Metal (any Mac that runs macOS 11); OpenCL is also offered to hosts that use it | NVIDIA: CUDA (GeForce/Quadro GTX 900 generation or newer, current driver). AMD and Intel: OpenCL 1.2 or later |
| Host | DaVinci Resolve or DaVinci Resolve Studio with OpenFX support | same |
| GPU memory | about 0.5 GB for UHD, 1.5 GB for 8K, on top of what Resolve needs | same |

Without a usable GPU, Celluloid Pro renders on the CPU with all cores. That is much slower, but the
image is the same: CPU and GPU run the same colour and texture code.

Linux builds exist only for development and testing; they are not a product.

### Installing on macOS

1. Quit DaVinci Resolve.
2. Open `CelluloidPro-<version>-macOS.dmg` and double-click `CelluloidPro-<version>-macOS.pkg`
   (the disk image also contains this manual and a short *READ ME FIRST*).
3. Follow the installer. The plug-in is installed to
   `/Library/OFX/Plugins/Celluloid.ofx.bundle`; an older version is removed first.
4. Start Resolve. The effect is in the Effects library under **OpenFX › Celluloid › Celluloid Pro**.

**Unsigned installers (Gatekeeper).** Until a Developer ID certificate is in place, the installer
is not signed or notarized and macOS refuses to open it with a message that it "cannot be verified".
To install anyway:

- macOS 14 and earlier: right-click (Control-click) the `.pkg`, choose **Open**, then **Open** again.
- macOS 15 and later: double-click the `.pkg` once and dismiss the warning, then open
  **System Settings › Privacy & Security**, scroll down and click **Open Anyway** next to the
  message about `CelluloidPro-….pkg`, and confirm with your password.

The installer clears the quarantine flag of the installed plug-in, so Resolve can load it. Signed
and notarized installers open without these steps.

### Installing on Windows

1. Quit DaVinci Resolve.
2. Run `CelluloidPro-<version>-Windows-Setup.exe` (administrator rights are required, because OFX
   plug-ins live in a system folder). The installer speaks English and German.
3. The plug-in is installed to `C:\Program Files\Common Files\OFX\Plugins\Celluloid.ofx.bundle`.
4. Start Resolve; the effect is under **OpenFX › Celluloid › Celluloid Pro**.

**Unsigned installer (SmartScreen).** Until the installer is Authenticode-signed, Microsoft
Defender SmartScreen may show "Windows protected your PC". Click **More info**, check that the file
name is `CelluloidPro-<version>-Windows-Setup.exe`, then click **Run anyway**.

**Manual installation (zip).** `CelluloidPro-<version>-Windows-bundle.zip` contains the bare
`Celluloid.ofx.bundle` folder. Copy the whole folder into
`C:\Program Files\Common Files\OFX\Plugins\` (create `OFX\Plugins` if it does not exist). If
Windows blocked the downloaded zip, right-click it › **Properties** › **Unblock** before extracting.

### Updating and uninstalling

Install a new version over the old one; the installers replace the previous bundle. Resolve keeps
your node settings, because the parameter identifiers never change between versions.

- macOS: quit Resolve and delete `/Library/OFX/Plugins/Celluloid.ofx.bundle`.
- Windows: **Settings › Apps › Installed apps › Celluloid Pro (OFX plug-in) › Uninstall**, or delete
  the bundle folder if you installed from the zip.

The personal build uses no license key. In the licensed build, license keys are stored per user
(section 10) and are not removed on uninstall.

### Where is this manual?

A copy of this manual is installed inside the plug-in bundle,
`Celluloid.ofx.bundle/Contents/Resources/MANUAL.md`, and the plug-in's **About** group points to it.
Third-party notices are in `Contents/Resources/Licenses/`.

---

## 2. First steps

1. On the Color page, add a serial node to a clip and drag **Celluloid Pro** onto it.
2. Set **Camera Log** (Input group) to the log encoding your footage was recorded in. The default is
   **Fujifilm F-Log2 C / F-Gamut C**. The node expects the *camera log signal*: do not convert the
   clip to Rec.709 (with a LUT, CST or colour management) before this node. See section 3.
3. Set **Output** (Output group) to your monitoring/delivery display: Rec.709 Gamma 2.4 for most SDR
   work.
4. Choose a **Look Preset**, or pick a **Film Stock** and a **Print Stock** yourself.
5. Expose: put a grey card at the right brightness with **Exposure**. Exposure acts *before* the
   negative, like opening the lens: highlights roll off and shadows build density the way an over-
   or under-exposed negative does.
6. Time the print: brighten or darken the print with **Print Density**, and balance its colour with
   **Printer Red / Green / Blue**, the way a film lab does.

**Where the node goes.** Primary balance (white balance, exposure matching between shots) and
secondaries (keys, windows) belong *before* Celluloid Pro, in the camera log space, so the film
"photographs" your corrected scene. Celluloid Pro then produces a finished display image: put only
light trims after it. When the colour is already finished by another tool, use **Texture Only**
mode (section 7).

**Mode at a glance.**

- **Full** — colour and texture: the complete film look.
- **Color Only** — the colour pipeline alone. Faster; it is exactly what an exported LUT contains.
- **Texture Only** — only halation, bloom, grain, acutance, gate weave and vignette, on an image that
  is *already* display-referred in the selected Output space.

---

## 3. Setting up Resolve: YRGB, RCM and ACES

Celluloid Pro needs two things from the project: the clip must arrive at the node **in the camera
log selected under Camera Log**, and whatever comes after the node must treat its output as a
**display image in the selected Output space**. The setups below achieve that.

> These setups follow Resolve's documented colour management. Celluloid Pro has been verified in
> our own OpenFX test host and in CI, not yet inside every Resolve version; if your Resolve behaves
> differently, the general rule above still tells you what to do.

### 3.1 DaVinci YRGB (not colour managed) — recommended

This is the simplest and most transparent setup, and the recommended one for F-Log2 C.

1. **Project Settings › Color Management**: Color science *DaVinci YRGB*. Timeline colour space
   and output colour space are labels only in this mode; set them to Rec.709 Gamma 2.4 for SDR
   (or your HDR format, section 9).
2. Do not apply a camera LUT or CST to the clips.
3. Celluloid Pro: **Camera Log = Fujifilm F-Log2 C / F-Gamut C**, **Output = Rec.709 (Gamma 2.4)**.
4. Grade before the node in F-Log2 C (Offset for exposure and balance works well in log), put
   Celluloid Pro last or before a final trim node.

The same works for every camera in the Camera Log list: ARRI LogC3 and LogC4, Sony S-Log3,
Panasonic V-Log, Canon Log 3, Blackmagic Film Gen 5, Fujifilm F-Log / F-Log2, DaVinci Intermediate,
ACEScct, and scene-linear Rec.2020.

**F-Log2 C or F-Log2?** Both use the same F-Log2 curve; F-Log2 C pairs it with the wider *F-Gamut C*
primaries. Use the entry that matches the camera's recording setting. Choosing the wrong one does
not change brightness but shifts saturated colours (F-Gamut C read as F-Gamut looks oversaturated).

### 3.2 DaVinci YRGB Color Managed (RCM)

In RCM, Resolve converts each clip into the timeline colour space and converts the timeline space to
the output space at the end. Celluloid Pro sits in between, so it must receive a log space it knows
and the conversion after it must be undone.

1. **Project Settings › Color Management**: Color science *DaVinci YRGB Color Managed*, colour
   processing mode *Custom*. Timeline colour space **DaVinci Wide Gamut / DaVinci Intermediate**.
   Output colour space **Rec.709 Gamma 2.4** (or your HDR format). Switch **output tone mapping** and
   **output gamut mapping** to *None*.
2. Set each clip's Input Colour Space to its camera (right-click the clip › Input Color Space).
3. Celluloid Pro: **Camera Log = DaVinci Intermediate / DaVinci Wide Gamut**, **Output = Rec.709
   (Gamma 2.4)** (the same display as the project output).
4. Add a node **after** Celluloid Pro with a **Color Space Transform**: Input *Rec.709 / Gamma 2.4*
   → Output *DaVinci Wide Gamut / DaVinci Intermediate*, tone mapping *None*, gamut mapping *None*,
   *Apply Forward OOTF* and *Apply Inverse OOTF* off. Resolve's output conversion then returns the
   image exactly to what Celluloid Pro rendered.

*If your Resolve version has no input colour space for F-Log2 C*, set those clips' Input Colour
Space to **Bypass**, set **Camera Log = Fujifilm F-Log2 C / F-Gamut C** on their Celluloid Pro node
and keep the CST after it. Or use the YRGB setup above, which avoids the question altogether.

### 3.3 ACES (ACEScct)

1. **Project Settings › Color Management**: Color science *ACEScct*.
2. Celluloid Pro: **Camera Log = ACEScct / AP1**, Output = the display of your ACES Output
   Transform (for example Rec.709 Gamma 2.4).
3. Undo the Output Transform after the node, choosing one of:
   - set **ACES Output Transform** to *No Output Transform* when Celluloid Pro is on every clip or on
     a timeline node, so its display image goes straight to the output; or
   - add an **ACES Transform** node after Celluloid Pro that converts the display image back to
     ACEScct with the *inverse* of your Output Transform (Input Transform = your display, e.g.
     Rec.709, as an inverse ODT; Output Transform = ACEScct).

The inverse route keeps other ACES clips untouched but brings back a little of the ACES output
rendering's highlight handling; the first route shows Celluloid Pro's image exactly.

### 3.4 HDR projects

Set Celluloid Pro's **Output** to *Rec.2100 PQ (HDR)* or *Rec.2100 HLG (HDR)* and the project's
output (YRGB: the timeline/output labels and HDR mastering settings; RCM: the output colour space)
to the same format. See section 9.

---

## 4. Thinking like a film lab

Celluloid Pro has three separate brightness controls, and they do different things:

| Control | Where it acts | What it does |
|---|---|---|
| **Exposure** | camera, before the negative | moves the scene along the negative's curve: over-exposure compresses highlights and gives a dense, rich negative; under-exposure thins the shadows and makes them grainier |
| **Push / Pull** | negative development | changes the *shape* of the negative's curve: contrast, speed, fog and grain |
| **Print Density** | printer, after the negative | brightens or darkens the print the way a timer does; highlights and blacks respond like print film, not like a gain |

**Printer lights** are the lab's colour balance, in **points**: one point is 0.025 log exposure on one
print layer (about 1/12 stop). Twelve points either way covers about one stop of correction. More
light in a channel adds that channel's *complementary* dye to the print:

| Control | + (more light) | − (less light) |
|---|---|---|
| Printer Red | less red, more cyan | warmer, more red |
| Printer Green | less green, more magenta | more green |
| Printer Blue | less blue, more yellow (warmer) | cooler, more blue |

At 0 / 0 / 0 every stock pair is automatically grey-balanced, so the same printer lights mean the
same correction on every stock. Grain is added in the negative's density domain, which is why it is
strongest in the mid-tones and shadows and why pushing a stock makes it coarser.

---

## 5. Controls reference

Every control is listed with its identifier (the name scripts and support logs use), its range and
default, and the tool tip Resolve shows for it. Controls marked *keyframable* can be animated in
Resolve's keyframe editor. Read-only rows are status lines.

<!-- BEGIN GENERATED: controls (tools/docs/gen_manual_controls.py) -->

### Look

*Look presets and processing mode.*

- **Look Preset** (`lookPreset`; default: Custom) — Curated starting points. Choosing a look sets the stock pair, development, print, halation, bloom, grain and lens settings in one go (input/output colour space, exposure and HDR settings are kept). Any later tweak keeps your changes; the status line below then shows "(modified)". Choose "Custom" to leave all settings as they are.
  - *Studio Classic* — The house look: balanced tungsten negative on the standard projection print - natural skin, dense mid-tones, fine Super 35 grain.
  - *Tungsten Night Drama* — High-speed tungsten negative pushed half a stop on the rich-contrast print, timed cool: deep blacks, warm practicals, moody night interiors.
  - *Soft Daylight Pastel* — Soft daylight negative on the low-contrast print, printed light and warm: airy pastel colour, open shadows, gentle bloom.
  - *Archive 1978* — Vintage negative on a faded archive print: golden warmth, lifted brown blacks, 16 mm grain, gate weave and lens fall-off.
  - *Silver Retention Thriller* — Pushed tungsten negative with partial bleach bypass on the silver-retention print: hard, metallic, desaturated contrast with crisp edges.
  - *Neon Nocturne* — Fast no-backing tungsten negative for city nights: red-orange halation around neon and practicals, coarse but soft grain, cool timing.
  - *Golden Hour Portrait* — Portrait colour negative through the lab scanner, timed warm: smooth golden skin, soft contrast, fine stills grain.
  - *Crisp Commercial* — Fine-grain daylight negative with extra colour density on the standard print: clean, saturated, sharp - restrained halation and grain.
  - *Summer Snapshot* — Consumer colour negative scanned like a lab print, timed warm: punchy holiday colour, visible stills grain, light vignette.
  - *Slide Show Brilliance* — Daylight reversal (slide) film projected directly: brilliant saturation, deep blacks, short latitude - expose carefully.
  - *Monochrome Noir* — Panchromatic black & white negative pushed one stop on the rich-contrast print: inky blacks, glowing highlights, strong vignette.
  - *Home Movie Super 8* — Reversal film in the Super 8 gauge: bold colour, big soft grain, warm halation, nervous gate weave and heavy vignetting.
  - *Worn Release Print* — A well-run 35 mm release print, timed warm: high-speed negative on the standard print with a few dark specks, the odd white negative speck, a persistent print scratch now and then, gentle gate weave and a rare moment of soft focus.
  - *Found Footage 16* — A forgotten 16 mm reel: daylight negative on the faded print, its cyan dye gone warm-magenta, coarse grain, nervous weave, lots of dust and lint, flickering scratches, hairs in the gate and focus that drifts in and out.
  - *Studio Modern* — The v1.2 default look as a preset: VISION3 250D on the modern print - dense lower mids, true blacks, soft ARRI-like highlight roll-off, calm skies and warm, low-chroma skin. Fine 35 mm grain.
  - *Soft Cinema* — ETERNA-style softness: low-contrast daylight negative on the matching Eterna-CP print - gentle contrast, restrained colour, open shadows and a quiet, slightly cool palette.
  - *Warm Print* — Classic release-print warmth: VISION3 250D printed on 2383 with Print Crossover - golden highlights, cool teal-leaning shadows, rich contrast. The character of a photochemical 2383 chain.
- **Preset Status** (`lookPresetStatus`; read-only; shows “Custom” until a preset is chosen) — Which look preset the current settings come from. "(modified)" means you have changed at least one look setting since applying it.
- **Mode** (`mode`; default: Full)
  - *Full* — complete film emulation (colour + halation, bloom, grain, lens and gate effects).
  - *Color Only* — the colour pipeline alone - exactly what an exported LUT contains.
  - *Texture Only* — for footage that is ALREADY graded/display-referred in the selected Output space; adds only the photographic texture (halation, bloom, grain, acutance, weave, vignette) and leaves the colour untouched. Use it after your own grade or after another film look.

### Input

*What the camera recorded.*

- **Camera Log** (`inputSpace`; default: Fujifilm F-Log2 C / F-Gamut C) — The log encoding and gamut of the incoming footage. Set this to match your camera (default: Fujifilm F-Log2 C / F-Gamut C). In a colour-managed Resolve project, use a timeline/working space that keeps the camera log intact for this node, or convert to the matching log space before this node. Wrong settings show up as overly flat or harsh, colour-shifted images.
  - Options: Fujifilm F-Log2 C / F-Gamut C · Fujifilm F-Log2 / F-Gamut · Fujifilm F-Log / F-Gamut · DaVinci Intermediate / DaVinci Wide Gamut · ARRI LogC3 (EI 800) / ARRI Wide Gamut 3 · ARRI LogC4 / ARRI Wide Gamut 4 · Sony S-Log3 / S-Gamut3.Cine · Panasonic V-Log / V-Gamut · Canon Log 3 / Cinema Gamut · Blackmagic Film Gen 5 / BMD Wide Gamut Gen 5 · ACEScct / AP1 · Linear Rec.2020
- **Enable Exposure** (`exposureEnable`; default on) — Switches the Exposure trim below on or off without losing its value. Off = the footage reaches the film exactly as it was recorded (0 stops).
- **Exposure** (`exposure`; -4 … 4; default 0; keyframable) — Camera exposure correction in stops, applied to the scene before it reaches the film. Because it acts before the negative, +/- exposure slides the image along the film's characteristic curve: highlights roll off and shadows build density the way a real over/under-exposed negative does.

### Camera Emulation

*Makes the footage look as if it had been shot on another camera, before it reaches the film.*

- **Enable Camera Emulation** (`cameraEmulationEnable`; default on) — Switches the camera emulation on or off without losing its settings. Off = the camera's own colour as decoded from its log encoding. It only acts on footage of the source camera (Fujifilm F-Log2 C / F-Log2 / F-Log input); other cameras pass through unchanged.
- **Emulation** (`cameraEmulation`; default: Fujifilm X-H2S -> ARRI ALEXA / AMIRA) — Fujifilm X-H2S -> ARRI ALEXA / AMIRA: renders the colours the ALEXA's ALEV III sensor would have recorded of the same scene. Fitted from measured spectral responsivities (ARRI ALEXA; Fujifilm X-Trans X-T4 as the X-H2S proxy) over about 950 natural and test reflectances under daylight, tungsten, LED and fluorescent light: a smooth, exposure-independent correction in scene-linear light that leaves greys untouched. It changes skin, foliage, skies and saturated light the way the two sensors differ; colours two sensors see identically stay identical (sensor metamerism a 3-channel correction cannot undo).
  - Options: Fujifilm X-H2S -> ARRI ALEXA / AMIRA
- **Strength** (`cameraEmulationStrength`; 0 … 1; default 1; keyframable) — Blend between the camera's own colour (0) and the full emulation (1).

### Negative

*Camera film stock and its development.*

- **Film Stock** (`negStock`; default: Kodak Vision3 250D 5207) — The camera negative (or reversal / black & white stock). It defines spectral sensitivity, tone curve, colour crosstalk, grain character and halation behaviour.
  - *CN 50D Fine Daylight* — Fine-grain daylight negative: clean, crisp and richly saturated with a neutral-cool balance.
  - *CN 200T Studio Tungsten* — Balanced tungsten studio negative: natural skin, dense mids, gently warm highlights. The reference stock.
  - *CN 250D Soft Daylight* — Low-contrast daylight negative: soft pastel colour, wide latitude, open shadows.
  - *CN 500T High Speed* — High-speed tungsten negative: visible grain, cool teal shadows, warm highlights.
  - *CN 800T No-Backing* — Fast tungsten negative without anti-halation backing: strong red halation, cyan-leaning shadows, warm compressed highlights, coarse grain.
  - *CN Vintage 85* — Vintage 1980s-style tungsten negative: golden highlights, green-teal shadows, softer saturation, denser base fog.
  - *C41 Portrait 400* — Portrait colour negative: flattering, smooth skin, low contrast, soft saturation.
  - *C41 Consumer 200* — Consumer colour negative: punchy, warm and saturated with yellow-leaning highlights.
  - *RV 100 Slide* — Daylight colour reversal (slide) film: brilliant saturation, high contrast, deep blacks, short latitude.
  - *BW 400 Panchromatic* — Classic panchromatic black & white negative: rich mid-tones, visible grain, natural tonal response to colour.
  - *BW 100 Fine* — Fine-grain black & white negative: crisp, higher contrast, slightly blue-leaning (darker reds) panchromatic response.
  - *Kodak Vision3 50D 5203* — VISION3 50D: the finest-grain daylight negative, clean and saturated with long highlight latitude.
  - *Kodak Vision3 250D 5207* — VISION3 250D: medium-speed daylight negative, natural colour and soft, open highlights.
  - *Kodak Vision3 200T 5213* — VISION3 200T: tungsten negative with fine grain, rich mid-tones and clean shadows.
  - *Kodak Vision3 500T 5219* — VISION3 500T: the high-speed tungsten workhorse: low shadow grain, warm under tungsten, big latitude.
  - *Fujifilm Eterna 250D 8563* — ETERNA 250D: soft-contrast daylight negative with restrained, calm colour (Fujifilm curves; spectral proxies).
  - *Kodak Portra 400* — PORTRA 400: portrait colour negative: warm, low-chroma skin, very wide latitude.
  - *Kodak Ektachrome E100* — EKTACHROME E100: daylight reversal: clean neutral greys, crisp, saturated colour, short latitude.
  - *Kodachrome 64 Style* — Kodachrome-style reversal (designed, no data sheet): dense, contrasty, warm reds, deep blue skies.
- **Enable Development** (`developmentEnable`; default on) — Switches the development controls below (Push / Pull, Color Density, Neg Bleach Bypass, Neutralize) on or off without losing their values. Off = the stock's standard development: no push or pull, nominal colour density, normal bleach, the stock's own curves. The film stock itself always stays active - it is the look.
- **Push / Pull** (`pushPull`; -2 … 3; default 0; keyframable) — Development in stops. Push (+) raises contrast and effective speed, lifts fog and makes the grain coarser (about x1.25 per stop); pull (-) gives a softer, lower-contrast, finer-grained negative. Unlike Exposure this changes the curve shape, not the position on it.
- **Color Density** (`interlayer`; 0 … 2; default 1; keyframable) — Interlayer (DIR) effect: how strongly the dye layers inhibit each other. 1 = the stock's nominal colour density; higher values give denser, more separated colours, 0 a flatter, purer-channel rendering. Neutrals stay neutral at any setting.
- **Neg Bleach Bypass** (`negBleach`; 0 … 1; default 0; keyframable) — Skips (partially) the bleach step of the negative so silver stays in the film alongside the dyes. The extra silver density builds with exposure: higher contrast, harder highlights and muted colour, strongest in the bright parts of the image. 0 = normal processing.
- **Neutralize** (`neutralize`; 0 … 1; default 0; keyframable) — Removes the stock's per-layer curve differences (the subtle colour crossover between shadows and highlights). 0 = the stock's natural character, 1 = perfectly neutral greys from black to white.

### Print

*How the negative becomes a positive image.*

- **Process** (`process`; default: Print)
  - *Print* — the negative is printed onto print stock and projected - the classic cinema look with dense blacks and rich colour.
  - *Lab Scan* — the negative is scanned and inverted by a lab scanner - a flatter, more neutral rendering that keeps the negative's character without print-stock contrast.
  - Reversal (slide) stocks are direct positives and ignore this setting.
- **Print Stock** (`printStock`; default: P6 Modern Print) — The print film the negative is printed on (Process = Print). It sets print contrast, black density, highlight roll-off and the colour of the projected image.
  - Options: P1 Standard Projection · P2 Low Contrast · P3 Rich Contrast · P4 Faded Archive · P5 Silver Retention · Kodak Vision 2383 · Kodak Vision Premier 2393 · Fujifilm Eterna-CP 3513DI · P6 Modern Print
- **Enable Print Timing** (`printEnable`; default on) — Switches the lab timing controls below (Printer Red / Green / Blue, Print Density, Print Bleach Bypass) on or off without losing their values. Off = the automatically grey-balanced light at 0 points and normal print processing. Process and Print Stock always stay active - they are the look.
- **Printer Red** (`printerR`; -12 … 12; default 0; keyframable) — Red printer light. More red light (+) adds cyan to the print: the image gets LESS red / more cyan. Fewer points (-) warm it towards red. Printer points as in a film lab: 1 point = 0.025 log exposure on that print layer, 0 = the stock's automatically grey-balanced light.
- **Printer Green** (`printerG`; -12 … 12; default 0; keyframable) — Green printer light. More green light (+) adds magenta to the print: the image gets LESS green / more magenta. Fewer points (-) push it towards green. Printer points as in a film lab: 1 point = 0.025 log exposure on that print layer, 0 = the stock's automatically grey-balanced light.
- **Printer Blue** (`printerB`; -12 … 12; default 0; keyframable) — Blue printer light. More blue light (+) adds yellow to the print: the image gets LESS blue / more yellow. Fewer points (-) cool it towards blue. Printer points as in a film lab: 1 point = 0.025 log exposure on that print layer, 0 = the stock's automatically grey-balanced light.
- **Print Density** (`printExposure`; -12 … 12; default 0; keyframable) — Overall printer light in points on all three layers. Positive values make the print BRIGHTER (less print density), negative values darker and denser. This is the lab's way to brighten or darken a print: highlights and blacks respond like real print film, not like a gain.
- **Print Bleach Bypass** (`printBleach`; 0 … 1; default 0; keyframable) — Bleach bypass on the print: retained silver deepens the blacks and shadows, raises contrast and desaturates - the classic gritty, metallic release-print look. 0 = normal processing.
- **Print Crossover** (`printCrossover`; -1 … 1; default 0; keyframable) — Warm highlights and cool shadows the way many release prints render them (the classic 2383 chain): positive values add yellow and remove cyan in the bright end of the print and do the opposite in the shadows, growing from the middle of the scale outward, while middle grey stays neutral. Negative values reverse it (cool highlights, warm shadows). 0 = the print stock exactly as designed. Print process only.

### Halation

*Red-orange glow around bright light sources.*

- **Enable Halation** (`halationEnable`; default on) — Switches halation on or off without losing its settings. Off skips the halation passes entirely (no render cost).
- **Halation Amount** (`halationAmount`; 0 … 2; default 0.5; keyframable) — Strength of halation: light that passes through the emulsion, reflects off the film base and re-exposes the red layer, creating a warm glow that hugs bright edges and practicals. Scaled by the stock (no-backing stocks glow much more). 0 = off.
- **Halation Radius** (`halationRadius`; 2 … 80; default 20; keyframable) — Spread of the glow in pixels at 1920 px image width (automatically scaled for other resolutions and proxy render scales).
- **Halation Threshold** (`halationThreshold`; 0 … 6; default 3.5; keyframable) — How bright a highlight must be before it halates, in stops above middle grey. The glow eases in over about a stop and a half above it, so broad bright areas (an overcast sky) barely halate while light sources and speculars glow fully. Lower values let more of the image glow.
- **Halation Hue** (`halationHue`; -1 … 1; default 0; keyframable) — Colour of the glow: -1 deep red, 0 the classic red-orange, +1 orange-yellow.

### Bloom

*Neutral diffusion of highlights.*

- **Enable Bloom** (`bloomEnable`; default on) — Switches bloom on or off without losing its settings. Off skips the bloom passes entirely (no render cost).
- **Bloom Amount** (`bloomAmount`; 0 … 2; default 0.15; keyframable) — Gentle, neutral glow of bright areas from lens and emulsion scatter - softens harsh digital highlights without the warm tint of halation. Scaled by the stock. 0 = off.
- **Bloom Radius** (`bloomRadius`; 5 … 200; default 40; keyframable) — Spread of the bloom in pixels at 1920 px image width (scaled automatically for other resolutions).

### Grain

*Film grain, generated in the film's density domain.*

- **Enable Grain** (`grainEnable`; default on) — Adds film grain. Grain is added to the dye densities of the negative, so it behaves like real grain: strongest in the mid-tones and shadows of the negative, varying per colour layer. Off skips the grain passes entirely and keeps the grain settings.
- **Grain Format** (`grainFormat`; default: Super 35) — Film gauge the image was 'shot' on. Smaller formats enlarge the grain more: Super 8 is very coarse, 16 mm clearly visible, Super 35 the cinema standard, 65 mm very fine; 35 mm Stills (36x24 full frame) is finer than Super 35.
  - Options: Super 8 · 16 mm · Super 35 · 65 mm · 35 mm Stills
- **Grain Amount** (`grainAmount`; 0 … 3; default 1; keyframable) — Grain strength. 1 = the stock's nominal granularity for the chosen format and development; push processing increases it automatically.
- **Grain Size** (`grainSize`; 0.5 … 3; default 1; keyframable) — Grain size relative to the stock and format (1 = nominal). Resolution independent: it looks the same at HD, UHD and in proxy renders.
- **Grain Softness** (`grainSoftness`; 0 … 1; default 0.5; keyframable) — 0 = crisp, sharply defined grains (like a sharp scan), 1 = soft clumps (like an optical print or a softer scan).
- **Animated Grain** (`grainAnimated`; default on) — On: a new grain pattern every frame, like real film. Off: a frozen pattern (useful for stills or for matching a static texture).
- **Grain Seed** (`grainSeed`; 0 … 9999; default 0; keyframable) — Changes the random grain pattern. Give different shots or layers different seeds so their grain never lines up.

### Lens & Gate

*Sharpness, gate weave and vignetting.*

- **Enable Acutance** (`acutanceEnable`; default on) — Switches acutance (Acutance, Acutance Radius) on or off without losing its settings. Off = the image's own sharpness; the passes are skipped.
- **Acutance** (`acutanceAmount`; -1 … 2; default 0.3; keyframable) — Edge contrast from development adjacency effects: crisp, film-like apparent sharpness without the halos of digital sharpening. Negative values soften the image.
- **Acutance Radius** (`acutanceRadius`; 0.5 … 10; default 2; keyframable) — Size of the adjacency effect in pixels at 1920 px width. Small = fine micro-contrast, larger = broader edge enhancement.
- **Enable Gate Weave** (`weaveEnable`; default on) — Switches gate weave (Gate Weave, Weave Speed) on or off without losing its settings. Off = a rock steady frame; the pass is skipped.
- **Gate Weave** (`weaveAmount`; 0 … 2; default 0; keyframable) — Subtle frame-to-frame movement of the film in the camera/projector gate, mostly vertical. 1 = typical 35 mm projection; keep it low for a believable result. 0 = off.
- **Weave Speed** (`weaveSpeed`; 0.25 … 4; default 1; keyframable) — How quickly the gate weave wanders (1 = natural; lower is a slow drift, higher a nervous jitter).
- **Enable Vignette** (`vignetteEnable`; default on) — Switches the vignette on or off without losing its setting. Off = even illumination; the pass is skipped.
- **Vignette** (`vignette`; 0 … 1; default 0; keyframable) — Natural lens light fall-off towards the corners, applied to the scene light before the film (so it darkens like an optical vignette, not a grey overlay). 0 = off.

### Film Damage

*Dust, scratches and hair on the negative and on the print - procedural, different every frame.*

- **Enable Film Damage** (`damageEnable`; default off) — Adds the dirt and wear of a real film copy: dust, scratches and the occasional hair. The damage is placed physically: dirt on the camera NEGATIVE holds back the printer light and prints as white specks in the print's own colour and grain, dirt on the PRINT shows dark on the screen. Off (the default) skips everything.
- **Enable Dust** (`dustEnable`; default on) — Dust and dirt specks, a new pattern on every frame (each frame of film collects its own dirt).
- **Dust Amount** (`dustAmount`; 0 … 2; default 0.6; keyframable) — How dirty the film is: the number of specks per frame. 0.5 = a clean projection copy with an occasional speck, 1 = a well-used print, 2 = a neglected reel.
- **Dust Size** (`dustSize`; 0.5 … 3; default 1; keyframable) — Size of the specks and lint fibres relative to a 35 mm frame (1 = nominal). Resolution independent.
- **White Dust Mix** (`dustNegative`; 0 … 1; default 0.35; keyframable) — Share of the dust that sat on the camera negative. Negative dirt prints as bright WHITE specks (slightly soft, out of the printer's focus); print dirt shows as sharp DARK specks. 0 = only dark specks, 1 = only white specks. Reversal (slide) film has no print: all of its dirt is dark.
- **Enable Scratches** (`scratchEnable`; default on) — Vertical scratches along the film's travel, cut by grit in a camera or projector gate.
- **Scratch Amount** (`scratchAmount`; 0 … 2; default 0.5; keyframable) — How many scratches run through the picture at a time (1 = one or two, 2 = a badly worn copy).
- **Scratch Persistence** (`scratchPersistence`; 0 … 1; default 0.6; keyframable) — How long a scratch stays and how continuous it is. Low = short, broken flicks that come and go within a few frames; high = long continuous lines that stay for seconds, wandering slightly with the film.
- **Emulsion Scratches** (`scratchEmulsion`; 0 … 1; default 0.3; keyframable) — Share of the scratches that cut into the emulsion instead of only the base. Base scratches scatter light and are neutral (dark on the print, white when on the negative). Emulsion scratches remove dye layers from the top and are coloured: on the print green, then yellow, then clear white as they get deeper; on the negative they print yellow, then red, then dark.
- **White Scratch Mix** (`scratchNegative`; 0 … 1; default 0.25; keyframable) — Share of the scratches on the camera negative (printed in: white or warm-coloured lines) instead of on the print (dark or green lines).
- **Enable Hair** (`hairEnable`; default on) — The occasional hair or fibre: on the film for a single frame, or caught in the projector gate - then it sticks out of the frame edge, slightly out of focus, fluttering for a few seconds.
- **Hair Amount** (`hairAmount`; 0 … 2; default 0.5; keyframable) — How often hairs appear (1 = a hair on the film about every second and now and then one in the gate).
- **Animated Damage** (`damageAnimated`; default on) — On: the damage changes from frame to frame like real film running through a projector. Off: frozen (the damage of frame 0), for stills.
- **Damage Seed** (`damageSeed`; 0 … 9999; default 0; keyframable) — Changes the random dust, scratches and hairs. Give different shots different seeds so the damage never repeats.

### Film Blur

*Momentary softness of projected film: focus drift and film breathing in the gate.*

- **Enable Film Blur** (`filmBlurEnable`; default off) — Now and then the picture goes soft and comes back, like film buckling in a warm projector gate or a focus that drifts. The blur acts on the projected image, so grain and damage soften with it. Off (the default) skips it; frames without blur cost nothing.
- **Blur Radius** (`filmBlurRadius`; 0.5 … 12; default 3; keyframable) — How soft the picture gets at the peak of a soft moment: radius of the out-of-focus blur in pixels at 1920 px width (scaled automatically for other resolutions).
- **Blur Frequency** (`filmBlurFrequency`; 0 … 1; default 0.3) — How often the picture goes soft: roughly the share of the time spent in a soft moment. 0 = never (only Focus Breathing), 1 = almost always drifting.
- **Blur Duration** (`filmBlurDuration`; 12 … 240; default 24) — Average length of a soft moment in frames, including easing in and out (individual moments vary from half to one and a half times this, and last at least 18 frames so they can ease in and out).
- **Focus Breathing** (`filmBlurBreathing`; 0 … 1; default 0.2; keyframable) — A continuous, gentle in-and-out drift of focus between the soft moments (share of the Blur Radius). 0 = sharp between them.
- **Chromatic Softness** (`filmBlurChromatic`; 0 … 1; default 0.3; keyframable) — Colour of the defocus: red and especially blue go softer than green, as with a real projection lens out of focus (faint coloured fringes on edges). 0 = neutral blur.
- **Blur Seed** (`filmBlurSeed`; 0 … 9999; default 0; keyframable) — Changes when the soft moments happen. Give different shots different seeds.

### Output

*Display encoding of the result.*

- **Output** (`outputSpace`; default: Rec.709 (Gamma 2.4)) — The display the result is encoded for. Rec.709 Gamma 2.4 for broadcast/HD grading monitors, sRGB for web/computer displays, P3 for cinema (DCI) or Apple/P3 displays, PQ or HLG for HDR deliverables. Your timeline/output colour space should match. In Texture Only mode this is the encoding of the incoming (already graded) image.
  - Options: Rec.709 (Gamma 2.4) · Rec.709 (sRGB) · P3-D65 (Gamma 2.6) · DCI-P3 (Gamma 2.6, D65 sim) · Rec.2100 PQ (HDR) · Rec.2100 HLG (HDR)
- **HDR Peak (nits)** (`hdrPeakNits`; 300 … 4000; default 1000; keyframable) — PQ output only: peak luminance of the HDR master. The film shoulder is unrolled smoothly so the brightest highlights reach this level.
- **Enable Highlight Unroll** (`hdrUnrollEnable`; default on) — PQ/HLG only: switches the HDR Highlight Unroll on or off without losing its value. Off = the SDR print look placed in the HDR container unchanged (as Unroll 0).
- **HDR Highlight Unroll** (`hdrUnroll`; 0 … 1; default 1; keyframable) — PQ/HLG only: 0 places the SDR print look in HDR unchanged (soft film highlights), 1 expands the print shoulder to use the full HDR range while keeping mid-tones and colour consistent with SDR.

### LUT Export

*Export the colour pipeline as a 3D LUT (.cube).*

- **LUT Size** (`lutSize`; default: 33) — Lattice size of the exported 3D LUT. 33 is the usual choice; 65 is the most accurate (larger file); 17 for devices with small LUT memory.
  - Options: 17 · 33 · 65
- **Folder** (`lutPath`; folder; default empty) — Folder the LUT is written to (created if needed). Leave empty for the default folder `<Documents>/Celluloid` LUTs of the user who exports. File name: `Celluloid_<input>_<negative>_<print>_<output>.cube`. Add this folder to Resolve's LUT list (Project Settings > Color Management > Open LUT Folder) to use the LUTs elsewhere.
- **Export LUT** (`lutExport`; button) — Writes the current colour settings (Color Only: input space, exposure, stocks, development, print, output; no grain or other texture) as a .cube 3D LUT for on-set monitoring, cameras or other applications. The LUT expects the selected camera log as input (Linear Rec.2020: the file carries a log2 shaper).

### About

*Version information.*

- **Version** (`aboutVersion`; read-only) — Plugin version, build variant and build.
- **Manual** (`aboutManual`; read-only; “See the User Manual (MANUAL.md in the plugin bundle's Resources folder).”) — The user manual explains every control, recommended Resolve setups (YRGB, RCM, ACES) and workflow recipes. It is installed inside the plugin bundle: Celluloid.ofx.bundle/Contents/Resources.

<!-- END GENERATED: controls -->

**Notes on the reference.**

- **Every module has its own switch.** The first control of each module is an **Enable** checkbox
  (Enable Exposure, Development, Print Timing, Halation, Bloom, Grain, Acutance, Gate Weave,
  Vignette, Film Damage with Dust / Scratches / Hair, Film Blur, Highlight Unroll). Unticking it switches the module off while its values stay where you
  left them, so you can compare with and without at a click, or keep a dialled-in setting for later.
  What "off" means:

  | Switch | Off renders as |
  |---|---|
  | Enable Exposure | Exposure 0 (the footage as recorded) |
  | Enable Development | standard development: Push / Pull 0, Color Density 1, no Neg Bleach Bypass, Neutralize 0 — the film stock stays |
  | Enable Print Timing | printer lights and Print Density at 0 points (automatic grey balance), no Print Bleach Bypass — Process and Print Stock stay |
  | Enable Halation / Bloom / Grain / Acutance / Gate Weave / Vignette | the effect is skipped completely (no render cost) |
  | Enable Film Damage (and Dust / Scratches / Hair) | no damage at all; a sub-switch off = that kind of damage at Amount 0 |
  | Enable Film Blur | always sharp; the passes are skipped |
  | Enable Highlight Unroll | the SDR print look in the HDR container (as Unroll 0) |

  The stock pair, Process and Mode are never switched off: the choice of stocks *is* the look. A
  switched-off module renders exactly like the module at its neutral values. The switches of the
  version 1.0 modules are on by default, so projects saved with version 1.0 open and render
  unchanged; the new Film Damage and Film Blur modules are off by default.
- **Halation, bloom and grain scale with the stock.** Each negative has its own halation strength
  (no-backing stocks glow much more), bloom strength and grain size; the Amount and Size controls
  are relative to that. Black & white stocks automatically use monochrome grain.
- **Radii are resolution independent.** Halation, bloom and acutance radii and the grain size are
  given for a 1920-pixel-wide image and scale with the actual width, so HD, UHD, proxies and
  render-scale previews all look the same.
- **Dimmed controls.** Controls that have no effect in the current state are greyed out:
  - Color Only mode: all texture controls. Texture Only mode: all colour controls (Camera Log,
    Exposure, negative and print).
  - A module switched off (Enable box unticked): that module's controls.
  - Film Damage off: the Dust, Scratches and Hair switches and every damage control.
  - Process = Lab Scan: Print Stock and Print Bleach Bypass.
  - Reversal stocks (RV 100 Slide): Process, Print Stock, printer lights, Print Density and Print
    Bleach Bypass — a slide is a direct positive; time it with Exposure and balance before the node.
  - Black & white stocks: Color Density, Neutralize and the three printer colour lights (Print
    Density still works).
  - HDR Peak needs Output = PQ; HDR Highlight Unroll needs a PQ or HLG Output.
- **No License group.** This is the personal build (section 10): there is no license key, no
  watermark, and LUT export always works. **About › Version** reads "… - Personal build".

---

## 6. Film stocks and look presets

### Negative and reversal stocks

| Stock | Balance | Character |
|---|---|---|
| CN 50D Fine Daylight | daylight | fine grain, crisp, richly saturated, neutral-cool |
| **CN 200T Studio Tungsten** (default) | tungsten | natural skin, dense mid-tones, gently warm highlights — the reference stock |
| CN 250D Soft Daylight | daylight | low contrast, pastel colour, wide latitude |
| CN 500T High Speed | tungsten | visible grain, cool teal shadows, warm highlights |
| CN 800T No-Backing | tungsten | no anti-halation backing: strong red halation, coarse grain |
| CN Vintage 85 | tungsten | 1980s character: golden highlights, green-teal shadows, denser fog |
| C41 Portrait 400 | daylight | flattering, smooth skin, low contrast |
| C41 Consumer 200 | daylight | punchy, warm, saturated, yellow-leaning highlights |
| RV 100 Slide | daylight | reversal: brilliant colour, deep blacks, short latitude (no print stage) |
| BW 400 Panchromatic | — | classic black & white, rich mid-tones, visible grain |
| BW 100 Fine | — | fine-grain black & white, crisper, darker reds |

"Tungsten" and "daylight" describe the light each stock is balanced for. With digital footage you
balance in the camera or before the node anyway; the balance shows mainly as each stock's colour
character. Printer lights correct any remaining tint.

### Print stocks

| Print | Character |
|---|---|
| **P1 Standard Projection** (default) | deep neutral blacks, dense mid-tones, graceful highlight roll-off — the reference print |
| P2 Low Contrast | open shadows, softer roll-off, gentler saturation |
| P3 Rich Contrast | deeper blacks, punchier mid-tones, denser colour |
| P4 Faded Archive | aged dyes (cyan fades first), lifted warm blacks, yellowed base |
| P5 Silver Retention | retained silver: deep shadows, higher contrast, muted colour |

Instead of a print, **Process = Lab Scan** scans and inverts the negative: a flatter, more neutral
rendering that keeps the negative's character without print contrast.

### Look presets

Fourteen curated looks are in the **Look Preset** menu: Studio Classic, Tungsten Night Drama, Soft
Daylight Pastel, Archive 1978, Silver Retention Thriller, Neon Nocturne, Golden Hour Portrait, Crisp
Commercial, Summer Snapshot, Slide Show Brilliance, Monochrome Noir, Home Movie Super 8, and the two
film-damage looks Worn Release Print and Found Footage 16 (each is described in the controls
reference above).

A preset is a complete recipe: it sets every look parameter (stocks, development, print, printer
lights, halation, bloom, grain, lens and gate, film damage and film blur), and parameters it does
not mention return to their defaults (so every preset except the two damage looks switches film
damage and film blur off), so a preset always gives the same image. It never changes Camera Log,
Exposure, Mode, Output, HDR settings, the grain, damage and blur seeds, LUT export or license
settings. After any edit the
**Preset Status** line reads "*name* (modified)"; it is recalculated from the actual values, so it
stays correct after undo and after reopening a project.

Further reading in the source repository:

- [STOCKS.md](https://github.com/serhatbarda/celluloid-pro/blob/main/docs/STOCKS.md) — the colour
  model and every stock's design data.
- [LOOKS.md](https://github.com/serhatbarda/celluloid-pro/blob/main/docs/LOOKS.md) — each preset's
  settings and intent, with a contact sheet.

---

## 7. Workflow recipes

The recipes assume a correctly set Camera Log and Output (section 3) and a balanced, exposed clip.
Values are starting points; trust your monitor.

### Night exterior (streets, neon, practicals)

1. Preset **Neon Nocturne** (CN 800T No-Backing on P1) for strong halation, or **Tungsten Night
   Drama** (CN 500T pushed half a stop on P3) for deeper blacks.
2. Expose for the faces and the practicals, not for the black sky: night footage often needs
   **Exposure** +0.3 to +0.7 so the shadows sit on the toe of the curve instead of below it.
3. Halation: if every lit window glows, raise **Halation Threshold** toward 4–4.5 stops; if neon
   does not glow enough, raise **Halation Amount** rather than lowering the threshold, so skin
   edges stay clean. **Halation Hue** −0.2 gives a deeper red ring.
4. Time the print cool with **Printer Blue** −0.5 to −1.5; keep sodium light orange by not
   over-correcting.
5. Grain: night stocks are coarse by design; **Grain Softness** 0.55–0.7 keeps it organic in dark
   areas.

### Skin-centric interview

1. Preset **Studio Classic** (CN 200T on P1) for neutral, dense skin, or **Golden Hour Portrait**
   (C41 Portrait 400, Lab Scan) for softer, warmer skin.
2. Balance white before the node; then fine-tune skin with the printer lights in half points
   (**Printer Red** −0.5 warms, **Printer Green** +0.5 removes a green cast).
3. Keep **Color Density** between 0.9 and 1.0: higher values separate colours but deepen skin
   saturation.
4. Grain **Amount** 0.6–0.8, **Size** 1.0, **Softness** 0.6; **Acutance** 0.2 so pores do not
   sharpen.
5. Halation **Threshold** 3.5 (default) or higher: a bright window behind the subject should not
   bleed red onto the hair line.

### Music video

1. Start from **Silver Retention Thriller** (hard, metallic) or **Crisp Commercial** (clean,
   saturated). For colour-saturated stage light try **Slide Show Brilliance** (reversal: expose
   carefully, latitude is short, and there is no print stage to time).
2. Push for attitude: **Push / Pull** +1 to +2 raises contrast and grain together; **Print Bleach
   Bypass** 0.3–0.6 adds the gritty release-print look.
3. Animate **Print Density** or **Exposure** (both keyframable) for flashes and fades that behave
   like film, with highlights rolling off instead of clipping.
4. Grain **Format** 16 mm or Super 8 for a home-made texture; **Gate Weave** 0.3–0.6 for a
   projector feel.
5. Render the colour in Color Only on the grade node and the texture once on a timeline node in
   Texture Only mode (see *Colour and texture separately* below) if you cut many shots fast.

### Archival look

1. Preset **Archive 1978** (CN Vintage 85 on P4 Faded Archive) for faded colour film, or **Home
   Movie Super 8** for amateur reversal footage, or **Monochrome Noir** for black & white.
2. **Grain Format** 16 mm or Super 8, **Grain Softness** 0.7–0.8, **Acutance** 0–0.1 (old lenses
   and prints are soft).
3. **Gate Weave** 0.4–0.8 at **Weave Speed** 0.8, **Vignette** 0.3–0.5.
4. On Archive 1978, fade further with **Printer Red** −1 (warmer) and **Print Density** −1 for
   denser, older-looking prints; P4's lifted blacks already provide the faded base. The Super 8
   preset uses a reversal stock, which has no printer lights: warm it with the balance before the
   node and set brightness with **Exposure**.
5. For black & white, **BW 400 Panchromatic** renders colour like a real panchromatic emulsion: use
   the colour of the scene (a warm or cool balance before the node) the way a cinematographer uses
   coloured filters.

### Film damage: dust, scratches, hair and soft focus

Film damage is placed where it happens physically, so it takes the print's colour and grain
instead of sitting on top like an overlay:

- **Dust and dirt** on the camera *negative* hold back the printer light: they print as bright,
  slightly soft **white** specks. Dirt on the *print* is in the projector beam: sharp **dark**
  specks. **White Dust Mix** sets the share of each. Each frame of film collects its own dirt, so
  the pattern is new every frame; a few specks are lint fibres.
- **Scratches** run along the film's travel and stay for many frames, wandering slightly. **Scratch
  Persistence** low = short, broken flicks; high = long continuous lines that last for seconds.
  Base scratches are neutral (dark on the print, white on the negative). **Emulsion Scratches** cut
  through the dye layers from the top: on a print they show **green** (magenta layer gone), then
  yellow, then clear white as they get deeper; on the negative they print yellow, then red.
- **Hair**: now and then a hair or fibre for a single frame, or a hair caught in the projector gate
  that sticks out of the frame edge, soft and fluttering, for a few seconds.
- **Film Blur**: now and then the picture goes soft and comes back (film buckling in a warm gate,
  focus drift). **Blur Frequency** sets how often, **Blur Duration** how long a soft moment lasts,
  **Focus Breathing** adds a gentle continuous drift, **Chromatic Softness** makes red and blue
  softer than green like a real projection lens.

Recipe:

1. Start from **Worn Release Print** (a well-run 35 mm print) or **Found Footage 16** (a battered
   16 mm reel), or tick **Enable Film Damage** on any look.
2. Judge damage at 100 % zoom and in playback: single specks are one frame long and read as
   "film" only in motion.
3. Keep damage subtle for drama (Dust Amount 0.3–0.6, Scratch Amount 0.2–0.5, Hair 0.2) and
   generous for archival or music-video work.
4. Give every shot a different **Damage Seed** (and **Blur Seed**) so the damage never repeats from
   cut to cut. For stills, untick **Animated Damage**.
5. Damage and film blur appear in Full and Texture Only mode, never in Color Only or in an exported
   LUT (a LUT cannot hold texture).

### HDR deliverable

1. Grade in SDR first if the SDR version is the reference, then switch **Output** to *Rec.2100 PQ*
   (or HLG) with the project set accordingly (section 3.4).
2. **HDR Peak** = the mastering display's peak (1000 nits is common); **HDR Highlight Unroll** 1 uses
   the full range; 0.3–0.6 keeps a softer, more filmic highlight roll-off.
3. Mid-tones and colour stay consistent with SDR, so the SDR trims usually carry over; re-check
   halation, whose glow is more visible in HDR.
4. HLG uses the BT.2100 1000-nit reference: HDR Peak does not apply.

### Colour and texture separately

Use **Color Only** on the grade node of each clip and a second Celluloid Pro instance in **Texture
Only** mode on a timeline node or at the end of the node tree (same Output setting). Grain and
halation are then added once to the finished image, after any trims, and the colour nodes render
faster.

### Texture on an existing grade

Texture Only adds halation, bloom, grain, acutance, gate weave and vignette to an image that is
already display-referred in the selected **Output** space (for example a Rec.709 grade or another
film look). Colour is left untouched.

### Matching two cameras

Give each camera group its own Celluloid Pro node with its own **Camera Log**, and the same stocks
and printer lights. Both cameras are then rendered through the same negative and print. Match
exposure and white balance before the node, in each camera's log.

---

## 8. LUT export

**Export LUT** (LUT Export group; always available in the personal build) writes the **Color Only** pipeline of the
current settings — Camera Log, Exposure, stocks, development, print, printer lights and Output —
as a `.cube` 3D LUT. Grain, halation, bloom and the other texture effects cannot be stored in a LUT
and are left out.

- **Size:** 17³, 33³ (default) or 65³ points. A 33³ LUT matches the plug-in to about 0.03 code values
  (99th percentile); use 65³ for critical work and 17³ only for devices with little LUT memory.
- **File name:** `Celluloid_<input>_<negative>_<print>_<output>.cube`, for example
  `Celluloid_Fujifilm-F-Log2-C_CN-200T-Studio-Tungsten_P1-Standard-Projection_Rec.709-Gamma-2.4.cube`
  (the print part is `Lab-Scan` or `Reversal` where applicable). An existing file of the same name
  is replaced, and the confirmation message says so.
- **Folder:** the **Folder** field, or — when it is empty — `Documents/Celluloid LUTs/` of the user
  who exports. The folder is created when needed.
- **Input:** a LUT maps one camera's log code values. Export one per camera log. For Linear Rec.2020
  input the file carries a 1D log2 shaper (−8.5 … +8.5 stops around mid grey) in front of the 3D
  LUT, which Resolve and Adobe applications read.
- Texture Only mode on the node does not matter: the LUT always contains the colour pipeline.

To use the LUTs elsewhere in Resolve, open **Project Settings › Color Management › Open LUT Folder**,
copy them there and click **Update Lists**. For cameras and monitors, check that the device accepts
the chosen lattice size and the camera log you selected.

---

## 9. HDR

With **Output** = *Rec.2100 PQ (HDR)* or *Rec.2100 HLG (HDR)* the projected print is shown on an
HDR display. Mid grey sits at about 15 nits, consistent with the SDR look, and **HDR Highlight
Unroll** expands the print's soft shoulder smoothly (monotonic, without kinks) toward **HDR Peak**:

- Unroll **0** places the SDR print look unchanged in the HDR container (soft film highlights).
- Unroll **1** uses the full HDR range while mid-tones and colour stay consistent with SDR.
- **HDR Peak** (PQ only, 300–4000 nits) is the peak of the mastering display. HLG always uses the
  BT.2100 reference (1000-nit system gamma), so HDR Peak is dimmed for HLG.

In Texture Only mode with a PQ or HLG Output, the incoming image must already be in that HDR
encoding.

---

## 10. Personal build and licensing

This copy of Celluloid Pro is the **personal build** for non-commercial use, the default
configuration of the source repository:

- **No license check.** The plug-in never looks for a license key, offline or online.
- **No watermark.** Every render is clean, at any resolution and in every mode.
- **LUT export always works** (section 8).
- The **License** group is hidden. Its parameters still exist inside the plug-in, so projects saved
  with any earlier version, licensed or trial, open unchanged.
- **About › Version** reads "Celluloid Pro *version* - Personal build".

Nothing in the image pipeline differs between the builds: a personal-build render is
pixel-identical to a licensed render of the same settings.

### The licensed build (`-DCEL_LICENSING=ON`)

The commercial variant is still in the source and builds with the CMake option
`-DCEL_LICENSING=ON` (README, CMake options). It shows the License group and behaves as follows.

Without a license key the licensed build runs as a **trial**: every feature works except **LUT export**,
and renders carry a small "CELLULOID PRO — TRIAL" badge in the bottom-right corner plus a faint
diagonal band. There is no time limit.

**Activate.** Open the **License** group, paste your key into **License Key** (keys start with
`CELL1-`; line breaks and spaces from e-mail wrapping are ignored) and click **Activate**. The key is
checked on your computer, with no internet connection, and stored for your user account; the
watermark disappears with the next render, and License Status shows the licensee and edition.
**Deactivate** removes the key from this computer (for example before moving to a new machine).

Where keys are stored:

| | per user (written by Activate) | machine-wide (studios, installed by an administrator) |
|---|---|---|
| macOS | `~/Library/Application Support/Celluloid/license.key` | `/Library/Application Support/Celluloid/license.key` |
| Windows | `%APPDATA%\Celluloid\license.key` | `%PROGRAMDATA%\Celluloid\license.key` |

Render nodes and containers can also provide the key in the environment variable
`CELLULOID_LICENSE_KEY`. If several sources hold a key, a valid paid key (Indie, Pro, Studio) wins
over a Trial-edition evaluation key. A key copied into place by hand is picked up within a few
seconds, without restarting Resolve.

Keys are personal: the licensee's name is part of the key and is shown in the License group. The
number of seats is part of the key and of the license agreement; it is not enforced technically.
Keep your purchase e-mail: if you reinstall or change computers, paste the key again.

---

## 11. Performance

- **GPU.** Celluloid Pro renders on the GPU Resolve uses: Metal on macOS, CUDA on NVIDIA and OpenCL
  on AMD/Intel under Windows. Each GPU backend runs a self test when the plug-in loads, and only
  backends that pass are offered to Resolve. Check **Preferences › Memory and GPU** in Resolve: the
  GPU processing mode must be Metal, CUDA or OpenCL, not CPU.
- **Turn off what you do not use.** Halation, bloom, acutance, gate weave, vignette, grain and film
  damage each skip their passes entirely when their Enable box is unticked (or at 0); film blur costs
  nothing on frames that are sharp. Halation and bloom are the
  most expensive texture effects.
- **Color Only** is the fastest mode. Grade in Color Only and add texture once at the end (section 7)
  when playback is tight.
- **Changing stocks** costs a one-off calculation of a few tens of milliseconds the first time a pair
  is used; switching back to a recently used pair, and every other control, is instant.
- **Proxies and render scale.** Grain, halation and the other texture effects are resolution
  independent, so proxy and reduced-resolution playback look like the full-resolution render.
- **CPU rendering** uses every core for one frame at a time, which avoids over-subscription when
  Resolve renders several frames in parallel.
- **GPU memory.** Working buffers are reused from frame to frame and released after about ten
  seconds of inactivity: about 0.43 GB per GPU queue at UHD, 1.5 GB at 8K.

**Diagnostics for support cases** (set before starting Resolve):

- `CELLULOID_LOG=1` — diagnostics on the standard error output (GPU self tests, preset checks).
- `CELLULOID_DISABLE_GPU=1` — do not offer any GPU backend (CPU only), or a list such as
  `opencl,cuda,metal` to disable only those.

---

## 12. Troubleshooting

| Symptom | Cause and remedy |
|---|---|
| Celluloid Pro is not in the Effects library | Check that `Celluloid.ofx.bundle` is in the OFX plug-in folder (section 1) and restart Resolve. On macOS confirm the installer was allowed to run. Resolve caches plug-ins: if it lists the effect as failed, delete Resolve's OFX plug-in cache or reinstall, then restart. |
| Image is flat and washed out, or harsh and colour-shifted | **Camera Log** does not match the footage, or the clip was converted (LUT, CST, colour management) before the node. Section 3. |
| Image looks right in the node but wrong in the viewer / render | A colour-managed project converts the plug-in's display image again. Add the conversion back to the timeline space after the node, or use YRGB (section 3). |
| Too dark or too bright | **Exposure** for the scene, **Print Density** for the print. Avoid fixing exposure with a gain after the node. |
| Colour cast on every stock | Balance before the node, then fine-tune with printer lights in half points. |
| Bright edges glow red everywhere | Raise **Halation Threshold** (stops over mid grey) or lower **Halation Amount**; CN 800T No-Backing glows by design. |
| Grain looks too strong or too fine in the viewer | Judge grain at 100 % zoom; the viewer scales it. Grain is resolution independent and matches the final render. |
| Grain looks identical on two layered clips | Give each instance a different **Grain Seed**. |
| Grain is frozen | **Animated Grain** is off. |
| Dust and scratches do not change / are identical on two clips | **Animated Damage** is off, or both clips use the same **Damage Seed**. |
| Damage or film blur missing | Film Damage / Film Blur are off by default and never render in Color Only mode. |
| "LUT export is part of the licensed version" | Licensed build only: activate a license key (section 10). The personal build always exports. |
| "This license key could not be activated" | Licensed build only. Paste the complete key including `CELL1-`; the message explains the reason (for example an expired subscription key or a key for another product). |
| Watermark still visible after activation | Licensed build only (the personal build never watermarks). Check License Status. A machine-wide or environment key with an error is reported there. Re-render the frame (Resolve may show a cached frame). |
| Slow playback | Check Resolve's GPU processing mode, lower texture settings or use Color Only plus one Texture Only node (section 11). With `CELLULOID_LOG=1` the plug-in logs which GPU backends passed their self test. |
| A GPU error in one frame | The plug-in re-renders a failed GPU frame on the CPU and, if that fails, asks Resolve to render on the CPU; persistent errors disable that GPU backend for the node. Update the GPU driver. |

---

## 13. Known limitations

Version 1.0.0 is honest about what has and has not been verified yet:

- **Inside DaVinci Resolve.** The plug-in has been verified in our own headless OpenFX test host
  (CPU and OpenCL rendering, every stock pair, mode, preset and output) and in automated macOS and
  Windows builds, but not yet in every Resolve version and GPU mode. Status dialogs, the folder
  picker and GPU render paths inside Resolve are being tested now.
- **GPU speed.** The target of real-time UHD playback on a mid-range GPU has not been measured on
  real GPUs yet. A bandwidth/compute estimate puts a UHD frame with every effect on at roughly
  11–18 ms on mid-range to upper-mid NVIDIA cards and 30–50 ms on base Apple silicon; Color Only is
  much faster. The CUDA backend has been compiled for every supported NVIDIA generation but not yet
  executed on NVIDIA hardware; Metal has run only on a virtual GPU in CI.
- **Real footage.** The looks were designed on carefully built synthetic F-Log2 C scenes (skin,
  skies, neon, foliage, high and low key). Film damage and film blur (1.1) were tuned on real
  F-Log2 C frames from a Fujifilm X-H2S; a full review of the looks on real clips is in progress.
- **Seat counts** (licensed build only) in license keys are informational: an offline key can be
  copied. Seat limits are part of the license agreement, not technical copy protection.
- **Texture Only** works on display-referred images; it does not know how a previous grade was made,
  so halation thresholds refer to the display image, not to scene light.
