// Celluloid Pro - film damage and film blur: host-side design (v1.1, texture). See TextureDesign.h
// and the kernels in src/kernels/DamageKernels.h.
//
// Physical model (what is drawn where, see also ColorKernels.h celc_film_xyz_dmg):
//   * Dust and dirt sit on the camera negative (they hold back printer light: WHITE specks on the
//     print, often slightly soft because the optical printer focuses on the emulsion, not on the
//     grit) or on the release print (DARK, sharp specks on the screen). Dust lies on one frame: a new
//     pattern every frame. Lint fibres are thin curled arcs.
//   * Scratches are cut along the film's travel by grit in a gate or on a roller, so they run
//     vertically, persist over many frames at almost the same position and wander slowly with the
//     film's lateral float; in film coordinates the scratch is continuous, so its breaks and wobble
//     move by one frame height per frame (a new pattern each frame at a stable position). Base
//     scratches scatter light out of the beam (neutral: dark on the print, white when on the
//     negative); emulsion scratches cut through the dye layers from the top and show coloured.
//   * Hair: a hair or fibre on the film shows for one frame; a hair caught in the projector gate
//     stays for seconds, slightly out of focus, protruding from the frame edge and fluttering.
// Film blur: momentary loss of focus in projection (film buckling in the gate, focus drift) as
// eased events plus slow breathing; per-channel sigma for the lens's longitudinal colour.
#include "TextureDesign.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace cel { namespace tex {

namespace {

constexpr double kPi = 3.14159265358979323846;

uint32_t hu(uint32_t x) {   // lowbias32, same as cel_hash_u32 (Portable.h)
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x;
}
uint32_t key(uint32_t a, uint32_t b) { return hu(a * 0x9e3779b1U ^ hu(b + 0x632be5abU)); }
double u01(uint32_t h, uint32_t k) { return (double(hu(h ^ (k * 0x9e3779b9U))) + 0.5) / 4294967296.0; }
double gauss(uint32_t h, uint32_t k) {
    return std::sqrt(-2.0 * std::log(u01(h, k))) * std::cos(2.0 * kPi * u01(h, k + 1U));
}
uint32_t frameKey(double frame) {
    const double f = std::max(-2.0e9, std::min(2.0e9, std::floor(frame)));
    return uint32_t(int32_t(f));
}
// stationary smooth noise with unit variance (cubic B-spline through Gaussian knots)
double smoothNoise(double t, uint32_t seed) {
    const double fl = std::floor(t), x = t - fl;
    const double u = 1.0 - x, x2 = x * x, x3 = x2 * x;
    const double w[4] = {u * u * u / 6.0, (3.0 * x3 - 6.0 * x2 + 4.0) / 6.0, (-3.0 * x3 + 3.0 * x2 + 3.0 * x + 1.0) / 6.0, x3 / 6.0};
    double s = 0.0, n2 = 0.0;
    for (int i = 0; i < 4; ++i) {
        s += w[i] * gauss(key(seed, frameKey(fl + double(i - 1))), 0U);
        n2 += w[i] * w[i];
    }
    return s / std::sqrt(n2);
}
double clamp(double v, double lo, double hi, double fallback) {
    if (!std::isfinite(v)) return fallback;
    return std::min(hi, std::max(lo, v));
}
double smoothstep(double e0, double e1, double x) {
    double t = std::min(1.0, std::max(0.0, (x - e0) / (e1 - e0)));
    return t * t * (3.0 - 2.0 * t);
}
int bitsToInt(uint32_t u) { int i; std::memcpy(&i, &u, sizeof(i)); return i; }

struct V2 { double x, y; };
V2 operator+(V2 a, V2 b) { return {a.x + b.x, a.y + b.y}; }
V2 operator-(V2 a, V2 b) { return {a.x - b.x, a.y - b.y}; }
V2 operator*(V2 a, double s) { return {a.x * s, a.y * s}; }

void putHair(float* row, const V2 P[5], double hw, double dens, double soft, bool negative, double taper) {
    for (int k = 0; k < 5; ++k) { row[2 * k] = float(P[k].x); row[2 * k + 1] = float(P[k].y); }
    row[10] = float(hw);
    row[11] = float(dens);
    row[12] = float(soft);
    row[13] = negative ? 1.f : 0.f;
    row[14] = float(taper);
    row[15] = 0.f;
}

// an S- or C-curved hair from its centre line: start a, end b, bends b1, b2 (fractions of the length,
// perpendicular), as two quadratic Bezier segments sharing P2 = (P1 + P3) / 2 (smooth, C1, at the joint)
void curvedHair(V2 a, V2 b, double b1, double b2, V2 P[5]) {
    const V2 d = b - a;
    const double len = std::sqrt(d.x * d.x + d.y * d.y);
    const V2 n = len > 1e-9 ? V2{-d.y / len, d.x / len} : V2{0.0, 1.0};
    P[0] = a;
    P[4] = b;
    P[1] = a + d * 0.25 + n * (b1 * len);
    P[3] = a + d * 0.75 + n * (b2 * len);
    P[2] = (P[1] + P[3]) * 0.5;
}

} // namespace

bool damageActive(const TextureParams& tp) {
    if (!tp.damageEnabled) return false;
    auto pos = [](float v) { return std::isfinite(v) && v > 0.f; };
    return pos(tp.dustAmount) || pos(tp.scratchAmount) || pos(tp.hairAmount);
}

double damageFrame(const TextureParams& tp) {
    if (!tp.damageAnimated || !std::isfinite(tp.time)) return 0.0;
    return std::max(-2.0e9, std::min(2.0e9, std::floor(tp.time + 0.5)));
}

DamageDesign designDamage(const TextureParams& tp, int w, int h, float pixelScale) {
    DamageDesign d;
    const double ps = (std::isfinite(pixelScale) && pixelScale > 1e-4f) ? double(pixelScale) : 1.0;
    const double ip = 1.0 / ps;
    // anti-aliasing / edge width in ref px: at least one rendered pixel, never sharper than 0.6 ref px
    // (nothing on film is perfectly sharp), capped for tiny proxies
    const double aa = std::min(3.0, std::max(0.6, ip));
    d.f[0] = float(ip);
    d.f[1] = float(aa);
    if (!damageActive(tp)) return d;
    const double Wr = std::max(1, w) * ip, Hr = std::max(1, h) * ip;   // frame in ref px
    const double frame = damageFrame(tp);
    const uint32_t fk = frameKey(frame);
    const uint32_t base = hu(uint32_t(tp.damageSeed) * 0x9e3779b1U ^ 0x5d2c3f1bU);

    // ---- dust (drawn in the kernel from a per-frame seed) -------------------------------------
    const double A = clamp(tp.dustAmount, 0.0, 2.0, 0.0);
    const double S = clamp(tp.dustSize, 0.25, 4.0, 1.0);
    const double negShare = clamp(tp.dustNegative, 0.0, 1.0, 0.35);
    if (A > 0.0) {
        d.dust = true;
        const double refArea = 1920.0 * 1080.0;
        const double Cf = 14.0 * S, Cc = 48.0 * S;            // cell sizes (footprints < C / 2)
        const double nFine = 26.0 * A * A, nCoarse = 3.5 * A * A;   // particles per 1920 x 1080 frame
        d.f[2] = float(1.0 / Cf); d.f[3] = float(Cf);
        d.f[4] = float(std::min(1.0, nFine * Cf * Cf / refArea));
        d.f[5] = float(0.45 * S); d.f[6] = float(2.2 * S);
        d.f[7] = float(1.0 / Cc); d.f[8] = float(Cc);
        d.f[9] = float(std::min(1.0, nCoarse * Cc * Cc / refArea));
        d.f[10] = float(1.4 * S); d.f[11] = float(7.0 * S);
        d.f[12] = 0.35f;                                         // share of lint fibres in the coarse layer
        d.f[13] = float(negShare);
        const double fs = std::min(S, 1.5);
        d.f[14] = float(0.35 * fs); d.f[15] = float(0.75 * fs);  // fibre half width
        d.f[16] = float(12.0 * S); d.f[17] = float(34.0 * S);    // fibre length (reach 17 S + edge < C / 2)
        d.i[0] = bitsToInt(hu(base ^ hu(fk * 0x85ebca6bU + 0x2545f491U)));
        d.i[1] = 1;
    }

    // ---- scratches (per-frame tables) ------------------------------------------------------------
    const double SA = clamp(tp.scratchAmount, 0.0, 2.0, 0.0);
    if (SA > 0.0) {
        const double P = clamp(tp.scratchPersistence, 0.0, 1.0, 0.6);
        const double emu = clamp(tp.scratchEmulsion, 0.0, 1.0, 0.3);
        const double negS = clamp(tp.scratchNegative, 0.0, 1.0, 0.25);
        const double life = 4.0 * std::pow(100.0, P);           // mean life: 4 frames .. 400 frames
        const double pAct = std::min(0.9, 0.3 * SA);              // share of the slots carrying a scratch
        for (int k = 0; k < kDamageMaxScratches; ++k) {
            const uint32_t sk = key(base ^ 0x3c6ef372U, uint32_t(k));
            const double E = life * (0.7 + 0.8 * u01(sk, 1U));
            const double q = (frame + u01(sk, 2U) * E) / E;
            const double e = std::floor(q), frac = q - e;
            const uint32_t he = key(sk, frameKey(e));
            if (!(u01(he, 1U) < pAct)) continue;
            if (frac < 0.12 * u01(he, 2U) || frac > 1.0 - 0.12 * u01(he, 3U)) continue;   // starts / ends
            const bool emulsion = u01(he, 4U) < emu;
            const bool negSide = u01(he, 5U) < negS;
            const uint32_t hf = key(he, fk);                       // per-frame variation of this scratch
            const double x0 = Wr * (0.03 + 0.94 * u01(he, 6U));
            const double drift = (0.5 + 2.5 * u01(he, 7U)) * smoothNoise(frame / 28.0, he ^ 0x1b56c4e9U);
            const double x = x0 + drift + 0.18 * gauss(hf, 1U);
            const double slope = (u01(he, 8U) - 0.5) * 0.004;
            const double hw = emulsion ? 0.40 + 0.60 * u01(he, 9U) : 0.50 + 0.90 * u01(he, 9U);   // half width (ref px)
            double strength;
            // emulsion: depth in layers, mostly about one layer (the top dye layer, partly the next), now and
            // then a deep gouge down to the base
            if (emulsion) { const double q = u01(he, 10U); strength = 0.45 + 1.1 * q + 1.2 * q * q * q * q; }
            else if (negSide) strength = 0.30 + 0.60 * u01(he, 10U);    // density, prints white
            else strength = 0.20 + 0.50 * u01(he, 10U);                 // density, dark
            strength *= 0.8 + 0.2 * u01(hf, 3U);                         // flicker
            const double wa = 0.25 + 1.60 * u01(he, 11U);              // meander amplitude (ref px)
            const double lambda = 180.0 + 700.0 * u01(he, 12U);
            const double wf = 2.0 * kPi / lambda;
            const double wp = std::fmod(2.0 * kPi * u01(he, 13U) + wf * frame * Hr, 2.0 * kPi);
            const double Lm = (30.0 + 160.0 * u01(he, 14U)) * (0.5 + P);
            const double thr = std::min(0.8, std::max(0.02, 0.5 - 0.48 * P + 0.15 * (u01(he, 15U) - 0.5)));
            const double off = std::fmod(std::fabs(frame) * Hr, Lm * 256.0);
            float* r = d.scratch[d.nScratch++];
            r[0] = float(x); r[1] = float(slope); r[2] = float(hw); r[3] = float(strength);
            r[4] = float(wa); r[5] = float(wf); r[6] = float(wp); r[7] = float((negSide ? 1 : 0) + (emulsion ? 2 : 0));
            r[8] = float(1.0 / Lm); r[9] = float(thr); r[10] = float(off); r[11] = float(hu(he ^ 0x7f4a7c15U) & 0xffffffU);
        }
    }

    // ---- hair ------------------------------------------------------------------------------------
    const double HA = clamp(tp.hairAmount, 0.0, 2.0, 0.0);
    if (HA > 0.0) {
        // a hair or fibre on the film: one frame
        const uint32_t hf = key(base ^ 0xa0761d65U, fk);
        if (u01(hf, 1U) < std::min(0.85, 0.025 * HA * HA + 0.01 * HA)) {
            const bool thick = u01(hf, 2U) < 0.6;
            const double len = 70.0 + 260.0 * u01(hf, 3U);
            const double hw = thick ? 1.0 + 1.2 * u01(hf, 4U) : 0.45 + 0.35 * u01(hf, 4U);
            const double dens = 1.2 + 1.6 * u01(hf, 5U);
            const bool neg = u01(hf, 6U) < negShare;
            const V2 c{Wr * (0.08 + 0.84 * u01(hf, 7U)), Hr * (0.08 + 0.84 * u01(hf, 8U))};
            const double th = 2.0 * kPi * u01(hf, 9U);
            const V2 dir{std::cos(th), std::sin(th)};
            V2 P[5];
            curvedHair(c - dir * (0.5 * len), c + dir * (0.5 * len), 0.25 * (u01(hf, 10U) - 0.5), 0.25 * (u01(hf, 11U) - 0.5), P);
            putHair(d.hair[d.nHair++], P, hw, dens, neg ? 1.2 : 0.25, neg, 0.3 * u01(hf, 12U));
        }
        // a hair caught in the projector gate: seconds long, at the frame edge, out of focus, fluttering
        const double slotLen = 300.0;
        const double ks = std::floor(frame / slotLen);
        for (int j = -1; j <= 0 && d.nHair < kDamageMaxHairs; ++j) {
            const uint32_t hs = key(base ^ 0xe7037ed1U, frameKey(ks + j));
            if (!(u01(hs, 1U) < std::min(0.8, 0.22 * HA))) continue;
            const double start = (ks + j) * slotLen + u01(hs, 2U) * slotLen * 0.6;
            const double dur = 36.0 + 220.0 * u01(hs, 3U);
            if (frame < start || frame >= start + dur) continue;
            const int edge = std::min(3, int(u01(hs, 4U) * 4.0));
            const double along = 0.1 + 0.8 * u01(hs, 5U);
            V2 b, in;
            switch (edge) {
                case 0: b = {along * Wr, -4.0}; in = {0.0, 1.0}; break;          // bottom row (OFX y = 0)
                case 1: b = {along * Wr, Hr + 4.0}; in = {0.0, -1.0}; break;     // top
                case 2: b = {-4.0, along * Hr}; in = {1.0, 0.0}; break;          // left
                default: b = {Wr + 4.0, along * Hr}; in = {-1.0, 0.0}; break;    // right
            }
            const double ang = (25.0 + 50.0 * u01(hs, 6U)) * kPi / 180.0 * (u01(hs, 7U) < 0.5 ? 1.0 : -1.0);
            const V2 side{-in.y, in.x};
            const V2 dir = in * std::sin(std::fabs(ang)) + side * (std::cos(std::fabs(ang)) * (ang > 0 ? 1.0 : -1.0));
            const double len = 50.0 + 200.0 * u01(hs, 8U);
            V2 P[5];
            curvedHair(b, b + dir * len, 0.3 * (u01(hs, 9U) - 0.5), 0.3 * (u01(hs, 10U) - 0.5), P);
            // flutter in the air flow: the tip moves most, the root not at all
            const V2 n{-dir.y, dir.x};
            const double amp = 1.0 + 4.0 * u01(hs, 11U);
            const double wig = amp * smoothNoise(frame / 2.5, hs ^ 0x2d358dccU) + 0.3 * gauss(key(hs, fk), 1U);
            P[1] = P[1] + n * (0.15 * wig);
            P[3] = P[3] + n * (0.65 * wig);
            P[4] = P[4] + n * wig;
            P[2] = (P[1] + P[3]) * 0.5;   // keep the joint smooth
            putHair(d.hair[d.nHair++], P, 1.1 + 1.3 * u01(hs, 12U), 1.6 + 1.4 * u01(hs, 13U), 1.0 + 1.5 * u01(hs, 14U), false,
                    0.55);   // root at the frame edge, thinning toward the tip
            break;
        }
    }
    d.i[2] = d.nScratch;
    d.i[3] = d.nHair;
    return d;
}

// ---- film blur ------------------------------------------------------------------------------------
double filmBlurStrength(const TextureParams& tp, double time) {
    if (!tp.blurEnabled || !std::isfinite(time)) return 0.0;
    const double f = clamp(tp.blurFrequency, 0.0, 1.0, 0.0);
    const double D = clamp(tp.blurDuration, 12.0, 1.0e4, 24.0);
    const double br = clamp(tp.blurBreathing, 0.0, 1.0, 0.0);
    const uint32_t seed = hu(uint32_t(tp.blurSeed) * 0x9e3779b1U ^ 0x1b873593U);
    const double t = std::max(-1.0e9, std::min(1.0e9, time));
    double e = 0.0;
    if (f > 1e-4) {
        const double S = D / f;                       // slot length: one event per slot (90 %)
        const double k0 = std::floor(t / S);
        for (int j = -2; j <= 0; ++j) {
            const double k = k0 + j;
            const uint32_t hk = key(seed, frameKey(k));
            if (!(u01(hk, 1U) < 0.9)) continue;
            const double len = std::max(18.0, D * (0.5 + u01(hk, 2U)));   // >= 18 frames: room to ease
            const double start = k * S + u01(hk, 3U) * std::max(0.0, S - len);
            const double tt = (t - start) / len;
            if (!(tt > 0.0 && tt < 1.0)) continue;
            // ease in over >= 8 frames (35 % of the event), ease out over >= 9 frames (40 %): focus drifts,
            // it does not snap. The ^1.6 starts and ends the drift gently: a fraction of a pixel of
            // defocus already removes the finest detail, so a linear start reads as a sudden change.
            const double rise = std::min(0.45, std::max(0.35, 8.0 / len));
            const double fall = std::min(0.50, std::max(0.40, 9.0 / len));
            const double env = std::pow(smoothstep(0.0, rise, tt) * (1.0 - smoothstep(1.0 - fall, 1.0, tt)), 1.6);
            e = std::max(e, (0.55 + 0.45 * u01(hk, 4U)) * env);
        }
    }
    double b = 0.0;
    if (br > 0.0) b = 0.4 * br * std::min(1.0, std::max(0.0, 0.5 + 0.35 * smoothNoise(t / 36.0, seed ^ 0x51ed270bU)));
    return 1.0 - (1.0 - e) * (1.0 - b);
}

FilmBlurDesign designFilmBlur(const TextureParams& tp, int w, int h, float pixelScale) {
    FilmBlurDesign fb;
    const double s = filmBlurStrength(tp, tp.time);
    if (!(s > 0.0)) return fb;
    const double ps = (std::isfinite(pixelScale) && pixelScale > 1e-4f) ? double(pixelScale) : 1.0;
    const double r = clamp(tp.blurRadius, 0.0, 50.0, 0.0);
    const double c = clamp(tp.blurChromatic, 0.0, 1.0, 0.0);
    const double sig = s * 0.5 * r * ps;
    fb.sigma[0] = sig * (1.0 + 0.35 * c);
    fb.sigma[1] = sig;
    fb.sigma[2] = sig * (1.0 + 0.6 * c);
    const double smax = std::max(fb.sigma[0], std::max(fb.sigma[1], fb.sigma[2]));
    if (!(smax >= 0.05)) return fb;
    fb.on = true;
    int L = 0;
    auto inherent = [](int l) { const double s4 = std::ldexp(1.0, 2 * l); return l == 0 ? 0.0 : 0.25 * (s4 - 1.0) + s4 / 3.0; };
    if (smax > kMaxBlurSigma) {
        int maxL = 1;   // deepest useful level: the image is a single pixel there
        while (maxL < 12 && (levelSize(w, maxL) > 1 || levelSize(h, maxL) > 1)) ++maxL;
        for (L = 1; L < maxL; ++L)
            if ((smax * smax - inherent(L)) / std::ldexp(1.0, 2 * L) <= kMaxBlurSigma * kMaxBlurSigma) break;
    }
    fb.level = L;
    const double s4 = std::ldexp(1.0, 2 * L);
    for (int ch = 0; ch < 3; ++ch)
        fb.g[ch] = gaussKernel(std::sqrt(std::max(0.0, (fb.sigma[ch] * fb.sigma[ch] - inherent(L)) / s4)));
    fb.g[3] = fb.g[1];
    return fb;
}

}} // namespace cel::tex
