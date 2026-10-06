// Celluloid Pro - host-side design of the texture passes (texture agent). See TextureDesign.h.
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

// ---- grain -----------------------------------------------------------------------------------------
// Dominant (green layer) dye-cloud sigma in px of a 1920 px wide frame, for grainSize = 1 and a
// 200-speed-class emulsion. Derived from the physical gate width: the same emulsion is magnified
// more when the gate is smaller (Super 35 24.9 mm, 16 mm/Super 16 ~12.4 mm, Super 8 5.8 mm,
// 65 mm 52.5 mm, 135 full frame 36 mm), with S35 calibrated so grain at 1080p reads like a
// typical 2K scan.
double grainFormatSigma(GrainFormat f) {
    switch (f) {
        case GrainFormat::Super8:    return 3.40;
        case GrainFormat::Mm16:      return 1.60;
        case GrainFormat::Super35:   return 0.80;
        case GrainFormat::Mm65:      return 0.40;
        case GrainFormat::Stills135: return 0.60;
        default:                     return 0.80;
    }
}
// Visible granularity grows with magnification. Per-pixel RMS of the grain field is normalised to
// 1, so the colour pipeline should scale its grain amplitude by this factor for the chosen format
// (square root of the relative grain size: Super 8 ~2x, 16 mm ~1.4x, 65 mm ~0.7x of Super 35).
float grainFormatAmplitude(GrainFormat f) {
    return float(std::sqrt(grainFormatSigma(f) / grainFormatSigma(GrainFormat::Super35)));
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

struct Quad {                               // trapezoid quadrature for E[.] over N(0,1)
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

struct ShapeMoments { double mu, sigma, corr; };

ShapeMoments shapeMoments(const GrainShape& s, double b, const Quad& q) {
    double a = std::sqrt(std::max(0.0, 1.0 - b * b));
    double invX = 1.0 / std::sqrt(a * a * (1.0 + s.m * s.m) + b * b);
    double k2 = s.kappa * s.kappa;
    double m1 = 0.0, m2 = 0.0, g2 = 0.0;
    for (size_t j = 0; j < q.z.size(); ++j) {          // shared field s
        double sh = q.z[j], gs = 0.0;
        for (size_t i = 0; i < q.z.size(); ++i) {      // per-layer noise n
            double X = (a * q.z[i] * (1.0 + s.m * (sh * sh - 1.0) * kInvSqrt2) + b * sh) * invX;
            double d = X - s.t;
            double y = X + s.gamma * std::sqrt(d * d + k2);
            gs += q.w[i] * y;
            m2 += q.w[j] * q.w[i] * y * y;
        }
        m1 += q.w[j] * gs;
        g2 += q.w[j] * gs * gs;
    }
    double var = std::max(1e-12, m2 - m1 * m1);
    return {m1, std::sqrt(var), (g2 - m1 * m1) / var};
}

} // namespace

GrainShape grainShape(float softness) {
    static std::mutex mtx;
    static std::map<int, GrainShape> cache;
    double S = std::min(1.0, std::max(0.0, double(std::isfinite(softness) ? softness : 0.5f)));
    int key = int(std::lround(S * 10000.0));
    {
        std::lock_guard<std::mutex> lk(mtx);
        auto it = cache.find(key);
        if (it != cache.end()) return it->second;
    }
    S = key / 10000.0;
    GrainShape s{};
    // Softness: 0 = crisp, distinct, skewed grains; 1 = soft, nearly Gaussian clumps, and wider
    // (HF roll-off).
    s.sigmaScale = 1.0 + 0.35 * S;
    s.gamma = 0.25 * (1.0 - 0.6 * S);
    s.kappa = 0.12 + 0.55 * S;
    s.t = 0.0;
    s.m = 0.22;
    static const Quad q(0.08);
    // correlation is monotonic in b: bisection to hit kGrainLayerCorrelation
    double lo = 0.0, hi = 0.99;
    for (int it = 0; it < 30; ++it) {
        double mid = 0.5 * (lo + hi);
        if (shapeMoments(s, mid, q).corr < kGrainLayerCorrelation) lo = mid; else hi = mid;
    }
    s.b = 0.5 * (lo + hi);
    s.a = std::sqrt(1.0 - s.b * s.b);
    s.invX = 1.0 / std::sqrt(s.a * s.a * (1.0 + s.m * s.m) + s.b * s.b);
    static const Quad qf(0.02);                        // fine grid for the final normalisation
    ShapeMoments mm = shapeMoments(s, s.b, qf);
    s.mu = mm.mu;
    s.invSigma = 1.0 / mm.sigma;
    s.corr = mm.corr;
    std::lock_guard<std::mutex> lk(mtx);
    if (cache.size() > 256) cache.clear();
    cache[key] = s;
    return s;
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

namespace {
GrainOctave designOctave(const double sigmaFull[4], int w, int h, uint32_t seed) {
    GrainOctave o;
    o.used = true;
    o.seed = seed;
    for (int c = 0; c < 4; ++c) o.sigmaFull[c] = sigmaFull[c];
    double sgreen = sigmaFull[1];
    double D = std::max(1.0, sgreen / kGrainGridSigma);
    D = std::min(D, double(std::max(w, h)));                  // never coarser than the frame
    o.spacing = D;
    double up = D > 1.0 ? 1.0 / 3.0 : 0.0;
    int R = 0;
    for (int c = 0; c < 4; ++c) {
        double s = sigmaFull[c] / D;
        o.g[c] = gaussKernel(std::min(kMaxBlurSigma, std::sqrt(std::max(0.0, s * s - up))));
        R = std::max(R, o.g[c].radius);
        double sd = noiseStd1D(o.g[c], D);
        o.norm[c] = float(1.0 / (sd * sd));
    }
    o.margin = R + (D > 1.0 ? 2 : 0);
    o.gw = std::max(1, int(std::ceil(double(w) / D))) + 2 * o.margin;
    o.gh = std::max(1, int(std::ceil(double(h) / D))) + 2 * o.margin;
    return o;
}
double smoothstep(double e0, double e1, double x) {
    double t = std::min(1.0, std::max(0.0, (x - e0) / (e1 - e0)));
    return t * t * (3.0 - 2.0 * t);
}
} // namespace

GrainDesign designGrain(const TextureParams& tp, int w, int h, float pixelScale) {
    GrainDesign d;
    d.shape = grainShape(tp.grainSoftness);
    double S = std::min(1.0, std::max(0.0, double(std::isfinite(tp.grainSoftness) ? tp.grainSoftness : 0.5f)));
    double size = std::isfinite(tp.grainSize) ? std::min(20.0, std::max(0.05, double(tp.grainSize))) : 1.0;
    double ps = (std::isfinite(pixelScale) && pixelScale > 0.f) ? double(pixelScale) : 1.0;
    double sgRef = grainFormatSigma(tp.grainFormat) * size * d.shape.sigmaScale;   // @1920
    double sg = sgRef * ps;
    uint32_t seed = grainFrameSeed(tp.grainSeed, tp.grainAnimated, tp.time);
    double sc[4];
    for (int c = 0; c < 4; ++c) sc[c] = sg * kGrainRelSigma[c];
    d.coarse = designOctave(sc, w, h, seed);
    double beta = kGrainFineWeight * (1.0 - S) * smoothstep(0.45, 0.8, sgRef * kGrainFineRel);
    if (beta > 0.02) {
        double sf[4];
        for (int c = 0; c < 3; ++c) sf[c] = sc[c] * kGrainFineRel;
        sf[3] = 0.0;
        d.fine = designOctave(sf, w, h, hashU32(seed ^ 0x2545f491U));
        d.wFine = beta;
        d.wCoarse = std::sqrt(1.0 - beta * beta);
    }
    d.mono = tp.grainMonochrome;
    return d;
}

// ---- glow ------------------------------------------------------------------------------------------
GlowDesign designGlow(bool bloom, double bloomRadiusPx, bool hal, double halRadiusPx, int w, int h) {
    GlowDesign gd;
    struct Scale { int group; double sigma, weight; };
    std::vector<Scale> sc;
    auto ok = [](double v) { return std::isfinite(v) && v > 0.0; };
    if (bloom && ok(bloomRadiusPx))
        for (int i = 0; i < kBloomScales; ++i) sc.push_back({0, bloomRadiusPx * kBloomRel[i], kBloomW[i]});
    if (hal && ok(halRadiusPx))
        for (int i = 0; i < kHalScales; ++i) sc.push_back({1, halRadiusPx * kHalRel[i], kHalW[i]});
    if (sc.empty()) return gd;
    // deepest useful level: stop once the image is a single pixel
    int maxLevel = 1;
    while (maxLevel < 16 && (levelSize(w, maxLevel) > 1 || levelSize(h, maxLevel) > 1)) ++maxLevel;
    gd.level.assign(size_t(maxLevel) + 1, GlowLevel{});
    // per (group, level): accumulated weight and weighted variance (moment matching when several
    // scales collapse onto one level for tiny radii)
    std::vector<double> wsum[2], vsum[2];
    for (int g = 0; g < 2; ++g) { wsum[g].assign(size_t(maxLevel) + 1, 0.0); vsum[g].assign(size_t(maxLevel) + 1, 0.0); }
    for (const Scale& s : sc) {
        double sigma = std::min(s.sigma, 1.0e6);
        int l = 1;
        while (l < maxLevel && sigma * std::ldexp(1.0, -l) > 2.45) ++l;
        wsum[s.group][size_t(l)] += s.weight;
        vsum[s.group][size_t(l)] += s.weight * sigma * sigma;
    }
    int finest = 0, deepest = 0;
    for (int l = 1; l <= maxLevel; ++l) {
        if (wsum[0][size_t(l)] > 0.0 || wsum[1][size_t(l)] > 0.0) {
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
            if (ws <= 0.0) continue;
            double var = vsum[g][size_t(l)] / ws;
            Gauss1D k = gaussKernel(std::sqrt(std::max(0.0, (var - inherent) / s4)));
            if (g == 0) { L.gBloom = k; L.wBloom = float(ws); L.sigmaBloom = std::sqrt(var); }
            else        { L.gHal = k;   L.wHal = float(ws);   L.sigmaHal = std::sqrt(var); }
        }
    }
    return gd;
}

HalationLook halationLook(float amount, float thresholdStops, float hue) {
    HalationLook L{};
    double hh = std::isfinite(hue) ? std::min(1.0, std::max(-1.0, double(hue))) : 0.0;
    // tint anchors: deep red (-1), red-orange (0), orange-yellow (+1)
    const double A[3][3] = {{1.0, 0.06, 0.02}, {1.0, 0.24, 0.06}, {1.0, 0.50, 0.13}};
    int i0 = hh < 0.0 ? 0 : 1;
    double t = hh < 0.0 ? hh + 1.0 : hh;
    for (int c = 0; c < 3; ++c) L.tint[c] = float(A[i0][c] + (A[i0 + 1][c] - A[i0][c]) * t);
    double am = std::isfinite(amount) ? std::max(0.0, double(amount)) : 0.0;
    L.gain = float(am * 0.60);
    double th = std::isfinite(thresholdStops) ? std::min(12.0, std::max(-4.0, double(thresholdStops))) : 2.5;
    L.threshold = float(0.18 * std::exp2(th));
    L.knee = 0.5f * L.threshold;
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

}} // namespace cel::tex
