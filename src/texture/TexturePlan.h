// Celluloid Pro - texture-only plans and CPU backend diagnostics (texture agent).
// Internal API for tests, tools and diagnostics; the plugin uses engine::buildPlan (Engine.h).
#pragma once
#include "../engine/Engine.h"
#include <cstddef>
#include <utility>
#include <vector>

namespace cel { namespace tex {

// Image texture chain WITHOUT colour passes:
//   src = scene-linear RGBA (displayReferred = false, Full-mode semantics) or display-linear RGBA
//   (displayReferred = true, Texture-Only semantics) -> dst = weave, acutance, halation/bloom,
//   vignette applied (no grain). If every effect is off the plan is a single tx_copy.
//   hdrOutput (Texture-Only only): display values may exceed 1 (PQ/HLG); otherwise SDR, where the
//   Texture-Only chain never produces values above 1.0.
engine::Plan buildTexturePlan(int w, int h, const TextureParams& tp, bool displayReferred, bool hdrOutput = false);

// Grain field only: dst = grain noise (x, y, z = red, green, blue layer; w = 0) of size w x h,
// exactly what the colour pass receives as in1. src is not read (pass any w x h image).
engine::Plan buildGrainPlan(int w, int h, const TextureParams& tp);

// Effective pixel scale used by the plan builder (TextureParams::pixelScale if valid, else w/1920).
float effectivePixelScale(const TextureParams& tp, int w);

// CPU backend buffer pool statistics (process-wide).
struct CpuPoolStats {
    size_t allocations = 0;   // buffers ever allocated
    size_t idleBuffers = 0;   // buffers currently cached in the pool
    size_t idleBytes = 0;
};
CpuPoolStats cpuBackendPoolStats();
void         cpuBackendTrimPool();   // free all idle buffers

// Optional per-pass timing of the CPU backend (process-wide switch, for diagnostics/tests):
// when enabled, every execute() records wall-clock and process CPU time per pass; the result of
// the most recent execute() is returned by cpuBackendLastProfile().
struct PassTiming {
    engine::KernelId kernel;
    int    w, h;          // output size
    double wallMs;        // elapsed
    double cpuMs;         // CPU time of the whole process during the pass (all threads)
};
void cpuBackendProfile(bool enable);
std::vector<PassTiming> cpuBackendLastProfile();

}} // namespace cel::tex
