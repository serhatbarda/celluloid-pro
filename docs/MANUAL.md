# Reafilm Pro — User Manual

Version 1.0.0 · OpenFX plug-in for DaVinci Resolve on macOS and Windows

Reafilm Pro turns camera log footage into a photochemical film image. It does not apply a fixed
LUT: it simulates the chain a frame of film goes through. The scene exposes a colour negative
through the spectral sensitivities of its three layers; the negative is developed (with push/pull,
interlayer effects and optional bleach bypass); it is printed onto print stock with printer lights,
or scanned by a lab scanner; and the result is projected and encoded for your display. Halation,
bloom, film grain, development acutance, gate weave and lens vignetting are added at the point in
the chain where they physically happen.

All film stocks, look presets and colour science in Reafilm Pro are original designs. They are
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

Without a usable GPU, Reafilm Pro renders on the CPU with all cores. That is much slower, but the
image is the same: CPU and GPU run the same colour and texture code.

Linux builds exist only for development and testing; they are not a product.

### Installing on macOS

1. Quit DaVinci Resolve.
2. Open `ReafilmPro-<version>-macOS.dmg` and double-click `ReafilmPro-<version>-macOS.pkg`
   (the disk image also contains this manual and a short *READ ME FIRST*).
3. Follow the installer. The plug-in is installed to
   `/Library/OFX/Plugins/ReafilmPro.ofx.bundle`; an older version is removed first.
4. Start Resolve. The effect is in the Effects library under **OpenFX › Reafilm › Reafilm Pro**.

**Unsigned installers (Gatekeeper).** Until a Developer ID certificate is in place, the installer
is not signed or notarized and macOS refuses to open it with a message that it "cannot be verified".
To install anyway:

- macOS 14 and earlier: right-click (Control-click) the `.pkg`, choose **Open**, then **Open** again.
- macOS 15 and later: double-click the `.pkg` once and dismiss the warning, then open
  **System Settings › Privacy & Security**, scroll down and click **Open Anyway** next to the
  message about `ReafilmPro-….pkg`, and confirm with your password.

The installer clears the quarantine flag of the installed plug-in, so Resolve can load it. Signed
and notarized installers open without these steps.

### Installing on Windows

1. Quit DaVinci Resolve.
2. Run `ReafilmPro-<version>-Windows-Setup.exe` (administrator rights are required, because OFX
   plug-ins live in a system folder). The installer speaks English and German.
3. The plug-in is installed to `C:\Program Files\Common Files\OFX\Plugins\ReafilmPro.ofx.bundle`.
4. Start Resolve; the effect is under **OpenFX › Reafilm › Reafilm Pro**.

**Unsigned installer (SmartScreen).** Until the installer is Authenticode-signed, Microsoft
Defender SmartScreen may show "Windows protected your PC". Click **More info**, check that the file
name is `ReafilmPro-<version>-Windows-Setup.exe`, then click **Run anyway**.

**Manual installation (zip).** `ReafilmPro-<version>-Windows-bundle.zip` contains the bare
`ReafilmPro.ofx.bundle` folder. Copy the whole folder into
`C:\Program Files\Common Files\OFX\Plugins\` (create `OFX\Plugins` if it does not exist). If
Windows blocked the downloaded zip, right-click it › **Properties** › **Unblock** before extracting.

### Updating and uninstalling

Install a new version over the old one; the installers replace the previous bundle. Resolve keeps
your node settings, because the parameter identifiers never change between versions.

**Upgrading from Celluloid Pro.** The plug-in was renamed from Celluloid Pro to Reafilm Pro in
1.3.0, and its bundle from `Celluloid.ofx.bundle` to `ReafilmPro.ofx.bundle`. Delete the old
`Celluloid.ofx.bundle` from the OFX plug-ins folder (`/Library/OFX/Plugins` on macOS,
`C:\Program Files\Common Files\OFX\Plugins` on Windows), otherwise Resolve shows both. The
installers do this for you; after a manual (zip) install, delete it by hand. Saved projects keep
loading: the plug-in identifier and all parameters are unchanged, and license keys stay valid.

- macOS: quit Resolve and delete `/Library/OFX/Plugins/ReafilmPro.ofx.bundle`.
- Windows: **Settings › Apps › Installed apps › Reafilm Pro (OFX plug-in) › Uninstall**, or delete
  the bundle folder if you installed from the zip.

The personal build uses no license key. In the licensed build, license keys are stored per user
(section 10) and are not removed on uninstall.

### Where is this manual?

A copy of this manual is installed inside the plug-in bundle,
`ReafilmPro.ofx.bundle/Contents/Resources/MANUAL.md`, and the plug-in's **About** group points to it.
Third-party notices are in `Contents/Resources/Licenses/`.

---

## 2. First steps

1. On the Color page, add a serial node to a clip and drag **Reafilm Pro** onto it.
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
secondaries (keys, windows) belong *before* Reafilm Pro, in the camera log space, so the film
"photographs" your corrected scene. Reafilm Pro then produces a finished display image: put only
light trims after it. When the colour is already finished by another tool, use **Texture Only**
mode (section 7).

**Mode at a glance.**

- **Full** — colour and texture: the complete film look.
- **Color Only** — the colour pipeline alone. Faster; it is exactly what an exported LUT contains.
- **Texture Only** — only halation, bloom, grain, acutance, gate weave and vignette, on an image that
  is *already* display-referred in the selected Output space.

---

## 3. Setting up Resolve: YRGB, RCM and ACES

Reafilm Pro needs two things from the project: the clip must arrive at the node **in the camera
log selected under Camera Log**, and whatever comes after the node must treat its output as a
**display image in the selected Output space**. The setups below achieve that.

> These setups follow Resolve's documented colour management. Reafilm Pro has been verified in
> our own OpenFX test host and in CI, not yet inside every Resolve version; if your Resolve behaves
> differently, the general rule above still tells you what to do.

### 3.1 DaVinci YRGB (not colour managed) — recommended

This is the simplest and most transparent setup, and the recommended one for F-Log2 C.

1. **Project Settings › Color Management**: Color science *DaVinci YRGB*. Timeline colour space
   and output colour space are labels only in this mode; set them to Rec.709 Gamma 2.4 for SDR
   (or your HDR format, section 9).
2. Do not apply a camera LUT or CST to the clips.
3. Reafilm Pro: **Camera Log = Fujifilm F-Log2 C / F-Gamut C**, **Output = Rec.709 (Gamma 2.4)**.
4. Grade before the node in F-Log2 C (Offset for exposure and balance works well in log), put
   Reafilm Pro last or before a final trim node.

The same works for every camera in the Camera Log list: ARRI LogC3 and LogC4, Sony S-Log3,
Panasonic V-Log, Canon Log 3, Blackmagic Film Gen 5, Fujifilm F-Log / F-Log2, DaVinci Intermediate,
ACEScct, and scene-linear Rec.2020.

**F-Log2 C or F-Log2?** Both use the same F-Log2 curve; F-Log2 C pairs it with the wider *F-Gamut C*
primaries. Use the entry that matches the camera's recording setting. Choosing the wrong one does
not change brightness but shifts saturated colours (F-Gamut C read as F-Gamut looks oversaturated).

### 3.2 DaVinci YRGB Color Managed (RCM)

In RCM, Resolve converts each clip into the timeline colour space and converts the timeline space to
the output space at the end. Reafilm Pro sits in between, so it must receive a log space it knows
and the conversion after it must be undone.

1. **Project Settings › Color Management**: Color science *DaVinci YRGB Color Managed*, colour
   processing mode *Custom*. Timeline colour space **DaVinci Wide Gamut / DaVinci Intermediate**.
   Output colour space **Rec.709 Gamma 2.4** (or your HDR format). Switch **output tone mapping** and
   **output gamut mapping** to *None*.
2. Set each clip's Input Colour Space to its camera (right-click the clip › Input Color Space).
3. Reafilm Pro: **Camera Log = DaVinci Intermediate / DaVinci Wide Gamut**, **Output = Rec.709
   (Gamma 2.4)** (the same display as the project output).
4. Add a node **after** Reafilm Pro with a **Color Space Transform**: Input *Rec.709 / Gamma 2.4*
   → Output *DaVinci Wide Gamut / DaVinci Intermediate*, tone mapping *None*, gamut mapping *None*,
   *Apply Forward OOTF* and *Apply Inverse OOTF* off. Resolve's output conversion then returns the
   image exactly to what Reafilm Pro rendered.

*If your Resolve version has no input colour space for F-Log2 C*, set those clips' Input Colour
Space to **Bypass**, set **Camera Log = Fujifilm F-Log2 C / F-Gamut C** on their Reafilm Pro node
and keep the CST after it. Or use the YRGB setup above, which avoids the question altogether.

### 3.3 ACES (ACEScct)

1. **Project Settings › Color Management**: Color science *ACEScct*.
2. Reafilm Pro: **Camera Log = ACEScct / AP1**, Output = the display of your ACES Output
   Transform (for example Rec.709 Gamma 2.4).
3. Undo the Output Transform after the node, choosing one of:
   - set **ACES Output Transform** to *No Output Transform* when Reafilm Pro is on every clip or on
     a timeline node, so its display image goes straight to the output; or
   - add an **ACES Transform** node after Reafilm Pro that converts the display image back to
     ACEScct with the *inverse* of your Output Transform (Input Transform = your display, e.g.
     Rec.709, as an inverse ODT; Output Transform = ACEScct).

The inverse route keeps other ACES clips untouched but brings back a little of the ACES output
rendering's highlight handling; the first route shows Reafilm Pro's image exactly.

### 3.4 HDR projects

Set Reafilm Pro's **Output** to *Rec.2100 PQ (HDR)* or *Rec.2100 HLG (HDR)* and the project's
output (YRGB: the timeline/output labels and HDR mastering settings; RCM: the output colour space)
to the same format. See section 9.

---

## 4. Thinking like a film lab

Reafilm Pro has three separate brightness controls, and they do different things:

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
  - *Signature 250D / 2383* — VISION3 250D on the Kodak 2383 release print with ALEXA colour and a gentle film roll-off: warm colour head, steel-blue shadow toning, print crossover. Fine, crisp 65 mm grain on a film edge with the digital sharpening removed, a tight halation with a soft Aura and a Glimmerglass bloom - texture and depth.
  - *Studio Classic* — The house look: VISION3 200T on the Kodak 2383 release print - natural skin, dense mid-tones, fine Super 35 grain.
  - *Tungsten Night Drama* — VISION2 500T pushed half a stop on the high-contrast Eterna-CP 3521XD print, timed cool: deep blacks, warm practicals, moody night interiors.
  - *Soft Daylight Pastel* — Portra 400 on the gentle Eterna-CP 3513DI print, printed light and warm: airy pastel colour, open shadows, gentle bloom.
  - *Archive 1978* — VISION2 500T on a faded 2383 print: golden warmth, lifted brown blacks, 16 mm grain, gate weave and lens fall-off.
  - *Silver Retention Thriller* — VISION3 200T pushed, with partial bleach bypass on the negative and on the 2383 print: hard, metallic, desaturated contrast with crisp edges.
  - *Neon Nocturne* — VISION2 500T for city nights: red-orange halation around neon and practicals, coarse but soft grain, cool timing.
  - *Golden Hour Portrait* — Portra 400 through the lab scanner, timed warm: smooth golden skin, soft contrast, fine stills grain.
  - *Crisp Commercial* — VISION3 50D with extra colour density on the 2383 print: clean, saturated, sharp - restrained halation and grain.
  - *Summer Snapshot* — Reala 500D scanned like a lab print, timed warm: punchy holiday colour, visible stills grain, light vignette.
  - *Slide Show Brilliance* — Ektachrome E100 reversal (slide) film projected directly: brilliant saturation, deep blacks, short latitude - expose carefully.
  - *Monochrome Noir* — Double-X 5222 pushed one stop on the Premier 2393 print: inky blacks, glowing highlights, strong vignette.
  - *Home Movie Super 8* — Reversal film in the Super 8 gauge: bold colour, big soft grain, warm halation, nervous gate weave and heavy vignetting.
  - *Worn Release Print* — A well-run 35 mm release print, timed warm: Eterna Vivid 160 on the 2383 print with a few dark specks, the odd white negative speck, a persistent print scratch now and then, gentle gate weave and a rare moment of soft focus.
  - *Found Footage 16* — A forgotten 16 mm reel: Eterna 250D on a faded 2383 print, its cyan dye gone warm-magenta, coarse grain, nervous weave, lots of dust and lint, flickering scratches, hairs in the gate and focus that drifts in and out.
  - *Studio Modern* — VISION3 250D on the high-D-max Fujifilm Eterna-CP 3521XD print - dense lower mids, true deep blacks, soft highlight roll-off, calm skies and warm, low-chroma skin. Fine 35 mm grain.
  - *Soft Cinema* — ETERNA-style softness: low-contrast daylight negative on the matching Eterna-CP print - gentle contrast, restrained colour, open shadows and a quiet, slightly cool palette.
  - *Warm Print* — Classic release-print warmth: VISION3 250D printed on 2383 with Print Crossover - golden highlights, cool teal-leaning shadows, rich contrast. The character of a photochemical 2383 chain.
  - *Sunlit 35mm (soft scan)* — Sunlit 35 mm on a soft, low-contrast scan: a Soft-aim print with full Print Crossover, yellow-green mids, open lifted blacks from a light Black Pro-Mist veil plus halation, and a gentle film edge (digital sharpening removed, weak shadow acutance) that stays soft on dark scenes.
  - *Theatrical 2383 Warm* — A warm theatrical release: VISION3 250D through the IP / IN release chain onto 2383, the colour head dialled warm with exposure preserved, gentle golden highlight toning, projector weave and lens fall-off.
  - *Soft Telecine Scan* — A flat, gentle telecine transfer: VISION3 250D on the telecine render, lifted printed blacks and a held-back white, a light white Pro-Mist, softened film edge and the slow float of a capstan transport.
  - *500T Night Exterior* — VISION3 500T pushed half a stop for night exteriors on Premier 2393: deep printed blacks, teal-toned shadows under warm practicals, strong halation, Super 35 grain and pin-registered camera weave.
  - *Scope Bleach Bypass* — A 2.39 scope frame through the camera gate with a partial bleach bypass on 2383: retained silver, steely desaturated contrast, cool-toned shadows and a hard, crisp edge.
  - *16mm Documentary* — Handheld Super 16 for documentary work: Eterna 500 on the Kodak 2383 print through the Super 16 camera gate, big lively 16 mm grain, a touch of camera weave and a lens with real mechanical vignetting.
  - *Faded Magenta Print* — An old release print gone magenta: lifted printed blacks, a softened white, the cyan dye faded towards magenta-red in highlight and shadow, printer-gate clear border, projector weave and soft 35 mm grain.
  - *Sunlit 35mm Full Gate* — The Sunlit 35mm look printed full gate and timed warmer: a warm colour head, lifted printed blacks and golden highlights, framed by the camera aperture's rounded rebate with a little overscan, a slow telecine float and the faint cos^4 fall-off of the taking lens.
- **Preset Status** (`lookPresetStatus`; read-only; shows “Custom” until a preset is chosen) — Which look preset the current settings come from. "(modified)" means you have changed at least one look setting since applying it.
- **Mode** (`mode`; default: Full)
  - *Full* — complete film emulation (colour + halation, bloom, grain, lens and gate effects).
  - *Color Only* — the colour pipeline alone - exactly what an exported LUT contains.
  - *Texture Only* — for footage that is ALREADY graded/display-referred in the selected Output space; adds only the photographic texture (halation, bloom, grain, acutance, weave, vignette) and leaves the colour untouched. Use it after your own grade or after another film look.
- **Pipeline Stage** (`pipelineStage`; default: Full Chain) — Where the film chain stops (or starts), so one node can do one job and you can build the chain from several nodes or finish with your own display transform (OpenDRT, 2499 DRT, ARRI REVEAL, ACES). Full Chain: camera -> negative -> print -> display (the complete look). Negative -> Log Scan: stops after the developed negative and writes it as a Cineon-style log scan (printing density as the selected Print Stock sees it, 0.002 density per 10-bit code value, clear film at code 95). Feed it to a second node set to Print Only. Negative -> Scene: the developed negative turned back into scene light (grey-balanced, film colour, curve shape, halation and grain kept, print and display left out), encoded in Scene Encoding (Output group). Put your own DRT after the node. Print Only: the input is a Log Scan from this plug-in (or any Cineon printing-density scan); prints it on the Print Stock and renders the display. Camera Log and Sensor Match are not used. Texture Only: same as Mode = Texture Only. Negative Only (clean): Negative -> Scene without any texture (no halation, bloom, grain, acutance, weave, vignette, damage) - the node that only flips the negative stock. ALEXA Rec.709: no film - the camera rendered the way the ALEXA's classic LogC3 -> Rec.709 look renders it (our own fitted model, see ALEXA Rendering). Textures (grain, halation, bloom, damage...) still apply. Rec.709 Gamma 2.4 is the reference display; other outputs get the same rendering re-encoded (HDR: the SDR rendering placed with grey at 15 nits). Grain, halation and damage always act in the film domain of the stage: in the negative for the negative exits, on the scanned negative's density for Print Only. Reversal (slide) stocks have no negative stage and always render the full chain.
  - Options: Full Chain · Negative -> Log Scan · Negative -> Scene · Print Only · Texture Only · Negative Only (clean) · ALEXA Rec.709
- **Gauge** (`grainFormat`; default: Super 35) — Film gauge the image was 'shot' on. It sets the whole texture ladder at once, like changing the camera: grain size and strength (Super 8 very coarse, 16 mm clearly visible, Super 35 the cinema standard, 65 mm very fine; 35 mm Stills between Super 35 and 65 mm), the film's own sharpness (Film Resolution), and the size of the halation ring and of the acutance edge effect.
  - Options: Super 8 · 16 mm · Super 35 · 65 mm · 35 mm Stills
- **User Preset** (`userPresetName`; text field) — Name of your own preset. Save User Preset writes every setting of this node (all groups, including input, output and every module switch; not the License, the LUT folder or False Colour) as `<name>.json` to the Reafilm Presets folder of your user account (macOS: ~/Library/Application Support/Reafilm/Presets, Windows: %APPDATA%\Reafilm\Presets, Linux: ~/.config/reafilm/presets). Load User Preset reads it back. Presets are plain text and can be copied between computers.
- **Save User Preset** (`userPresetSave`; button) — Saves all current settings under the User Preset name (an existing preset of that name is replaced).
- **Load User Preset** (`userPresetLoad`; button) — Loads the preset named in User Preset. Settings missing from the file (e.g. from an older version) return to their defaults; entries this version does not know are ignored and reported.

### Input

*What the camera recorded.*

- **Camera Log** (`inputSpace`; default: Fujifilm F-Log2 C / F-Gamut C) — The log encoding and gamut of the incoming footage. Set this to match your camera (default: Fujifilm F-Log2 C / F-Gamut C). In a colour-managed Resolve project, use a timeline/working space that keeps the camera log intact for this node, or convert to the matching log space before this node. Wrong settings show up as overly flat or harsh, colour-shifted images.
  - Options: Fujifilm F-Log2 C / F-Gamut C · Fujifilm F-Log2 / F-Gamut · Fujifilm F-Log / F-Gamut · DaVinci Intermediate / DaVinci Wide Gamut · ARRI LogC3 (EI 800) / ARRI Wide Gamut 3 · ARRI LogC4 / ARRI Wide Gamut 4 · Sony S-Log3 / S-Gamut3.Cine · Panasonic V-Log / V-Gamut · Canon Log 3 / Cinema Gamut · Blackmagic Film Gen 5 / BMD Wide Gamut Gen 5 · ACEScct / AP1 · Linear Rec.2020
- **Enable Exposure** (`exposureEnable`; default on) — Switches the Exposure trim below on or off without losing its value. Off = the footage reaches the film exactly as it was recorded (0 stops).
- **Exposure** (`exposure`; -4 … 4; default 0; keyframable) — Camera exposure correction in stops, applied to the scene before it reaches the film. Because it acts before the negative, +/- exposure slides the image along the film's characteristic curve: highlights roll off and shadows build density the way a real over/under-exposed negative does.
- **Enable Scene White Balance** (`sceneWbEnable`; default off) — Corrects the white balance of the footage in scene light, before it reaches the film (as if the camera had been set differently). Off = the footage's own white balance.
- **Scene Temperature (K)** (`sceneWbTemperature`; 2000 … 15000; default 6500; keyframable) — Colour temperature of the light the footage is assumed to be balanced for. 6500 = unchanged. Higher values warm the image, lower values cool it (set 3200 for daylight-balanced footage shot under tungsten light). Applied before the negative, so the film responds to the corrected light.
- **Scene Tint** (`sceneWbTint`; -100 … 100; default 0; keyframable) — Green-magenta correction: positive values add magenta (remove a green cast, e.g. from fluorescent or LED light), negative values add green. 0 = unchanged.

### Highlight Latitude

*Gives the camera's highlights the latitude of a camera with more headroom. Works on the scene light right after the F-Log2 decode, before Sensor Match, the negative and the ALEXA rendering: it finds where the X-H2S clips, rebuilds clipped colour channels so blown highlights go white instead of cyan, yellow or magenta, and stretches the last stops below the clip so they roll into the film's (or the ALEXA's) shoulder instead of stopping at a wall. Fujifilm F-Log2 C / F-Log2 footage only; everything below the shoulder is untouched.*

- **Enable Highlight Latitude** (`latitudeEnable`; default off) — Switches Highlight Latitude on or off without losing its settings. Off = the camera's highlights exactly as recorded.
- **Latitude** (`latitudeStops`; 0 … 4; default 2.5; keyframable) — Extra headroom in stops: the camera's clip level is placed this much higher, and the stops below it are stretched smoothly up to it, so highlights reach the shoulder of the film or the ALEXA rendering the way they would on a camera with this many more stops above grey. 0 = no extension (clip reconstruction only).
- **Shoulder Softness** (`latitudeSoftness`; 0 … 1; default 0.4; keyframable) — How far below the clip the extension starts: 0 = only the last half stop (hardest), 1 = the last three stops (softest, longest roll). Everything below that point is unchanged.
- **Clip Reconstruction** (`latitudeReconstruct`; 0 … 1; default 1; keyframable) — Rebuilds colour channels that clip before the others. Near-white highlights where one or two channels have clipped go smoothly to white instead of turning cyan (sky), yellow or magenta (skin, practicals); strongly coloured lights where only their own channel clips keep their colour. 0 = off.
- **Highlight Desaturation** (`latitudeDesaturate`; 0 … 1; default 0.3; keyframable) — Desaturates the stretched top stops toward white, the way a negative's colour thins out as it nears its shoulder. 0 = keep the colour of the highlights (only the clip reconstruction acts).
- **Sensor Clip** (`latitudeClip`; 0.6 … 1; default 0.854; keyframable) — F-Log2 code value (0..1, full range) at which the camera's channels clip. 0.854 (10-bit 874) is where the X-H2S clips in F-Log2 / F-Log2 C, measured on its footage: about 6.3 stops above 18 % grey. Lower it if a clip still shows colour casts; raise it for footage that clips higher.

### Sensor Match

*The subtle, sensor-level part of looking like another camera: what that camera's sensor would have recorded of the same scene, before any look or rendering. It does not add the camera's display look (for the ALEXA look see ALEXA Rendering).*

- **Enable Sensor Match** (`cameraEmulationEnable`; default on) — Switches the sensor match on or off without losing its settings. Off = the camera's own colour as decoded from its log encoding. It only acts on footage of the source camera (Fujifilm F-Log2 C / F-Log2 / F-Log input); other cameras pass through unchanged.
- **Sensor** (`cameraEmulation`; default: Fujifilm X-H2S -> ARRI ALEXA / AMIRA) — Fujifilm X-H2S -> ARRI ALEXA / AMIRA: renders the colours the ALEXA's ALEV III sensor would have recorded of the same scene. Fitted from measured spectral responsivities (ARRI ALEXA; Fujifilm X-Trans X-T4 as the X-H2S proxy) over about 950 natural and test reflectances under daylight, tungsten, LED and fluorescent light: a smooth, exposure-independent correction in scene-linear light that leaves greys untouched. It changes skin, foliage, skies and saturated light the way the two sensors differ; colours two sensors see identically stay identical (sensor metamerism a 3-channel correction cannot undo).
  - Options: Fujifilm X-H2S -> ARRI ALEXA / AMIRA
- **Strength** (`cameraEmulationStrength`; 0 … 1; default 1; keyframable) — Blend between the camera's own colour (0) and the full sensor match (1).

### ALEXA Rendering

*Our own parametric model of the ALEXA's classic LogC3 -> Rec.709 rendering (tone curve, hue- and brightness-dependent saturation and hue, skin), fitted to ARRI's published look. Two uses: Pipeline Stage = ALEXA Rec.709 renders the camera that way instead of a film print; ALEXA Colour brings only its colour into the film chain.*

- **Enable ALEXA Rec.709** (`alexaDisplayEnable`; default on) — Only matters when Pipeline Stage = ALEXA Rec.709. Off = that node renders the Full Chain instead (quick A/B between the ALEXA rendering and the film look).
- **Enable ALEXA Colour** (`alexaColourEnable`; default off) — Switches ALEXA Colour on or off without losing its value. Off = the film sees the camera's colour unchanged.
- **ALEXA Colour** (`alexaColour`; 0 … 1; default 1; keyframable) — How much of the ALEXA rendering's colour (its hue shifts and saturation per hue, skin) the scene gets before the negative, matched through the film: cherry reds, greens without a blue lean, cohesive skin, neutral whites and slightly cool shadows. Applied in scene light, so mid grey and exposure stay where they are and the film still does the tone and the print look. 0 = none, 1 = the full ALEXA colour.

### Tone

*Zone exposure, filmic contrast, highlight roll-off and dye saturation - the film responds to them physically.*

- **Enable Zones & Contrast** (`toneEnable`; default on) — Switches the zone exposure and Filmic Contrast below on or off without losing their values. Both act on the scene light after Exposure and before the negative, so the film's toe and shoulder still shape the result. At their defaults (0 stops, contrast 1) they change nothing.
- **Shadows** (`zoneShadows`; -4 … 4; default 0; keyframable) — Exposure in stops of everything BELOW Shadows Range (dark areas). Like Exposure, but only for that part of the tone scale: it acts on the scene light before the film, ratio-preserving (colours keep their hue and saturation, greys stay neutral), with a smooth transition set by Zone Falloff. Tones can never cross: a strong pull is softened so a brighter area always stays brighter.
- **Shadows Range** (`zoneShadowsRange`; -8 … 2; default -2; keyframable) — Where the Shadows zone ends, in stops relative to 18 % grey of the scene (after Exposure).
- **Lights** (`zoneLights`; -4 … 4; default 0; keyframable) — Exposure in stops of everything ABOVE Lights Range (bright mid-tones and above). Like Exposure, but only for that part of the tone scale: it acts on the scene light before the film, ratio-preserving (colours keep their hue and saturation, greys stay neutral), with a smooth transition set by Zone Falloff. Tones can never cross: a strong pull is softened so a brighter area always stays brighter.
- **Lights Range** (`zoneLightsRange`; -2 … 6; default 1; keyframable) — Where the Lights zone begins, in stops relative to 18 % grey of the scene.
- **Highlights** (`zoneHighlights`; -4 … 4; default 0; keyframable) — Exposure in stops of everything ABOVE Highlights Range (skies, windows, bright walls). Pull it down (-) to bring sky detail out of the print's shoulder into its straight part while faces stay where they are. Like Exposure, but only for that part of the tone scale: it acts on the scene light before the film, ratio-preserving (colours keep their hue and saturation, greys stay neutral), with a smooth transition set by Zone Falloff. Tones can never cross: a strong pull is softened so a brighter area always stays brighter.
- **Highlights Range** (`zoneHighlightsRange`; -1 … 8; default 3; keyframable) — Where the Highlights zone begins, in stops relative to 18 % grey of the scene (a bright sky is typically +3 to +5 stops).
- **Specular** (`zoneSpecular`; -4 … 4; default 0; keyframable) — Exposure in stops of everything ABOVE Specular Range (practicals, the sun, glints). Like Exposure, but only for that part of the tone scale: it acts on the scene light before the film, ratio-preserving (colours keep their hue and saturation, greys stay neutral), with a smooth transition set by Zone Falloff. Tones can never cross: a strong pull is softened so a brighter area always stays brighter.
- **Specular Range** (`zoneSpecularRange`; 0 … 10; default 5; keyframable) — Where the Specular zone begins, in stops relative to 18 % grey of the scene.
- **Zone Falloff** (`zoneFalloff`; 0.25 … 4; default 1; keyframable) — How softly every zone fades in around its range, in stops. Small = a tight zone, large = a gentle, wide blend.
- **Filmic Contrast** (`filmContrast`; 0.5 … 2; default 1; keyframable) — Contrast of the scene in log exposure about the Contrast Pivot, before the negative: the film's toe and shoulder still roll the ends off, so blacks and whites never clip. 1 = off, higher = punchier, lower = flatter. The colour density of the stock is kept (the channels follow half of the contrast change, so saturation neither drains nor goes neon).
- **Contrast Pivot** (`contrastPivot`; -3 … 3; default 0; keyframable) — The tone that Filmic Contrast leaves unchanged, in stops relative to 18 % grey (0 = middle grey).
- **Enable Film Roll-off** (`rolloffEnable`; default on) — Switches the Film Roll-off below on or off without losing its values. At Softness 0 it changes nothing.
- **Roll-off Start** (`rolloffStart`; 0.5 … 8; default 3; keyframable) — Where the reshaped highlight shoulder of the print begins, in stops above 18 % grey of the scene. Everything below stays exactly as the print stock renders it.
- **Roll-off Softness** (`rolloffSoftness`; -1 … 1; default 0; keyframable) — Shape of the print's highlight shoulder above Roll-off Start: + = a longer, softer shoulder (highlights compress gently and keep detail, white is reached later), - = a shorter, crisper shoulder (highlights reach print white sooner). It reshapes the print stock's own curve, so print white stays the clear print and printer points slide along the new curve. 0 = the stock. Print process only.
- **Enable Dye Saturation** (`dyeSatEnable`; default on) — Switches Dye Saturation below on or off without losing its values. At Saturation 1 and Balance 0 it changes nothing.
- **Dye Saturation** (`dyeSaturation`; 0 … 2; default 1; keyframable) — Saturation the way dye film makes it: more saturation means more dye in the layers that carry the colour, so saturated colours get deeper, denser and darker instead of brighter and neon (unlike RGB saturation). It scales each colour's dye difference from a neutral of the same brightness in the projected film (print, reversal original or scan), so greys, print white and black stay exactly as they are. 1 = the stock, 0 = black & white dyes.
- **Richness** (`dyeRichness`; 0 … 1; default 0.25; keyframable) — How much denser (darker) a colour gets as it gains saturation: the extra dye a richer colour carries. 0 = the dye differences are only spread (the colour keeps its brightness), 0.25 = film-like (strong colours deepen a little, skin barely darkens), 0.5 = rich, 1 = very dense. Lowering saturation does the reverse. Only acts when Dye Saturation or Balance moves.
- **Saturation Balance** (`dyeSatBalance`; -1 … 1; default 0; keyframable) — Where the dye saturation acts most: + = more saturation in the highlights and less in the shadows, - = the reverse (up to x2 / x0.5 at the ends). 0 = even.

### Negative

*Camera film stock and its development.*

- **Film Stock** (`filmStock`; default: Kodak Vision3 250D 5207) — The camera negative (or reversal / black & white stock), from the manufacturer's data sheet. It defines spectral sensitivity, tone curve, colour crosstalk, grain character and halation behaviour.
  - *Kodak Vision3 50D 5203* — VISION3 50D: the finest-grain daylight negative, clean and saturated with long highlight latitude.
  - *Kodak Vision3 250D 5207* — VISION3 250D: medium-speed daylight negative, natural colour and soft, open highlights.
  - *Kodak Vision3 200T 5213* — VISION3 200T: tungsten negative with fine grain, rich mid-tones and clean shadows.
  - *Kodak Vision3 500T 5219* — VISION3 500T: the high-speed tungsten workhorse: low shadow grain, warm under tungsten, big latitude.
  - *Kodak Vision2 500T 5218* — VISION2 500T: the 2000s high-speed tungsten negative: a little more grain and less latitude than VISION3, warm, punchy mids.
  - *Kodak Vision2 Expression 500T 5229* — VISION2 Expression 500T: low-contrast VISION2 tungsten negative for a soft, flat scan with lifted shadows.
  - *Kodak Portra 400* — PORTRA 400: portrait colour negative: warm, low-chroma skin, very wide latitude.
  - *Kodak Ektachrome 100D 5294* — EKTACHROME 100D: the motion-picture colour reversal: clean, saturated, contrasty, short latitude.
  - *Kodak Ektachrome E100* — EKTACHROME E100: daylight reversal: clean neutral greys, crisp, saturated colour, short latitude.
  - *Kodachrome 64 Style* — Kodachrome-style reversal (designed, no data sheet): dense, contrasty, warm reds, deep blue skies.
  - *Kodak Double-X 5222* — DOUBLE-X 5222: the classic panchromatic B&W negative: open grain, blue-heavy sensitivity, gentle toe.
  - *Fujifilm Eterna Vivid 160 8543* — ETERNA Vivid 160: tungsten negative with more saturation and contrast than ETERNA (Fujifilm curves; spectral proxies).
  - *Fujifilm Eterna 250D 8563* — ETERNA 250D: soft-contrast daylight negative with restrained, calm colour (Fujifilm curves; spectral proxies).
  - *Fujifilm Eterna 500 8573* — ETERNA 500: high-speed tungsten negative with calm, low-saturation ETERNA colour (Fujifilm curves; spectral proxies).
  - *Fujifilm Reala 500D 8592* — REALA 500D: high-speed daylight negative (Fujifilm curves; spectral proxies, the fourth colour layer is not modelled).
- **Enable Development** (`developmentEnable`; default on) — Switches the development controls below (Push / Pull, Color Density, Neg Bleach Bypass, Neutralize) on or off without losing their values. Off = the stock's standard development: no push or pull, nominal colour density, normal bleach, the stock's own curves. The film stock itself always stays active - it is the look.
- **Push / Pull** (`pushPull`; -2 … 3; default 0; keyframable) — Development in stops. Push (+) raises contrast and effective speed, lifts fog and makes the grain coarser (about x1.25 per stop); pull (-) gives a softer, lower-contrast, finer-grained negative. Unlike Exposure this changes the curve shape, not the position on it.
- **Color Density** (`interlayer`; 0 … 2; default 1; keyframable) — Interlayer (DIR) effect: how strongly the dye layers inhibit each other. 1 = the stock's nominal colour density; higher values give denser, more separated colours, 0 a flatter, purer-channel rendering. Neutrals stay neutral at any setting.
- **Neg Bleach Bypass** (`negBleach`; 0 … 1; default 0; keyframable) — Skips (partially) the bleach step of the negative so silver stays in the film alongside the dyes. The extra silver density builds with exposure: higher contrast, harder highlights and muted colour, strongest in the bright parts of the image. 0 = normal processing.
- **Neutralize** (`neutralize`; 0 … 1; default 0; keyframable) — Removes the stock's per-layer curve differences (the subtle colour crossover between shadows and highlights). 0 = the stock's natural character, 1 = perfectly neutral greys from black to white.
- **Stock Data** (`negProvenance`; read-only; “Data sheet: Kodak H-1-5207 (2015): spectral sensitivity, characteristic curves; dyes from H-1-5219t (VISION3 dye set). ECN-2. Fresh stock at the nominal process; fitted to the published curves, not measured from film.”) — Where the selected film stock's data comes from (v1.3): a manufacturer data sheet (fitted, not measured from film), partly borrowed from a named proxy stock, a Reafilm design, or a style approximation.

### Print

*How the negative becomes a positive image.*

- **Process** (`process`; default: Print)
  - *Print* — the negative is printed onto print stock and projected - the classic cinema look with dense blacks and rich colour.
  - *Lab Scan* — the negative is scanned and inverted by a lab scanner - a flatter, more neutral rendering that keeps the negative's character without print-stock contrast.
  - Reversal (slide) stocks are direct positives and ignore this setting.
- **Print Stock** (`printFilm`; default: Kodak Vision 2383) — The print film the negative is printed on (Process = Print), from the manufacturer's data sheet. It sets print contrast, black density, highlight roll-off and the colour of the projected image.
  - *Kodak Vision 2383* — the standard release print.
Kodak Vision Premier 2393: higher D-max, denser blacks, more saturated.
Fujifilm Eterna-CP 3521XD: deep blacks, rich contrast.
Fujifilm Eterna-CP 3513DI: neutral and gentle, made for digital intermediates.
- **Enable Print Timing** (`printEnable`; default on) — Switches the lab timing controls below (Printer Red / Green / Blue, Print Density, Print Bleach Bypass) on or off without losing their values. Off = the automatically grey-balanced light at 0 points and normal print processing. Process and Print Stock always stay active - they are the look.
- **Printer Red** (`printerR`; -12 … 12; default 0; keyframable) — Red printer light. More red light (+) adds cyan to the print: the image gets LESS red / more cyan. Fewer points (-) warm it towards red. Printer points as in a film lab: 1 point = 0.025 log exposure on that print layer, 0 = the stock's automatically grey-balanced light.
- **Printer Green** (`printerG`; -12 … 12; default 0; keyframable) — Green printer light. More green light (+) adds magenta to the print: the image gets LESS green / more magenta. Fewer points (-) push it towards green. Printer points as in a film lab: 1 point = 0.025 log exposure on that print layer, 0 = the stock's automatically grey-balanced light.
- **Printer Blue** (`printerB`; -12 … 12; default 0; keyframable) — Blue printer light. More blue light (+) adds yellow to the print: the image gets LESS blue / more yellow. Fewer points (-) cool it towards blue. Printer points as in a film lab: 1 point = 0.025 log exposure on that print layer, 0 = the stock's automatically grey-balanced light.
- **Print Density** (`printExposure`; -12 … 12; default 0; keyframable) — Overall printer light in points on all three layers. Positive values make the print BRIGHTER (less print density), negative values darker and denser. This is the lab's way to brighten or darken a print: highlights and blacks respond like real print film, not like a gain.
- **Print Bleach Bypass** (`printBleach`; 0 … 1; default 0; keyframable) — Bleach bypass on the print: retained silver deepens the blacks and shadows, raises contrast and desaturates - the classic gritty, metallic release-print look. 0 = normal processing.
- **Print Crossover** (`printCrossover`; -1 … 1; default 0; keyframable) — Warm highlights and cool shadows the way many release prints render them (the classic 2383 chain): positive values add yellow and remove cyan in the bright end of the print and do the opposite in the shadows, growing from the middle of the scale outward, while middle grey stays neutral. Negative values reverse it (cool highlights, warm shadows). 0 = the print stock exactly as designed. Print process only.
- **Print Stock Data** (`printProvenance`; read-only; “Data sheet: Kodak H-1-2383 (2022): spectral sensitivity, Status A curves, spectral dye density. ECP-2 nominal; lab aim, projection and rendering are set in this group. Fresh stock at the nominal process; fitted to the published curves, not measured from film.”) — Where the selected print stock's data comes from: a manufacturer data sheet (fitted, not measured from film), partly borrowed from a named proxy stock, a Reafilm design, or a style approximation.
- **Enable Print Lab** (`printLabEnable`; default on) — Switches the print lab controls below (Projection White, Process Aim, Print Contrast, Rendering) on or off without losing their values. Off = a contact print at the nominal process aim, adapted to D65 - exactly the v1.2 print.
- **Projection White** (`projectionWhite`; default: D65 (Adapted)) — The white the projected print is shown at. D65 (Adapted): the eye adapted to the projector lamp, mapped to the D65 white of a video display (the classic film-out preview). Theatre Xenon (Unadapted): the print as a colorimeter reads it off a xenon-lit screen - the clear print base and the lamp give a warmer, slightly yellow white. D60 Sim: adapted to the ACES D60 white and shown on a D65 display (a slightly warm white, as in DCI / ACES D60 sim mastering).
  - Options: D65 (Adapted) · Theatre Xenon (Unadapted) · D60 Sim
- **Process Aim** (`printAim`; default: Nominal) — Where the lab runs its print process within its tolerance band (ECP-2 development). Nominal: the data-sheet aim. Rich: one unit more development - higher print gamma, deeper D-max, slightly more fog; denser blacks and more saturated colour. Soft: one unit less - lower gamma and D-max, lifted blacks, gentler colour. The lab re-times the print, so middle grey stays put.
  - Options: Nominal · Rich · Soft
- **Print Contrast** (`printContrast`; -1 … 1; default 0; keyframable) — Fine control of print development in the same units as Process Aim (added to it): + raises the print's gamma and D-max (more contrast, deeper blacks), - lowers them. Each layer reacts by its own development sensitivity (the bottom yellow layer most). Middle grey stays at the print's aim.
- **Rendering** (`printRender`; default: Contact Print) — How the print is made and viewed. Contact Print: the camera negative printed directly (answer / show print). Release Print (IP / IN): the negative is duplicated twice on VISION Color Intermediate 2242 (interpositive, internegative) at the lab aims before printing - slightly softer toe and shoulder and a subtle colour shift, as in a theatrical release print. Telecine / Scan: the print scanned on a telecine; every layer is balanced between print D-min and D-max and the black is set just above D-max - neutral blacks and highlights.
  - Options: Contact Print · Release Print (IP / IN) · Telecine / Scan
- **Enable Colour Head** (`colourHeadEnable`; default on) — Switches the Colour Head (Cyan / Magenta / Yellow filtration, Preserve Exposure) on or off without losing its values. Off = no extra filtration.
- **Head Cyan** (`headCyan`; -60 … 60; default 0; keyframable) — Cyan filtration: holds back red printer light, so the print gets LESS cyan dye - the image turns more red. Negative values remove cyan from the pack (image more cyan). In CC (colour compensating units: 10 CC = 0.10 density) of an idealised dichroic filter in the printer's colour head, relative to the balanced filter pack (0). The filter is integrated over the printer lamp, the negative and the print layers' sensitivities, so it acts like a real filter (with its small cross-talk), not like a channel gain. Print process only.
- **Head Magenta** (`headMagenta`; -60 … 60; default 0; keyframable) — Magenta filtration: holds back green printer light - the image turns more green. Negative values: more magenta. In CC (colour compensating units: 10 CC = 0.10 density) of an idealised dichroic filter in the printer's colour head, relative to the balanced filter pack (0). The filter is integrated over the printer lamp, the negative and the print layers' sensitivities, so it acts like a real filter (with its small cross-talk), not like a channel gain. Print process only.
- **Head Yellow** (`headYellow`; -60 … 60; default 0; keyframable) — Yellow filtration: holds back blue printer light - the image turns more blue. Negative values: warmer, more yellow. (To remove a cast, add the filter of the cast's colour, as in a darkroom.) In CC (colour compensating units: 10 CC = 0.10 density) of an idealised dichroic filter in the printer's colour head, relative to the balanced filter pack (0). The filter is integrated over the printer lamp, the negative and the print layers' sensitivities, so it acts like a real filter (with its small cross-talk), not like a channel gain. Print process only.
- **Preserve Exposure** (`headPreserve`; 0 … 1; default 1; keyframable) — Filters also take light away. 1 = the printer's overall light is corrected so the print's middle grey keeps its brightness (what a colour timer does); 0 = the filtration's light loss stays (more filtration = a lighter print, as on an enlarger without compensation).
- **Printer Lights** (`printLights`; read-only; “R 25.0  G 25.0  B 25.0”) — The printer lights in the lab convention: 1 point = 0.025 log exposure, nominal 25-25-25 = the stock's grey-balanced light. Higher numbers = more light on that layer = more of its dye (a darker, less red / green / blue print). Shows Printer Red / Green / Blue and Print Density, and the Colour Head filtration as its equivalent in points (before Preserve Exposure).
- **Enable Print Black / White** (`printEndsEnable`; default on) — Switches Print Black, Print White and De-Fog on or off without losing their values.
- **Print Black** (`printBlack`; -1.5 … 0.25; default 0; keyframable) — Density at the black end of the print (its D-max), in density units of the print: + = deeper, denser blacks; - = a lifted, milky black like a flashed or under-developed print. The tones above the shadows are left alone (middle grey moves less than half a percent). This is the print's density, not a video lift. Print process only.
- **Print White** (`printWhite`; -0.15 … 0.4; default 0; keyframable) — Density at the white end of the print (its D-min): + = greyer, dimmer whites, as a print on stock with more base fog or a slightly over-printed answer print; - = cleaner whites that reach the clear base sooner. Acts only near white. Print process only.
- **De-Fog** (`printDefog`; 0 … 1; default 0; keyframable) — Removes this share of the print stock's own fog density from its white end (anti-fog in the developer): clearer, slightly brighter and more neutral whites. Fog on the NEGATIVE is not offered: the lab's grey balance times it out completely. Print process only.
- **Enable Print Toning** (`printToneEnable`; default on) — Switches Print Toning (shadow and highlight toning) on or off without losing its values.
- **Shadow Toning** (`toneShadowAmount`; 0 … 1; default 0; keyframable) — Dye toning of the print's dense tones, like a selenium or blue toner on a print: the shadows take on the Shadow Hue while middle grey and the whites stay neutral. 1 = 0.10 density of toning. 0 = off.
- **Shadow Hue** (`toneShadowHue`; 0 … 360; default 220; keyframable) — Colour of the shadow toning, in degrees: 0 red, 30 sepia / orange, 60 yellow, 120 green, 180 cyan, 220 steel blue, 240 blue, 300 magenta.
- **Highlight Toning** (`toneHighlightAmount`; 0 … 1; default 0; keyframable) — Dye toning of the print's light tones (just above white, never the clear base itself): a warm or cool cast in the highlights while middle grey and black stay neutral. 1 = 0.10 density. 0 = off.
- **Highlight Hue** (`toneHighlightHue`; 0 … 360; default 50; keyframable) — Colour of the highlight toning in degrees (see Shadow Hue). 40 .. 60 = a warm, golden print.

### Halation

*Red-orange glow around bright light sources.*

- **Enable Halation** (`halationEnable`; default on) — Switches halation on or off without losing its settings. Off skips the halation passes entirely (no render cost).
- **Halation Amount** (`halationAmount`; 0 … 2; default 0.5; keyframable) — Strength of halation: light that passes through the emulsion, reflects off the film base and re-exposes the red (and a little of the green) layer, creating a warm ring that hugs bright edges and practicals. It is added to the film's exposure, so the film curve shapes it like the image. Scaled by the stock (no-backing stocks glow much more). 0 = off.
- **Halation Radius** (`halationRadius`; 2 … 80; default 20; keyframable) — Size of the halation ring in pixels at 1920 px image width for Super 35 (automatically scaled for other resolutions, proxy render scales and the Gauge: smaller gauges enlarge the ring).
- **Halation Threshold (0 = physical)** (`halationThreshold`; 0 … 6; default 0; keyframable) — 0 (default) = physical halation, no threshold: light reflects off the film base, so bright edges get a thin warm ring. Practicals, lamps, the sun, speculars and clipped highlights get the full share; a bright sky edge (about 4 stops above middle grey) still gets about three quarters of it, while skin and mid-tones hardly halate. Flat areas stay clean because the ring only shows where light spills beyond an edge. Any value above 0 switches to a different, artistic mode: a gate where only highlights brighter than this many stops above middle grey halate (the v1.1 behaviour; the glow eases in over about a stop and a half). Small values therefore halate MORE of the image than 0.
- **Halation Hue** (`halationHue`; -1 … 1; default 0; keyframable) — Colour of the glow: how much of the reflected light re-exposes the green layer as well as the red. -1 deep red, 0 the classic red-orange, +1 orange-yellow.
- **Halation Saturation** (`halationSaturation`; 0 … 2; default 1; keyframable) — Colour strength of the glow. 0 = a neutral, uncoloured glow, 1 = the film's natural red-orange, 2 = deeper colour. The brightness of the glow stays the same.
- **Halation Background** (`halationBackground`; -1 … 1; default 0; keyframable) — How the glow's colour reacts to what is behind it. Above 0 the glow is more colourful over dark backgrounds and paler over bright ones (as on a projected print); below 0 the other way round. 0 = the same colour everywhere.
- **Enable Aura** (`auraEnable`; default off) — Adds a second, much wider and fainter glow around strong light sources: light that bounces more than once inside the film base. It gives lamps and the sun a soft warm haze around the ring. Needs Halation on. Off (the default) costs nothing.
- **Aura Radius** (`auraRadius`; 40 … 400; default 150; keyframable) — Size of the aura in pixels at 1920 px image width for Super 35 (scaled automatically for other resolutions and the Gauge).
- **Aura Strength** (`auraStrength`; 0 … 2; default 0.5; keyframable) — Brightness of the aura, relative to the halation amount. 0 = none.
- **Aura Green** (`auraGreen`; 0 … 1; default 0.35; keyframable) — Share of the green layer in the aura: 0 = deep red, higher values turn it orange and yellow.
- **Show Halation (diagnostic)** (`halationShow`; default: Off) — DIAGNOSTIC VIEW - switch off before rendering. Effect Only: only the light halation (and Aura) adds, 8x brighter, on black. Mask: which parts of the image feed halation (red = their full share, over a dim grey picture). If several Show controls are on, the one furthest down the pipeline is shown.
  - Options: Off · Effect Only · Mask

### Bloom

*Neutral diffusion of highlights.*

- **Enable Bloom** (`bloomEnable`; default on) — Switches bloom on or off without losing its settings. Off skips the bloom passes entirely (no render cost).
- **Bloom Amount** (`bloomAmount`; 0 … 2; default 0.15; keyframable) — Gentle, neutral glow of bright areas from lens and emulsion scatter - softens harsh digital highlights without the warm tint of halation. Scaled by the stock. 0 = off.
- **Bloom Radius** (`bloomRadius`; 5 … 200; default 40; keyframable) — Spread of the bloom in pixels at 1920 px image width (scaled automatically for other resolutions).
- **Bloom Threshold** (`bloomThreshold`; 0 … 8; default 0; keyframable) — Limits the bloom to bright areas: only levels from about this many stops above 18 % grey glow (soft onset). 0 = off, every level contributes as before (diffusion is linear, so the glow follows the brightest parts anyway).
- **Bloom Keep Highlights** (`bloomKeepHighlights`; 0 … 1; default 0; keyframable) — 0 = energy preserving (as before): the light that spreads into the glow is taken from the bright source, so highlights soften a little and overall brightness stays the same. Higher values keep the source at full brightness and add the glow on top (brighter, more 'mist').
- **Bloom Filter** (`bloomFilter`; default: Classic)
  - *Classic* — the bloom as before (Bloom Amount sets it). The others emulate a diffusion filter in front of the lens, modelled from the physics of each filter type (our own model, not measured filter data): a fixed share of ALL light is scattered into a soft halo, so highlights glow, every bright edge gets a soft halo and the scattered light lifts the shadows. The share is set by Filter Grade; Bloom Amount is not used.
  - *Black Pro-Mist* — halo with a restrained veil, keeps the blacks fairly deep.
  - *Pro-Mist (white)* — more light in a wide veil - milky, lower contrast.
  - *Glimmerglass* — tight, bright halo around highlights, little veil.
  - *Hollywood Black Magic* — fine, close diffusion that softens detail more than it glows.
  - *CineBloom* — soft, medium glow without a wide tail.
- **Filter Grade** (`bloomFilterGrade`; 0.125 … 2; default 0.25; keyframable) — Strength of the diffusion filter in filter grades: 0.125 = 1/8, 0.25 = 1/4, 0.5 = 1/2, 1, 2. Bloom Radius scales the size of its halo. Classic ignores it.
- **Show Bloom (diagnostic)** (`bloomShow`; default off) — DIAGNOSTIC VIEW - switch off before rendering. Shows where the bloom or diffusion filter brightens the picture (lighter than grey, e.g. the shadow lift of a mist filter) or takes light from highlights (darker): grey = unchanged, white = +1 stop.

### Grain

*Film grain, generated in the film's density domain.*

- **Enable Grain** (`grainEnable`; default on) — Adds film grain: dye clouds of a fast (coarse) and a slow (fine) emulsion in every colour layer, developed from each layer's own exposure (granularity measured from published film data). Coarse, sparse grain lives in the shadows and lower mid-tones, finer grain in the highlights, and the film curves shape it - it sits inside the image instead of on top of it. Off skips the grain passes entirely and keeps the grain settings.
- **Grain Amount** (`grainAmount`; 0 … 3; default 1; keyframable) — Grain strength. 1 = the stock's nominal granularity for the chosen Gauge and development; push processing increases it automatically.
- **Grain Size** (`grainSize`; 0.25 … 3; default 1; keyframable) — Grain size relative to the stock and format (1 = nominal; down to 0.25 for very fine grain, which resolves best on a 4K-6K timeline). Resolution independent: it looks the same at HD, UHD and in proxy renders.
- **Grain Softness** (`grainSoftness`; 0 … 1; default 0.5; keyframable) — 0 = crisp, sharply defined dye clouds (like a sharp scan), 1 = soft clumps with soft edges (like an optical print or a softer scan). Integrate (Film Resolution) softens grain and image together.
- **Animated Grain** (`grainAnimated`; default on) — On: a new grain pattern every frame, like real film. Off: a frozen pattern (useful for stills or for matching a static texture).
- **Grain Seed** (`grainSeed`; 0 … 9999; default 0; keyframable) — Changes the random grain pattern. Give different shots or layers different seeds so their grain never lines up.
- **Grain Colour** (`grainColour`; 0 … 1.5; default 1; keyframable) — How independent the grain of the three colour layers is. 0 = the layers share one grain pattern (neutral, almost black-and-white grain), 1 = the stock's natural, slightly coloured grain, 1.5 = more colour in the grain (as in an underexposed or pushed negative).
- **Grain Shadows** (`grainShadows`; 0 … 2; default 1; keyframable) — Grain strength in the shadows (about 1.5 stops under middle grey and darker), relative to the stock. 1 = nominal.
- **Grain Mids** (`grainMids`; 0 … 2; default 1; keyframable) — Grain strength in the mid-tones, relative to the stock. 1 = nominal.
- **Grain Highlights** (`grainHighlights`; 0 … 2; default 1; keyframable) — Grain strength in the highlights (about 2 stops over middle grey and brighter), relative to the stock. 1 = nominal.
- **Show Grain (diagnostic)** (`grainShow`; default off) — DIAGNOSTIC VIEW - switch off before rendering. Shows only the grain the film adds, 4x stronger, around middle grey (grey = no grain). View it at 100 % zoom: where the image is brighter or darker the grain changes as on film.

### Film Resolution

*The film's own sharpness, so grain and picture share one resolution.*

- **Enable Film Resolution** (`filmResolutionEnable`; default on) — Gives the image the modulation transfer of the camera negative for the chosen Gauge (measured from published film data: the red layer softest, blue sharpest). Digital micro-contrast is finer than any film grain; without this the grain looks laid on top of a too-sharp picture. Off skips the passes (Integrate still applies).
- **Integrate** (`integrate`; 0 … 1; default 0.5; keyframable) — The print and scan resolution that picture AND grain share: higher values soften both together, so the grain is part of the image instead of an overlay (the colourist's 'defocus under the grain', done physically). 0 = off.
- **Digital Sharpening Removal** (`sharpenRemoval`; 0 … 1.5; default 0; keyframable) — Undoes the edge sharpening of mirrorless cameras before the film resolution: the fine bright and dark halos along edges are pulled back, so film edges replace digital ones. 1 = removes a typical in-camera sharpening (strength 0.5 at the camera's own pixel size); frame relative, so a downscaled timeline gets correspondingly less. 0 = off.
- **Show Removed Detail (diagnostic)** (`filmResolutionShow`; default off) — DIAGNOSTIC VIEW - switch off before rendering. Shows the detail Film Resolution and Digital Sharpening Removal take away, 4x stronger, around middle grey (grey = unchanged; bright and dark lines = softened edges and texture).

### Lens & Gate

*Sharpness, gate weave and vignetting.*

- **Enable Acutance** (`acutanceEnable`; default on) — Switches acutance (Acutance, Acutance Radius) on or off without losing its settings. Off = the image's own sharpness; the passes are skipped.
- **Acutance** (`acutanceAmount`; -1 … 2; default 0.35; keyframable) — Edge contrast from development adjacency effects: crisp, film-like apparent sharpness without the halos of digital sharpening. Negative values soften the image.
- **Acutance Radius** (`acutanceRadius`; 0.5 … 10; default 2; keyframable) — Size of the adjacency effect in pixels at 1920 px width for Super 35 (scaled with the Gauge: the effect is a fixed distance on the film). Small = fine micro-contrast, larger = broader edge enhancement.
- **Show Acutance (diagnostic)** (`acutanceShow`; default off) — DIAGNOSTIC VIEW - switch off before rendering. Shows the edge contrast acutance adds (brighter than grey) or takes away (darker), 4x stronger, around middle grey (grey = unchanged).
- **Enable Gate Weave** (`weaveEnable`; default on) — Switches gate weave (Gate Weave, Weave Speed) on or off without losing its settings. Off = a rock steady frame; the pass is skipped.
- **Gate Weave** (`weaveAmount`; 0 … 2; default 0; keyframable) — Subtle frame-to-frame movement of the film in the camera/projector gate, mostly vertical. 1 = typical 35 mm projection; keep it low for a believable result. 0 = off.
- **Weave Speed** (`weaveSpeed`; 0.25 … 4; default 1; keyframable) — How quickly the gate weave wanders (1 = natural; lower is a slow drift, higher a nervous jitter).
- **Enable Vignette** (`vignetteEnable`; default on) — Switches the vignette on or off without losing its setting. Off = even illumination; the pass is skipped.
- **Vignette** (`vignette`; 0 … 1; default 0; keyframable) — Natural lens light fall-off towards the corners, applied to the scene light before the film (so it darkens like an optical vignette, not a grey overlay). 0 = off.
- **Weave Style** (`weaveStyle`; default: Projector) — The kind of gate weave (Gate Weave sets its size). Projector: the classic 35 mm projection weave, mostly vertical, wandering slowly with a little frame-to-frame jitter. Camera (Pin-Registered): a registration-pin camera - tiny, almost only frame-to-frame jitter. Telecine (Float): a film scanned on a capstan telecine - a slow, smooth float with more sideways and rotational drift.
  - Options: Projector · Camera (Pin-Registered) · Telecine (Float)
- **Mechanical Vignette** (`vignetteMechanical`; 0 … 1; default 0; keyframable) — Mechanical vignetting (a lens hood, matte box or the lens barrel cutting the light cone): a darker rim with a defined edge, on top of the natural cos^4 fall-off (Vignette). Applied to the scene light before the film. 0 = off.
- **Mechanical Size** (`vignetteMechSize`; 0.5 … 1.5; default 1; keyframable) — Where the mechanical vignette starts, as a share of the half diagonal (1 = it just reaches into the corners; smaller = a tighter circle).
- **Mechanical Softness** (`vignetteMechSoftness`; 0.05 … 1; default 0.35; keyframable) — Width of the mechanical vignette's edge: small = a hard edge (a hood far from the lens), large = a gentle roll-off.

### Film Gate

*The film gate's frame line as a density event on the film: aspect, rounded corners, soft edge, gate hair.*

- **Enable Film Gate** (`gateEnable`; default off) — Shows the edge of the film gate (the frame line). Off = the full picture, as before.
- **Gate Aspect** (`gateAspect`; default: Full Frame) — The aperture of the gate. Full Frame: the gate around the whole picture (only the Overscan shows). Every other aspect is fitted inside the frame (letterbox or pillarbox); the image outside it is masked like a hard matte in the gate. Super 16 also uses the smaller gauge's tighter corners.
  - Options: Full Frame · 1.33 Academy · 1.66 European · 1.85 Flat · 2.39 Scope · 1.43 IMAX · Super 16 (1.66) · 1.78 (16:9)
- **Gate** (`gateSource`; default: Camera Gate (Rebate Prints Black)) — Which gate makes the edge. Camera Gate: the negative outside the camera aperture stays unexposed (clear D-min), so it PRINTS BLACK. Printer Gate: the print outside the printer aperture stays unexposed, so the border is the CLEAR print base (white on the screen; dark on a reversal original, which has no print). Either way the border stays clean: grain, halation, bloom, dust, weave and projection blur only act on the picture; the frame line stays steady while the picture weaves.
  - Options: Camera Gate (Rebate Prints Black) · Printer Gate (Clear Print Border)
- **Overscan** (`gateOverscan`; 0 … 0.15; default 0.03; keyframable) — How much of the area outside the gate is visible: the gate is inset by this share of the frame height on every side (0 = the gate edge sits exactly at the frame edge).
- **Corner Radius** (`gateCorner`; 0 … 80; default 14; keyframable) — Radius of the gate's rounded corners in pixels at 1920 px width (filed aperture plates have rounded corners).
- **Edge Softness** (`gateSoftness`; 0 … 20; default 2; keyframable) — Blur of the gate edge in pixels at 1920 px width: the aperture is slightly out of focus on the film.
- **Gate Hair** (`gateHair`; default off) — A hair caught in the gate: a thin curved fibre reaching in from the gate edge, steady from frame to frame. Camera gate: prints black; printer gate: prints clear.
- **Hair Seed** (`gateHairSeed`; 1 … 9999; default 1; keyframable) — Picks the shape and position of the gate hair.
- **Show Gate Mask (diagnostic)** (`gateShow`; default off) — DIAGNOSTIC VIEW - switch off before rendering. Shows the gate's transmission: white = picture, black = gate (and hair).

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

### Film Breath

*Small changes from frame to frame in exposure, development and printing, as on real film.*

- **Enable Film Breath** (`breathEnable`; default off) — Real film never repeats exactly: the camera shutter, the lab's development and the printer lights vary a little over time. Film Breath adds these slow, smooth changes in brightness, contrast and colour balance. The same Seed always gives the same result. Off (the default) leaves every frame the same.
- **Breath Profile** (`breathProfile`; default: Lab) — Character of the changes. Lab: slow, gentle drift between processing batches. Projector: faster lamp and shutter flicker. Old Camera: a worn camera motor - uneven exposure with some frame-to-frame flicker. Hand-cranked: strong, uneven flicker from frame to frame.
  - Options: Lab · Projector · Old Camera · Hand-cranked
- **Exposure Variation** (`breathExposure`; 0 … 0.5; default 0.05; keyframable) — Size of the brightness changes in stops (typical size; the profile can make it larger). 0 = none.
- **Development Variation** (`breathDevelopment`; 0 … 0.3; default 0.03; keyframable) — Size of the contrast changes from the developer (share of the film contrast). 0 = none.
- **Printer Light Variation** (`breathPrinter`; 0 … 3; default 0.5; keyframable) — Size of the colour balance changes in printer points (needs the Print stage). 0 = none.
- **Breath Period** (`breathPeriod`; 4 … 240; default 24) — Typical length of one slow change in frames. Short = restless, long = slow drift.
- **Breath Seed** (`breathSeed`; 0 … 9999; default 0; keyframable) — Changes the random sequence. Give different shots different seeds.

### Output

*Display encoding of the result.*

- **Output** (`outputSpace`; default: Rec.709 (Gamma 2.4)) — The display the result is encoded for. Rec.709 Gamma 2.4 for broadcast/HD grading monitors, sRGB for web/computer displays, P3 for cinema (DCI) or Apple/P3 displays, PQ or HLG for HDR deliverables. Your timeline/output colour space should match. In Texture Only mode this is the encoding of the incoming (already graded) image.
  - Options: Rec.709 (Gamma 2.4) · Rec.709 (sRGB) · P3-D65 (Gamma 2.6) · DCI-P3 (Gamma 2.6, D65 sim) · Rec.2100 PQ (HDR) · Rec.2100 HLG (HDR) · Rec.709 (Gamma 2.2) · P3-D65 (Gamma 2.2) · Rec.709 (Gamma 2.4, D60 sim) · P3-D65 (Gamma 2.6, D60 sim)
- **HDR Peak (nits)** (`hdrPeakNits`; 300 … 4000; default 1000; keyframable) — PQ output only: peak luminance of the HDR master. The film shoulder is unrolled smoothly so the brightest highlights reach this level.
- **Enable Highlight Unroll** (`hdrUnrollEnable`; default on) — PQ/HLG only: switches the HDR Highlight Unroll on or off without losing its value. Off = the SDR print look placed in the HDR container unchanged (as Unroll 0).
- **HDR Highlight Unroll** (`hdrUnroll`; 0 … 1; default 1; keyframable) — PQ/HLG only: 0 places the SDR print look in HDR unchanged (soft film highlights), 1 expands the print shoulder to use the full HDR range while keeping mid-tones and colour consistent with SDR.
- **Scene Encoding** (`sceneEncoding`; default: ARRI LogC4 / ARRI Wide Gamut 4) — Pipeline Stage = Negative -> Scene / Negative Only (clean) only: the scene-referred encoding of the result. Set the next node (or Resolve's colour management) to read this encoding, then apply your display transform. ARRI LogC4 / AWG4 is the default; ACEScct for ACES pipelines, DaVinci Intermediate for DaVinci Wide Gamut timelines, Linear Rec.2020 for compositing.
  - Options: ARRI LogC4 / ARRI Wide Gamut 4 · ARRI LogC3 (EI 800) / ARRI Wide Gamut 3 · ACEScct / AP1 · DaVinci Intermediate / DaVinci Wide Gamut · Linear Rec.2020
- **Enable False Colour (diagnostic)** (`falseColourEnable`; default off) — DIAGNOSTIC VIEW - switch off before rendering. Replaces the image with exposure zones of the scene light that reaches the film, in stops relative to 18 % grey: purple below -5, blue -5 to -3, teal -3 to -1.5, grey -1.5 to -0.5, green +/-0.5 (grey card), pink +0.5 to +1.5 (skin), grey +1.5 to +3, yellow +3 to +4.5, orange +4.5 to +6, red above +6. Never saved in user presets and never written into exported LUTs.
- **Show Texture Difference (diagnostic)** (`textureDiffShow`; default off) — DIAGNOSTIC VIEW - switch off before rendering. Shows the final picture minus the picture with every texture module off, 4x stronger, around middle grey: everything halation, bloom, grain, sharpness, weave and damage change.
- **Before / After Wipe (diagnostic)** (`diagWipe`; default off) — DIAGNOSTIC VIEW - switch off before rendering. Splits the viewer: the untouched input on the left, the result on the right, with a white line at Wipe Position. While a Show view is on, that view fills the whole viewer instead.
- **Wipe Position** (`diagWipePosition`; 0 … 1; default 0.5; keyframable) — DIAGNOSTIC VIEW - where the before / after line sits, as a fraction of the image width (0 = all after, 1 = all before).

### LUT Export

*Export the colour pipeline as a 3D LUT (.cube).*

- **LUT Size** (`lutSize`; default: 33) — Lattice size of the exported 3D LUT. 33 is the usual choice; 65 is the most accurate (larger file); 17 for devices with small LUT memory.
  - Options: 17 · 33 · 65
- **Folder** (`lutPath`; folder; default empty) — Folder the LUT is written to (created if needed). Leave empty for the default folder `<Documents>/Reafilm` LUTs of the user who exports. File name: `Reafilm_<input>_<negative>_<print>_<output>.cube`. Add this folder to Resolve's LUT list (Project Settings > Color Management > Open LUT Folder) to use the LUTs elsewhere.
- **Export LUT** (`lutExport`; button) — Writes the current colour settings (Color Only: input space, exposure, stocks, development, print, output; no grain or other texture) as a .cube 3D LUT for on-set monitoring, cameras or other applications. The LUT expects the selected camera log as input (Linear Rec.2020: the file carries a log2 shaper).

### About

*Version information.*

- **Version** (`aboutVersion`; read-only) — Plugin version, build variant and build.
- **Manual** (`aboutManual`; read-only; “See the User Manual (MANUAL.md in the plugin bundle's Resources folder).”) — The user manual explains every control, recommended Resolve setups (YRGB, RCM, ACES) and workflow recipes. It is installed inside the plugin bundle: ReafilmPro.ofx.bundle/Contents/Resources.

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
  - Reversal stocks (Ektachrome, Kodachrome): Process, Print Stock, printer lights, Print Density and Print
    Bleach Bypass — a slide is a direct positive; time it with Exposure and balance before the node.
  - Black & white stocks: Color Density, Neutralize and the three printer colour lights (Print
    Density still works).
  - HDR Peak needs Output = PQ; HDR Highlight Unroll needs a PQ or HLG Output.
- **No License group.** This is the personal build (section 10): there is no license key, no
  watermark, and LUT export always works. **About › Version** reads "… - Personal build".

---

## 6. Film stocks and look presets

### Negative and reversal stocks

Film Stock and Print Stock list real film only, each modelled from the manufacturer's data sheet
(Stock Data says how). Projects or user presets saved with one of the older designed stocks (CN, C41,
RV, BW, P1-P6) open on the nearest real stock.

| Stock | Balance | Character |
|---|---|---|
| Kodak Vision3 50D 5203 | daylight | the finest grain, clean and saturated, long highlight latitude |
| **Kodak Vision3 250D 5207** (default) | daylight | natural colour, soft open highlights |
| Kodak Vision3 200T 5213 | tungsten | fine grain, rich mid-tones, clean shadows |
| Kodak Vision3 500T 5219 | tungsten | the high-speed workhorse: low shadow grain, big latitude |
| Kodak Vision2 500T 5218 | tungsten | 2000s high speed: a little more grain, warm punchy mids |
| Kodak Vision2 Expression 500T 5229 | tungsten | low contrast, flat scan, lifted shadows |
| Kodak Portra 400 | daylight | portrait negative: warm low-chroma skin, very wide latitude |
| Kodak Ektachrome 100D 5294 | daylight | motion-picture reversal: clean, saturated, contrasty (no print stage) |
| Kodak Ektachrome E100 | daylight | reversal: neutral greys, crisp saturated colour, short latitude (no print stage) |
| Kodachrome 64 Style | daylight | style approximation (no public data): dense, warm reds, deep blue skies |
| Kodak Double-X 5222 | — | classic black & white negative: open grain, gentle toe |
| Fujifilm Eterna Vivid 160 8543 | tungsten | more saturation and contrast than Eterna |
| Fujifilm Eterna 250D 8563 | daylight | soft contrast, restrained calm colour |
| Fujifilm Eterna 500 8573 | tungsten | high speed with calm, low-saturation colour |
| Fujifilm Reala 500D 8592 | daylight | high-speed daylight negative |

"Tungsten" and "daylight" describe the light each stock is balanced for. With digital footage you
balance in the camera or before the node anyway; the balance shows mainly as each stock's colour
character. Printer lights correct any remaining tint.

### Print stocks

| Print | Character |
|---|---|
| **Kodak Vision 2383** (default) | the standard release print: deep blacks, rich colour, warm highlights |
| Kodak Vision Premier 2393 | higher D-max: denser blacks, more saturated colour than 2383 |
| Fujifilm Eterna-CP 3521XD | high D-max: deeper blacks and richer contrast than 3513DI |
| Fujifilm Eterna-CP 3513DI | made for digital intermediates: neutral, gentle highlights, clean colour |

A faded or silver-retention print is the real print with **Print Black / Print White**, **Shadow
Toning**, **Dye Saturation** or **Print Bleach Bypass** (the Archive 1978 and Found Footage 16 presets
show a faded print).

Instead of a print, **Process = Lab Scan** scans and inverts the negative: a flatter, more neutral
rendering that keeps the negative's character without print contrast.

### Look presets

The **Look Preset** menu starts with **Signature 250D / 2383** (the owner's own setup: 250D on a
2383 release print, fine 65 mm grain, tight halation with Aura, Glimmerglass bloom, digital sharpening
removed). Every other preset now shares its film edge (Integrate 0, Digital Sharpening Removal 1.5,
Acutance Radius 0.5, Aura 50 / 0.15) unless it sets its own. Then come Studio Classic, Tungsten Night Drama, Soft
Daylight Pastel, Archive 1978, Silver Retention Thriller, Neon Nocturne, Golden Hour Portrait, Crisp
Commercial, Summer Snapshot, Slide Show Brilliance, Monochrome Noir, Home Movie Super 8, the two
film-damage looks Worn Release Print and Found Footage 16, Studio Modern, Soft Cinema, Warm Print and
Sunlit 35mm (soft scan) (each is described in the controls reference above).

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
3. Halation: the default physical halation (Threshold 0) gives every lit window and neon tube a thin
   warm ring in proportion to its brightness; if it is too much, lower **Halation Amount**. Only if you
   want practicals to glow and nothing else, set **Halation Threshold** to 3.5–4.5 stops (the v1.1
   gate). **Halation Hue** −0.2 gives a deeper red ring.
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
5. Halation: the physical default leaves flat bright areas clean; if a bright window behind the
   subject still rims the hair line too strongly, lower **Halation Amount** or set **Halation
   Threshold** to about 3.5 stops.

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
2. **Gauge** 16 mm or Super 8 (Look group), **Grain Softness** 0.7–0.8, **Acutance** 0–0.1 (old lenses
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

Use **Color Only** on the grade node of each clip and a second Reafilm Pro instance in **Texture
Only** mode on a timeline node or at the end of the node tree (same Output setting). Grain and
halation are then added once to the finished image, after any trims, and the colour nodes render
faster.

### Texture on an existing grade

Texture Only adds halation, bloom, grain, acutance, gate weave and vignette to an image that is
already display-referred in the selected **Output** space (for example a Rec.709 grade or another
film look). Colour is left untouched.

### Matching two cameras

Give each camera group its own Reafilm Pro node with its own **Camera Log**, and the same stocks
and printer lights. Both cameras are then rendered through the same negative and print. Match
exposure and white balance before the node, in each camera's log.

### Building the chain from stages (Pipeline Stage)

**Pipeline Stage** (Look group) lets one node do one part of the film chain, the way a lab splits
the work between negative development, scanning, printing and projection. **Full Chain** (the
default) is everything in one node, exactly as before.

A three-node chain in Resolve (each node its own Reafilm Pro instance, same Print Stock):

1. **Negative -> Log Scan**: your camera log in (Camera Log as usual), the developed negative out
   as a Cineon-style printing-density scan (clear film at code 95, 0.002 density per 10-bit code).
   Grain, halation and negative dust/scratches are rendered here, in the negative. Grade *the scan*
   after this node if you like: offset = printer lights, like a timer would.
2. **Print Only**: the scan in, printed on the Print Stock and rendered to your Output. Printer
   lights, print density, print bleach and print damage live here. Leave the texture modules off
   (they already ran in node 1), or add print-side damage only.
3. **Texture Only** (optional): grain or weave on the finished display image, for example after
   titles or a final trim.

Nodes 1 + 2 with the same settings look like one Full Chain node (within two 10-bit code values;
extremely saturated or out-of-gamut colours can differ a little more, because the print node only
sees the scan).

**Your own display transform.** Set Pipeline Stage to **Negative -> Scene**: the node gives you the
developed negative turned back into scene light - film colour, curve shape, halation and grain kept,
print and display left out - in **Scene Encoding** (Output group): ARRI LogC4 / AWG4 (default), ARRI
LogC3 / AWG3, ACEScct / AP1, DaVinci Intermediate / DaVinci Wide Gamut or Linear Rec.2020. Follow it
with your DRT (OpenDRT, 2499 DRT, ARRI REVEAL, an ACES output transform) set to read that encoding.
Grey stays at 18 % grey, so exposure carries over.

**Negative Only (clean)** is the same without any texture (no halation, bloom, grain, acutance,
weave, vignette or damage): the node that only swaps the camera for the negative stock. Put the
texture in a later Texture Only node or keep it out entirely.

Notes: reversal (slide) stocks have no negative stage, so every stage renders the full chain with
them. In the negative stages Process, the print timing controls, Output and Film Blur are greyed:
they belong to the print. Print Only ignores Camera Log and Camera Emulation (the input is a scan).

### The print lab: projection, process aim, release prints (v1.3)

The lab block in the **Print** group sets how the print is made and shown. It works on every print
stock and makes most difference on the data-sheet prints (Kodak Vision 2383, Vision Premier 2393 and
Fujifilm Eterna-CP 3513DI). With **Enable Print Lab** off, or with every setting at its default, the
print looks exactly as it did in v1.2.

- **Projection White**:
  - **D65 (Adapted)**: the classic film-out preview on a video display.
  - **Theatre Xenon (Unadapted)**: what a light meter reads off a xenon-lit cinema screen, with a
    warmer, slightly yellow white. Use it to judge the print as an audience sees it.
  - **D60 Sim**: the ACES D60 white on a D65 display.
- **Process Aim**: the lab's development within its tolerance band.
  - **Rich**: more gamma, a deeper D-max and denser blacks.
  - **Soft**: less of each, with open blacks and gentler colour.

  **Print Contrast** fine-tunes the aim in the same units. The lab re-times the print, so middle grey
  stays put and only the contrast around it changes.
- **Rendering**:
  - **Contact Print**: printed straight from the camera negative.
  - **Release Print (IP / IN)**: duplicated twice on Kodak 2242 intermediate film first, as a
    theatrical release print is. The toe and shoulder get a little softer and the colour shifts
    slightly.
  - **Telecine / Scan**: the print transferred on a telecine, with neutral blacks and highlights and
    the black set just above the print's D-max.

**Stock Data** (Negative group) and **Print Stock Data** (Print group) are read-only lines that say
where the selected stock's numbers come from:

- a manufacturer data sheet, fitted rather than measured from film
- a partial data sheet, with the rest borrowed from a named proxy stock
- a Reafilm design
- a style approximation

The full list is in [STOCKS.md](STOCKS.md), section 5c.

### Bright highlights: zones, contrast, roll-off and dye saturation (Tone group)

**Why Exposure alone does not tame a bright sky.** Exposure moves the whole scene along the film's
curve, exactly like opening or closing the lens. Near the top the print curve is a long, flat shoulder:
the last five stops above a bright sky all land between L* 95 and 99, so one or two stops less exposure
mostly darkens the faces and the mid-tones, while the sky stays almost white. That is how film behaves;
to change *only* the highlights, use the Tone group.

**HDR-wheels-style recipe: pull the highlights down, keep the mids.**

1. Leave Exposure where the faces look right.
2. **Highlights** -1.5 with **Highlights Range** +2.5 (stops above grey where the zone begins). The
   sky and bright walls come down into the print's straight part, where they keep their colour and
   detail; grey and skin move by about half an L*.
3. **Specular** -1 (Range +5) for sun glints, chrome and practicals that are still too hot.
4. Optional: **Shadows** +0.3 (Range -2) to open the shadows a little, **Zone Falloff** larger for a
   softer blend between the zones, smaller for a sharper split.
5. If the top end now looks too dense, give the print a longer shoulder: **Roll-off Softness** +0.5 at
   **Roll-off Start** +3.

On test footage this recipe takes the brightest tenth of a sunny exterior from L* 89.5 to 75 and of
a bright sky from 92.5 to 80, with the mid-tones changing by about 0.5 L*. Exposure -1 on the same shot
darkens the mid-tones by 15 L* and still leaves that sky at 87.

**What each control does.**

- **Zones** (Shadows, Lights, Highlights, Specular): exposure in stops for one part of the tonal
  range, on the scene light before the negative, so the film still renders the result (halation and
  grain follow it, like Exposure). Each Range is in stops relative to 18 % grey. The zones blend
  smoothly and never reverse tones: a strong pull compresses, it does not turn bright into dark.
  Colours keep their hue and saturation.
- **Filmic Contrast** with **Contrast Pivot**: contrast in log exposure about the pivot (default 18 %
  grey), before the negative, like a harder or softer development of the scene. Saturation follows the
  contrast half-way, the way film behaves: it neither drains (as a pure luminance curve would) nor turns
  neon (as a per-channel curve would). Greys stay grey.
- **Film Roll-off** (Start, Softness; Print process): reshapes the print's highlight shoulder - the
  way the print approaches its clear base (white). + = a longer, softer shoulder with more detail in
  the brightest parts and white reached later; - = a harder shoulder that reaches white sooner. Below
  the start the stock is unchanged; printer points still slide along the new curve.
- **Dye Saturation** (Saturation, Richness, Saturation Balance): saturation the way dye film makes it.
  More saturation means more dye in the layers that carry the colour, so strong colours get deeper and
  slightly darker instead of brighter and neon; greys, white and black do not move. **Richness** sets
  how much darker (0 = the colour keeps its brightness, 0.25 default, 1 = very dense). Compared with
  RGB saturation of the same strength on test footage, Dye Saturation 1.5 lowers saturated colours by
  2 to 3 L* (RGB: no change) and skin by under 2 L*. **Saturation Balance** moves the
  change towards the highlights (+) or the shadows (-).

Every part has its own switch (Enable Zones & Contrast, Enable Film Roll-off, Enable Dye Saturation),
and at their defaults the image is exactly the same as without them. Zones and Filmic Contrast do
nothing in Print Only (the input there is a scan, not scene light); Film Roll-off needs the print
(Process = Print); Dye Saturation does not apply to black & white stocks.

### Highlight headroom: Highlight Latitude (v1.3)

The X-H2S clips about 6.3 stops above grey in F-Log2 C (code 0.854). Above that the channels stop one
after the other, so a bright sky turns flat cyan-grey, a lit wall or a practical goes yellow or
magenta, and the brightest stops hit the film's shoulder as a wall instead of rolling into it. An
ALEXA keeps going for about another stop and a half. **Highlight Latitude** (its own group, off by
default, F-Log2 C / F-Log2 only) closes that gap in scene light, right after the decode and before
Sensor Match, the negative and the ALEXA rendering:

- **Clip reconstruction**: near-white highlights where one or two channels have clipped go smoothly to
  white. Strongly coloured lights, where only their own channel clips, keep their hue.
- **Extension**: the last stops below the clip (Shoulder Softness: half a stop to three stops) are
  stretched smoothly and monotonically up to **Latitude** stops above it, so they reach the film's or
  the ALEXA's shoulder the way a camera with more headroom would. Everything below that is untouched,
  so exposure, skin and mid-tones do not change.
- **Highlight Desaturation** thins the colour of the stretched stops toward white, as a negative does
  near its shoulder.

On the owner footage the defaults take a clipped sky from a flat grey-green wall to a soft white, with
the residual colour in clipped areas (mean C* 1.6 -> 0.7, ARRI 0.7) and the stops above grey it takes
to reach 98 % of the output (7.75 -> 6 through the default film, ARRI 5.75) close to ARRI's own
rendering of the same frames. Sensor
Clip only needs changing for footage that clips elsewhere (lower it if a clip still shows a cast).

### Looking like an ALEXA (Sensor Match and ALEXA Rendering, v1.3)

A camera's "look" has two parts. Reafilm Pro models each one separately:

1. **The sensor** (Sensor Match group): the colours the ALEXA's sensor would have recorded of the
   same scene. This change is subtle and scene-referred. It has no tone curve and greys stay put. It
   is on by default for F-Log2 C / F-Log2 / F-Log footage.
2. **The rendering** (ALEXA Rendering group): how the ALEXA's classic LogC3 -> Rec.709 look turns that
   scene into a picture, with its tone curve, its saturation and hue per hue and brightness, and its
   skin. Reafilm Pro uses its own parametric model, fitted to ARRI's published look. No ARRI LUT is
   included or needed. On the owner footage the model is within a mean of 0.6 dE2000 of ARRI's own
   rendering.

Two recipes:

- **The ALEXA look without film**: set **Pipeline Stage = ALEXA Rec.709**. Grain, halation, bloom and
  damage still apply if you want them, on the display picture. Rec.709 Gamma 2.4 is the reference
  output. P3, PQ and HLG get the same rendering re-encoded, and HDR places grey at 15 nits. Switch
  **Enable ALEXA Rec.709** off to compare with the film chain on the same node.
- **ALEXA colour, film tone**: keep the Full Chain and turn on **Enable ALEXA Colour**. The scene gets
  the ALEXA rendering's colour before the negative, so exposure, mid grey and the film's curve and
  print stay as they are. Since 1.3 ALEXA Colour is matched *through* the film: with the default
  stocks it gives cherry reds (a hint of blue rather than orange), greens without a blue lean,
  skin that holds together across skin tones, whites that stay neutral where the film would drift
  magenta, and shadows a touch cooler (restrained: a b* of about -0.8 at three stops under). **ALEXA
  Colour** at 0.3-0.6 is a gentle nudge. Very saturated light outside Rec.709, and super-whites
  above the camera's range, pass unchanged.
- **The ARRI look on film** (all three together, F-Log2 C footage): **Enable Highlight Latitude** at
  its defaults, **Enable ALEXA Colour** at 1, and a subtle **Bloom** (Bloom Amount 0.1-0.2, Bloom
  Radius 30-60). ARRI highlights are compression plus a faint glow, not a hard roll-off: Highlight
  Latitude gives the headroom and the clean white clip, Bloom the glow. Keep Halation for film
  character; the ALEXA itself has none. For the ALEXA look without film, use Highlight Latitude
  with Pipeline Stage = ALEXA Rec.709 instead.

Through the default film, ALEXA Colour brings the measured hue of a saturated red from 41 to 31
degrees (ARRI 30), a mid green from 156 to 136 (ARRI 140), the spread of the six skin tones from
0.46 to 0.40 (ARRI 0.42), and the tint of whites from up to 2.5 a* to 0.6. Since the saturated-colour
review it also corrects hue and saturation per exposure stop: a bright red (+1.5 stops) sits at 24 degrees
instead of a crimson 20 (ARRI 26), a mid green reaches C* 26 (was 21, ARRI 29), skin and orange separate
more (0.83 of their scene difference, was 0.78, ARRI 0.97) and skin drifts 11 degrees in hue from -2 to
+2 stops (was 15, ARRI 7). What the film keeps as its own: greens and cyans lose saturation in the
shadows, and the strongest greens stay below the ALEXA because the print cannot hold more.

### Grain, halation colour and Film Breath (Texture II, v1.3)

**Grain since 1.3 looks like scanned film, not a noise overlay.** Each colour layer has its own
dye clouds, partly shared with the other layers, so the grain carries a little colour and takes on
the colours of the picture. The clouds clump into irregular groups, and the grain is coarser in the
shadows than in the highlights. It is generated at the full output resolution: on a 6K timeline
(for example Fujifilm X-H2S 6.2K open gate) the finest structure is one to two pixels across, and
at HD or in a proxy render it looks the same relative to the frame. **Existing projects show
different grain after the update**: finer and more detailed at 4K / 6K, clumpier, with subtle
colour, and about 15 % less strong at HD. Raise Grain Amount slightly if you want the old strength.

- **Grain Colour**: 0 for neutral, almost black-and-white grain on a colour stock; 1.5 for more
  colour, as on a pushed or under-exposed negative.
- **Grain Shadows / Mids / Highlights**: grain strength per tonal range. For a clean face with
  gritty shadows, try Shadows 1.4, Mids 0.8, Highlights 0.6.
- **Halation Saturation** and **Halation Background**: 0 saturation gives a neutral glow, 2 a deep
  red one. A positive Background makes the glow more colourful over dark areas and paler over bright
  ones.
- **Enable Aura**: a second, very wide and faint warm haze around strong lights (Halation must be
  on). Aura Radius 150-250 with Strength 0.3-0.6 is subtle; no-backing stocks suit more.
- **Film Breath** (off by default): slow, smooth changes in exposure, contrast and printer lights
  from frame to frame. *Lab* is barely visible drift, *Projector* a faster flicker, *Old Camera*
  and *Hand-cranked* are stronger and more uneven. The changes repeat exactly for the same Breath
  Seed and frame, so renders are deterministic. Give each shot its own seed.

### User presets, scene white balance, false colour and more outputs (v1.3)

**User presets.** Type a name into **User Preset** (Look group) and press **Save User Preset**.
Every setting of the node is written to `<name>.json`: the look, input, output and every module
switch. The License, the LUT folder and False Colour are not saved. To use it in another node or
project, type the same name and press **Load User Preset**. If you press Load with an unknown name,
the message lists the presets you have saved. Presets are plain text files in your user folder:

- macOS: `~/Library/Application Support/Reafilm/Presets`
- Windows: `%APPDATA%\Reafilm\Presets`
- Linux: `~/.config/reafilm/presets`

If a preset is not found there, the plug-in also looks in the folder of the same name from before
the rename (`Celluloid` / `celluloid`). You can copy preset files between computers. A preset from
an older version loads fine: settings it does not contain return to their defaults. Entries this
version does not know are ignored, and the message lists them.

**Scene White Balance** (Input group, off by default). This corrects the colour of the light before
the film sees it, as if the camera had been set differently. Set **Scene Temperature** to the colour
temperature the footage looks balanced for. For example, use 3200 K for daylight-balanced footage
shot under tungsten light; it removes the orange cast. Higher values warm the picture. **Scene Tint**
adds magenta (+) or green (-). Because the correction acts in scene light, the negative and the print
react to it like a real camera filter; it is not a grade on top. It is not available in Print Only,
because that stage's input is already a film scan.

**False Colour** (Output group, off by default) is a diagnostic view for exposure. Each zone is
measured in stops of scene brightness relative to 18 % grey, before the film:

| Colour | Stops from 18 % grey |
|---|---|
| purple | below -5 (crushed) |
| blue | -5 to -3 |
| teal | -3 to -1.5 |
| grey ramp | -1.5 to -0.5 |
| **green** | -0.5 to +0.5 (mid grey) |
| **pink** | +0.5 to +1.5 (typical skin) |
| grey ramp | +1.5 to +3 |
| yellow | +3 to +4.5 |
| orange | +4.5 to +6 |
| red | above +6 (clipping) |

**Switch it off before you render.** A LUT export never contains it, and user presets do not save it.

**Display outputs.** Output Space now also offers four new choices:

- **Rec.709 (Gamma 2.2)** and **P3-D65 (Gamma 2.2)**: for computer monitors and web viewing. The
  light on screen is the same as with the gamma 2.4 / 2.6 output.
- **Rec.709 (Gamma 2.4, D60 sim)** and **P3-D65 (Gamma 2.6, D60 sim)**: the image shows white at
  D60, the slightly warmer white of film projection. The monitor itself stays calibrated to D65.

**Bloom Threshold and Keep Highlights** (Bloom group):

- **Bloom Threshold** (stops above 18 % grey, 0 = off): only parts of the image brighter than the
  threshold bloom. Try 3 to 4 stops to make only practicals and specular highlights glow.
- **Bloom Keep Highlights**: 0 is the normal bloom, which spreads the light of bright areas into
  their surroundings and keeps the total energy. 1 adds the glow on top and keeps the source at full
  brightness, so light sources stay bright and crisp.

### Lens diffusion filters, a softer film edge and the Sunlit 35mm look (v1.3)

**Bloom Filter** (Bloom group, default *Classic* = the bloom as before). The other choices emulate
a diffusion filter in front of the lens: *Black Pro-Mist*, *Pro-Mist (white)*, *Glimmerglass*,
*Hollywood Black Magic* and *CineBloom*. These are emulations modelled from how such filters work and
from public descriptions; no filter data was measured or copied. A diffusion filter scatters a fixed
fraction of **all** the light. There is no threshold, so keep Bloom Threshold at 0. The scattered
light forms a sharp core plus a wide halo, and it is added in scene light before the negative. The
total light stays the same. **Filter Grade** works like the filter strengths printed on the ring:
1/8 = 0.125, 1/4 = 0.25, 1/2, 1 and 2. With a filter selected, Bloom Amount is not used.

- *Black Pro-Mist*: a wide, faint veil that opens up the shadows a little (veiling glare) and
  softens contrast without making the image milky.
- *Pro-Mist (white)*: the widest and strongest veil, milky and pastel.
- *Glimmerglass* and *Hollywood Black Magic*: mostly a tight glow around highlights and fine
  detail, with very little shadow lift.
- *CineBloom*: a softer bloom with less spread.

**Digital Sharpening Removal** (Film Resolution group, default 0). Undoes some of the edge sharpening
built into cameras before the film resolution is applied, so very crisp footage (for example from
6K mirrorless cameras) gets a softer edge. It scales with the frame size. You mainly need it when
Integrate is low; at the default Integrate, Film Resolution already removes most of the digital crispness.

**Shadow acutance.** Acutance is the edge contrast that film development produces. It now fades out
in the deep shadows, as on real film, where the density curve flattens in the toe. Dark and
underexposed shots no longer get extra edge contrast.

**Sunlit 35mm (soft scan)** (Look Preset). A sunlit 35 mm look on a soft, low-contrast scan. It has
lifted blacks, yellow-green mid-tones, warm-leaning highlights and a gentle edge. The recipe, if you
want to build it yourself or start from it:

| Control | Value |
|---|---|
| Process Aim | Soft |
| Print Crossover | 1.00 |
| Printer Red / Printer Green / Printer Blue | +0.5 / -1.0 / +2.5 |
| Filmic Contrast | 0.85 |
| Roll-off Softness | 0.40 |
| Dye Saturation | 0.90 |
| Bloom Filter / Filter Grade | Black Pro-Mist / 0.125 (1/8) |
| Halation Amount | 0.70 |
| Integrate | 0.80 |
| Digital Sharpening Removal | 0.75 |
| Acutance | 0.15 |
| Grain Amount | 0.70 |

Everything else stays at its default (Vision3 250D negative, P6 Modern Print). On dark scenes, lower the
Filter Grade or Grain Amount if you want even less texture.

### Checking where effects apply (diagnostic views, v1.3)

Halation, a mist filter or the softer film edge can be subtle on a small viewer. Each texture module
therefore has its own **Show** control, at the end of the module's group, that replaces the picture
with that module's contribution alone. The views use the real render path (the same passes as the
final image, on CPU and GPU) and only exist while one is on; with every view off the render is the
normal one and costs nothing extra.

| Control (group) | What you see |
|---|---|
| Show Halation: Effect Only (Halation) | only the light halation and Aura add, 8x brighter, on black |
| Show Halation: Mask (Halation) | which parts feed halation: red = their full share, over a dim grey picture |
| Show Bloom (Bloom) | where the bloom or diffusion filter brightens (lighter than grey - the shadow lift of a mist filter) or takes light from highlights (darker); white = +1 stop |
| Show Grain (Grain) | only the grain, 4x stronger, around middle grey |
| Show Acutance (Lens & Gate) | edge contrast added (lighter than grey) or taken away (darker), 4x |
| Show Removed Detail (Film Resolution) | the fine detail Film Resolution and Digital Sharpening Removal take away, 4x |
| Show Texture Difference (Output) | the final picture minus the picture with every texture module off, 4x, around middle grey |
| Before / After Wipe + Wipe Position (Output) | the untouched input left of a white line, the result right of it |

- A Show control is greyed while its module is off, and in Color Only mode.
- If several are on, the one furthest down the pipeline wins: Film Resolution, then Acutance, Bloom,
  Halation, Grain and finally Texture Difference. While a Show view is on it fills the viewer, so the
  wipe waits until it is off.
- They are diagnostics: switch them off before rendering. They are not stored in user presets, look
  presets leave them alone, and an exported LUT never contains them (the export message says so when one
  is on).
- In the licensed build without a license the watermark is drawn over the views too.

Highlight Latitude and Film Damage have no view of their own yet; Show Texture Difference includes the
damage.

### Colour timing, the film gate and seven new looks (Beyond Filmbox I)

**Colour timer** (Print group; Print process only, greyed for Lab Scan, reversal and the negative stages):

- **Colour Head**: Cyan / Magenta / Yellow filters in the printer, in CC units, like a lab's dichroic
  head. Adding a filter takes away that light, so the print loses the matching dye:
  - +Yellow gives a bluer print.
  - +Magenta gives a greener print.
  - +Cyan gives a redder print.

  The effect is computed through the printer lamp, the print stock's sensitivity and the negative's dyes.
  **Preserve Exposure** (1 = full) keeps middle grey at the same brightness. On a black & white stock only
  Preserve applies.
- **Printer Lights** (read-only): the timing in the lab convention. Nominal is 25-25-25, and 1 point =
  0.025 log E, which is the Printer R/G/B points. When the head is on, its equivalent lights are shown too.
- **Print Black / Print White / De-Fog**: density at the print's two ends, not video lift / gain.
  - Print Black below 0 lifts the maximum black density, and above 0 deepens it.
  - Print White above 0 holds the whites back with density, and below 0 cleans the print base.
  - De-Fog removes the print stock's own fog. The negative's fog is already cancelled by the grey balance.
- **Print Toning**: a dye tone in the shadows (dense areas) and in the highlights (thin areas). Each has
  a Hue (degrees) and an Amount.

**Film Gate** group (off by default):

- **Gate**:
  - The **Camera Gate** masks the scene light before the negative. The unexposed rebate prints black,
    and the frame line weaves, grains and halates with the picture.
  - The **Printer Gate** is a clear, unexposed border on the print.
- **Gate Aspect**: Full Frame, 1.33, 1.66, 1.85, 2.39, 1.43 IMAX, Super 16 (1.66) or 1.78.
- **Overscan**: how much rebate shows.
- **Corner Radius** and **Edge Softness**: the gate's shape and edge.
- **Gate Hair** (with a Seed): a hair caught at the aperture edge.
- **Show Gate Mask**: shows where the gate passes light (white = picture).

The gate is a texture, so it never goes into an exported LUT.

**Lens & Gate** group:

- **Weave Style**:
  - Projector: the existing weave.
  - Camera (Pin-Registered): a small frame-to-frame jitter.
  - Telecine (Float): a slow, smooth float with more sideways and rotational movement.
- **Mechanical Vignette** (Amount, Size, Softness): the dark edge where the lens hood or matte box cuts
  the image circle. It is on top of the Vignette's cos^4 fall-off and follows the Vignette switch.

**New looks** (Look Preset, appended):

| Look | What it gives you |
|---|---|
| Theatrical 2383 Warm | 250D through the release chain onto 2383, head timed warm, golden highlight toning |
| Soft Telecine Scan | telecine render, lifted printed blacks, white Pro-Mist, telecine float |
| 500T Night Exterior | 500T pushed half a stop on 2393, deep blacks, teal shadow toning, strong halation |
| Scope Bleach Bypass | 2.39 camera gate, partial bleach bypass, steely contrast, cool shadows |
| 16mm Documentary | Super 16 gate, Eterna 250D, 16 mm grain, camera weave, mechanical vignette |
| Faded Magenta Print | an aged print: lifted blacks, magenta-red toning, lower saturation |
| Sunlit 35mm Full Gate | the Sunlit 35mm soft scan timed warmer (warm head, lifted blacks), framed full gate |

---

## 8. LUT export

**Export LUT** (LUT Export group; always available in the personal build) writes the **Color Only** pipeline of the
current settings — Camera Log, Exposure, stocks, development, print, printer lights and Output —
as a `.cube` 3D LUT. Grain, halation, bloom and the other texture effects cannot be stored in a LUT
and are left out.

- **Size:** 17³, 33³ (default) or 65³ points. A 33³ LUT matches the plug-in to about 0.03 code values
  (99th percentile); use 65³ for critical work and 17³ only for devices with little LUT memory.
- **File name:** `Reafilm_<input>_<negative>_<print>_<output>.cube`, for example
  `Reafilm_Fujifilm-F-Log2-C_CN-200T-Studio-Tungsten_P1-Standard-Projection_Rec.709-Gamma-2.4.cube`
  (the print part is `Lab-Scan` or `Reversal` where applicable). An existing file of the same name
  is replaced, and the confirmation message says so.
- **Folder:** the **Folder** field, or — when it is empty — `Documents/Reafilm LUTs/` of the user
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

This copy of Reafilm Pro is the **personal build** for non-commercial use, the default
configuration of the source repository:

- **No license check.** The plug-in never looks for a license key, offline or online.
- **No watermark.** Every render is clean, at any resolution and in every mode.
- **LUT export always works** (section 8).
- The **License** group is hidden. Its parameters still exist inside the plug-in, so projects saved
  with any earlier version, licensed or trial, open unchanged.
- **About › Version** reads "Reafilm Pro *version* - Personal build".

Nothing in the image pipeline differs between the builds: a personal-build render is
pixel-identical to a licensed render of the same settings.

### The licensed build (`-DCEL_LICENSING=ON`)

The commercial variant is still in the source and builds with the CMake option
`-DCEL_LICENSING=ON` (README, CMake options). It shows the License group and behaves as follows.

Without a license key the licensed build runs as a **trial**: every feature works except **LUT export**,
and renders carry a small "REAFILM PRO — TRIAL" badge in the bottom-right corner plus a faint
diagonal band. There is no time limit.

**Activate.** Open the **License** group, paste your key into **License Key** (keys start with
`CELL1-`; line breaks and spaces from e-mail wrapping are ignored) and click **Activate**. The key is
checked on your computer, with no internet connection, and stored for your user account; the
watermark disappears with the next render, and License Status shows the licensee and edition.
**Deactivate** removes the key from this computer (for example before moving to a new machine).

Where keys are stored:

| | per user (written by Activate) | machine-wide (studios, installed by an administrator) |
|---|---|---|
| macOS | `~/Library/Application Support/Reafilm/license.key` | `/Library/Application Support/Reafilm/license.key` |
| Windows | `%APPDATA%\Reafilm\license.key` | `%PROGRAMDATA%\Reafilm\license.key` |

A key activated before the rename to Reafilm Pro (in the old `Celluloid` folders) is still read,
so there is no need to activate again.

Render nodes and containers can also provide the key in the environment variable
`REAFILM_LICENSE_KEY` (the old name `CELLULOID_LICENSE_KEY` still works). If several sources hold a key, a valid paid key (Indie, Pro, Studio) wins
over a Trial-edition evaluation key. A key copied into place by hand is picked up within a few
seconds, without restarting Resolve.

Keys are personal: the licensee's name is part of the key and is shown in the License group. The
number of seats is part of the key and of the license agreement; it is not enforced technically.
Keep your purchase e-mail: if you reinstall or change computers, paste the key again.

---

## 11. Performance

- **GPU.** Reafilm Pro renders on the GPU Resolve uses: Metal on macOS, CUDA on NVIDIA and OpenCL
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

- `REAFILM_LOG=1` — diagnostics on the standard error output (GPU self tests, preset checks).
- `REAFILM_DISABLE_GPU=1` — do not offer any GPU backend (CPU only), or a list such as
  `opencl,cuda,metal` to disable only those.

The old names `CELLULOID_LOG` and `CELLULOID_DISABLE_GPU` still work when the new ones are not set.

---

## 12. Troubleshooting

| Symptom | Cause and remedy |
|---|---|
| Reafilm Pro is not in the Effects library | Check that `ReafilmPro.ofx.bundle` is in the OFX plug-in folder (section 1) and restart Resolve. On macOS confirm the installer was allowed to run. Resolve caches plug-ins: if it lists the effect as failed, delete Resolve's OFX plug-in cache or reinstall, then restart. |
| Image is flat and washed out, or harsh and colour-shifted | **Camera Log** does not match the footage, or the clip was converted (LUT, CST, colour management) before the node. Section 3. |
| Image looks right in the node but wrong in the viewer / render | A colour-managed project converts the plug-in's display image again. Add the conversion back to the timeline space after the node, or use YRGB (section 3). |
| Too dark or too bright | **Exposure** for the scene, **Print Density** for the print. Avoid fixing exposure with a gain after the node. |
| Colour cast on every stock | Balance before the node, then fine-tune with printer lights in half points. |
| Bright edges glow red everywhere | Lower **Halation Amount**, or set **Halation Threshold** above 0 (stops over mid grey: only highlights above it halate); CN 800T No-Backing glows by design. |
| Grain looks too strong or too fine in the viewer | Judge grain at 100 % zoom; the viewer scales it. Grain is resolution independent and matches the final render. |
| Grain looks identical on two layered clips | Give each instance a different **Grain Seed**. |
| Grain is frozen | **Animated Grain** is off. |
| Dust and scratches do not change / are identical on two clips | **Animated Damage** is off, or both clips use the same **Damage Seed**. |
| Damage or film blur missing | Film Damage / Film Blur are off by default and never render in Color Only mode. |
| "LUT export is part of the licensed version" | Licensed build only: activate a license key (section 10). The personal build always exports. |
| "This license key could not be activated" | Licensed build only. Paste the complete key including `CELL1-`; the message explains the reason (for example an expired subscription key or a key for another product). |
| Watermark still visible after activation | Licensed build only (the personal build never watermarks). Check License Status. A machine-wide or environment key with an error is reported there. Re-render the frame (Resolve may show a cached frame). |
| Slow playback | Check Resolve's GPU processing mode, lower texture settings or use Color Only plus one Texture Only node (section 11). With `REAFILM_LOG=1` the plug-in logs which GPU backends passed their self test. |
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
