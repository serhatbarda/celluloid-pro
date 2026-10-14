// Reafilm Pro - host-side design of the texture passes (texture agent).
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

// ---- gauge (film format) ------------------------------------------------------------------------
// Physical camera gate width (mm) and the gauge ladder of grain amplitude / size calibrated to the
// owner's power grade (16 : 35 : 65 = 1.7 : 1 : 0.67 amplitude, 1.25 : 1 : 0.5 size; Super 8 and
// 35 mm stills placed on the same ladder by gate width).
double gaugeGateMm(GrainFormat f);
// sqrt(gate(S35) / gate(f)): radii of physical effects that are a fixed distance ON THE FILM
// (adjacency effect, halation ring) grow as the gauge gets smaller; the square root is the owner's
// ladder (his 16 mm grade uses ~1.2x the 35 mm halation radius).
double gaugeRadiusScale(GrainFormat f);

// ---- grain (v1.2: exposure-coupled dual-emulsion dye clouds, src/kernels/GrainKernels.h) --------
double      grainFormatSigma(GrainFormat f);      // fast-emulsion (coarse) cloud sigma, px @1920, grainSize 1
float       grainFormatAmplitude(GrainFormat f);  // density amplitude factor vs Super 35 (gauge ladder)
const char* grainFormatName(GrainFormat f);

// Layer sizes relative to the green layer, and the shared (inter-layer) structure.
constexpr double kGrainRelSigma[4] = {1.12, 1.00, 1.45, 1.00};   // red, green, blue own fields, shared
// Shared share of every layer's sensitivity field (variance): colour film grain reads as luminance
// grain with a visible but subtle colour component (v1.3: 0.85, was 0.98 - the owner found the v1.2 grain
// colourless; the three layers have their own silver halide, so their clouds are largely independent).
constexpr double kGrainSharedVar = 0.85;
// Fine (slow emulsion) cloud size relative to the coarse one, and its floor (px @1920).
constexpr double kGrainFineRel = 0.25;     // v1.3 (was 0.45): finer slow-emulsion clouds, resolved at 4K-6K
constexpr double kGrainFineMinPx = 0.0;    // v1.3 (was 0.35): none - the fine clouds follow the gauge, the scan aperture keeps them off the pixel grid
// Emulsion development curves (stops relative to 18 % grey). Fitted to the KODAK VISION3 granularity
// curves (reference/filmdata/fit_grain.py, w = 0.6: fast centre -3.25, width 1.56, fog z -1.42; slow
// centre +2.12, width 4.88; A_fast / A_slow 1.26), then the FAST emulsion is placed for our stocks'
// camera-to-print tone scale (v1.2 default look): centre -0.7 stops, width 1.0, fog z -2.1, A_fast / A_slow 1.36, so the coarse
// grain sits in the print's lower mid-tones (L* 20..50) and thins out in the deep shadows, as in the
// owner's references (look-dev T1; tools/look/texture_metrics.py on the F-Log2 C footage). The slow
// emulsion keeps the Kodak fit.
constexpr double kGrainFastCentre = -0.7, kGrainFastWidth = 1.0, kGrainFogZ = -2.1, kGrainFogKappa = 0.6;
constexpr double kGrainSlowCentre = 2.12, kGrainSlowWidth = 4.88;
constexpr double kGrainFastSlowRatio = 1.36;   // A_fast / A_slow
// v1.3 grain structure ("grain looked cheap": pixel-level, single-scale noise at HD):
//  - scan aperture: every cloud is seen through the scanner's optics and pixel (sigma >= this many
//    OUTPUT pixels), so the slow emulsion's sub-pixel clouds no longer render as per-pixel white noise.
//    The amplitude calibration (pixel aperture law) is unchanged: same RMS, softer and rounder grain.
//  - clumping: the sensitivity field of each emulsion is a mixture of the cloud scale and a scale
//    kGrainClumpRel x larger (clouds merge into clumps), amplitude weight kGrainClumpMix.
//  - cloud size: the film's own clouds are kGrainCloudScale x the v1.2 sizes (which were tuned to look
//    right at HD, where the pixel itself softens them): at 4K-6K the grain stays fine (a few pixels,
//    finest structure 1-2 px) instead of growing into soft blobs; at HD the scan aperture brings it
//    back to about the v1.2 size.
//  The scan aperture is per output pixel at full resolution (render scale < 1: per full-res pixel, so
//  a proxy carries the downsampled grain).
constexpr double kGrainCloudScale = 0.5;
constexpr double kGrainScanSigmaPx = 0.5;
constexpr double kGrainClumpMix = 0.35;
constexpr double kGrainClumpRel = 2.0;
// v1.3 tonal balance zones on the layer exposure (stops re 18 % grey): logistic weights, shadows below
// kGrainToneShadow, highlights above kGrainToneHigh, slope kGrainToneSlope per stop (GrainKernels.h)
// soft indicator width w = kGrainWidth0 + kGrainWidthSoft * grainSoftness (v1.3: was 0.45 + 0.40 S; the
// narrower indicator made the mid-tone grain two-tone / bimodal, a digital look)
constexpr double kGrainWidth0 = 0.9, kGrainWidthSoft = 0.4;
constexpr double kGrainToneShadow = -1.5, kGrainToneHigh = 2.0, kGrainToneSlope = 1.6;
// shared (inter-layer) variance for Grain Colour c: 1 - (1 - kGrainSharedVar) c^2
double grainSharedVar(float grainColour);
// overall level: n has unit std at grey times this (the colour pass then scales by the stock's grain
// sigma, which v1.1 multiplied by sqrt(D - Dmin) ~ 0.8 at grey)
constexpr double kGrainGain = 1.42;

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
// above the B-spline reconstruction basis (isotropic, no grid structure) and every channel still
// fits the 7-tap radius.
constexpr double kGrainGridSigma = 1.30;
struct GrainDesign {
    GrainOctave coarse;          // fast emulsion: x, y, z = own layer fields, w = shared
    GrainOctave fine;            // slow emulsion: x, y, z = own layer fields, w = shared
    double   cs = 1.0, ci = 0.0; // shared / own field weights (cs^2 + ci^2 = 1)
    double   w = 0.6;            // soft indicator width (grainSoftness)
    double   aFast = 1.0, aSlow = 1.0;   // amplitudes: unit std at grey (incl. the Integrate print MTF loss)
    bool     mono = false;
    float    f[24] = {};         // GrainKernels.h argument block f[0..23]
    int      i[6] = {};          // i[0] = 2, i[3..5]
    bool     tonal = false;      // v1.3 tonal balance active (colour pass i[6] = 1, f[30], f[31])
    float    toneShadow = 0.f, toneHigh = 0.f;   // f[30], f[31]: shadow / highlight gain relative to the mids, - 1
};
GrainDesign designGrain(const TextureParams& tp, int w, int h, float pixelScale);
uint32_t    grainFrameSeed(int grainSeed, bool animated, double time);
// host reference of the model (double): standard deviation of the density fluctuation (units of the
// grain sigma at grey) at exposure s stops for indicator width w (quadrature over the field)
double grainModelStd(double s, double w);

// ---- film resolution (v1.2): negative MTF + Integrate (print / scan resolution shared by image and
// grain). Gaussian fits of the KODAK VISION3 250D / 500T modulation-transfer curves
// (reference/filmdata/vision3_texture.json): sigma on the film in micrometres per layer.
constexpr double kNegMtfSigmaUm[3] = {4.4, 3.4, 2.9};   // red, green, blue layer
constexpr double kIntegrateSigmaUm = 10.0;               // print + scan system at Integrate = 1
// share of that system blur the grain receives: the colourist's "soften the picture under the grain"
// (the grain is re-imaged by the same optics but its dye clouds are the finest structure the scan
// keeps, so it loses far less than the camera image)
constexpr double kIntegrateGrainShare = 0.35;
struct MtfDesign {
    bool    on = false;
    double  sigma[3] = {0, 0, 0};   // full-res px per channel (image)
    Gauss1D g[4];                   // per channel (alpha: identity), one pass of `passes`
    int     passes = 1;             // v1.3: cascaded h+v passes (sigma / sqrt(passes) each) when a
                                    // sigma exceeds the 7-tap limit (6K and larger at high Integrate)
    double  grainSigma = 0.0;       // full-res px: the part of the print / scan blur the grain also gets
};
MtfDesign designMtf(const TextureParams& tp, float pixelScale);

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
// v1.1 (thresholded) halation: centre-peaked multi-scale glow
constexpr double kHalRel[kHalScales] = {0.2, 0.35, 0.6, 1.0, 1.8, 3.2};
constexpr double kHalW[kHalScales]   = {0.16, 0.24, 0.24, 0.18, 0.12, 0.06};
// v1.2 physical halation: an ANNULAR point spread (light reflected at the back of the base returns at
// a ring around its source; the inner negative scale empties the centre) with a long tail; >= 0
// everywhere, value at the centre ~0.18 of the ring peak, peak at ~0.6 radius, integral 1.
constexpr double kHalRingRel[kHalScales] = {0.3, 0.55, 0.85, 1.3, 2.0, 3.2};
constexpr double kHalRingW[kHalScales]   = {-0.10, 0.18, 0.30, 0.28, 0.20, 0.14};
GlowDesign designGlow(bool bloom, double bloomRadiusPx, bool hal, double halRadiusPx, int w, int h, bool halRing = false,
                      int bloomFilter = 0);

// v1.3 Bloom Filter: lens diffusion filters in front of the lens (scene-linear, before the negative,
// threshold-free, energy preserving: the scattered fraction is taken from the image). Each filter is a
// sharp core (the unscattered 1 - f) plus a multi-Gaussian halo that approximates its power-law PSF
// (sigma relative to the Bloom Radius, weights sum 1), and a scattered fraction per filter grade
// f = frac * min(grade, 2)^0.7 (1/8 .. 2). The wide tail of the halo is the veiling glare that lifts
// the shadows. ORDER = the OFX choice order of bloomFilter (append only); 0 = Classic (the v1.0 bloom).
constexpr int kBloomFilterCount = 6;
constexpr int kBloomFilterScales = 6;
struct BloomFilterShape {
    double rel[kBloomFilterScales];
    double w[kBloomFilterScales];
    double frac;                 // scattered fraction at grade 1
};
const BloomFilterShape& bloomFilterShape(int filter);   // filter 1 .. kBloomFilterCount - 1
double bloomFilterFraction(int filter, double grade);   // scattered fraction f (0 for Classic)

// v1.3 Aura: scale ladder (sigma relative to the aura radius) and weights (sum 1); a broad, smooth
// lobe without the annular dip of the main halation; gain per unit auraStrength relative to the
// halation gain
constexpr int    kAuraScales = 4;
constexpr double kAuraRel[kAuraScales] = {0.3, 0.55, 1.0, 1.7};
constexpr double kAuraW[kAuraScales]   = {0.15, 0.30, 0.33, 0.22};
constexpr double kAuraGainPerStrength = 0.6;
GlowDesign designAura(double auraRadiusPx, int w, int h);

struct HalationLook {
    float tint[3];               // added light colour (red = 1)
    float gain;                  // per unit halationAmount
    float threshold, knee, emax; // threshold / emax scene-linear, knee in stops over the threshold
    float srcW[3];               // halation source weights
    bool  ring;                  // v1.2 physical mode (threshold <= 0): linear source, annular PSF
    float chroma = 0.f;          // physical mode: chroma activity K of the tint (tx_hb_composite f[24])
    float chromaRef = 1.f;       //   and its reference level h0 (scene-linear added light, f[25])
};
// thresholdStops <= 0: physical, threshold-free halation (a fixed fraction of the red-weighted light,
// only extreme energies compressed; annular PSF). > 0: the v1.1 soft-knee gate (unchanged).
HalationLook halationLook(float amount, float thresholdStops, float hue, bool displayReferred = false);
constexpr double kHalRingGainPerAmount = 0.14;  // physical mode: amount 0.5 = 7 % of the red light halates (v1.3; was 0.10)
// physical mode highlight emphasis (stops over 18 % grey of red-weighted light): the halating share
// is x1/(1 + (k/yh)^2), half at k. v1.3 (reference-look WP): +3.5 stops (was +5.95, which a real
// camera's clip at +6..7 stops hardly reached, so the default halation was invisible): ~0.03 for skin
// at +1 stop, ~0.73 for a +4.2-stop sky edge, ~0.97 for clipped sources and practicals
constexpr double kHalEmphasisStops = 3.5;
// display-referred textures (Texture Only, ALEXA Rendering stage): display white 1.0 is +2.47 stops, so
// the knee sits at +1.5 stops there (~0.8 at white, ~0.1 at grey)
constexpr double kHalEmphasisStopsDisplay = 1.5;
constexpr float kBloomMixPerAmount = 0.10f;  // bloom mix s = amount * this
constexpr float kBloomBMax = 48.f;           // bloom source compression

// v1.3 chroma halation (tx_hb_composite f[26], f[27]): saturation of the tint and the background
// response: effective saturation = sat * (1 + bg (1 - 2 Y / (Y + 0.18))), Y = the image under the halo
inline bool halationChromaActive(const TextureParams& tp) {
    return tp.halationSaturation != 1.f || tp.halationBackground != 0.f;
}

// ---- Film Breath (v1.3) -------------------------------------------------------------------------------
// Unit-RMS smoothed sequence in (seed, frame) for one source (channel), profile shaped; continuous in
// frame for the smooth part, per-frame jitter on top.
double breathSequence(BreathProfile profile, double period, int seed, uint32_t channel, double frame);
FilmBreath filmBreathAt(const FilmBreathParams& bp, double time);

// ---- acutance ------------------------------------------------------------------------------------
// v1.3 reference-look WP: the adjacency effect weakens in the negative's toe (thin density): the
// acutance gain is weighted by Yb / (Yb + Yt) of the local mean luminance, Yt this many stops under
// 18 % grey (0.94 at grey, 0.67 at -3, 0.5 at -4, 0.2 at -6 stops)
constexpr double kAcutToeStops = -4.0;
// v1.3 Digital Sharpening Removal: inverse of a typical in-camera unsharp mask, applied before the film
// MTF in log luminance (the camera sharpens its log-encoded signal). The halo of the mirrorless camera
// sits at its own pixel scale: sigma 1 px of a 6240 px frame = 0.31 px at 1920 (frame relative, so a
// downscaled timeline, whose halo is already finer than its pixels, gets correspondingly less), and
// amount 1 undoes an unsharp mask of strength 0.5 (gain 1 / (1 + 0.5) at the halo frequencies)
constexpr double kSharpRemovalSigma1920 = 1920.0 / 6240.0;
constexpr float  kSharpRemovalGain = 1.0f / 3.0f;   // a / (1 + a), a = 0.5
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
// Lab & Gate: the weave of a WeaveStyle (Projector = weaveOffset above, bit-identical)
WeaveOffset weaveOffset(double time, float amount, float speed, int style);

// ---- film gate (Lab & Gate; kernel tx_gate in TextureKernels.h) ----------------------------
constexpr int kGateAspectCount = 8;
extern const float kGateAspects[kGateAspectCount];   // 0 = Full Frame (the frame's own aspect)
constexpr float kGateRebateDensity = 4.f;             // printer gate: density of the unexposed print border (neg ND)
struct GateDesign {
    bool on = false;
    float f[24] = {};   // tx_gate f[] block (see TextureKernels.h)
};
GateDesign designGate(const TextureParams& tp, int w, int h, float ps);

// ---- film damage (v1.1, src/texture/Damage.cpp; kernels in src/kernels/DamageKernels.h) ---------
// Everything per frame is derived from (damageSeed, frame) on the host: dust is drawn in the kernel
// from a per-frame seed, scratches and hairs are generated here and handed to tx_damage as tables.
constexpr int kDamageMaxScratches = 6;   // scratch slots (each with its own life cycle)
constexpr int kDamageMaxHairs = 2;       // one hair on the film (1 frame) + one caught in the gate
struct DamageDesign {
    bool  dust = false;
    int   nScratch = 0, nHair = 0;
    float f[32] = {};            // tx_damage f[] (see DamageKernels.h)
    int   i[8] = {};             // tx_damage i[]
    float scratch[kDamageMaxScratches][12] = {};   // table rows (12 floats per scratch)
    float hair[kDamageMaxHairs][16] = {};          // table rows (16 floats per hair)
    bool any() const { return dust || nScratch > 0 || nHair > 0; }
};
// true when the damage module draws anything at all (enabled and some amount > 0)
bool damageActive(const TextureParams& tp);
DamageDesign designDamage(const TextureParams& tp, int w, int h, float pixelScale);
// damage frame number: round(time) when animated, 0 when frozen
double damageFrame(const TextureParams& tp);

// ---- film blur (v1.1): projection focus drift ------------------------------------------------------
// Blur strength over time in [0, 1]: soft "events" (ease in >= 8 frames, hold, ease out >= 9 frames)
// that occupy about 0.8 x blurFrequency of the time, each blurDuration frames long on average
// (0.5 .. 1.5 x, at least 18 frames), random peak
// 0.55 .. 1, plus continuous breathing (smooth noise) of up to 0.4 x blurBreathing; combined as a
// soft union. Deterministic in (blurSeed, time); continuous in time.
double filmBlurStrength(const TextureParams& tp, double time);
struct FilmBlurDesign {
    bool    on = false;
    int     level = 0;           // 0: blur at full res; L: on pyramid level L, B-spline upsampled
    Gauss1D g[4];                // per channel (r, g, b, alpha) blur on that level
    double  sigma[3] = {0, 0, 0};// intended full-res sigma per channel (px)
};
// sigma = strength * radius / 2 (a defocus disc of radius r has the per-axis variance of a Gaussian
// of sigma r / 2), per channel x (1 + 0.35 c, 1, 1 + 0.6 c) for blurChromatic c (longitudinal colour of
// the projection lens). Off (no passes) below 0.05 px.
FilmBlurDesign designFilmBlur(const TextureParams& tp, int w, int h, float pixelScale);

// ---- Texture-Only tone map (display-linear <-> pseudo scene) -----------------------------------
// sdr = true (SDR outputs): above Yk the curve approaches 1.0 asymptotically (never exceeds it);
// sdr = false (HDR outputs): exact linear continuation (display values > 1 survive).
struct ToneMap { float x0, p, yk, xk, slope, sdr; };
ToneMap toneMap(bool sdr);
double toneMapToScene(double Y, bool sdr = false);    // host reference (double) of cel_txh_d2s
double toneMapToDisplay(double x, bool sdr = false);  // host reference of cel_txh_s2d

}} // namespace cel::tex
