// Reafilm Pro - texture parameters (spatial / temporal effects).  FIXED (architect).
// The texture agent implements the passes as portable kernels (src/kernels/TextureKernels.h,
// listed in src/kernels/TextureKernelList.inc), the plan builder (src/engine/Plan.cpp ->
// engine::buildPlan) and the CPU backend (src/engine/CpuBackend.cpp). See SPEC §5.
#pragma once

namespace cel { namespace tex {

enum class GrainFormat : int {   // ORDER IS THE OFX CHOICE-PARAM ORDER
    Super8 = 0,
    Mm16,
    Super35,                     // default
    Mm65,
    Stills135,                   // 35mm stills (full frame 36x24)
    Count
};

struct TextureParams {
    // global
    double time          = 0.0;  // frame number from the host (drives animation)
    float  pixelScale    = 1.f;  // = rendered image width / 1920; all pixel sizes below are at 1920 px

    // gate weave (applied first, moves the whole frame)
    float weaveAmount    = 0.f;  // 0 .. 2, 1 = typical 35mm projector weave
    float weaveSpeed     = 1.f;  // 0.25 .. 4

    // acutance / adjacency effect, in log exposure (negative = soften)
    float acutanceAmount = 0.f;  // -1 .. 2
    float acutanceRadius = 2.f;  // px @1920

    // halation: red-layer re-exposure from base reflection
    float halationAmount    = 0.f;   // 0 .. 2 (already multiplied by stock halationScale)
    float halationRadius    = 20.f;  // px @1920
    float halationThreshold = 2.5f;  // stops above 18% grey where it starts (soft)
    float halationHue       = 0.f;   // -1 (deep red) .. +1 (orange-yellow)

    // bloom: lens/emulsion diffusion, neutral
    float bloomAmount    = 0.f;   // 0 .. 2 (already multiplied by stock bloomScale)
    float bloomRadius    = 40.f;  // px @1920

    // lens vignette (natural falloff), scene-linear
    float vignette       = 0.f;   // 0 .. 1

    // grain field (structure only; amplitude is applied by the colour kernel in density)
    bool        grainEnabled  = false;
    GrainFormat grainFormat   = GrainFormat::Super35;
    float grainSize      = 1.f;   // 0.5 .. 3 (already multiplied by stock grainSizeScale)
    float grainSoftness  = 0.5f;  // 0 = crisp grains, 1 = soft clumps
    bool  grainAnimated  = true;  // false = static pattern
    int   grainSeed      = 0;
    bool  grainMonochrome= false; // B&W stocks: one shared field

    // film damage (v1.1, SPEC §5): procedural dust, scratches and hair, deterministic per frame and
    // seed. The damage field is applied by the colour pass: negative-side defects before printing
    // (they print white / take the print's colour), print-side defects on the projected print (dark).
    // damageEnabled = false or every amount 0: no passes at all.
    bool  damageEnabled      = false;
    float dustAmount         = 0.f;   // 0 .. 2 (0 = no dust; 1 = a lightly soiled print)
    float dustSize           = 1.f;   // 0.5 .. 3
    float dustNegative       = 0.35f; // 0 .. 1 share of the dust on the negative (prints white)
    float scratchAmount      = 0.f;   // 0 .. 2
    float scratchPersistence = 0.6f;  // 0 .. 1 short flickering flicks .. long continuous runs
    float scratchEmulsion    = 0.3f;  // 0 .. 1 share of emulsion (coloured) vs base (neutral) scratches
    float scratchNegative    = 0.25f; // 0 .. 1 share of the scratches on the negative
    float hairAmount         = 0.f;   // 0 .. 2
    bool  damageAnimated     = true;  // false = frozen (the damage of frame 0)
    int   damageSeed         = 0;

    // film blur (v1.1, SPEC §5): momentary projection focus drift ("sometimes soft"), applied to the
    // finished (display-referred) image so grain and damage soften with it. blurEnabled = false or a
    // frame whose blur is below 0.05 px: no passes.
    bool  blurEnabled        = false;
    float blurRadius         = 3.f;   // px @1920: defocus disc radius at the peak of an event
    float blurFrequency      = 0.3f;  // 0 .. 1 share of the time the image is soft
    float blurDuration       = 24.f;  // frames, mean length of a soft event (ease in, hold, ease out)
    float blurBreathing      = 0.2f;  // 0 .. 1 continuous gentle focus breathing (share of the radius)
    float blurChromatic      = 0.3f;  // 0 .. 1 per-channel (longitudinal chromatic) defocus
    int   blurSeed           = 0;

    // film resolution (v1.2, SPEC §5): the negative's modulation transfer (per layer, format
    // dependent, KODAK VISION3 data) applied to the scene-linear image before acutance, and
    // `integrate`, the print / scan resolution that image AND grain share (0 = off). Both off: no passes,
    // grain unchanged.
    bool  filmResolution     = false;
    float integrate          = 0.f;   // 0 .. 1

    // ---- v1.3 Texture II (appended, SPEC 13): defaults = neutral, so every existing caller renders
    // unchanged (the v1.3 grain structure itself changed, see CHANGELOG)
    // grain colour / tone: inter-layer decorrelation of the dye clouds (0 = one shared structure,
    // 1 = the data-sheet correlation, 1.5 = more colour grain) and the tonal balance, gains on the
    // grain of the negative's layer exposure (shadows / mid-tones / highlights; 1 = the model)
    float grainColour        = 1.f;   // 0 .. 1.5
    float grainShadows       = 1.f;   // 0 .. 2
    float grainMids          = 1.f;   // 0 .. 2
    float grainHighlights    = 1.f;   // 0 .. 2
    // chroma halation: saturation of the re-exposure light (0 = neutral glow, 1 = the physical
    // red / green share) and the background response (0 = additive only; 1 = the halo carries more
    // colour over dark backgrounds and less over bright ones)
    float halationSaturation = 1.f;   // 0 .. 2
    float halationBackground = 0.f;   // 0 .. 1
    // Aura: a second, very wide base-reflection lobe (multiple reflections inside the base) of the
    // same halation light, with its own radius, strength and green share. Needs halation on.
    bool  auraEnabled        = false;
    float auraRadius         = 150.f; // px @1920 (x gauge scale, like the halation radius)
    float auraStrength       = 0.5f;  // 0 .. 2, relative to the halation amount
    float renderScale        = 1.f;   // v1.3 host render scale (proxy < 1): the scan aperture is per full-res pixel
    float auraGreen          = 0.35f; // 0 .. 1 green share of the aura light (red = 1)
    // ---- v1.3 P1 extras (appended): bloom threshold and Keep Highlights; defaults = the bloom as before
    float bloomThreshold     = 0.f;   // 0 .. 8 stops: only levels above ~this far over 18 % grey bloom (0 = all)
    float bloomKeep          = 0.f;   // 0 .. 1: share of the blooming light NOT taken from its source (0 = energy kept)
    // ---- v1.3 reference-look WP (appended): lens diffusion filter and digital sharpening removal; defaults
    // = as before. bloomFilter 0 = Classic (bloomAmount / the v1.0 PSF); 1.. = a diffusion filter
    // (tex::bloomFilterShape) whose scattered fraction is set by bloomFilterGrade (1/8 .. 2) instead.
    int   bloomFilter        = 0;
    float bloomFilterGrade   = 0.25f; // 0.125 .. 2
    float sharpenRemoval     = 0.f;   // 0 .. 1.5 Digital Sharpening Removal (Film Resolution; 0 = off)
};

// ---- v1.3 Film Breath: per-frame exposure / development / printer-light variation ----------------
// Evaluated on the host per frame (cel::tex::filmBreathAt) and applied to the colour look of that frame
// (engine::FrameSettings::breath records the values). Deterministic in (seed, frame), smoothed over
// time (1/f-like octaves of smooth noise plus per-frame jitter, the share set by the profile).
enum class BreathProfile : int {   // ORDER IS THE OFX CHOICE-PARAM ORDER, append only
    Lab = 0,                       // slow, smooth drift between prints / development runs (default)
    Projector,                     // lamp and shutter flicker: faster, with frame-to-frame jitter
    OldCamera,                     // irregular camera shutter / motor speed, strong per-frame jitter
    HandCranked,                   // large slow surges of a hand-cranked camera plus jitter
    Count
};
struct FilmBreathParams {
    bool  enabled     = false;
    BreathProfile profile = BreathProfile::Lab;
    float exposure    = 0.05f;   // stops RMS of the camera exposure jitter (scene, before the negative)
    float development = 0.03f;   // RMS of the development contrast variation (relative gamma)
    float printer     = 0.5f;    // printer points RMS of the printer-light variation (per lamp + common)
    float period      = 24.f;    // frames: time scale of the slowest variation
    int   seed        = 0;
};
struct FilmBreath {
    float exposure = 0.f;                // stops added to the scene exposure
    float contrast = 1.f;                // multiplier on the development contrast (filmic contrast)
    float printer[3] = {0.f, 0.f, 0.f};  // printer points added to the R, G, B lamps
};

}} // namespace cel::tex
