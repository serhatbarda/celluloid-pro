// Celluloid Pro - host-side design of the texture passes (texture agent).
// Everything here is pure, deterministic math that turns cel::tex::TextureParams into the f[]/i[]
// values of the portable kernels in src/kernels/TextureKernels.h. Used by src/engine/Plan.cpp and
// by the tests. All "Px" inputs are actual pixels (i.e. already multiplied by pixelScale).
#pragma once
#include "Texture.h"
#include <cstdint>
#include <vector>

namespace cel { namespace tex {

// ---- discrete Gaussians -----------------------------------------------------------------------
// Symmetric 1D kernel with radius <= 7 (tx_blur4 limit): w[0] = centre, w[k] = tap at +-k,
// normalised to sum 1. variance = sum k^2 w (two-sided), sumSq = sum of squared weights.
struct Gauss1D {
    int    radius = 0;
    float  w[8] = {1.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f};
    double variance = 0.0;
    double sumSq = 1.0;
};
constexpr double kMaxBlurSigma = 2.33;   // sigma that still fits radius 7 at 3 sigma
Gauss1D gaussKernel(double sigma);

// Size of pyramid level L (ceil division, never below 1).
int levelSize(int fullSize, int level);

// ---- grain -------------------------------------------------------------------------------------
double      grainFormatSigma(GrainFormat f);      // green-layer grain sigma, px @1920, grainSize 1
float       grainFormatAmplitude(GrainFormat f);  // suggested amplitude factor vs Super 35 (see .cpp)
const char* grainFormatName(GrainFormat f);

// Layer sizes relative to the green layer, and the shared clump field.
constexpr double kGrainRelSigma[4] = {1.12, 1.00, 1.45, 1.60};   // red, green, blue, shared

struct GrainShape {          // distribution shaping (from grainSoftness)
    double sigmaScale;       // softness widens the grain (HF roll-off)
    double gamma, t, kappa;  // y = X + gamma * sqrt((X - t)^2 + kappa^2)
    double m;                // clump modulation of the per-layer grain by the shared field
    double a, b;             // X = (a n (1 + m (s^2-1)/sqrt2) + b s) * invX; b from the layer correlation
    double invX;
    double mu, invSigma;     // normalisation of y (quadrature, exact for Gaussian n, s)
    double corr;             // resulting inter-layer correlation of y
};
constexpr double kGrainLayerCorrelation = 0.40;   // SPEC: 0.3 .. 0.5
GrainShape grainShape(float softness);             // cached, thread-safe

struct GrainOctave {
    bool     used = false;
    double   spacing = 1.0;      // generation grid spacing in full-res px (>= 1, any real value)
    int      margin = 0;         // extra grid px on every side (blur + B-spline support)
    int      gw = 0, gh = 0;     // generation grid size = ceil(w / spacing) + 2 margin (same for h)
    double   sigmaFull[4] = {};  // intended sigma per channel in full-res px
    Gauss1D  g[4];               // per channel blur on the generation grid
    float    norm[4] = {1.f, 1.f, 1.f, 1.f};  // 1 / std of the blurred (and upsampled) noise
    uint32_t seed = 0;           // tx_grain_gen seed
};
// The generation grid is chosen so the green layer has sigma = kGrainGridSigma grid px: well
// above the B-spline reconstruction basis (isotropic, no grid structure) and the widest channel
// (shared field, 1.6x) still fits the 7-tap radius.
constexpr double kGrainGridSigma = 1.30;
// Fine octave: crisp sub-structure of the dye clouds (real grain has a much flatter Wiener
// spectrum than a Gaussian). Sigma relative to the layer sigma; its weight fades out with
// softness and when it would be sub-pixel at the 1920 px reference (so the look stays
// resolution independent).
constexpr double kGrainFineRel = 0.40;
constexpr double kGrainFineWeight = 0.55;     // amplitude share (0.3 of the variance) at softness 0
struct GrainDesign {
    GrainOctave coarse;          // x, y, z = layers, w = shared clump field
    GrainOctave fine;            // x, y, z = layers (w unused)
    double   wCoarse = 1.0, wFine = 0.0;   // octave amplitudes, wCoarse^2 + wFine^2 = 1
    GrainShape shape{};
    bool     mono = false;
};
GrainDesign designGrain(const TextureParams& tp, int w, int h, float pixelScale);
uint32_t    grainFrameSeed(int grainSeed, bool animated, double time);

// ---- glow pyramid (halation + bloom) --------------------------------------------------------------
struct GlowLevel {
    bool    used = false;        // this level contributes a scale (blur + accumulate)
    Gauss1D gBloom, gHal;        // in-level blur (level px)
    float   wBloom = 0.f, wHal = 0.f;
    double  sigmaBloom = 0.0, sigmaHal = 0.0;   // intended full-res sigma of this scale
};
struct GlowDesign {
    int nLevels = 0;             // deepest level computed (>= 1)
    int finest = 0;              // finest used level (the composite samples it)
    std::vector<GlowLevel> level; // index 0..nLevels (0 unused)
};
// Scale ladders (sigma relative to the radius parameter) and their weights (sum 1).
constexpr int    kBloomScales = 4;
constexpr double kBloomRel[kBloomScales] = {0.25, 0.5, 1.0, 2.0};
constexpr double kBloomW[kBloomScales]   = {0.16, 0.24, 0.32, 0.28};
constexpr int    kHalScales = 6;
constexpr double kHalRel[kHalScales] = {0.2, 0.35, 0.6, 1.0, 1.8, 3.2};
constexpr double kHalW[kHalScales]   = {0.16, 0.24, 0.24, 0.18, 0.12, 0.06};
GlowDesign designGlow(bool bloom, double bloomRadiusPx, bool hal, double halRadiusPx, int w, int h);

struct HalationLook {
    float tint[3];               // added light colour (red = 1)
    float gain;                  // per unit halationAmount
    float threshold, knee, emax; // threshold / emax scene-linear, knee in stops over the threshold
    float srcW[3];               // halation source weights
};
HalationLook halationLook(float amount, float thresholdStops, float hue);
constexpr float kBloomMixPerAmount = 0.10f;  // bloom mix s = amount * this
constexpr float kBloomBMax = 48.f;           // bloom source compression

// ---- acutance ------------------------------------------------------------------------------------
struct AcutDesign {
    int     level = 0;           // 0: blur at full res, else pyramid level
    Gauss1D g;
    float   k = 0.25f;           // soft limit (log2)
    float   gmax = 1.f;          // hard clamp (log2) = 0.3 log10
    float   eps = 1e-4f;
};
AcutDesign designAcutance(double radiusPx);

// ---- vignette ------------------------------------------------------------------------------------
constexpr float kVignetteCornerTan = 0.9f;   // tan(field angle) at the corner: amount 1 = -1.7 stops

// ---- gate weave ------------------------------------------------------------------------------------
struct WeaveOffset { double dx = 0, dy = 0, rot = 0; };   // px @1920, radians
WeaveOffset weaveOffset(double time, float amount, float speed);

// ---- Texture-Only tone map (display-linear <-> pseudo scene) -----------------------------------
// sdr = true (SDR outputs): above Yk the curve approaches 1.0 asymptotically (never exceeds it);
// sdr = false (HDR outputs): exact linear continuation (display values > 1 survive).
struct ToneMap { float x0, p, yk, xk, slope, sdr; };
ToneMap toneMap(bool sdr);
double toneMapToScene(double Y, bool sdr = false);    // host reference (double) of cel_txh_d2s
double toneMapToDisplay(double x, bool sdr = false);  // host reference of cel_txh_s2d

}} // namespace cel::tex
