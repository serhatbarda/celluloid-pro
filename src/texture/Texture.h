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
};

}} // namespace cel::tex
