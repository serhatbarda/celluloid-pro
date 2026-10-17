# Changelog

All notable changes to Reafilm Pro (called Celluloid Pro before 1.3.0). Versions follow [Semantic Versioning](https://semver.org);
parameter identifiers and choice orders are permanent, so projects saved with an older version open
unchanged in a newer one.

## [1.6.0] — 2026-10-17

### Added

- **Film Edge** (Film Gate group): draws the film strip around the gate, like a scan of the whole negative -
  bright perforations and the edge print (frame number, half-frame arrow, bar code) in the rebate.
  35 mm Still Negative, 35 mm Motion Picture or 120 Medium Format, sized from the real film; the colours come
  from the same colour pass as the frame, so they follow the look and the output. New Gate Aspects 1.50
  (35 mm Stills), 1.00 (6x6) and 1.25 (6x7); Overscan now goes to 0.25 so the whole edge fits.
- `docs/QUICKSTART.md`: one-page quick start - node placement, input / look / output, monitoring LUTs,
  Blackmagic Pocket 6K BRAW setup and stills (RAW, JPEG). The Camera Log hint names the BRAW settings.

## [1.5.0] — 2026-10-17

### Added

- **Rec.709 video and sRGB inputs** (Camera Log menu, appended) for footage without a log profile -
  camcorders, phones, JPEG stills. The display curve is undone, and the clipped video shoulder above
  60 % linear is expanded back to about +4.5 stops, so bright skies and lamps still roll into the
  film's shoulder instead of hard-clipping. Gamut: Rec.709.
- **Still Photo** switch (Input, off by default) for grading single photographs (RAW debayered by Resolve,
  or JPEG via the sRGB input): the texture is frozen to one frame and gate weave, Film Blur and Film
  Breath are left out.

### Changed

- New plug-in icon: a film frame with a low sun, its red halation and the frame edge drawn in the three dye
  layers of colour film (reads clearly at the small sizes Resolve shows).
- Preset contact sheets (`docs/validation/presets.png`, `contact_presets.png`) show the rebuilt presets;
  web links point to the renamed repository `serhatbarda/reafilm-pro`.

## [1.4.0] — 2026-10-17

### Changed

- **Before / After Wipe**: the "before" side is now a plain Rec.709 view of the camera (the neutral ALEXA
  rendering, in the output space) instead of the flat log input, so the comparison is meaningful. Texture Only
  and Print Only still show the input as it is.
- **Look presets rebuilt: one preset per real camera stock (15)**, each on its own negative + print pairing
  and using the full set of controls (colour head, print ends and toning, film gate, weave styles,
  mechanical vignette, breathing, damage, bloom filters, grain format). Signature 250D / 2383 is unchanged.
  Crisp Commercial and Soft Cinema keep their recipes; Studio Modern (now 200T), Warm Print (now Vision2
  500T), Theatrical 2383 Warm (now Expression 500T) and Sunlit 35mm (soft scan) (now Eterna Vivid 160 on a
  telecine scan, refined) keep their character on a new stock. New: Scope Night 500T / 2393, Portra 400
  Editorial, Super 8 Ektachrome Diary, E100 Slide Projector, Kodachrome Travelogue, Double-X Silver Print,
  Eterna 500 Skip Bleach, Reala Green Day. The other presets were removed. Projects store the preset menu
  index, so an older project may show another preset's name; its saved values and render are unchanged.

## [1.3.0] — 2026-10-14

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
- **ALEXA Rendering group**: our own parametric model of the ALEXA's classic LogC3 -> Rec.709
  rendering (tone curve, saturation and hue per hue and brightness, skin), fitted to ARRI's published
  look. No ARRI LUT is shipped or loaded.
  - **Pipeline Stage = ALEXA Rec.709** (appended): renders the camera that way instead of a film print,
    on every output (SDR, P3, PQ, HLG). Texture is applied display-referred. *Enable ALEXA Rec.709* off
    gives the Full Chain for a quick A/B.
  - **ALEXA Colour** (default off): only the colour of that rendering, applied in scene light before the
    negative, at grey brightness. Greys and exposure are left alone, and the film still makes the tone.
  - Mean dE2000 against ARRI's rendering on the owner footage is 0.57, with a 95th percentile of 1.05.
  - The **Sensor Match** group (previously Camera Emulation) has new labels only; its IDs are unchanged.
  - Off or at the defaults, projects render unchanged. Results match across CPU, OpenCL, CUDA and Metal.
- **Highlight Latitude group** (ARRI look II, default off, F-Log2 C / F-Log2 footage): more highlight
  headroom in scene light, before Sensor Match, the negative and the ALEXA rendering. Clipped channels
  are rebuilt so near-white highlights go white instead of cyan, yellow or magenta (coloured lights keep
  their hue), and the last stops below the X-H2S clip are stretched smoothly and monotonically up to
  *Latitude* extra stops. Controls: Latitude, Shoulder Softness, Clip Reconstruction, Highlight
  Desaturation, Sensor Clip (0.854, measured on X-H2S footage). Below the shoulder nothing changes.
- **ALEXA Colour matched through the film** (ARRI look II): its hue and saturation bands are refitted
  so that the default film with ALEXA Colour matches ARRI's rendering, plus a per-stop term that keeps
  the film's whites neutral and its shadows slightly cool. Through the default film: saturated red hue
  41 -> 31 degrees (ARRI 30), mid green 156 -> 136 (ARRI 140), skin spread 0.46 -> 0.40 (ARRI 0.42),
  white tint up to 2.5 -> 0.6 a*. ALEXA Colour is still off by default; projects that had it on look
  different (closer to the ALEXA). The manual's "Looking like an ALEXA" adds an ARRI-look recipe:
  Highlight Latitude, ALEXA Colour and a subtle Bloom.
- **Texture II**: every part has its own switch or is neutral at its default.
  - **Halation colour**: *Halation Saturation* (0 = neutral glow, 2 = deep colour) and *Halation
    Background* (glow colour reacts to what is behind it). New **Aura** (*Enable Aura*, default off;
    Radius, Strength, Green): a second, much wider and fainter base reflection around strong lights.
  - **Grain colour and tone**: *Grain Colour* (how independent the colour layers' grain is) and
    *Grain Shadows / Mids / Highlights* (grain strength per tonal range), applied in the film's
    density before the print.
  - **Film Breath group** (*Enable Film Breath*, default off): smooth, repeatable frame-to-frame
    changes of exposure, development contrast and printer lights. Profiles Lab, Projector, Old
    Camera and Hand-cranked; Period and Seed.
  - Off or at the defaults these render unchanged, and all match across CPU, OpenCL, CUDA and Metal.
- **P1 extras**:
  - **User presets** (Save/Load User Preset in the Look group): every setting as versioned JSON in
    the Reafilm Presets folder, with a fallback to the old Celluloid folder. Older or newer files load
    tolerantly: missing settings return to their defaults, and unknown entries are reported.
  - **Scene White Balance**: temperature and tint in scene light, before the negative. Off by default.
  - **False Colour**: an exposure diagnostic in stops relative to 18 % grey. Off by default, never in
    LUT exports or presets.
  - **New outputs**: Rec.709 / P3-D65 Gamma 2.2, Rec.709 Gamma 2.4 D60 sim and P3-D65 Gamma 2.6 D60
    sim (appended to Output Space).
  - **Bloom Threshold** and **Bloom Keep Highlights**.
  - Defaults render exactly as before.
- **Lens diffusion filters, a softer film edge and a reference-look preset**:
  - **Bloom Filter** (Bloom group): *Classic* (the bloom as before, default) or emulations of
    *Black Pro-Mist*, *Pro-Mist (white)*, *Glimmerglass*, *Hollywood Black Magic* and *CineBloom*, with
    **Filter Grade** 1/8 .. 2. The filter scatters a fixed share of all light: a sharp core plus a
    wide halo, in scene light before the negative, with no threshold. Total light is kept. Black
    mists lift the shadows slightly with veiling glare; white mist is milkier. These are modelled
    from how the filters work, not from measured filter data.
  - **Digital Sharpening Removal** (Film Resolution group, default 0): undoes in-camera edge
    sharpening before the film resolution is applied, scaled with the frame size.
  - **Sunlit 35mm (soft scan)** look preset (appended): soft-aim print, full crossover, yellow-green
    mids, lifted blacks from a 1/8 Black Pro-Mist, halation, and a gentle edge that stays soft in
    the shadows. The recipe is in the manual.
- **Diagnostic views ("Show" controls)**, each at the end of its own module's group: **Show Halation**
  (Off / Effect Only / Mask), **Show Bloom**, **Show Grain**, **Show Acutance**, **Show Removed Detail**
  (Film Resolution); in Output **Show Texture Difference** and a **Before / After Wipe** with **Wipe
  Position**. They render the module's isolated contribution through the real render path, greyed
  while their module is off, the one furthest down the pipeline wins, never stored in presets nor in
  LUTs, and with every view off the render is unchanged and costs nothing. Manual: "Checking where
  effects apply".
- **Lab & Gate** (each module has its own switch; at the defaults projects render unchanged):
  - **Colour timer** (Print group, Print process only). The **Colour Head** adds Cyan / Magenta / Yellow
    dichroic filtration in CC units. It is computed through the printer lamp, the print stock's
    sensitivities and the negative's dyes, with **Preserve Exposure**. **Print Black / Print White / De-Fog**
    are density changes at the print's two ends. **Print Toning** adds a shadow and a highlight dye tone
    (hue + amount). **Printer Lights** shows the lab printer lights (25-25-25 nominal, 1 point = 0.025 log E).
  - **Film Gate** group: a camera gate (the unexposed rebate prints black, with grain and halation) or a
    printer gate (clear print border). Aspects: Full Frame, 1.33, 1.66, 1.85, 2.39, 1.43 IMAX, Super 16,
    1.78. It has Overscan, rounded Corners, a soft edge, an optional **Gate Hair** with a seed, and
    **Show Gate Mask**.
  - **Weave Style** (Projector / Camera (Pin-Registered) / Telecine (Float)) and a **Mechanical Vignette**
    (Amount, Size, Softness) on top of the lens's cos^4 fall-off (Lens & Gate).
  - Seven look presets appended: Theatrical 2383 Warm, Soft Telecine Scan, 500T Night Exterior, Scope
    Bleach Bypass, 16mm Documentary, Faded Magenta Print, Sunlit 35mm Full Gate. CPU and GPU give the
    same result.

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

- **Real film only** (owner request). Film Stock lists the 15 real negatives and Print Stock the four
  real prints (Kodak 2383, Kodak Premier 2393, Fujifilm Eterna-CP 3521XD and 3513DI). The designed
  stocks (CN, C41, RV, BW, P1-P6) are gone from the menus. A project or user preset that used one
  opens on the nearest real stock, so its look changes. The parameter IDs are unchanged: the saved
  negStock and printStock are now hidden, and the visible lists are views of them. **The default
  print is now Kodak Vision 2383 instead of P6 Modern Print, so the default look changes**: P6 was
  2383's dyes with our own tone curve. The presets now use real stocks; faded and silver-retention
  prints are 2383 with the print controls. Studio Modern now uses Premier 2393 so it stays distinct
  from Warm Print.
- **Signature 250D / 2383 preset**, first in the Look Preset menu: the owner's own setup. Because it
  is inserted first, a project saved earlier shows the next preset's name in the menu; its saved values
  and its render are unchanged. Every other preset now shares its film edge (Integrate 0, Digital
  Sharpening Removal 1.5, Acutance Radius 0.5, a soft Aura) unless it sets its own.
- **Finer grain**: Grain Size now goes down to 0.25 (was 0.5).
- **The film gate border stays clean**: grain, halation, bloom, dust, scratches, weave and projection
  blur now act only on the picture. The frame line holds steady while the picture weaves under it.
- **Grain looks different in existing projects** (owner request: the grain looked cheap, too even,
  colourless and too low-res at 6K). The default grain is now generated at the full output resolution
  and has fine detail at 4K and 6K, the finest structure one to two pixels across at 6K. Its dye
  clouds clump, the colour layers share less of their grain so it shows subtle colour that follows the
  image, and the mid-tones are no longer two-tone. At HD it is about 15 % less strong. A 6K frame
  scaled down to HD matches an HD render, and proxy renders use the render scale. The parameter IDs
  are unchanged, and grain costs the same render time.
- **Renamed to Reafilm Pro.** The plug-in label, the Resolve menu group (*OpenFX › Reafilm*), the
  About and help texts, the bundle (`ReafilmPro.ofx.bundle`, binary `ReafilmPro.ofx`), the
  installers, disk image and zip (`ReafilmPro-<version>-…`), exported LUT names and titles
  (`Reafilm_….cube` in `<Documents>/Reafilm LUTs`, previously `Celluloid LUTs`), the *Reafilm
  original* style labels and the trial watermark (*REAFILM PRO — TRIAL*) use the new name.
  Saved projects keep loading: the OFX plug-in identifier, its version, all parameter identifiers
  and choice orders are unchanged, so the rendered pixels are too. License keys stay valid.
  - **Delete the old `Celluloid.ofx.bundle`** from the OFX plug-ins folder, otherwise Resolve
    shows both. The macOS and Windows installers remove it; after a manual install delete it by hand.
  - Environment variables: `REAFILM_LOG`, `REAFILM_DISABLE_GPU` and `REAFILM_LICENSE_KEY`. The old
    `CELLULOID_*` names still work when the new ones are not set.
  - License files are written to the `Reafilm` folder (`~/.config/reafilm` on Linux,
    `Application Support/Reafilm` on macOS, `%APPDATA%\Reafilm` on Windows, and the machine-wide
    equivalents). A key in the old `Celluloid` / `celluloid` folder is still read, so there is no
    need to activate again.
- **Fujifilm Eterna-CP 3513DI**: the curve used since v1.2 was read from the 3521XD figure of the
  Fujifilm data sheet, with a 10 % short exposure axis. Print 7 keeps that curve, so saved projects
  render exactly as before. It is now called *Fujifilm Eterna-CP 3513DI (v1.2 legacy curve)*. The
  corrected 3513DI is the new print 10 and is close to Kodak 2383, like the real stock. The Soft Cinema
  preset still uses print 7.
- **ALEXA Colour renders saturated colours closer to the ALEXA** (saturated-colour review). Projects with
  ALEXA Colour on look different: bright reds stay red instead of turning crimson, mid greens are more
  saturated, skin and orange separate more, and skin keeps its hue better from shadows to highlights.
  ALEXA Colour now also never pushes a colour past Rec.2020 into the negative. With ALEXA Colour off,
  nothing changes. The film's own cool, green-poor shadows are kept.

- **Halation is visible at the defaults** (owner report "threshold 0 = no halation"). Physical
  halation now gives its full share from about +3.5 stops over grey instead of +6. That level was
  barely reached by a real camera clip, so the default halation was nearly invisible. Display-referred
  textures (Texture Only, ALEXA Rec.709 stage) use a knee at +1.5 stops. The gain per unit of Amount
  is 0.14 instead of 0.10. Projects with Halation Threshold 0 show clearly warmer rings around
  practicals, the sun, speculars and bright sky edges. Skin and mid-tones are almost unaffected. The
  control is now labelled **Halation Threshold (0 = physical)**, and its tool-tip explains that any
  value above 0 switches to the old gate mode. Projects with a threshold above 0 are unchanged.
- **Acutance fades out in the shadows**, as the adjacency effect does in the negative's toe (half
  strength 4 stops under grey). Dark and underexposed shots no longer get extra edge contrast. In
  Full mode, shadow edges are softer: overshoot 13 % -> 6 % on a -6 / -3.5 stop edge. Mid-tone edges
  are about 6 % weaker.
- **Film Resolution at 6K and above with high Integrate** is no longer capped by the 7-tap kernel. It
  is applied as cascaded passes, so the softness is the same fraction of the frame at every
  resolution. Lower resolutions render exactly as before.

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
