// Reafilm Pro - host-side design of the texture passes (texture agent). See TextureDesign.h.
#include "TextureDesign.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>

namespace cel { namespace tex {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kInvSqrt2 = 0.70710678118654752440;

uint32_t hashU32(uint32_t x) {             // same lowbias32 as cel_hash_u32 (Portable.h)
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x;
}
double hash01(uint32_t x) { return (double(hashU32(x)) + 0.5) / 4294967296.0; }
double hashGauss(uint32_t x) {             // exact N(0,1) (Box-Muller) for host-side sequences
    double u1 = hash01(x), u2 = hash01(x ^ 0xa511e9b3U);
    return std::sqrt(-2.0 * std::log(u1)) * std::cos(2.0 * kPi * u2);
}

// cubic B-spline basis on 4 taps
void bsplineW(double t, double w[4]) {
    double t2 = t * t, t3 = t2 * t, u = 1.0 - t;
    w[0] = u * u * u / 6.0;
    w[1] = (3.0 * t3 - 6.0 * t2 + 4.0) / 6.0;
    w[2] = (-3.0 * t3 + 3.0 * t2 + 3.0 * t + 1.0) / 6.0;
    w[3] = t3 / 6.0;
}

// Stationary smooth noise in u (unit variance at every u): B-spline through hashed N(0,1) knots,
// divided by the phase-dependent standard deviation of the B-spline combination.
double smoothNoise(double u, uint32_t seed) {
    double fl = std::floor(u);
    double t = u - fl;
    int64_t k = int64_t(fl);
    double w[4]; bsplineW(t, w);
    double s = 0.0, n2 = 0.0;
    for (int i = 0; i < 4; ++i) {
        uint32_t key = hashU32(uint32_t(int32_t(k + i - 1)) * 0x9e3779b1U ^ seed);
        s += w[i] * hashGauss(key);
        n2 += w[i] * w[i];
    }
    return s / std::sqrt(n2);
}

} // namespace

// ------------------------------------------------------------------------------------------------
Gauss1D gaussKernel(double sigma) {
    Gauss1D g;
    if (!(sigma > 1e-3)) return g;                      // identity (also catches NaN)
    int r = int(std::ceil(3.0 * sigma));
    r = std::max(1, std::min(7, r));
    double w[8] = {0, 0, 0, 0, 0, 0, 0, 0}, sum = 0.0;
    for (int k = 0; k <= r; ++k) {
        w[k] = std::exp(-double(k) * double(k) / (2.0 * sigma * sigma));
        sum += (k == 0 ? 1.0 : 2.0) * w[k];
    }
    g.radius = r;
    g.variance = 0.0; g.sumSq = 0.0;
    for (int k = 0; k <= 7; ++k) {
        double v = k <= r ? w[k] / sum : 0.0;
        g.w[k] = float(v);
        g.variance += (k == 0 ? 0.0 : 2.0) * double(k) * double(k) * v;
        g.sumSq += (k == 0 ? 1.0 : 2.0) * v * v;
    }
    return g;
}

int levelSize(int fullSize, int level) {
    int s = std::max(1, fullSize);
    for (int l = 0; l < level; ++l) s = (s + 1) / 2;
    return s;
}

// ---- gauge -------------------------------------------------------------------------------------------
// Camera aperture widths: Super 8 5.79 mm, Super 16 12.52 mm, Super 35 24.89 mm, 65 mm 5-perf 52.48 mm,
// 35 mm stills (135 full frame) 36 mm.
double gaugeGateMm(GrainFormat f) {
    switch (f) {
        case GrainFormat::Super8:    return 5.79;
        case GrainFormat::Mm16:      return 12.52;
        case GrainFormat::Super35:   return 24.89;
        case GrainFormat::Mm65:      return 52.48;
        case GrainFormat::Stills135: return 36.0;
        default:                     return 24.89;
    }
}
double gaugeRadiusScale(GrainFormat f) { return std::sqrt(gaugeGateMm(GrainFormat::Super35) / gaugeGateMm(f)); }

// ---- grain -----------------------------------------------------------------------------------------
// Coarse (fast emulsion) dye-cloud sigma in px of a 1920 px wide frame, grainSize 1. The owner's
// power-grade ladder for 16 / 35 / 65 (size 1.25 : 1 : 0.5); Super 8 2.5x and 35 mm stills 0.75x of
// Super 35 (between 35 and 65 by gate width).
double grainFormatSigma(GrainFormat f) {
    switch (f) {
        case GrainFormat::Super8:    return 2.00;
        case GrainFormat::Mm16:      return 1.00;
        case GrainFormat::Super35:   return 0.80;
        case GrainFormat::Mm65:      return 0.40;
        case GrainFormat::Stills135: return 0.60;
        default:                     return 0.80;
    }
}
// Density amplitude vs Super 35 (owner's ladder 1.7 : 1 : 0.67 for 16 / 35 / 65; Super 8 and stills
// placed on the same curve: amplitude ~ (gate ratio)^0.72).
float grainFormatAmplitude(GrainFormat f) {
    switch (f) {
        case GrainFormat::Super8:    return 2.60f;
        case GrainFormat::Mm16:      return 1.70f;
        case GrainFormat::Super35:   return 1.00f;
        case GrainFormat::Mm65:      return 0.67f;
        case GrainFormat::Stills135: return 0.80f;
        default:                     return 1.00f;
    }
}
const char* grainFormatName(GrainFormat f) {
    switch (f) {
        case GrainFormat::Super8:    return "Super 8";
        case GrainFormat::Mm16:      return "16 mm";
        case GrainFormat::Super35:   return "Super 35";
        case GrainFormat::Mm65:      return "65 mm";
        case GrainFormat::Stills135: return "35 mm Stills";
        default:                     return "?";
    }
}

namespace {

// Gauss-Hermite-like trapezoid quadrature over N(0,1)
struct Quad {
    std::vector<double> z, w;
    explicit Quad(double h) {
        const double L = 8.5;
        int n = int(std::ceil(2.0 * L / h)) + 1;
        double step = 2.0 * L / double(n - 1), sum = 0.0;
        for (int i = 0; i < n; ++i) {
            double x = -L + step * i;
            z.push_back(x);
            w.push_back(std::exp(-0.5 * x * x));
            sum += w.back();
        }
        for (double& v : w) v /= sum;
    }
};
double phi(double x) { return 0.5 * std::erfc(-x * kInvSqrt2); }
// fast / slow emulsion probit arguments at exposure s (== celg_layer)
void emulsionZ(double s, double& zf, double& zs) {
    double z = (s - kGrainFastCentre) / kGrainFastWidth, d = z - kGrainFogZ;
    zf = 0.5 * (z + kGrainFogZ + std::sqrt(d * d + kGrainFogKappa * kGrainFogKappa));
    zs = (s - kGrainSlowCentre) / kGrainSlowWidth;
}
// variance of the soft indicator Phi((G + c z) / w) - Phi(z) over G ~ N(0,1)
double indicatorVar(double z, double w) {
    static const Quad q(0.01);
    double c = std::sqrt(1.0 + w * w), m = phi(z), v = 0.0;
    for (size_t i = 0; i < q.z.size(); ++i) {
        double d = phi((q.z[i] + c * z) / w) - m;
        v += q.w[i] * d * d;
    }
    return v;
}
// amplitudes (fast, slow) with unit total std at grey and the fitted fast / slow ratio
void grainAmplitudes(double w, double& aF, double& aS) {
    double zf, zs;
    emulsionZ(0.0, zf, zs);
    double v = kGrainFastSlowRatio * kGrainFastSlowRatio * indicatorVar(zf, w) + indicatorVar(zs, w);
    aS = kGrainGain / std::sqrt(std::max(v, 1e-12));
    aF = kGrainFastSlowRatio * aS;
}

} // namespace

double grainModelStd(double s, double w) {
    double aF, aS, zf, zs;
    grainAmplitudes(w, aF, aS);
    emulsionZ(s, zf, zs);
    return std::sqrt(aF * aF * indicatorVar(zf, w) + aS * aS * indicatorVar(zs, w));
}

// ---- film resolution ----------------------------------------------------------------------------------
MtfDesign designMtf(const TextureParams& tp, float pixelScale) {
    MtfDesign d;
    const double ps = (std::isfinite(pixelScale) && pixelScale > 0.f) ? double(pixelScale) : 1.0;
    const double umPerPx = gaugeGateMm(tp.grainFormat) * 1000.0 / 1920.0 / ps;   // film micrometres per pixel
    const double integ = std::isfinite(tp.integrate) ? std::min(1.0, std::max(0.0, double(tp.integrate))) : 0.0;
    const double sys = integ * kIntegrateSigmaUm / umPerPx;
    double smax = 0.0;
    for (int c = 0; c < 3; ++c) {
        double neg = tp.filmResolution ? kNegMtfSigmaUm[c] / umPerPx : 0.0;
        d.sigma[c] = std::sqrt(neg * neg + sys * sys);
        smax = std::max(smax, d.sigma[c]);
    }
    // v1.3: a sigma beyond the 7-tap kernel (6K and larger with high Integrate) is split into cascaded
    // passes instead of being clamped, so the film resolution stays the same relative to the frame
    if (smax > kMaxBlurSigma) d.passes = std::min(8, int(std::ceil((smax / kMaxBlurSigma) * (smax / kMaxBlurSigma))));
    const double div = std::sqrt(double(d.passes));
    for (int c = 0; c < 3; ++c) {
        d.g[c] = gaussKernel(std::min(kMaxBlurSigma, d.passes > 1 ? d.sigma[c] / div : d.sigma[c]));
        if (d.g[c].radius > 0) d.on = true;
    }
    d.g[3] = Gauss1D{};                  // alpha: identity (the kernel keeps it exactly, i[5])
    d.grainSigma = kIntegrateGrainShare * sys;
    return d;
}

namespace {
// Mean (over output positions) standard deviation factor of unit white noise blurred by g on a
// generation grid of the given spacing and B-spline upsampled to full res (1D; the 2D value is
// its square). spacing == 1: no upsampling.
double noiseStd1D(const Gauss1D& g, double spacing) {
    if (spacing <= 1.0) return std::sqrt(g.sumSq);
    auto wk = [&](int k) -> double { k = std::abs(k); return k <= g.radius ? double(g.w[k]) : 0.0; };
    double C[7];
    for (int d = -3; d <= 3; ++d) {
        double c = 0.0;
        for (int k = -g.radius; k <= g.radius; ++k) c += wk(k) * wk(k + d);
        C[d + 3] = c;
    }
    const int n = 4096;
    double acc = 0.0;
    for (int x = 0; x < n; ++x) {
        double s = (x + 0.5) / spacing - 0.5;
        double t = s - std::floor(s), b[4];
        bsplineW(t, b);
        double v = 0.0;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j) v += b[i] * b[j] * C[i - j + 3];
        acc += v;
    }
    return std::sqrt(acc / n);
}
} // namespace

uint32_t grainFrameSeed(int grainSeed, bool animated, double time) {
    int frame = 0;
    if (animated && std::isfinite(time)) {
        double f = std::floor(time + 0.5);
        f = std::max(-2.0e9, std::min(2.0e9, f));
        frame = int(f);
    }
    return hashU32(uint32_t(grainSeed) * 0x9e3779b1U ^ hashU32(uint32_t(frame) * 0x85ebca6bU + 0x632be5abU));
}

double grainSharedVar(float grainColour) {
    double c = std::isfinite(grainColour) ? std::min(1.5, std::max(0.0, double(grainColour))) : 1.0;
    return 1.0 - (1.0 - kGrainSharedVar) * c * c;
}

namespace {
// v1.3 clumped cloud kernel: (1 - m) G(s) + m G(kGrainClumpRel s), both clamped to the 7-tap radius
Gauss1D clumpKernel(double s) {
    Gauss1D a = gaussKernel(std::min(kMaxBlurSigma, s));
    if (!(s > 1e-3)) return a;
    Gauss1D b = gaussKernel(std::min(kMaxBlurSigma, kGrainClumpRel * s));
    Gauss1D g;
    g.radius = std::max(a.radius, b.radius);
    g.variance = 0.0; g.sumSq = 0.0;
    for (int k = 0; k <= 7; ++k) {
        double v = (1.0 - kGrainClumpMix) * double(a.w[k]) + kGrainClumpMix * double(b.w[k]);
        if (k > g.radius) v = 0.0;
        g.w[k] = float(v);
        g.variance += (k == 0 ? 0.0 : 2.0) * double(k) * double(k) * v;
        g.sumSq += (k == 0 ? 1.0 : 2.0) * v * v;
    }
    return g;
}

GrainOctave designOctave(const double sigmaFull[4], int w, int h, uint32_t seed) {
    GrainOctave o;
    o.used = true;
    o.seed = seed;
    for (int c = 0; c < 4; ++c) o.sigmaFull[c] = sigmaFull[c];
    double sgreen = sigmaFull[1];
    // v1.3: the green clump scale must fit the 7-tap kernel too, so the clumps scale with the gauge
    double D = std::max(1.0, std::max(sgreen / kGrainGridSigma, sgreen * kGrainClumpRel / kMaxBlurSigma));
    D = std::min(D, double(std::max(w, h)));                  // never coarser than the frame
    o.spacing = D;
    double up = D > 1.0 ? 1.0 / 3.0 : 0.0;
    int R = 0;
    for (int c = 0; c < 4; ++c) {
        double s = sigmaFull[c] / D;
        o.g[c] = clumpKernel(std::sqrt(std::max(0.0, s * s - up)));
        R = std::max(R, o.g[c].radius);
        double sd = noiseStd1D(o.g[c], D);
        o.norm[c] = float(1.0 / (sd * sd));
    }
    o.margin = R + (D > 1.0 ? 2 : 0);
    o.gw = std::max(1, int(std::ceil(double(w) / D))) + 2 * o.margin;
    o.gh = std::max(1, int(std::ceil(double(h) / D))) + 2 * o.margin;
    return o;
}
} // namespace

GrainDesign designGrain(const TextureParams& tp, int w, int h, float pixelScale) {
    GrainDesign d;
    double S = std::min(1.0, std::max(0.0, double(std::isfinite(tp.grainSoftness) ? tp.grainSoftness : 0.5f)));
    double size = std::isfinite(tp.grainSize) ? std::min(20.0, std::max(0.05, double(tp.grainSize))) : 1.0;
    double ps = (std::isfinite(pixelScale) && pixelScale > 0.f) ? double(pixelScale) : 1.0;
    // softness: wider clouds (HF roll-off) and a softer cloud edge (indicator width)
    const double sigmaScale = 1.0 + 0.35 * S;
    d.w = kGrainWidth0 + kGrainWidthSoft * S;
    const double cRef = grainFormatSigma(tp.grainFormat) * kGrainCloudScale * size * sigmaScale;   // @1920
    const double fRef = std::max(kGrainFineRel * cRef, std::min(cRef, kGrainFineMinPx));
    // Integrate: the print / scan system blurs the grain like the image (Gaussian clouds stay
    // Gaussian: sigma^2 adds); a blurred field keeps rms ~ 1 / sigma (2D)
    const MtfDesign mtf = designMtf(tp, pixelScale);
    const double gs = mtf.grainSigma;
    const double c0 = cRef * ps, f0 = fRef * ps;
    const double c1 = std::sqrt(c0 * c0 + gs * gs), f1 = std::sqrt(f0 * f0 + gs * gs);
    uint32_t seed = grainFrameSeed(tp.grainSeed, tp.grainAnimated, tp.time);
    double sc[4], sf[4];
    // v1.3 scan aperture (film-referenced like the clouds: 0.6 px of a 1920 px frame): softens the
    // cloud shape only; the RMS stays (the octave norm re-normalises, the amplitudes are unchanged)
    const double rs = (std::isfinite(tp.renderScale) && tp.renderScale > 0.f) ? std::min(1.0, double(tp.renderScale)) : 1.0;
    const double scan = kGrainScanSigmaPx * rs;
    for (int c = 0; c < 4; ++c) {
        double a = c1 * kGrainRelSigma[c], b = f1 * kGrainRelSigma[c];
        sc[c] = std::sqrt(a * a + scan * scan);
        sf[c] = std::sqrt(b * b + scan * scan);
    }
    d.coarse = designOctave(sc, w, h, seed);
    d.fine = designOctave(sf, w, h, hashU32(seed ^ 0x2545f491U));
    const double shared = grainSharedVar(tp.grainColour);
    d.cs = std::sqrt(shared);
    d.ci = std::sqrt(1.0 - shared);
    grainAmplitudes(d.w, d.aFast, d.aSlow);
    d.aFast *= c0 / c1;
    d.aSlow *= f0 / f1;
    // pixel aperture (Selwyn): the amplitudes are calibrated for 1920 px across the gate; a pixel that
    // covers more film (lower resolution, render scale < 1) averages more clouds, so the per-pixel
    // fluctuation follows var ~ 1 / (sigma^2 + pixel^2 / 12) with the cloud sigma in 1920-px units --
    // a 0.5 render then carries the grain of the downsampled full-resolution frame
    // v1.3: the scan aperture (sigma `scan` output px) is part of what one pixel averages; with it in the
    // law a 4K / 6K frame carries more per-pixel grain than HD, and box-downsampled to HD it carries the
    // calibrated (HD) grain - fit-to-screen viewing of a 6K timeline shows the nominal grain
    auto aperture = [&](double sigmaPx) {
        double sr = sigmaPx / ps, k = (scan / ps) * (scan / ps);
        return std::sqrt((sr * sr + k + 1.0 / 12.0) / (sr * sr + k + 1.0 / (12.0 * ps * ps)));
    };
    d.aFast *= aperture(c1);
    d.aSlow *= aperture(f1);
    d.mono = tp.grainMonochrome;
    // v1.3 tonal balance: the mids gain scales both amplitudes; shadows / highlights relative to it
    auto gainOf = [](float v) { return std::isfinite(v) ? std::min(4.0, std::max(0.0, double(v))) : 1.0; };
    const double gS = gainOf(tp.grainShadows), gH = gainOf(tp.grainHighlights);
    const double gM = std::max(0.01, gainOf(tp.grainMids));
    if (gM != 1.0) { d.aFast *= gM; d.aSlow *= gM; }
    d.tonal = gS != gM || gH != gM;
    d.toneShadow = float(gS / gM - 1.0);
    d.toneHigh = float(gH / gM - 1.0);
    // kernel argument block (GrainKernels.h)
    float* f = d.f;
    f[0] = float(1.0 / d.coarse.spacing); f[1] = float(d.coarse.margin);
    f[2] = float(1.0 / d.fine.spacing);   f[3] = float(d.fine.margin);
    for (int c = 0; c < 4; ++c) { f[4 + c] = d.coarse.norm[c]; f[8 + c] = d.fine.norm[c]; }
    f[12] = float(d.cs); f[13] = float(d.ci);
    f[14] = float(1.0 / d.w); f[15] = float(std::sqrt(1.0 + d.w * d.w));
    f[16] = float(kGrainFastCentre); f[17] = float(1.0 / kGrainFastWidth);
    f[18] = float(kGrainFogZ); f[19] = float(kGrainFogKappa * kGrainFogKappa);
    f[20] = float(kGrainSlowCentre); f[21] = float(1.0 / kGrainSlowWidth);
    f[22] = float(d.aFast); f[23] = float(d.aSlow);
    d.i[0] = 2;
    d.i[3] = d.coarse.spacing > 1.0 ? 1 : 0;
    d.i[4] = d.fine.spacing > 1.0 ? 1 : 0;
    d.i[5] = d.mono ? 1 : 0;
    return d;
}

// ---- glow ------------------------------------------------------------------------------------------
namespace {
struct GlowScale { int group; double sigma, weight; };
GlowDesign glowFromScales(const std::vector<GlowScale>& sc, int w, int h);
} // namespace

// v1.3 Bloom Filter PSFs (see TextureDesign.h). Shapes from the published behaviour of each filter type:
// black mists scatter into a bright core halo plus a wide, faint veil (veiling glare that lifts the
// shadows without turning the image milky); white mists spread more light into a wide veil (milky, lower contrast); Glimmerglass keeps
// a tight, bright halo around highlights with little veil; Hollywood Black Magic is a fine, close
// diffusion that softens detail more than it glows; CineBloom is a soft glow of medium spread with no
// wide tail. Our own parametrisation, no measured or vendor data.
const BloomFilterShape& bloomFilterShape(int filter) {
    static const BloomFilterShape table[kBloomFilterCount] = {
        {{0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0}, 0.0},                                    // Classic (unused)
        {{0.08, 0.25, 0.7, 2.0, 6.0, 20.0}, {0.28, 0.22, 0.16, 0.12, 0.11, 0.11}, 0.20},    // Black Pro-Mist
        {{0.1, 0.3, 1.0, 3.0, 8.0, 20.0}, {0.12, 0.14, 0.16, 0.18, 0.20, 0.20}, 0.30},    // Pro-Mist (white)
        {{0.05, 0.12, 0.3, 0.7, 1.5, 3.0}, {0.34, 0.27, 0.19, 0.11, 0.06, 0.03}, 0.12},   // Glimmerglass
        {{0.04, 0.1, 0.25, 0.6, 1.5, 4.0}, {0.32, 0.28, 0.20, 0.11, 0.06, 0.03}, 0.15},   // Hollywood Black Magic
        {{0.15, 0.3, 0.55, 0.9, 1.4, 2.0}, {0.14, 0.20, 0.24, 0.20, 0.14, 0.08}, 0.15},   // CineBloom
    };
    return table[std::min(std::max(filter, 1), kBloomFilterCount - 1)];
}

double bloomFilterFraction(int filter, double grade) {
    if (filter <= 0 || filter >= kBloomFilterCount || !std::isfinite(grade) || !(grade > 0.0)) return 0.0;
    return bloomFilterShape(filter).frac * std::pow(std::min(grade, 2.0), 0.7);
}

GlowDesign designGlow(bool bloom, double bloomRadiusPx, bool hal, double halRadiusPx, int w, int h, bool halRing,
                      int bloomFilter) {
    std::vector<GlowScale> sc;
    auto ok = [](double v) { return std::isfinite(v) && v > 0.0; };
    if (bloom && ok(bloomRadiusPx) && bloomFilter > 0 && bloomFilter < kBloomFilterCount) {
        const BloomFilterShape& f = bloomFilterShape(bloomFilter);
        for (int i = 0; i < kBloomFilterScales; ++i) sc.push_back({0, bloomRadiusPx * f.rel[i], f.w[i]});
    } else if (bloom && ok(bloomRadiusPx))
        for (int i = 0; i < kBloomScales; ++i) sc.push_back({0, bloomRadiusPx * kBloomRel[i], kBloomW[i]});
    if (hal && ok(halRadiusPx))
        for (int i = 0; i < kHalScales; ++i)
            sc.push_back({1, halRadiusPx * (halRing ? kHalRingRel[i] : kHalRel[i]), halRing ? kHalRingW[i] : kHalW[i]});
    return glowFromScales(sc, w, h);
}

// v1.3 Aura: one smooth, wide lobe (group 1 = the halation channel of GlowLevel)
GlowDesign designAura(double auraRadiusPx, int w, int h) {
    std::vector<GlowScale> sc;
    if (std::isfinite(auraRadiusPx) && auraRadiusPx > 0.0)
        for (int i = 0; i < kAuraScales; ++i) sc.push_back({1, auraRadiusPx * kAuraRel[i], kAuraW[i]});
    return glowFromScales(sc, w, h);
}

namespace {
GlowDesign glowFromScales(const std::vector<GlowScale>& sc, int w, int h) {
    GlowDesign gd;
    if (sc.empty()) return gd;
    // deepest useful level: stop once the image is a single pixel
    int maxLevel = 1;
    while (maxLevel < 16 && (levelSize(w, maxLevel) > 1 || levelSize(h, maxLevel) > 1)) ++maxLevel;
    gd.level.assign(size_t(maxLevel) + 1, GlowLevel{});
    // per (group, level): accumulated weight and weighted variance (moment matching when several
    // scales collapse onto one level for tiny radii)
    std::vector<double> wsum[2], vsum[2];
    for (int g = 0; g < 2; ++g) { wsum[g].assign(size_t(maxLevel) + 1, 0.0); vsum[g].assign(size_t(maxLevel) + 1, 0.0); }
    for (const GlowScale& s : sc) {
        double sigma = std::min(s.sigma, 1.0e6);
        int l = 1;
        while (l < maxLevel && sigma * std::ldexp(1.0, -l) > 2.45) ++l;
        wsum[s.group][size_t(l)] += s.weight;
        vsum[s.group][size_t(l)] += s.weight * sigma * sigma;
    }
    int finest = 0, deepest = 0;
    for (int l = 1; l <= maxLevel; ++l) {
        if (wsum[0][size_t(l)] != 0.0 || wsum[1][size_t(l)] != 0.0) {
            gd.level[size_t(l)].used = true;
            if (!finest) finest = l;
            deepest = l;
        }
    }
    gd.finest = finest;
    gd.nLevels = deepest;
    gd.level.resize(size_t(deepest) + 1);
    // variance bookkeeping (full-res px^2): downsampling to level l adds 0.25 (4^l - 1); every
    // B-spline reconstruction from a used level j on the way up adds 4^j / 3.
    for (int l = 1; l <= deepest; ++l) {
        GlowLevel& L = gd.level[size_t(l)];
        if (!L.used) continue;
        double s4 = std::ldexp(1.0, 2 * l);
        double inherent = 0.25 * (s4 - 1.0);
        for (int j = 1; j <= l; ++j)
            if (gd.level[size_t(j)].used) inherent += std::ldexp(1.0, 2 * j) / 3.0;
        for (int g = 0; g < 2; ++g) {
            double ws = wsum[g][size_t(l)];
            if (ws == 0.0) continue;   // (the annular halation PSF has a negative inner scale)
            double var = vsum[g][size_t(l)] / ws;
            Gauss1D k = gaussKernel(std::sqrt(std::max(0.0, (var - inherent) / s4)));
            if (g == 0) { L.gBloom = k; L.wBloom = float(ws); L.sigmaBloom = std::sqrt(var); }
            else        { L.gHal = k;   L.wHal = float(ws);   L.sigmaHal = std::sqrt(var); }
        }
    }
    return gd;
}
} // namespace

HalationLook halationLook(float amount, float thresholdStops, float hue, bool displayReferred) {
    HalationLook L{};
    double hh = std::isfinite(hue) ? std::min(1.0, std::max(-1.0, double(hue))) : 0.0;
    double th = std::isfinite(thresholdStops) ? std::min(12.0, std::max(-4.0, double(thresholdStops))) : 0.0;
    double am = std::isfinite(amount) ? std::max(0.0, double(amount)) : 0.0;
    L.ring = !(th > 0.0);
    if (L.ring) {
        // v1.2 physical halation: red (+ some green) light re-exposes the red- (and green-) sensitive
        // layers; the tint is the light ADDED to the scene-linear exposure: deep red (-1), red with the
        // green-layer share of the reflected light (0: R 1, G 0.25, B 0), orange (+1). The film then
        // renders it (a warm ring, never magenta, see VALIDATION.md).
        const double A[3][3] = {{1.0, 0.10, 0.0}, {1.0, 0.25, 0.0}, {1.0, 0.45, 0.03}};
        int i0 = hh < 0.0 ? 0 : 1;
        double t = hh < 0.0 ? hh + 1.0 : hh;
        for (int c = 0; c < 3; ++c) L.tint[c] = float(A[i0][c] + (A[i0 + 1][c] - A[i0][c]) * t);
        L.gain = float(am * kHalRingGainPerAmount);   // fraction of the red-weighted light
        L.threshold = 0.f;
        L.knee = float(0.18 * std::exp2(displayReferred ? kHalEmphasisStopsDisplay : kHalEmphasisStops));   // emphasis, linear
        L.emax = float(0.18 * 512.0);                  // compress only beyond +9 stops
        L.srcW[0] = 0.85f; L.srcW[1] = 0.15f; L.srcW[2] = 0.0f;
        // chroma activity: the green share of the tint runs from x0.5 (faint light: the outer tail,
        // weak sources - deep red) to x1.5 (strong light at bright sources - orange); h0 = 0.05 is
        // about 2 stops under grey of added light
        L.chroma = 0.5f;
        L.chromaRef = 0.05f;
        return L;
    }
    // v1.1 soft-knee gate (unchanged)
    const double A[3][3] = {{1.0, 0.06, 0.02}, {1.0, 0.24, 0.06}, {1.0, 0.50, 0.13}};
    int i0 = hh < 0.0 ? 0 : 1;
    double t = hh < 0.0 ? hh + 1.0 : hh;
    for (int c = 0; c < 3; ++c) L.tint[c] = float(A[i0][c] + (A[i0 + 1][c] - A[i0][c]) * t);
    L.gain = float(am * 0.24);
    L.threshold = float(0.18 * std::exp2(th));
    L.knee = 2.0f;
    L.emax = 32.f;
    L.srcW[0] = 0.60f; L.srcW[1] = 0.30f; L.srcW[2] = 0.10f;
    return L;
}

// ---- acutance --------------------------------------------------------------------------------------
AcutDesign designAcutance(double radiusPx) {
    AcutDesign d;
    double s = (std::isfinite(radiusPx) && radiusPx > 0.0) ? std::min(radiusPx, 1.0e5) : 0.5;
    if (s <= kMaxBlurSigma) { d.level = 0; d.g = gaussKernel(s); return d; }
    for (int l = 1; l < 16; ++l) {
        double s4 = std::ldexp(1.0, 2 * l);
        double inherent = 0.25 * (s4 - 1.0) + s4 / 3.0;
        double r = std::sqrt(std::max(0.0, (s * s - inherent) / s4));
        if (r <= kMaxBlurSigma || l == 15) { d.level = l; d.g = gaussKernel(r); return d; }
    }
    return d;
}

// ---- gate weave --------------------------------------------------------------------------------------
// Smooth pseudo-random path: three incommensurate sines (claw/sprocket-like periodicities, random
// phases) + two octaves of stationary smooth noise (slow wander), plus a small independent
// per-frame registration jitter. Mostly vertical; tiny rotation. Normalised so amount 1 gives
// ~0.6 px RMS vertical and ~0.28 px RMS horizontal at 1920 px (typical 35 mm projection).
WeaveOffset weaveOffset(double time, float amount, float speed) {
    WeaveOffset o;
    if (!std::isfinite(time) || !std::isfinite(amount) || !(amount > 0.f)) return o;
    double sp = std::isfinite(speed) ? std::min(8.0, std::max(0.05, double(speed))) : 1.0;
    double u = time * sp;
    const double P[3] = {41.3, 17.9, 7.37}, A[3] = {0.50, 0.35, 0.20};
    const double Q[2] = {13.0, 4.6}, B[2] = {0.60, 0.35};
    double var = 0.0;
    for (int i = 0; i < 3; ++i) var += 0.5 * A[i] * A[i];
    for (int i = 0; i < 2; ++i) var += B[i] * B[i];
    const double jit = 0.22;
    double frame = std::floor(time + 0.5);
    frame = std::max(-2.0e9, std::min(2.0e9, frame));
    auto axis = [&](uint32_t seed) {
        double v = 0.0;
        for (int i = 0; i < 3; ++i) v += A[i] * std::sin(2.0 * kPi * (u / P[i] + hash01(seed + uint32_t(i))));
        for (int i = 0; i < 2; ++i) v += B[i] * smoothNoise(u / Q[i], hashU32(seed + 17u + uint32_t(i)));
        v /= std::sqrt(var);
        v += jit * hashGauss(hashU32(uint32_t(int32_t(frame)) ^ (seed * 0x27d4eb2dU)));
        return v / std::sqrt(1.0 + jit * jit);
    };
    double am = std::min(4.0, double(amount));
    o.dy = 0.60 * am * axis(0x1b873593U);
    o.dx = 0.28 * am * axis(0xcc9e2d51U);
    o.rot = 1.0e-4 * am * axis(0xe6546b64U);
    return o;
}

// Lab & Gate: weave styles. Projector = the weave above (bit-identical). Camera (pin-registered): the
// registration pins hold the frame, so only a small, mostly frame-to-frame jitter is left. Telecine (capstan
// float): no pins at all - a slow, smooth float with a larger sideways and rotational share and almost no jitter.
WeaveOffset weaveOffset(double time, float amount, float speed, int style) {
    if (style != int(WeaveStyle::Camera) && style != int(WeaveStyle::Telecine)) return weaveOffset(time, amount, speed);
    WeaveOffset o;
    if (!std::isfinite(time) || !std::isfinite(amount) || !(amount > 0.f)) return o;
    const bool cam = style == int(WeaveStyle::Camera);
    double sp = std::isfinite(speed) ? std::min(8.0, std::max(0.05, double(speed))) : 1.0;
    double u = time * sp * (cam ? 1.0 : 0.4);
    const double P[3] = {41.3, 17.9, 7.37}, A[3] = {0.50, 0.35, 0.20};
    const double Q[2] = {13.0, 4.6}, B[2] = {0.60, 0.35};
    double var = 0.0;
    for (int i = 0; i < 3; ++i) var += 0.5 * A[i] * A[i];
    for (int i = 0; i < 2; ++i) var += B[i] * B[i];
    const double jit = cam ? 1.6 : 0.05;
    double frame = std::floor(time + 0.5);
    frame = std::max(-2.0e9, std::min(2.0e9, frame));
    auto axis = [&](uint32_t seed) {
        double v = 0.0;
        for (int i = 0; i < 3; ++i) v += A[i] * std::sin(2.0 * kPi * (u / P[i] + hash01(seed + uint32_t(i))));
        for (int i = 0; i < 2; ++i) v += B[i] * smoothNoise(u / Q[i], hashU32(seed + 17u + uint32_t(i)));
        v /= std::sqrt(var);
        v += jit * hashGauss(hashU32(uint32_t(int32_t(frame)) ^ (seed * 0x27d4eb2dU)));
        return v / std::sqrt(1.0 + jit * jit);
    };
    double am = std::min(4.0, double(amount));
    o.dy = (cam ? 0.22 : 0.50) * am * axis(cam ? 0x5bd1e995U : 0x68e31da4U);
    o.dx = (cam ? 0.12 : 0.55) * am * axis(cam ? 0x1b56c4e9U : 0xb5297a4dU);
    o.rot = (cam ? 0.2e-4 : 2.5e-4) * am * axis(cam ? 0x85ebca6bU : 0xc2b2ae35U);
    return o;
}

// ---- film gate (Lab & Gate) -------------------------------------------------------------------
// OFX choice order (append only): Full Frame (the frame's own aspect), 1.33 Academy, 1.66, 1.85, 2.39, 1.43 IMAX,
// Super 16 (1.66), 1.78, v1.6: 1.50 (35 mm stills), 1.00 (6x6), 1.25 (6x7)
const float kGateAspects[kGateAspectCount] = {0.f, 1.3333f, 1.66f, 1.85f, 2.39f, 1.43f, 1.66f, 1.7778f, 1.5f, 1.f, 1.25f};

GateDesign designGate(const TextureParams& tp, int w, int h, float ps) {
    GateDesign g;
    if (!tp.gateEnabled || w < 1 || h < 1) return g;
    auto fin = [](float v, float lo, float hi, float def) { return std::isfinite(v) ? std::min(hi, std::max(lo, v)) : def; };
    const int ai = std::min(std::max(tp.gateAspect, 0), kGateAspectCount - 1);
    const double m = double(fin(tp.gateOverscan, 0.f, 0.25f, 0.03f)) * h;
    const double aw = std::max(1.0, w - 2.0 * m), ah = std::max(1.0, h - 2.0 * m);
    double hw = 0.5 * aw, hh = 0.5 * ah;
    if (ai > 0) {
        const double A = kGateAspects[ai];
        if (A >= aw / ah) hh = hw / A; else hw = hh * A;
    }
    const double s16 = ai == 6 ? 0.6 : 1.0;   // the smaller gauge's aperture has proportionally tighter corners
    const double r = std::min(std::min(hw, hh), double(fin(tp.gateCorner, 0.f, 80.f, 14.f)) * ps * s16);
    float* f = g.f;
    f[0] = 0.5f * float(w); f[1] = 0.5f * float(h);
    f[2] = float(hw); f[3] = float(hh); f[4] = float(r);
    f[5] = float(std::max(0.6, double(fin(tp.gateSoftness, 0.f, 20.f, 2.f)) * ps));
    f[6] = kGateRebateDensity;
    if (tp.gateHair) {
        // a hair caught at the aperture: rooted in the rebate just outside one edge, curving into the picture
        const uint32_t sd = hashU32(uint32_t(std::max(tp.gateHairSeed, 0)) * 0x9e3779b9U ^ 0x2545f491U);
        const int side = int(hashU32(sd) & 3U);
        const double t = 0.15 + 0.7 * hash01(sd + 1u), len = (0.05 + 0.06 * hash01(sd + 2u)) * h;
        const double bend = (hash01(sd + 3u) - 0.5) * 0.9, wig = (hash01(sd + 4u) - 0.5) * 0.25;
        double ex, ey, nx, ny;   // root on the gate edge, inward normal
        switch (side) {
            case 0: ex = f[0] - hw + 2.0 * hw * t; ey = f[1] - hh; nx = 0; ny = 1; break;
            case 1: ex = f[0] + hw; ey = f[1] - hh + 2.0 * hh * t; nx = -1; ny = 0; break;
            case 2: ex = f[0] - hw + 2.0 * hw * t; ey = f[1] + hh; nx = 0; ny = -1; break;
            default: ex = f[0] - hw; ey = f[1] - hh + 2.0 * hh * t; nx = 1; ny = 0; break;
        }
        const double tx = -ny, ty = nx, out0 = 6.0 * ps;
        for (int k = 0; k < 5; ++k) {
            const double sk = k / 4.0, along = sk * len - out0;
            const double side2 = len * (bend * sk * sk + wig * std::sin(3.0 * kPi * sk));
            f[8 + 2 * k] = float(ex + nx * along + tx * side2);
            f[9 + 2 * k] = float(ey + ny * along + ty * side2);
        }
        f[7] = float(std::max(0.35, 0.9 * ps));
    }
    // v1.6 Film Edge: the strip's real dimensions (mm), scaled by the gate's size across the strip.
    // 135 / 35 mm: KS perforations 1.981 x 2.794 mm, pitch 4.75 mm; 120: no perforations, 2.75 mm rebate.
    const int edge = std::min(std::max(tp.filmEdge, 0), int(FilmEdge::Count) - 1);
    if (edge != int(FilmEdge::Off)) {
        struct Strip { double frameMm, pitch, holeCentre, textCentre, textH, filmEdge; };
        const Strip st = edge == int(FilmEdge::Still35)  ? Strip{24.0, 4.75, 2.08, 4.45, 1.1, 5.5}
                       : edge == int(FilmEdge::Motion35) ? Strip{24.89, 4.75, 1.64, 4.10, 1.0, 5.04}
                                                         : Strip{56.0, 0.0, 0.0, 1.50, 1.2, 2.75};
        const double across = edge == int(FilmEdge::Motion35) ? 2.0 * hw : 2.0 * std::min(hw, hh);
        f[18] = float(across / st.frameMm);
        f[19] = float(st.pitch);
        f[20] = 1.981f; f[21] = 2.794f; f[22] = 0.5f;
        f[23] = float(st.holeCentre);
        f[28] = float(st.textCentre); f[29] = float(st.textH);
        f[30] = float(-0.5 * st.pitch);   // a perforation centred on the frame
        f[31] = float(st.filmEdge);
        g.edge = edge;
        g.frameNumber = std::min(std::max(tp.frameNumber, 0), 99);
    }
    g.on = true;
    return g;
}

// ---- Film Breath (v1.3) -------------------------------------------------------------------------------
// Each profile: period scale, share of the per-frame (white) jitter in the variance, and the size of the
// exposure / development / printer variation per unit Amount.
//   Lab          slow, smooth drift between takes / processing batches
//   Projector    lamp and shutter flicker: faster, a little frame-to-frame jitter
//   Old Camera   a worn camera motor: uneven shutter, clear frame-to-frame jitter
//   Hand-cranked strong, fast, uneven exposure from frame to frame
namespace {
struct BreathShape { double periodScale, jitter, exposure, development, printer; };
BreathShape breathShape(BreathProfile p) {
    switch (p) {
        case BreathProfile::Projector:   return {0.5, 0.30, 1.0, 0.5, 0.6};
        case BreathProfile::OldCamera:   return {0.6, 0.50, 1.6, 0.8, 1.0};
        case BreathProfile::HandCranked: return {0.35, 0.70, 2.6, 1.0, 1.0};
        case BreathProfile::Lab:
        default:                         return {1.0, 0.08, 1.0, 1.0, 1.0};
    }
}
} // namespace

double breathSequence(BreathProfile profile, double period, int seed, uint32_t channel, double frame) {
    if (!std::isfinite(frame)) return 0.0;
    const BreathShape sh = breathShape(profile);
    double P = std::isfinite(period) ? std::min(1000.0, std::max(2.0, period)) : 24.0;
    P *= sh.periodScale;
    P = std::max(1.0, P);
    frame = std::max(-2.0e9, std::min(2.0e9, std::floor(frame + 0.5)));
    const uint32_t base = hashU32(uint32_t(seed) * 0x9e3779b1U ^ hashU32(channel * 0x85ebca6bU + 0x5bd1e995U));
    const double W[3] = {1.0, 0.7, 0.5};
    double v = 0.0, n2 = 0.0;
    for (int k = 0; k < 3; ++k) {
        v += W[k] * smoothNoise(frame / (P * std::ldexp(1.0, -k)), hashU32(base + uint32_t(k) * 0x27d4eb2dU));
        n2 += W[k] * W[k];
    }
    v /= std::sqrt(n2);
    double j = sh.jitter;
    double white = hashGauss(hashU32(uint32_t(int32_t(frame)) * 0xcc9e2d51U ^ base));
    return std::sqrt(1.0 - j) * v + std::sqrt(j) * white;
}

FilmBreath filmBreathAt(const FilmBreathParams& bp, double time) {
    FilmBreath b;
    if (!bp.enabled || !std::isfinite(time)) return b;
    const BreathShape sh = breathShape(bp.profile);
    auto amt = [](float v, float hi) { return std::isfinite(v) ? std::min(double(hi), std::max(0.0, double(v))) : 0.0; };
    const double e = amt(bp.exposure, 1.f), d = amt(bp.development, 0.5f), pr = amt(bp.printer, 4.f);
    auto seq = [&](uint32_t ch) { return breathSequence(bp.profile, bp.period, bp.seed, ch, time); };
    if (e > 0.0) b.exposure = float(e * sh.exposure * seq(1));
    if (d > 0.0) b.contrast = float(std::min(2.0, std::max(0.5, 1.0 + d * sh.development * seq(2))));
    if (pr > 0.0) {
        double common = seq(3);
        for (int c = 0; c < 3; ++c)
            b.printer[c] = float(pr * sh.printer * (common + 0.5 * seq(4 + uint32_t(c))) / std::sqrt(1.25));
    }
    return b;
}

// ---- Texture-Only tone map ---------------------------------------------------------------------------
namespace {
struct TM { double x0, p, yk, xk, slope; };
const TM& tm() {
    static const TM t = [] {
        TM r{};
        double q1 = 0.40 / 0.60, q2 = 0.95 / 0.05;          // grey -> 0.40, +6 stops -> 0.95
        r.p = std::log(q2 / q1) / std::log(64.0);
        r.x0 = 0.18 / std::pow(q1, 1.0 / r.p);
        r.yk = 0.92;
        auto inv = [&](double Y) { double v = std::pow(Y, 1.0 / 2.4); return r.x0 * std::pow(v / (1.0 - v), 1.0 / r.p); };
        r.xk = inv(r.yk);
        double h = 1e-6;
        r.slope = (inv(r.yk) - inv(r.yk - h)) / h;
        return r;
    }();
    return t;
}
} // namespace

ToneMap toneMap(bool sdr) {
    const TM& t = tm();
    return {float(t.x0), float(t.p), float(t.yk), float(t.xk), float(t.slope), sdr ? 1.f : 0.f};
}
double toneMapToScene(double Y, bool sdr) {
    const TM& t = tm();
    if (Y <= 0.0) return Y;
    if (Y >= t.yk) {
        if (sdr) {
            double L = (1.0 - t.yk) * t.slope;
            double u = (std::min(Y, 0.999999) - t.yk) / (1.0 - t.yk);
            return t.xk - L * std::log(1.0 - u);
        }
        return t.xk + (Y - t.yk) * t.slope;
    }
    double v = std::pow(Y, 1.0 / 2.4);
    return t.x0 * std::pow(v / (1.0 - v), 1.0 / t.p);
}
double toneMapToDisplay(double x, bool sdr) {
    const TM& t = tm();
    if (x <= 0.0) return x;
    if (x >= t.xk) {
        if (sdr) return 1.0 - (1.0 - t.yk) * std::exp(-(x - t.xk) / ((1.0 - t.yk) * t.slope));
        return t.yk + (x - t.xk) / t.slope;
    }
    double q = std::pow(x / t.x0, t.p);
    return std::pow(q / (1.0 + q), 2.4);
}

// ---- v1.10 light leaks -------------------------------------------------------------------------------
// Light reaching the negative through a worn camera door or at the end of a roll: warm (red-orange to
// yellow) glows reaching in from the frame edges, added to the scene light so the film burns them like
// any over-exposure. Moving pictures: two lanes of 3 s windows (at 24 fps), each holding one leak with
// probability `frequency` (fast rise, slow fade, a little flicker, the glow growing as it brightens),
// and a rare roll-end flash that floods the frame for a few frames. Still Photo: one leak, always there,
// picked by the seed. Deterministic in (seed, frame).
namespace {
void leakGlow(LightLeak& L, uint32_t k, double env, const TextureParams& tp, int w, int h) {
    if (L.n >= 3 || !(env > 1e-3)) return;
    const double H = double(h), W = double(w), sz = std::min(2.0, std::max(0.3, double(tp.leakSize)));
    const double e = hash01(k ^ 0x51ed270bU);
    const int edge = e < 0.38 ? 0 : e < 0.76 ? 1 : e < 0.88 ? 2 : 3;   // mostly the sides (the camera door)
    const double along = 0.1 + 0.8 * hash01(k ^ 0x2f8a6c9dU), out = (0.05 + 0.2 * hash01(k ^ 0x9b4e31a7U)) * H;
    const double grow = 0.75 + 0.25 * env;
    const double ra = sz * (0.35 + 0.55 * hash01(k ^ 0x7c15f0e3U)) * H * grow;   // along the edge
    const double rb = sz * (0.25 + 0.30 * hash01(k ^ 0xd3a9b671U)) * H * grow;   // into the frame
    float* g = L.g[L.n++];
    if (edge < 2) { g[0] = float(edge == 0 ? -out : W + out); g[1] = float(along * H); g[2] = float(1.0 / rb); g[3] = float(1.0 / ra); }
    else          { g[0] = float(along * W); g[1] = float(edge == 2 ? -out : H + out); g[2] = float(1.0 / ra); g[3] = float(1.0 / rb); }
    const double a = std::min(2.0, std::max(0.0, double(tp.leakAmount))) * (1.5 + 3.5 * hash01(k ^ 0x46b1e5c3U)) * env;
    g[4] = float(a);
    g[5] = float(a * (0.18 + 0.42 * hash01(k ^ 0xa0761d65U)));
    g[6] = float(a * (0.02 + 0.08 * hash01(k ^ 0xe7037ed1U)));
}
// rise over `att` frames, then an exponential fade to the end of `dur`
double leakEnvelope(double f, double dur, double att) {
    if (f < 0.0 || f >= dur) return 0.0;
    if (f < att) { double u = (f + 1.0) / att; return u * u * (3.0 - 2.0 * u); }
    double u = (f - att) / std::max(1.0, dur - att);
    return std::exp(-3.0 * u) * (1.0 - u * u * u);
}
} // namespace

LightLeak lightLeakAt(const TextureParams& tp, int w, int h) {
    LightLeak L;
    if (!tp.leakEnabled || !(tp.leakAmount > 0.f) || w <= 0 || h <= 0) return L;
    const uint32_t seed = hashU32(uint32_t(tp.leakSeed) * 0x9e3779b1U ^ 0x6c8e9cf5U);
    if (tp.leakStill) {
        leakGlow(L, hashU32(seed ^ 1u), 1.0, tp, w, h);
        if (hash01(seed ^ 0x3c6ef372U) < 0.5) leakGlow(L, hashU32(seed ^ 2u), 0.6, tp, w, h);
        return L;
    }
    if (!std::isfinite(tp.time)) return L;
    const double t = std::max(-2.0e9, std::min(2.0e9, std::floor(tp.time + 0.5)));
    const double fq = std::min(1.0, std::max(0.0, double(tp.leakFrequency)));
    const double flick = 1.0 + 0.12 * std::max(-2.0, std::min(2.0, hashGauss(hashU32(uint32_t(int32_t(t)) ^ seed))));
    const double Wn = 72.0;
    for (int lane = 0; lane < 2; ++lane) {
        const double u = t + lane * 0.5 * Wn;
        const int32_t win = int32_t(std::floor(u / Wn));
        const uint32_t k = hashU32(uint32_t(win) * 0x85ebca6bU ^ seed ^ (lane ? 0xc2b2ae35U : 0u));
        if (!(hash01(k) < fq)) continue;
        const double dur = 14.0 + 42.0 * hash01(k ^ 0x27d4eb2fU), att = 2.0 + 3.0 * hash01(k ^ 0x165667b1U);
        const double start = std::floor((Wn - dur) * hash01(k ^ 0xd35a2d97U));
        leakGlow(L, k, leakEnvelope(u - win * Wn - start, dur, att) * flick, tp, w, h);
    }
    if (tp.leakRollEnd) {   // roll-end flash: rare, short, floods the frame
        const double Wr = 240.0;
        const int32_t win = int32_t(std::floor(t / Wr));
        const uint32_t k = hashU32(uint32_t(win) * 0x8da6b343U ^ seed ^ 0x1b56c4e9U);
        if (hash01(k) < 0.35 * fq) {
            const double dur = 6.0 + 10.0 * hash01(k ^ 0x27d4eb2fU);
            const double start = std::floor((Wr - dur) * hash01(k ^ 0xd35a2d97U));
            const double env = leakEnvelope(t - win * Wr - start, dur, 2.0) * flick;
            const int before = L.n;
            leakGlow(L, k, env, tp, w, h);
            if (L.n > before) {   // a wide, bright flood from that edge
                float* g = L.g[before];
                g[2] *= 0.3f; g[3] *= 0.3f;
                for (int c = 4; c < 7; ++c) g[c] *= 3.f;
            }
        }
    }
    return L;
}

}} // namespace cel::tex
