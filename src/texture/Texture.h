// Celluloid Pro - texture parameters (spatial / temporal effects).  FIXED (architect).
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
};

}} // namespace cel::tex
