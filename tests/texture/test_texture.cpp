// Celluloid Pro - texture unit tests (texture agent).
// Grain statistics, determinism, animation, resolution independence, halation/bloom energy,
// acutance, vignette, weave, tone map, NaN robustness, edge cases (1x1, odd sizes, negative and
// padded strides), plan structure, buffer pool reuse, and timings at 1080p / 2160p.
//
// Usage: test_texture [--quick] [--no-timing | --timing-only]
#include "TexTestUtil.h"
#include "core/Core.h"
#include <algorithm>
#include <cstring>
#include <functional>
#include <map>
#include <set>

using namespace textest;
using cel::tex::GrainFormat;
using cel::tex::TextureParams;
namespace eng = cel::engine;

namespace {

int g_fail = 0, g_checks = 0;
bool g_quick = false;

#define CHECK(cond, ...) do { ++g_checks; if (!(cond)) { ++g_fail; std::printf("  FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

void section(const char* s) { std::printf("[%s]\n", s); }

const GrainFormat kFormats[5] = {GrainFormat::Super8, GrainFormat::Mm16, GrainFormat::Super35, GrainFormat::Mm65, GrainFormat::Stills135};

TextureParams grainParams(GrainFormat f, float soft, double time = 0.0, float ps = 1.f) {
    TextureParams tp;
    tp.grainEnabled = true; tp.grainFormat = f; tp.grainSoftness = soft; tp.time = time; tp.pixelScale = ps;
    return tp;
}
TextureParams everythingOn(float ps) {
    TextureParams tp;
    tp.pixelScale = ps; tp.time = 17;
    tp.weaveAmount = 1.f; tp.acutanceAmount = 0.3f; tp.acutanceRadius = 2.f;
    tp.halationAmount = 0.5f; tp.bloomAmount = 0.15f; tp.vignette = 0.5f;
    tp.grainEnabled = true;
    return tp;
}

struct Moments { double mean[3], var[3], corr[3]; };   // corr: rg, gb, rb
Moments moments(const Img& g) {
    Moments m{};
    double s[3] = {0, 0, 0}, ss[3] = {0, 0, 0}, x[3] = {0, 0, 0};
    const size_t n = size_t(g.w) * g.h;
    for (size_t i = 0; i < n; ++i) {
        const float* p = &g.px[i * 4];
        for (int c = 0; c < 3; ++c) { s[c] += p[c]; ss[c] += double(p[c]) * p[c]; }
        x[0] += double(p[0]) * p[1]; x[1] += double(p[1]) * p[2]; x[2] += double(p[0]) * p[2];
    }
    for (int c = 0; c < 3; ++c) { m.mean[c] = s[c] / n; m.var[c] = ss[c] / n - m.mean[c] * m.mean[c]; }
    auto cr = [&](double sxy, int a, int b) { return (sxy / n - m.mean[a] * m.mean[b]) / std::sqrt(m.var[a] * m.var[b]); };
    m.corr[0] = cr(x[0], 0, 1); m.corr[1] = cr(x[1], 1, 2); m.corr[2] = cr(x[2], 0, 2);
    return m;
}

// normalised autocorrelation of channel c at horizontal/vertical lag d (averaged)
double autocorr(const Img& g, int c, double lag) {
    int d0 = int(std::floor(lag));
    double t = lag - d0;
    auto ac = [&](int d) {
        if (d == 0) return 1.0;
        double s = 0, v = 0, m = 0; size_t n = 0;
        for (int y = 0; y < g.h; ++y) for (int x = 0; x < g.w; ++x) m += g.at(x, y)[c];
        m /= double(g.w) * g.h;
        for (int y = 0; y + d < g.h; y += 1)
            for (int x = 0; x + d < g.w; x += 1) {
                double a0 = g.at(x, y)[c] - m;
                s += a0 * (g.at(x + d, y)[c] - m) + a0 * (g.at(x, y + d)[c] - m);
                v += 2.0 * a0 * a0; ++n;
            }
        return s / v;
    };
    return (1 - t) * ac(d0) + t * ac(d0 + 1);
}
// lag where the autocorrelation falls to 0.5 (linear interpolation between integer lags)
double halfWidth(const Img& g, int c) {
    double prev = 1.0;
    for (int d = 1; d < 64; ++d) {
        double a = autocorr(g, c, d);
        if (a < 0.5) return (d - 1) + (prev - 0.5) / (prev - a);
        prev = a;
    }
    return 64.0;
}

bool allFinite(const Img& im) {
    for (float v : im.px) if (!std::isfinite(v)) return false;
    return true;
}
bool sameBits(const Img& a, const Img& b) {
    return a.w == b.w && a.h == b.h && std::memcmp(a.px.data(), b.px.data(), a.px.size() * sizeof(float)) == 0;
}

// smooth test scene (scene-linear): gradients, a bright window, lamps, fine texture
Img testScene(int W, int H) {
    Img im(W, H);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            float* p = im.at(x, y);
            double u = (x + 0.5) / W, v = (y + 0.5) / H;
            double base = 0.02 + 0.3 * u * v + 0.05 * std::sin(x * 0.7) * std::sin(y * 0.9);
            p[0] = float(base * 1.1); p[1] = float(base); p[2] = float(base * 0.9); p[3] = float(0.5 + 0.5 * u);
            if (u > 0.6 && u < 0.85 && v > 0.15 && v < 0.5) { p[0] = 7.f; p[1] = 7.f; p[2] = 7.5f; }
            double dx = u - 0.25, dy = v - 0.3;
            if (dx * dx * W * W + dy * dy * H * H < 0.0004 * W * W) { p[0] = 50.f; p[1] = 38.f; p[2] = 20.f; }
        }
    return im;
}

// ------------------------------------------------------------------------------------------------
void testGrainStatistics() {
    section("grain: normalisation, layer correlation, distribution");
    const int W = g_quick ? 1536 : 3072, H = g_quick ? 1536 : 3072;   // big canvas at pixelScale 1
    const float softs[3] = {0.f, 0.5f, 1.f};
    for (GrainFormat f : kFormats)
        for (float s : softs) {
            double mean[3] = {0, 0, 0}, var[3] = {0, 0, 0}, corr[3] = {0, 0, 0};
            const int frames = 3;
            for (int fr = 0; fr < frames; ++fr) {
                Img g = grainField(W, H, grainParams(f, s, 10 + fr));
                Moments m = moments(g);
                for (int c = 0; c < 3; ++c) { mean[c] += m.mean[c] / frames; var[c] += m.var[c] / frames; corr[c] += m.corr[c] / frames; }
            }
            std::printf("  %-12s soft %.1f  mean %+.4f %+.4f %+.4f  var %.4f %.4f %.4f  corr rg %.3f gb %.3f rb %.3f\n",
                        cel::tex::grainFormatName(f), s, mean[0], mean[1], mean[2], var[0], var[1], var[2], corr[0], corr[1], corr[2]);
            for (int c = 0; c < 3; ++c) {
                CHECK(std::fabs(mean[c]) < 0.02, "%s soft %.1f mean[%d] = %f", cel::tex::grainFormatName(f), s, c, mean[c]);
                CHECK(std::fabs(var[c] - 1.0) < 0.02, "%s soft %.1f var[%d] = %f", cel::tex::grainFormatName(f), s, c, var[c]);
                CHECK(corr[c] > 0.30 && corr[c] < 0.50, "%s soft %.1f corr[%d] = %f", cel::tex::grainFormatName(f), s, c, corr[c]);
            }
        }
    // monochrome: one field
    {
        TextureParams tp = grainParams(GrainFormat::Super35, 0.5f, 3);
        tp.grainMonochrome = true;
        Img g = grainField(1024, 1024, tp);
        Moments m = moments(g);
        CHECK(m.corr[0] > 0.9999 && m.corr[1] > 0.9999, "mono correlation %f %f", m.corr[0], m.corr[1]);
        CHECK(std::fabs(m.var[1] - 1.0) < 0.03, "mono var %f", m.var[1]);
    }
    // non-Gaussian (clumpy) distribution: positive excess kurtosis, mild positive skew at softness 0
    {
        Img g = grainField(2048, 2048, grainParams(GrainFormat::Super35, 0.f, 5));
        double m2 = 0, m3 = 0, m4 = 0; size_t n = size_t(g.w) * g.h;
        for (size_t i = 0; i < n; ++i) { double v = g.px[i * 4 + 1]; m2 += v * v; m3 += v * v * v; m4 += v * v * v * v; }
        m2 /= n; m3 /= n; m4 /= n;
        double skew = m3 / std::pow(m2, 1.5), kurt = m4 / (m2 * m2);
        std::printf("  distribution (S35, soft 0): skew %.3f kurtosis %.3f\n", skew, kurt);
        CHECK(kurt > 3.5 && kurt < 7.0, "kurtosis %f", kurt);
        CHECK(skew > 0.2 && skew < 1.2, "skew %f", skew);
    }
    // blue layer coarsest
    {
        Img g = grainField(1024, 1024, grainParams(GrainFormat::Mm16, 0.5f, 1));
        double hr = halfWidth(g, 0), hg = halfWidth(g, 1), hb = halfWidth(g, 2);
        std::printf("  16 mm layer half-widths (px): r %.3f g %.3f b %.3f\n", hr, hg, hb);
        CHECK(hb > hr && hb > hg, "blue layer must be coarsest: r %f g %f b %f", hr, hg, hb);
    }
    // format ordering of dominant size: Super8 >> 16 > S35 > 65, Stills ~ S35 * 0.75
    {
        double hw[5];
        for (int i = 0; i < 5; ++i) hw[i] = halfWidth(grainField(1024, 1024, grainParams(kFormats[i], 0.5f, 2, 1.f)), 1);
        std::printf("  green half-width by format: S8 %.2f 16 %.2f S35 %.2f 65 %.2f 135 %.2f\n", hw[0], hw[1], hw[2], hw[3], hw[4]);
        CHECK(hw[0] > 1.8 * hw[1] && hw[1] > hw[2] && hw[2] > hw[3], "format ordering");
        CHECK(hw[4] < hw[2] && hw[4] > hw[3], "stills between 65 and S35");
    }
    // softness rolls off high frequencies: neighbour correlation grows with softness
    {
        double a0 = autocorr(grainField(1024, 1024, grainParams(GrainFormat::Mm16, 0.f, 4)), 1, 1);
        double a1 = autocorr(grainField(1024, 1024, grainParams(GrainFormat::Mm16, 1.f, 4)), 1, 1);
        std::printf("  16 mm lag-1 autocorrelation: soft 0 %.3f, soft 1 %.3f\n", a0, a1);
        CHECK(a1 > a0 + 0.03, "softness must reduce HF: %f vs %f", a0, a1);
    }
}

void testGrainTemporal() {
    section("grain: determinism, animation, static mode, seed");
    const int W = 1280, H = 720;
    Img a = grainField(W, H, grainParams(GrainFormat::Super35, 0.5f, 42));
    Img b = grainField(W, H, grainParams(GrainFormat::Super35, 0.5f, 42));
    CHECK(sameBits(a, b), "same frame must be bit-identical");
    for (GrainFormat f : kFormats) {
        Img f0 = grainField(W, H, grainParams(f, 0.5f, 100));
        Img f1 = grainField(W, H, grainParams(f, 0.5f, 101));
        double s = 0, s0 = 0, s1 = 0; size_t n = size_t(W) * H;
        for (size_t i = 0; i < n; ++i) { s += double(f0.px[i * 4 + 1]) * f1.px[i * 4 + 1]; s0 += double(f0.px[i * 4 + 1]) * f0.px[i * 4 + 1]; s1 += double(f1.px[i * 4 + 1]) * f1.px[i * 4 + 1]; }
        double r = s / std::sqrt(s0 * s1);
        double tol = (f == GrainFormat::Super8) ? 0.05 : 0.02;
        std::printf("  %-12s frame-to-frame correlation %+.4f\n", cel::tex::grainFormatName(f), r);
        CHECK(std::fabs(r) < tol, "%s animated frames correlated: %f", cel::tex::grainFormatName(f), r);
    }
    // sub-frame times round to the same frame
    CHECK(sameBits(grainField(64, 64, grainParams(GrainFormat::Super35, 0.5f, 7.0)),
                   grainField(64, 64, grainParams(GrainFormat::Super35, 0.5f, 7.2))), "sub-frame time must not change grain");
    // static
    TextureParams s0 = grainParams(GrainFormat::Super35, 0.5f, 1); s0.grainAnimated = false;
    TextureParams s1 = s0; s1.time = 77;
    CHECK(sameBits(grainField(W, H, s0), grainField(W, H, s1)), "static grain must not change over time");
    TextureParams s2 = s0; s2.grainSeed = 5;
    CHECK(!sameBits(grainField(W, H, s0), grainField(W, H, s2)), "seed must change the pattern");
}

void testGrainResolution() {
    section("grain: resolution independence (autocorrelation width ~ pixelScale)");
    const GrainFormat fs[3] = {GrainFormat::Super8, GrainFormat::Mm16, GrainFormat::Super35};
    for (GrainFormat f : fs) {
        Img g1 = grainField(1024, 1024, grainParams(f, 0.5f, 3, 1.f));
        Img g2 = grainField(2048, 2048, grainParams(f, 0.5f, 3, 2.f));
        double h1 = halfWidth(g1, 1), h2 = halfWidth(g2, 1);
        double hb1 = halfWidth(g1, 2), hb2 = halfWidth(g2, 2);
        std::printf("  %-12s half-width ps1 %.3f ps2 %.3f ratio %.3f (blue %.3f)\n", cel::tex::grainFormatName(f), h1, h2, h2 / h1, hb2 / hb1);
        // at pixelScale 1 the finest formats are close to the pixel; allow a little more there
        double tol = f == GrainFormat::Super35 ? 0.12 : 0.06;
        CHECK(std::fabs(h2 / h1 - 2.0) < 2.0 * tol, "%s ratio %f", cel::tex::grainFormatName(f), h2 / h1);
        CHECK(std::fabs(hb2 / hb1 - 2.0) < 2.0 * tol, "%s blue ratio %f", cel::tex::grainFormatName(f), hb2 / hb1);
    }
    // per-pixel variance stays 1 at any scale
    Moments m = moments(grainField(1500, 1500, grainParams(GrainFormat::Mm16, 0.5f, 3, 3.3f)));
    CHECK(std::fabs(m.var[1] - 1.0) < 0.04, "variance at pixelScale 3.3: %f", m.var[1]);
}

// ------------------------------------------------------------------------------------------------
void testHalationBloom() {
    section("halation / bloom: energy behaviour");
    const int W = 960, H = 540;
    auto scene = [&](float v) {
        Img im(W, H, 0.01f, 0.01f, 0.01f);
        for (int y = 170; y < 370; ++y) for (int x = 380; x < 580; ++x) { float* p = im.at(x, y); p[0] = p[1] = p[2] = v; }
        return im;
    };
    auto added = [&](const Img& in, const Img& out, int c) {
        double s = 0;
        for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) {
            bool inside = x >= 380 && x < 580 && y >= 170 && y < 370;
            if (!inside) s += out.at(x, y)[c] - in.at(x, y)[c];
        }
        return s;
    };
    TextureParams tp; tp.pixelScale = 0.5f; tp.halationAmount = 0.5f;
    Img in = scene(10.f);
    Img out = textured(in, tp);
    // flat highlight area: untouched and untinted
    double maxDev = 0;
    for (int y = 172; y < 368; ++y) for (int x = 382; x < 578; ++x) {
        const float* p = out.at(x, y);
        maxDev = std::max(maxDev, std::max(std::fabs(p[0] - 10.0), std::max(std::fabs(p[1] - 10.0), std::fabs(p[2] - 10.0))));
    }
    std::printf("  flat highlight interior max deviation %.3g\n", maxDev);
    CHECK(maxDev < 1e-4, "flat highlight changed by %g", maxDev);
    // glow outside: warm (R > G > B), monotonic decay
    double prev = 1e9; bool mono = true;
    for (int x = 580; x < 760; x += 4) {
        const float* p = out.at(x, 270);
        double r = p[0] - 0.01;
        if (r > prev + 1e-7) mono = false;
        prev = r;
        if (x < 640) CHECK(p[0] > p[1] && p[1] > p[2] && p[2] >= 0.01f - 1e-6f, "glow colour at x=%d: %f %f %f", x, p[0], p[1], p[2]);
    }
    CHECK(mono, "halation must decay monotonically away from the edge");
    std::printf("  glow just outside edge: R %.4f G %.4f B %.4f ; 40 px out: R %.4f\n", out.at(581, 270)[0], out.at(581, 270)[1], out.at(581, 270)[2], out.at(620, 270)[0]);
    // linear in amount
    TextureParams t2 = tp; t2.halationAmount = 1.0f;
    double e1 = added(in, out, 0), e2 = added(in, textured(in, t2), 0);
    std::printf("  added red energy amount 0.5: %.2f, amount 1.0: %.2f (ratio %.4f)\n", e1, e2, e2 / e1);
    CHECK(std::fabs(e2 / e1 - 2.0) < 0.01, "halation must be linear in amount: %f", e2 / e1);
    // below threshold: nothing
    Img dim = scene(0.18f * std::exp2(1.5f));   // 1 stop below the 2.5-stop threshold (and below the knee)
    Img dimOut = textured(dim, tp);
    CHECK(std::fabs(added(dim, dimOut, 0)) < 1e-6, "no halation below threshold: %g", added(dim, dimOut, 0));
    // more energy for brighter sources, compressed at extremes
    double eA = added(scene(4.f), textured(scene(4.f), tp), 0), eB = added(scene(40.f), textured(scene(40.f), tp), 0);
    double eC = added(scene(4000.f), textured(scene(4000.f), tp), 0);
    std::printf("  added energy: src 4 -> %.2f, 40 -> %.2f, 4000 -> %.2f\n", eA, eB, eC);
    CHECK(eB > eA && eC > eB, "halation must grow with source energy");
    CHECK(eC < 4.0 * eB, "extreme energies must be compressed (%f vs %f)", eC, eB);
    // hue: G/R ratio grows from deep red to orange
    double ratio[3];
    const float hues[3] = {-1.f, 0.f, 1.f};
    for (int i = 0; i < 3; ++i) {
        TextureParams th = tp; th.halationHue = hues[i];
        Img o = textured(in, th);
        ratio[i] = (o.at(590, 270)[1] - 0.01) / (o.at(590, 270)[0] - 0.01);
    }
    std::printf("  glow G/R by hue -1/0/+1: %.3f %.3f %.3f\n", ratio[0], ratio[1], ratio[2]);
    CHECK(ratio[0] < ratio[1] && ratio[1] < ratio[2] && ratio[0] < 0.12 && ratio[2] < 0.7, "halation hue");
    // bloom: neutral and energy conserving
    TextureParams tb; tb.pixelScale = 0.5f; tb.bloomAmount = 1.f;
    Img bs = scene(3.f);
    Img bo = textured(bs, tb);
    double sumIn = 0, sumOut = 0, maxTint = 0;
    for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) {
        sumIn += bs.at(x, y)[1]; sumOut += bo.at(x, y)[1];
        maxTint = std::max(maxTint, double(std::fabs(bo.at(x, y)[0] - bo.at(x, y)[2])));
    }
    std::printf("  bloom energy in %.1f out %.1f (rel %.2e), max tint %.2g\n", sumIn, sumOut, (sumOut - sumIn) / sumIn, maxTint);
    CHECK(std::fabs(sumOut - sumIn) / sumIn < 0.01, "bloom must conserve energy");
    CHECK(maxTint < 1e-5, "bloom must be neutral");
    CHECK(bo.at(600, 270)[1] > 0.0101f && bo.at(470, 270)[1] < 3.f, "bloom spreads light outwards");
    // resolution independence of the glow: profile at 1080p equals 2x-scaled profile at 540p
    {
        TextureParams r1; r1.pixelScale = 0.5f; r1.halationAmount = 1.f; r1.bloomAmount = 0.3f;
        TextureParams r2 = r1; r2.pixelScale = 1.f;
        Img lo = scene(20.f);
        Img hi(W * 2, H * 2, 0.01f, 0.01f, 0.01f);
        for (int y = 340; y < 740; ++y) for (int x = 760; x < 1160; ++x) { float* p = hi.at(x, y); p[0] = p[1] = p[2] = 20.f; }
        Img ol = textured(lo, r1), oh = textured(hi, r2);
        double worst = 0;
        for (int d = 4; d < 160; d += 6) {
            double vl = ol.at(580 + d, 270)[0] - 0.01;
            double vh = 0.5 * ((oh.at(1160 + 2 * d, 540)[0] - 0.01) + (oh.at(1160 + 2 * d - 1, 540)[0] - 0.01));
            worst = std::max(worst, std::fabs(vh - vl) / std::max(vl, 1e-3));
        }
        std::printf("  glow profile 540p vs 1080p (scaled): max rel diff %.3f\n", worst);
        CHECK(worst < 0.12, "glow must be resolution independent (%f)", worst);
    }
}

void testAcutanceVignetteWeave() {
    section("acutance / vignette / weave / tone map");
    // acutance: flat stays flat, fine detail gains contrast, edges limited, negative softens
    {
        TextureParams tp; tp.pixelScale = 1.f; tp.acutanceAmount = 0.3f;
        Img flat(256, 256, 0.18f, 0.18f, 0.18f);
        Img fo = textured(flat, tp);
        double dev = 0;
        for (size_t i = 0; i < fo.px.size(); ++i) dev = std::max(dev, double(std::fabs(fo.px[i] - flat.px[i])));
        CHECK(dev < 1e-6, "acutance changed a flat field by %g", dev);
        Img tex(256, 256);
        for (int y = 0; y < 256; ++y) for (int x = 0; x < 256; ++x) {
            float v = float(0.18 * std::exp2(0.1 * ((x / 2 + y / 2) % 2 ? 1 : -1)));
            float* p = tex.at(x, y); p[0] = p[1] = p[2] = v; p[3] = 1;
        }
        auto contrast = [](const Img& im) { double a = std::log2(im.at(128, 128)[1]), b = std::log2(im.at(130, 128)[1]); return std::fabs(a - b); };
        Img ts = textured(tex, tp);
        TextureParams tn = tp; tn.acutanceAmount = -1.f;
        Img tsn = textured(tex, tn);
        std::printf("  2px checker log contrast: in %.4f, +0.3 -> %.4f, -1 -> %.4f\n", contrast(tex), contrast(ts), contrast(tsn));
        CHECK(contrast(ts) > contrast(tex) * 1.1, "acutance must sharpen fine detail");
        CHECK(contrast(tsn) < contrast(tex) * 0.6, "negative acutance must soften");
        // big edge: overshoot limited
        Img edge(256, 64);
        for (int y = 0; y < 64; ++y) for (int x = 0; x < 256; ++x) { float v = x < 128 ? 0.05f : 2.0f; float* p = edge.at(x, y); p[0] = p[1] = p[2] = v; p[3] = 1; }
        TextureParams t2 = tp; t2.acutanceAmount = 2.f;
        Img eo = textured(edge, t2);
        double over = 0, under = 1e9;
        for (int x = 100; x < 160; ++x) { over = std::max(over, double(eo.at(x, 32)[1])); under = std::min(under, double(eo.at(x, 32)[1])); }
        std::printf("  edge 0.05|2.0 at amount 2: max %.3f (+%.2f stop), min %.4f (%.2f stop)\n", over, std::log2(over / 2.0), under, std::log2(under / 0.05));
        CHECK(std::log2(over / 2.0) < 0.55 && std::log2(0.05 / under) < 0.55, "edge halo must stay limited");
        // large radius takes the pyramid path; must still be flat-preserving and finite
        TextureParams t3 = tp; t3.acutanceRadius = 10.f; t3.pixelScale = 2.f;
        Img big = textured(testScene(800, 450), t3);
        CHECK(allFinite(big), "acutance large radius finite");
    }
    // vignette
    {
        TextureParams tp; tp.pixelScale = 1.f; tp.vignette = 1.f;
        Img o = textured(Img(400, 200, 1.f, 1.f, 1.f), tp);
        double c = (o.at(199, 99)[1] + o.at(200, 100)[1]) * 0.5;
        double corner = o.at(0, 0)[1];
        double k = cel::tex::kVignetteCornerTan;
        double rc = std::sqrt(199.5 * 199.5 + 99.5 * 99.5) / std::sqrt(200.0 * 200 + 100 * 100);
        double expect = std::pow(1.0 / (1.0 + rc * rc * k * k), 2.0);
        std::printf("  vignette centre %.5f corner %.4f (expect %.4f, %.2f stops)\n", c, corner, expect, std::log2(corner));
        CHECK(std::fabs(c - 1.0) < 1e-4, "vignette centre");
        CHECK(std::fabs(corner - expect) < 1e-4, "vignette corner");
        // circular: same radius on x and y axes gives the same value
        CHECK(std::fabs(o.at(200 + 80, 100)[1] - o.at(200, 100 + 80)[1]) < 1e-6 ||
              std::fabs(o.at(199 + 80, 100)[1] - o.at(200, 99 + 80)[1]) < 1e-6, "vignette must be circular (aspect correct)");
        CHECK(std::fabs(o.at(280, 100)[1] - o.at(200, 180)[1]) < 2e-3, "vignette circular");
        TextureParams th = tp; th.vignette = 0.5f;
        Img oh = textured(Img(400, 200, 1.f, 1.f, 1.f), th);
        CHECK(std::fabs(oh.at(0, 0)[1] - (1.0 + expect) / 2.0) < 1e-4, "vignette amount mixes linearly");
    }
    // weave: 48-frame sequence
    {
        double s2y = 0, s2x = 0, maxy = 0, sd = 0, sdFast = 0, sdSlow = 0;
        cel::tex::WeaveOffset prev = cel::tex::weaveOffset(0, 1.f, 1.f), pf = cel::tex::weaveOffset(0, 1.f, 4.f), psl = cel::tex::weaveOffset(0, 1.f, 0.25f);
        for (int f = 0; f < 48; ++f) {
            cel::tex::WeaveOffset o = cel::tex::weaveOffset(f, 1.f, 1.f);
            s2y += o.dy * o.dy; s2x += o.dx * o.dx; maxy = std::max(maxy, std::fabs(o.dy));
            if (f) { sd += std::fabs(o.dy - prev.dy); }
            cel::tex::WeaveOffset of = cel::tex::weaveOffset(f, 1.f, 4.f), os = cel::tex::weaveOffset(f, 1.f, 0.25f);
            if (f) { sdFast += std::fabs(of.dy - pf.dy); sdSlow += std::fabs(os.dy - psl.dy); }
            prev = o; pf = of; psl = os;
        }
        double rmsY = std::sqrt(s2y / 48), rmsX = std::sqrt(s2x / 48);
        std::printf("  weave 48 frames: rms dy %.3f dx %.3f px, max |dy| %.3f, mean |step| %.3f (speed 4: %.3f, 0.25: %.3f)\n",
                    rmsY, rmsX, maxy, sd / 47, sdFast / 47, sdSlow / 47);
        CHECK(rmsY > 0.3 && rmsY < 1.0, "weave rms %f", rmsY);
        CHECK(rmsX < rmsY, "weave mostly vertical");
        CHECK(maxy < 2.0, "weave peak %f", maxy);
        CHECK(sd / 47 > 0.04 && sd / 47 < 0.35, "weave smooth but alive %f", sd / 47);
        CHECK(sdFast > sdSlow * 1.5, "weave speed");
        // long-run statistics
        double l2 = 0; const int N = 20000;
        for (int f = 0; f < N; ++f) { double v = cel::tex::weaveOffset(f, 1.f, 1.f).dy; l2 += v * v; }
        std::printf("  weave long-run rms dy %.3f px\n", std::sqrt(l2 / N));
        CHECK(std::fabs(std::sqrt(l2 / N) - 0.6) < 0.08, "weave long-run rms");
        // image is shifted by the plan's offset: track the centroid of a blob
        TextureParams tp; tp.pixelScale = 1.f; tp.weaveAmount = 2.f; tp.time = 5;
        Img blob(200, 200, 0.f, 0.f, 0.f, 1.f);
        for (int y = 0; y < 200; ++y) for (int x = 0; x < 200; ++x) {
            double d2 = (x + 0.5 - 100) * (x + 0.5 - 100) + (y + 0.5 - 100) * (y + 0.5 - 100);
            blob.at(x, y)[1] = float(std::exp(-d2 / 200.0));
        }
        Img wo = textured(blob, tp);
        double cx = 0, cy = 0, sw = 0;
        for (int y = 0; y < 200; ++y) for (int x = 0; x < 200; ++x) { double v = wo.at(x, y)[1]; cx += v * (x + 0.5); cy += v * (y + 0.5); sw += v; }
        cel::tex::WeaveOffset o = cel::tex::weaveOffset(5, 2.f, 1.f);
        std::printf("  weave blob shift (%.3f, %.3f) vs offset (%.3f, %.3f)\n", cx / sw - 100, cy / sw - 100, -o.dx, -o.dy);
        CHECK(std::fabs(cx / sw - 100 + o.dx) < 0.03 && std::fabs(cy / sw - 100 + o.dy) < 0.03, "weave shift");
    }
    // tone map (Texture-Only): HDR = exact bijection; SDR = exact below white, asymptotic to 1
    {
        double worst = 0, worstS = 0, prevS = -1e9; bool monoS = true, belowOne = true;
        for (double Y = -1.0; Y < 4.0; Y += 0.0137) {
            double r = cel::tex::toneMapToDisplay(cel::tex::toneMapToScene(Y, false), false);
            worst = std::max(worst, std::fabs(r - Y) / std::max(1e-3, std::fabs(Y)));
            if (Y < 0.9999) worstS = std::max(worstS, std::fabs(cel::tex::toneMapToDisplay(cel::tex::toneMapToScene(Y, true), true) - Y) / std::max(1e-3, std::fabs(Y)));
        }
        for (double x = 0.0; x < 1e6; x = x * 1.01 + 1e-3) {
            double y = cel::tex::toneMapToDisplay(x, true);
            if (y < prevS) monoS = false;
            if (y > 1.0) belowOne = false;
            prevS = y;
        }
        CHECK(worst < 1e-9, "HDR tone map round trip %g", worst);
        CHECK(worstS < 1e-7, "SDR tone map round trip below white %g", worstS);
        CHECK(monoS && belowOne, "SDR tone map must be monotonic and <= 1");
        CHECK(std::fabs(std::pow(cel::tex::toneMapToDisplay(0.18), 1 / 2.4) - 0.40) < 1e-6, "tone map grey");
        // kernel round trip through the pseudo-scene path with zero effect (halation below its
        // threshold): identity within float precision (SDR: below white; HDR: everywhere)
        TextureParams tp; tp.pixelScale = 1.f; tp.halationAmount = 0.5f; tp.halationThreshold = 12.f;
        Img g(512, 4);
        for (int y = 0; y < 4; ++y) for (int x = 0; x < 512; ++x) { float v = float(x) / 400.f; float* p = g.at(x, y); p[0] = v; p[1] = v * 0.5f; p[2] = v * 0.25f; p[3] = 1; }
        Img oh = textured(g, tp, true, true), os = textured(g, tp, true, false);
        double wh = 0, ws = 0, maxS = 0;
        for (size_t i = 0; i < oh.px.size(); ++i) {
            double rel = std::max(0.01, double(std::fabs(g.px[i])));
            wh = std::max(wh, double(std::fabs(oh.px[i] - g.px[i])) / rel);
            if (i % 4 != 3) maxS = std::max(maxS, double(os.px[i]));
            if (g.px[i] < 0.999f) ws = std::max(ws, double(std::fabs(os.px[i] - g.px[i])) / rel);
        }
        std::printf("  Texture-Only round trip max rel error: HDR %.2e, SDR (below white) %.2e; SDR max %.6f for input up to 1.28\n", wh, ws, maxS);
        CHECK(wh < 2e-5, "TO HDR round trip %g", wh);
        CHECK(ws < 2e-5, "TO SDR round trip %g", ws);
        CHECK(maxS <= 1.0, "TO SDR must stay <= 1 (%f)", maxS);
    }
    // Texture-Only SDR: everything on, display input with clipped whites, lamps, fine detail
    {
        const int W = 640, H = 360;
        Img d(W, H);
        for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) {
            float* p = d.at(x, y);
            double u = (x + 0.5) / W, v = (y + 0.5) / H;
            double b = 0.05 + 0.6 * u * v + 0.15 * ((x / 2 + y / 2) % 2);
            if (u > 0.55 && v < 0.5) b = 1.0;                                  // clipped window
            if ((x - 150) * (x - 150) + (y - 120) * (y - 120) < 300) b = 1.0;  // lamp
            if (y > 300 && y < 303) b = 0.98;                                   // bright line
            p[0] = float(std::min(1.0, b)); p[1] = float(std::min(1.0, b * 0.95)); p[2] = float(std::min(1.0, b * 0.9)); p[3] = 1;
        }
        TextureParams tp = everythingOn(W / 1920.f);
        tp.acutanceAmount = 2.f; tp.halationAmount = 2.f; tp.bloomAmount = 2.f; tp.halationThreshold = 0.f;
        Img o = textured(d, tp, true, false);
        double mx = 0;
        for (size_t i = 0; i < o.px.size(); ++i) if (i % 4 != 3) mx = std::max(mx, double(o.px[i]));
        Img oh = textured(d, tp, true, true);
        double mxh = 0;
        for (size_t i = 0; i < oh.px.size(); ++i) if (i % 4 != 3) mxh = std::max(mxh, double(oh.px[i]));
        std::printf("  Texture-Only, all effects at max: SDR max %.6f, HDR max %.4f\n", mx, mxh);
        CHECK(mx <= 1.0 && allFinite(o), "Texture-Only SDR output above 1: %f", mx);
        CHECK(mxh > 1.0, "HDR keeps its own range");
        // the glow still shows: the area right outside the window got warmer
        CHECK(o.at(200, 60)[0] > d.at(200, 60)[0] + 1e-3, "TO halation visible");
    }
}

// ------------------------------------------------------------------------------------------------
void testRobustness() {
    section("robustness: NaN/Inf/extreme inputs, edge sizes, strides");
    TextureParams tp = everythingOn(0.5f);
    {
        Img im = testScene(480, 270);
        const float bad[] = {NAN, INFINITY, -INFINITY, 1e30f, -1e30f, -5.f, 0.f, 3e38f};
        for (int i = 0; i < 400; ++i) {
            float* p = im.at((i * 37) % 480, (i * 53) % 270);
            p[i % 4] = bad[i % 8]; p[(i + 1) % 3] = bad[(i + 3) % 8];
        }
        Img o = textured(im, tp);
        CHECK(allFinite(o), "texture output must be finite with NaN/Inf inputs");
        Img od = textured(im, tp, true);
        CHECK(allFinite(od), "Texture-Only output must be finite");
        int nonzero = 0;
        for (int y = 0; y < 270; ++y) for (int x = 0; x < 480; ++x) nonzero += o.at(x, y)[1] != 0.f;
        CHECK(nonzero > 480 * 270 / 2, "bad pixels must not wipe the frame (%d)", nonzero);
        // all-extreme frame
        Img ex(97, 61, 1e30f, -1e30f, NAN, INFINITY);
        CHECK(allFinite(textured(ex, tp)), "all-extreme frame finite");
        CHECK(allFinite(textured(ex, tp, true)), "all-extreme frame finite (TO)");
    }
    const int sizes[][2] = {{1, 1}, {2, 1}, {1, 7}, {3, 5}, {17, 9}, {64, 1}, {1, 64}, {1921, 1081}};
    for (auto& s : sizes) {
        Img im = testScene(s[0], s[1]);
        TextureParams t = everythingOn(float(s[0]) / 1920.f);
        Img o = textured(im, t);
        Img g = grainField(s[0], s[1], t);
        Img od = textured(im, t, true);
        CHECK(allFinite(o) && allFinite(g) && allFinite(od), "size %dx%d finite", s[0], s[1]);
        // big pixel scale on a tiny image (huge radii relative to the frame)
        TextureParams t2 = everythingOn(3.f);
        CHECK(allFinite(textured(im, t2)) && allFinite(grainField(s[0], s[1], t2)), "size %dx%d with pixelScale 3 finite", s[0], s[1]);
    }
    // strides: packed vs padded vs negative (bottom-up), Full plan (stub or real colour)
    {
        const int W = 301, H = 157;
        Img src = testScene(W, H);
        TextureParams t = everythingOn(W / 1920.f);
        eng::FrameSettings fs; fs.tex = t; fs.look.grainAmount = 1.f; fs.watermark = true;
        cel::core::Pipeline pipe;
        pipe.prepare(fs.look);
        eng::Plan plan = eng::buildPlan(W, H, fs, pipe);
        std::string err;
        Img dst(W, H);
        bool ok = eng::cpuBackend().execute(plan, pipe.state(), pipe.stateVersion(), host(src), host(dst), eng::GpuContext{}, &err);
        CHECK(ok, "full plan execute: %s", err.c_str());
        CHECK(allFinite(dst), "full plan finite");
        for (int mode = 0; mode < 2; ++mode) {
            const int pad = mode == 0 ? 12 : 0;
            const int stride = W * 4 + pad;
            std::vector<float> sbuf(size_t(stride) * H, -7.f), dbuf(size_t(stride) * H, -7.f);
            // mode 0: padded top-down; mode 1: bottom-up (negative stride, data points at row 0 = last in memory)
            auto rowPtr = [&](std::vector<float>& b, int y) { return mode == 0 ? &b[size_t(y) * stride] : &b[size_t(H - 1 - y) * stride]; };
            for (int y = 0; y < H; ++y) std::memcpy(rowPtr(sbuf, y), src.at(0, y), size_t(W) * 16);
            eng::HostImage hs, hd;
            hs.w = hd.w = W; hs.h = hd.h = H;
            hs.rowStrideFloats = hd.rowStrideFloats = mode == 0 ? stride : -stride;
            hs.data = rowPtr(sbuf, 0); hd.data = rowPtr(dbuf, 0);
            ok = eng::cpuBackend().execute(plan, pipe.state(), pipe.stateVersion(), hs, hd, eng::GpuContext{}, &err);
            CHECK(ok, "stride mode %d: %s", mode, err.c_str());
            bool same = true, padOk = true;
            for (int y = 0; y < H; ++y) {
                if (std::memcmp(rowPtr(dbuf, y), dst.at(0, y), size_t(W) * 16) != 0) same = false;
                if (pad) for (int k = 0; k < pad; ++k) if (rowPtr(dbuf, y)[W * 4 + k] != -7.f) padOk = false;
            }
            CHECK(same, "stride mode %d must give bit-identical results", mode);
            CHECK(padOk, "row padding must not be written");
        }
        // invalid inputs are rejected
        eng::HostImage bad = host(dst); bad.w = W - 1;
        CHECK(!eng::cpuBackend().execute(plan, pipe.state(), 1, host(src), bad, eng::GpuContext{}, &err), "size mismatch must fail");
        eng::HostImage bs = host(src); bs.rowStrideFloats = 4;
        CHECK(!eng::cpuBackend().execute(plan, pipe.state(), 1, bs, host(dst), eng::GpuContext{}, &err), "short stride must fail");
    }
}

// ------------------------------------------------------------------------------------------------
std::vector<std::string> kernelSeq(const eng::Plan& p) {
    std::vector<std::string> v;
    for (const auto& d : p.passes) v.push_back(eng::kernelName(d.kernel));
    return v;
}
bool has(const std::vector<std::string>& v, const char* k) { return std::find(v.begin(), v.end(), k) != v.end(); }

// every internal buffer is written before it is read; no pass reads its output; dst written last
bool planValid(const eng::Plan& p, std::string* why) {
    std::set<int> written = {eng::BUF_SRC};
    for (size_t i = 0; i < p.passes.size(); ++i) {
        const auto& d = p.passes[i];
        for (int k = 0; k < CEL_PASS_MAX_INPUTS; ++k) {
            if (d.in[k] < 0) continue;
            if (d.in[k] == d.out) { *why = "pass " + std::to_string(i) + " reads its output"; return false; }
            if (!written.count(d.in[k])) { *why = "pass " + std::to_string(i) + " reads unwritten buffer"; return false; }
            const auto& b = p.buffers[size_t(d.in[k])];
            if (d.args.inW[k] != b.w || d.args.inH[k] != b.h) { *why = "input size mismatch"; return false; }
        }
        if (d.out == eng::BUF_SRC) { *why = "writes src"; return false; }
        const auto& ob = p.buffers[size_t(d.out)];
        if (d.args.outW != ob.w || d.args.outH != ob.h) { *why = "output size mismatch"; return false; }
        written.insert(d.out);
    }
    if (p.passes.empty() || p.passes.back().out != eng::BUF_DST) { *why = "last pass must write dst"; return false; }
    return true;
}

void testPlans() {
    section("plan structure");
    // kernel names
    std::set<std::string> names;
    for (int k = 0; k < eng::KID_COUNT; ++k) {
        std::string n = eng::kernelName(eng::KernelId(k));
        CHECK(n != "?" && !n.empty(), "kernel %d has no name", k);
        names.insert(n);
    }
    CHECK(int(names.size()) == eng::KID_COUNT, "kernel names unique");
    CHECK(std::string(eng::kernelName(eng::KID_COUNT)) == "?", "out of range name");
    cel::core::Pipeline pipe;
    eng::FrameSettings fs;
    std::string why;
    // ColorOnly
    fs.mode = cel::core::Mode::ColorOnly; fs.tex = everythingOn(1.f); fs.look.grainAmount = 1.f;
    auto seq = kernelSeq(eng::buildPlan(1920, 1080, fs, pipe));
    CHECK((seq == std::vector<std::string>{"decode", "color"}), "ColorOnly = decode, color");
    // Full, all texture off
    fs.mode = cel::core::Mode::Full; fs.tex = TextureParams{};
    seq = kernelSeq(eng::buildPlan(1920, 1080, fs, pipe));
    CHECK((seq == std::vector<std::string>{"decode", "color"}), "Full with texture off = decode, color");
    // Full, everything on, watermark
    fs.tex = everythingOn(1.f); fs.watermark = true;
    eng::Plan p = eng::buildPlan(1920, 1080, fs, pipe);
    seq = kernelSeq(p);
    std::printf("%s", p.debugString().c_str());
    CHECK(planValid(p, &why), "Full plan invalid: %s", why.c_str());
    CHECK(seq.front() == "decode" && seq.back() == "watermark", "Full: decode first, watermark last");
    for (const char* k : {"tx_weave", "tx_acut_luma", "tx_acut_apply", "tx_hb_prefilter", "tx_hb_composite", "tx_grain_gen", "tx_grain_finish", "color"})
        CHECK(has(seq, k), "Full plan lacks %s", k);
    CHECK(!has(seq, "tx_vignette"), "vignette must be fused into the composite");
    {
        auto it = std::find(seq.begin(), seq.end(), "color");
        const auto& cp = p.passes[size_t(it - seq.begin())];
        CHECK(cp.in[1] >= 2 && cp.args.i[0] == 1 && cp.in[2] == eng::BUF_SRC && cp.out >= 2, "color pass wiring");
        // order: weave < acutance < glow < color
        auto pos = [&](const char* k) { return std::find(seq.begin(), seq.end(), k) - seq.begin(); };
        CHECK(pos("tx_weave") < pos("tx_acut_luma") && pos("tx_acut_apply") < pos("tx_hb_prefilter") && pos("tx_hb_composite") < pos("color"), "texture pass order");
    }
    size_t memFull = 0;
    for (size_t i = 2; i < p.buffers.size(); ++i) memFull += size_t(p.buffers[i].w) * p.buffers[i].h * 16;
    std::printf("  Full plan: %zu passes, %zu buffers, %.1f MiB internal at 1080p\n", p.passes.size(), p.buffers.size(), memFull / 1048576.0);
    CHECK(memFull < size_t(7) * 1920 * 1080 * 16, "buffer reuse keeps memory bounded (%zu)", memFull);
    // grain off when amount is 0, no watermark
    fs.look.grainAmount = 0.f; fs.watermark = false;
    p = eng::buildPlan(1920, 1080, fs, pipe);
    seq = kernelSeq(p);
    CHECK(!has(seq, "tx_grain_gen") && seq.back() == "color" && p.passes.back().args.i[0] == 0 && p.passes.back().in[1] == -1, "grain amount 0 -> no grain");
    // vignette alone -> stand-alone pass
    fs.tex = TextureParams{}; fs.tex.vignette = 0.3f;
    seq = kernelSeq(eng::buildPlan(1920, 1080, fs, pipe));
    CHECK((seq == std::vector<std::string>{"decode", "tx_vignette", "color"}), "vignette alone");
    // Texture-Only
    fs.mode = cel::core::Mode::TextureOnly; fs.tex = everythingOn(1.f); fs.look.grainAmount = 1.f;
    p = eng::buildPlan(1920, 1080, fs, pipe);
    seq = kernelSeq(p);
    CHECK(planValid(p, &why), "TO plan invalid: %s", why.c_str());
    CHECK(seq.front() == "display_decode" && seq.back() == "display_finish" && has(seq, "tx_tonemap"), "Texture-Only structure");
    // every combination of effects x modes produces a valid plan
    int combos = 0;
    for (int m = 0; m < 3; ++m)
        for (int bits = 0; bits < 64; ++bits) {
            eng::FrameSettings f2;
            f2.mode = cel::core::Mode(m);
            f2.tex.pixelScale = 0.7f;
            if (bits & 1) f2.tex.weaveAmount = 1.f;
            if (bits & 2) f2.tex.acutanceAmount = -0.5f;
            if (bits & 4) f2.tex.halationAmount = 1.f;
            if (bits & 8) f2.tex.bloomAmount = 1.f;
            if (bits & 16) f2.tex.vignette = 1.f;
            if (bits & 32) { f2.tex.grainEnabled = true; f2.look.grainAmount = 1.f; }
            f2.watermark = (bits & 3) == 3;
            eng::Plan pp = eng::buildPlan(333, 177, f2, pipe);
            if (!planValid(pp, &why)) { CHECK(false, "combo m%d bits %d: %s", m, bits, why.c_str()); }
            ++combos;
        }
    std::printf("  %d mode x effect combinations validated\n", combos);
    // texture-only test plans
    p = cel::tex::buildTexturePlan(100, 50, TextureParams{}, false);
    CHECK(kernelSeq(p) == std::vector<std::string>{"tx_copy"} && planValid(p, &why), "empty texture plan = copy");
}

void testPool() {
    section("CPU backend buffer pool");
    eng::FrameSettings fs; fs.tex = everythingOn(0.4f); fs.look.grainAmount = 1.f;
    cel::core::Pipeline pipe; pipe.prepare(fs.look);
    Img src = testScene(768, 432), dst(768, 432);
    eng::Plan plan = eng::buildPlan(768, 432, fs, pipe);
    runPlan(plan, src, dst, nullptr, &pipe.state());
    size_t a1 = cel::tex::cpuBackendPoolStats().allocations;
    for (int i = 0; i < 3; ++i) { fs.tex.time = 20 + i; runPlan(eng::buildPlan(768, 432, fs, pipe), src, dst, nullptr, &pipe.state()); }
    size_t a2 = cel::tex::cpuBackendPoolStats().allocations;
    std::printf("  allocations after first frame %zu, after 3 more frames %zu, idle %.1f MiB\n", a1, a2, cel::tex::cpuBackendPoolStats().idleBytes / 1048576.0);
    CHECK(a2 == a1, "buffers must be reused across frames");
    // concurrent executes are safe and deterministic
    Img d1(768, 432), d2(768, 432);
    std::thread t1([&] { runPlan(plan, src, d1, nullptr, &pipe.state()); });
    std::thread t2([&] { runPlan(plan, src, d2, nullptr, &pipe.state()); });
    t1.join(); t2.join();
    CHECK(sameBits(d1, d2), "concurrent executes must agree");
    runPlan(plan, src, dst, nullptr, &pipe.state());
    CHECK(sameBits(d1, dst), "concurrent result equals serial result");
}

void testTimings() {
    section("timings (CPU backend, everything on)");
    unsigned hw = std::thread::hardware_concurrency();
    std::printf("  hardware threads: %u\n", hw);
    const int res[2][2] = {{1920, 1080}, {3840, 2160}};
    for (auto& r : res) {
        const int W = r[0], H = r[1];
        Img src = testScene(W, H), dst(W, H);
        eng::FrameSettings fs; fs.tex = everythingOn(W / 1920.f); fs.look.grainAmount = 1.f;
        cel::core::Pipeline pipe; pipe.prepare(fs.look);
        auto timeIt = [&](const std::function<eng::Plan()>& mk, int reps) {
            std::vector<double> t;
            for (int i = 0; i < reps; ++i) {
                double t0 = nowMs();
                eng::Plan p = mk();
                runPlan(p, src, dst, nullptr, &pipe.state());
                t.push_back(nowMs() - t0);
            }
            std::sort(t.begin(), t.end());
            return t[t.size() / 2];
        };
        int reps = g_quick ? 3 : 7;
        double full = timeIt([&] { fs.tex.time += 1; return eng::buildPlan(W, H, fs, pipe); }, reps);
        double tex = timeIt([&] { return cel::tex::buildTexturePlan(W, H, fs.tex, false); }, reps);
        double grain = timeIt([&] { return cel::tex::buildGrainPlan(W, H, fs.tex); }, reps);
        TextureParams g8 = fs.tex; g8.grainFormat = GrainFormat::Super8;
        double grain8 = timeIt([&] { return cel::tex::buildGrainPlan(W, H, g8); }, reps);
        double plan = 0;
        { double t0 = nowMs(); for (int i = 0; i < 20; ++i) eng::buildPlan(W, H, fs, pipe); plan = (nowMs() - t0) / 20; }
        const char* colorNote = "incl. colour passes as built";
        std::printf("  %dx%d: full frame %.1f ms (%s), texture passes %.1f ms, grain S35 %.1f ms, grain Super8 %.1f ms, buildPlan %.3f ms\n",
                    W, H, full, colorNote, tex, grain, grain8, plan);
        // per-kernel profile of the full plan (CPU backend profiling hook)
        if (!g_quick) {
            eng::Plan p = eng::buildPlan(W, H, fs, pipe);
            runPlan(p, src, dst, nullptr, &pipe.state());
            cel::tex::cpuBackendProfile(true);
            std::map<std::string, std::pair<double, double>> per;
            const int n = 3;
            double cpuTotal = 0;
            for (int i = 0; i < n; ++i) {
                runPlan(p, src, dst, nullptr, &pipe.state());
                for (const auto& e : cel::tex::cpuBackendLastProfile()) {
                    per[eng::kernelName(e.kernel)].first += e.wallMs / n;
                    per[eng::kernelName(e.kernel)].second += e.cpuMs / n;
                    cpuTotal += e.cpuMs / n;
                }
            }
            cel::tex::cpuBackendProfile(false);
            std::printf("    per kernel, wall ms / CPU ms (all threads):");
            for (auto& kv : per) std::printf(" %s %.1f/%.1f;", kv.first.c_str(), kv.second.first, kv.second.second);
            std::printf("\n    total CPU time per frame %.1f ms -> %.1f ms wall on %u idle cores\n", cpuTotal, cpuTotal / std::max(1u, hw), hw);
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    bool timing = true, only = false;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--timing-only")) only = true;
        if (!std::strcmp(argv[i], "--quick")) g_quick = true;
        if (!std::strcmp(argv[i], "--no-timing")) timing = false;
    }
    double t0 = nowMs();
    if (only) { testTimings(); std::printf("\n%d checks, %d failures\n", g_checks, g_fail); return g_fail ? 1 : 0; }
    testPlans();
    testGrainStatistics();
    testGrainTemporal();
    testGrainResolution();
    testHalationBloom();
    testAcutanceVignetteWeave();
    testRobustness();
    testPool();
    if (timing) testTimings();
    std::printf("\n%d checks, %d failures (%.1f s)\n", g_checks, g_fail, (nowMs() - t0) / 1000.0);
    return g_fail ? 1 : 0;
}
