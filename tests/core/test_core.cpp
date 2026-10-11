// Reafilm Pro - core colour science unit tests (plain C++ harness, no framework).
//   build: tests/core/build.sh   (or add_subdirectory(tests/core) -> target test_core, ctest "core")
//   run:   test_core [--golden <file>] [--quick]
#include "core/Core.h"
#include "core/Bake.h"
#include "core/Stocks.h"
#include "core/Spectral.h"
#include "kernels/ColorKernels.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace cel;
using namespace cel::core;

static int gFail = 0, gChecks = 0;
#define CHECK(cond, ...)                                                       \
    do {                                                                       \
        ++gChecks;                                                             \
        if (!(cond)) {                                                         \
            ++gFail;                                                           \
            if (gFail <= 400) {                                                 \
                std::printf("  FAIL %s:%d: ", __FILE__, __LINE__);             \
                std::printf(__VA_ARGS__);                                      \
                std::printf("\n");                                             \
            }                                                                  \
        }                                                                      \
    } while (0)

static double nowMs() {
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}
static bool finite3(Float3 v) { return std::isfinite(v.r) && std::isfinite(v.g) && std::isfinite(v.b); }

// ----------------------------------------------------------------------- 1. transfer functions
// 18% grey code values: computed from the cited documents (see ColorTransfer.h); cross-checked
// against colour-science 0.4.7 (reference/design notes).
static void testTransfers() {
    std::printf("[transfers]\n");
    const double greyCode[12] = {0.391007, 0.391007, 0.459318, 0.336043, 0.391007, 0.278396,
                                 0.410557, 0.423311, 0.343389, 0.383562, 0.413588, 0.18};
    double worst = 0;
    for (int sp = 0; sp < 12; ++sp) {
        float g = celc_encode_transfer(sp, 0.18f);
        CHECK(std::fabs(g - greyCode[sp]) < 2e-6, "space %d: 18%% grey code %.6f expected %.6f", sp, g, greyCode[sp]);
        float d = celc_decode_transfer(sp, float(greyCode[sp]));
        CHECK(std::fabs(d - 0.18) / 0.18 < 0.005, "space %d: grey decode %.6f", sp, d);
        // round trip over the whole useful range (scene 2^-14 .. 2^8 x 18%, plus linear segments)
        for (int k = -140; k <= 80; ++k) {
            float x = 0.18f * std::pow(2.0f, k / 10.0f);
            float y = celc_decode_transfer(sp, celc_encode_transfer(sp, x));
            // float32 kernels: relative 1e-5 down to -7 stops (x >= 0.0014); below that the code value's
            // float resolution dominates and the bound is absolute (1e-8 ~ -24 stops)
            double rel = std::fabs(y - x) / std::max(double(std::fabs(x)), 0.0014);
            worst = std::max(worst, rel);
            CHECK(rel < 1e-5, "space %d x=%g roundtrip %g rel %.2e", sp, x, y, rel);
        }
        for (float x : {-0.004f, -0.001f, 0.0f, 0.0005f}) {
            float y = celc_decode_transfer(sp, celc_encode_transfer(sp, x));
            CHECK(std::fabs(y - x) < 1e-6, "space %d x=%g linear-segment roundtrip %g", sp, x, y);
        }
    }
    std::printf("  worst round-trip relative error %.2e\n", worst);
    // full decode (incl. gamut matrix) of grey via the pipeline
    for (int sp = 0; sp < int(InputSpace::Count); ++sp) {
        Pipeline P;
        LookParams lp;
        lp.input = InputSpace(sp);
        P.prepare(lp);
        Float3 r = P.decodeToScene({float(greyCode[sp]), float(greyCode[sp]), float(greyCode[sp])});
        CHECK(std::fabs(r.r - 0.18f) < 0.0009f && std::fabs(r.g - 0.18f) < 0.0009f && std::fabs(r.b - 0.18f) < 0.0009f,
              "input %d grey decodes to %f %f %f", sp, r.r, r.g, r.b);
    }
}

// ----------------------------------------------------------------------- helpers
static LookParams lpFor(int neg, int prt, Process pr, OutputSpace out) {
    LookParams lp;
    lp.negStock = neg;
    lp.printStock = prt;
    lp.process = pr;
    lp.output = out;
    return lp;
}
// encoded output of scene-linear grey
static Float3 outGrey(const Pipeline& P, float lin) { return P.sceneToOutput({lin, lin, lin}, {0, 0, 0}); }

static double toDisplayLinear(OutputSpace o, double v) {
    switch (o) {
    case OutputSpace::Rec709_G24: return std::pow(std::max(v, 0.0), 2.4);
    case OutputSpace::Rec709_sRGB: return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
    case OutputSpace::Rec2100_PQ: return celc_pq_dec(float(v));
    case OutputSpace::Rec2100_HLG: return celc_hlg_oetf_inv(float(v));
    default: return std::pow(std::max(v, 0.0), 2.6);
    }
}

// ----------------------------------------------------------------------- 2. neutrality
static void testNeutrality() {
    std::printf("[neutrality: every stock pair x process x output]\n");
    auto negs = negativeStocks();
    int np = int(printStockNames().size());
    double worst = 0;
    int combos = 0;
    for (int n = 0; n < int(negs.size()); ++n)
        for (int pr = 0; pr < 2; ++pr)
            for (int p = 0; p < np; ++p) {
                if (pr == 1 && p > 0) continue;               // print stock unused by the scan
                if (negs[size_t(n)].reversal && (pr == 1 || p > 0)) continue;  // reversal ignores both
                for (int o = 0; o < int(OutputSpace::Count); ++o) {
                    Pipeline P;
                    P.prepare(lpFor(n, p, Process(pr), OutputSpace(o)));
                    Float3 g = outGrey(P, 0.18f);
                    ++combos;
                    // compare in display light relative to the grey level (code values for DCI: D65 sim)
                    double r = toDisplayLinear(OutputSpace(o), g.r), gg = toDisplayLinear(OutputSpace(o), g.g),
                           b = toDisplayLinear(OutputSpace(o), g.b);
                    double encSpread = std::max({g.r, g.g, g.b}) - std::min({g.r, g.g, g.b});
                    bool monoTint = negs[size_t(n)].monochrome && pr == 0;   // B&W shows the print base tint
                    if (OutputSpace(o) == OutputSpace::P3DCI_G26) {
                        // DCI white encoding with D65 simulated: grey must have the D65 white's channel ratios
                        const float* s = P.state().data();
                        double w[3];
                        for (int c = 0; c < 3; ++c) w[c] = s[CEL_CS_MOUT + c * 3] * 0.3127 / 0.3290 + s[CEL_CS_MOUT + c * 3 + 1] +
                                                         s[CEL_CS_MOUT + c * 3 + 2] * (1 - 0.3127 - 0.3290) / 0.3290;
                        double k = gg / w[1];
                        double dev = std::max({std::fabs(r / (k * w[0]) - 1), std::fabs(b / (k * w[2]) - 1)});
                        if (!monoTint) CHECK(dev < 0.02, "DCI grey not D65 neutral (%.4f) neg %d print %d", dev, n, p);
                    } else if (!monoTint) {
                        worst = std::max(worst, encSpread);
                        CHECK(encSpread <= 0.005, "neg %d print %d proc %d out %d: grey %.4f %.4f %.4f", n, p, pr, o, g.r, g.g, g.b);
                    }
                }
            }
    std::printf("  %d combinations, worst grey channel spread %.5f (code values)\n", combos, worst);
}

// ----------------------------------------------------------------------- 3. tone scale
static void testToneScale() {
    std::printf("[tone scale / SPEC 6 targets]\n");
    Pipeline P;
    P.prepare(lpFor(1, 0, Process::NegativeToPrint, OutputSpace::Rec709_G24));
    Float3 g = outGrey(P, 0.18f);
    std::printf("  CN200T+P1 Rec709: 18%% grey -> %.4f %.4f %.4f\n", g.r, g.g, g.b);
    CHECK(std::fabs(g.g - 0.40f) <= 0.03f, "grey %.4f not 0.40 +- 0.03", g.g);
    CHECK(std::max({g.r, g.g, g.b}) - std::min({g.r, g.g, g.b}) <= 0.005f, "grey not neutral");
    Float3 p6 = outGrey(P, 0.18f * 64.f), p8 = outGrey(P, 0.18f * 256.f), m6 = outGrey(P, 0.18f / 64.f);
    std::printf("  +6 stops %.4f  +8 stops %.4f  -6 stops %.4f\n", p6.g, p8.g, m6.g);
    CHECK(std::min({p6.r, p6.g, p6.b}) >= 0.93f, "+6 stops %.4f %.4f %.4f < 0.93", p6.r, p6.g, p6.b);
    CHECK(m6.g >= 0.03f && m6.g <= 0.10f && m6.r >= 0.03f && m6.r <= 0.10f && m6.b >= 0.03f && m6.b <= 0.10f,
          "-6 stops %.4f %.4f %.4f outside 0.03..0.10", m6.r, m6.g, m6.b);
    // no hard clip before +8: slope still positive at +8 stops
    Float3 a = outGrey(P, 0.18f * std::pow(2.f, 7.8f)), b = outGrey(P, 0.18f * std::pow(2.f, 8.2f));
    CHECK(b.g - a.g > 1e-4f, "hard clip before +8 (slope %.2e)", (b.g - a.g) / 0.4);
    // skin: hue within +-8 deg (CIE LCh, exposure-independent) of the scene, six skin tones x 3 exposures
    {
        const double skin[6][3] = {{72, 13, 16}, {64, 16, 20}, {56, 18, 24}, {47, 17, 24}, {37, 14, 19}, {68, 21, 11}};
        const double W[3] = {0.3127 / 0.3290, 1.0, (1 - 0.3127 - 0.3290) / 0.3290};
        spec::Mat3 toRgb = spec::npm(spec::Gamut::Rec2020).inverse(), to709 = spec::npm(spec::Gamut::Rec709);
        double worstDh = 0;
        for (auto& L : skin)
            for (double ev : {-1.0, 0.0, 1.0}) {
                double fy = (L[0] + 16) / 116, fx = fy + L[1] / 500, fz = fy - L[2] / 200;
                auto finv = [](double f) { return f > 6.0 / 29 ? f * f * f : 3 * (6.0 / 29) * (6.0 / 29) * (f - 4.0 / 29); };
                spec::Vec3 X = {finv(fx) * W[0], finv(fy) * W[1], finv(fz) * W[2]};
                spec::Vec3 rgb = toRgb * X;
                double k = std::pow(2.0, ev);
                Float3 o = P.sceneToOutput({float(rgb[0] * k), float(rgb[1] * k), float(rgb[2] * k)}, {0, 0, 0});
                spec::Vec3 lin = {std::pow(o.r, 2.4), std::pow(o.g, 2.4), std::pow(o.b, 2.4)};
                spec::Vec3 Y = to709 * lin;
                auto f = [](double t) { return t > std::pow(6.0 / 29, 3) ? std::cbrt(t) : t / (3 * (6.0 / 29) * (6.0 / 29)) + 4.0 / 29; };
                double a = 500 * (f(Y[0] / W[0]) - f(Y[1])), b = 200 * (f(Y[1]) - f(Y[2] / W[2]));
                double hin = std::atan2(L[2], L[1]), hout = std::atan2(b, a);
                double dh = std::remainder((hout - hin) * 180.0 / 3.14159265358979323846, 360.0);
                worstDh = std::max(worstDh, std::fabs(dh));
                CHECK(std::fabs(dh) <= 8.0, "skin (%g %g %g) at %+g EV: hue shift %.1f deg", L[0], L[1], L[2], ev, dh);
            }
        std::printf("  skin hue shift (6 tones x 3 exposures): worst %.1f deg\n", worstDh);
    }
    Pipeline Q;
    Q.prepare(lpFor(1, 3, Process::NegativeToPrint, OutputSpace::Rec709_G24));
    Float3 q6 = outGrey(Q, 0.18f / 64.f);
    std::printf("  P4 -6 stops %.4f\n", q6.g);
    CHECK(q6.g >= 0.10f, "P4 -6 stops %.4f not lifted >= 0.10", q6.g);
}

// monotonic -10..+10 stops, every channel, every stock pair, never > 1 (SDR)
static void testMonotonic() {
    std::printf("[monotonicity -10..+10 stops, all pairs, SDR + HDR]\n");
    auto negs = negativeStocks();
    int np = int(printStockNames().size());
    int bad = 0;
    for (int n = 0; n < int(negs.size()); ++n)
        for (int pr = 0; pr < 2; ++pr)
            for (int p = 0; p < np; ++p) {
                if ((pr == 1 || negs[size_t(n)].reversal) && p > 0) continue;
                for (OutputSpace o : {OutputSpace::Rec709_G24, OutputSpace::Rec2100_PQ, OutputSpace::Rec2100_HLG}) {
                    Pipeline P;
                    P.prepare(lpFor(n, p, Process(pr), o));
                    Float3 prev = outGrey(P, 0.18f * std::pow(2.f, -10.f));
                    float mx = 0;
                    for (int k = 1; k <= 2000; ++k) {
                        float st = -10.f + 20.f * k / 2000.f;
                        Float3 v = outGrey(P, 0.18f * std::pow(2.f, st));
                        // 5e-6 is a third of one 16-bit code value (1.5e-5). The v1.2 stocks with
                        // high print D-max (2393) or the HDR rescale of Lab Scan / Eterna leave a
                        // flat floor in the last stops before black where the encoded value
                        // wanders by ~2e-6 (float noise of the XYZ -> RGB matrix and PQ / HLG).
                        const float tol = 5e-6f;
                        bool ok = v.r >= prev.r - tol && v.g >= prev.g - tol && v.b >= prev.b - tol;
                        if (!ok) {
                            ++bad;
                            CHECK(ok, "non-monotonic neg %d print %d proc %d out %d at %.2f stops: %.6f %.6f %.6f -> %.6f %.6f %.6f",
                                  n, p, pr, int(o), st, prev.r, prev.g, prev.b, v.r, v.g, v.b);
                            break;
                        }
                        mx = std::max({mx, v.r, v.g, v.b});
                        prev = v;
                    }
                    if (o == OutputSpace::Rec709_G24) CHECK(mx <= 1.0f, "SDR output > 1 (%f) neg %d print %d", mx, n, p);
                }
            }
    std::printf("  non-monotonic ramps: %d\n", bad);
}

// CIELAB (D65) -> scene-linear Rec.2020 (Y = L*-relative luminance, white Y = 1)
static spec::Vec3 labToRec2020(double L, double a, double b) {
    const double W[3] = {0.3127 / 0.3290, 1.0, (1 - 0.3127 - 0.3290) / 0.3290};
    double fy = (L + 16) / 116, fx = fy + a / 500, fz = fy - b / 200;
    auto finv = [](double f) { return f > 6.0 / 29 ? f * f * f : 3 * (6.0 / 29) * (6.0 / 29) * (f - 4.0 / 29); };
    spec::Vec3 X = {finv(fx) * W[0], finv(fy) * W[1], finv(fz) * W[2]};
    return spec::npm(spec::Gamut::Rec2020).inverse() * X;
}

// smoothness of the tone/colour scale (no LUT-interpolation sawtooth that a later grade would turn
// into Mach bands): along fine exposure ramps the local slope of every output channel may deviate
// from its 61-sample running quadratic fit by at most 5 % of the local mean |slope| (where the channel
// moves at all; relative to the fastest channel), for every print pair and the reversal stock
static void testSmoothness() {
    std::printf("[smoothness: slope ripple along exposure ramps, all print pairs]\n");
    struct Ramp { const char* name; double L, a, b, s0, s1; bool neutral; };
    const Ramp ramps[] = {{"neutral", 0, 0, 0, -8, 8, true}, {"sky", 50, -3, -22, -3, 5, false}, {"blue", 29, 18, -50, -4, 6, false},
                          {"skin", 62, 15, 21, -6, 6, false}};
    const int N = 4096, Wn = 61;
    auto negs = negativeStocks();
    int np = int(printStockNames().size());
    double worst = 0;
    std::string wname;
    for (int n = 0; n < int(negs.size()); ++n)
        for (int p = 0; p < np; ++p) {
            if (negs[size_t(n)].reversal && p > 0) continue;
            Pipeline P;
            P.prepare(lpFor(n, p, Process::NegativeToPrint, OutputSpace::Rec709_G24));
            for (const Ramp& r : ramps) {
                spec::Vec3 c = r.neutral ? spec::Vec3{0.18, 0.18, 0.18} : labToRec2020(r.L, r.a, r.b);
                std::vector<float> o(size_t(N) * 3);
                for (int k = 0; k < N; ++k) {
                    double g = std::pow(2.0, r.s0 + (r.s1 - r.s0) * k / (N - 1));
                    Float3 v = P.sceneToOutput({float(c[0] * g), float(c[1] * g), float(c[2] * g)}, {0, 0, 0});
                    o[size_t(k) * 3] = v.r; o[size_t(k) * 3 + 1] = v.g; o[size_t(k) * 3 + 2] = v.b;
                }
                // per channel: deviation of the slope from its running mean, relative to the local
                // running mean |slope| of the FASTEST channel (a channel that hardly moves along the
                // ramp must not turn rounding-level wiggles into large relative numbers)
                std::vector<double> sl[3], sm[3], am[3];
                for (int ch = 0; ch < 3; ++ch) {
                    sl[ch].assign(size_t(N - 1), 0.0); sm[ch].assign(size_t(N - 1), 0.0); am[ch].assign(size_t(N - 1), 0.0);
                    for (int k = 0; k < N - 1; ++k) sl[ch][size_t(k)] = double(o[size_t(k + 1) * 3 + ch]) - o[size_t(k) * 3 + ch];
                    // running quadratic (Savitzky-Golay, order 2) fit of the slope: smooth curvature of
                    // the tone/colour scale is not ripple, a LUT sawtooth is
                    const int mm = Wn / 2;
                    for (int k = mm; k < N - 1 - mm; ++k) {
                        double a = 0, b = 0;
                        for (int j = -mm; j <= mm; ++j) {
                            double cj = 3.0 * (3.0 * mm * mm + 3.0 * mm - 1.0 - 5.0 * j * j) / ((2.0 * mm - 1.0) * (2.0 * mm + 1.0) * (2.0 * mm + 3.0));
                            a += cj * sl[ch][size_t(k + j)];
                            b += std::fabs(sl[ch][size_t(k + j)]);
                        }
                        sm[ch][size_t(k)] = a; am[ch][size_t(k)] = b / Wn;
                    }
                }
                std::vector<double> amax(size_t(N - 1), 0.0);
                for (int k = 0; k < N - 1; ++k) amax[size_t(k)] = std::max({am[0][size_t(k)], am[1][size_t(k)], am[2][size_t(k)]});
                std::vector<double> sorted(amax.begin() + Wn, amax.end() - Wn);
                std::nth_element(sorted.begin(), sorted.begin() + sorted.size() / 2, sorted.end());
                double med = sorted[sorted.size() / 2];
                for (int ch = 0; ch < 3; ++ch) {
                    double w = 0;
                    for (int k = Wn; k < N - 1 - Wn; ++k) {
                        float v = o[size_t(k) * 3 + ch];
                        if (v < 0.03f || v > 0.95f || amax[size_t(k)] < 0.15 * med || amax[size_t(k)] <= 0) continue;
                        w = std::max(w, std::fabs(sl[ch][size_t(k)] - sm[ch][size_t(k)]) / amax[size_t(k)]);
                    }
                    CHECK(w <= 0.05, "slope ripple %.3f > 0.05: neg %d print %d ramp %s channel %d", w, n, p, r.name, ch);
                    if (w > worst) { worst = w; wname = std::to_string(n) + "/" + std::to_string(p) + " " + r.name; }
                }
            }
        }
    std::printf("  worst slope ripple %.4f (%s)\n", worst, wname.c_str());
}

// ----------------------------------------------------------------------- 4. robustness sweep
static void testRobustness() {
    std::printf("[NaN/Inf sweep]\n");
    const float vals[] = {-1e6f, -100.f, -1.f, -0.1f, -1e-6f, 0.f, 1e-7f, 0.18f, 1.f, 1.5f, 10.f, 1e3f, 1e6f, 1e30f,
                          INFINITY, -INFINITY, NAN};
    const int nv = int(sizeof(vals) / sizeof(vals[0]));
    long n = 0, badc = 0;
    for (int neg : {1, 8, 9}) {
        for (int proc = 0; proc < 2; ++proc)
            for (OutputSpace o : {OutputSpace::Rec709_G24, OutputSpace::Rec2100_PQ, OutputSpace::Rec2100_HLG, OutputSpace::P3DCI_G26}) {
                Pipeline P;
                LookParams lp = lpFor(neg, 3, Process(proc), o);
                lp.grainAmount = 2.f;
                lp.pushPull = 3.f;
                lp.interlayer = 2.f;
                lp.negBleach = 1.f;
                lp.printBleach = 1.f;
                P.prepare(lp);
                for (int a = 0; a < nv; ++a)
                    for (int b = 0; b < nv; ++b)
                        for (int c = 0; c < nv; ++c) {
                            Float3 in = {vals[a], vals[b], vals[c]};
                            Float3 r1 = P.processColor(in);
                            Float3 r2 = P.sceneToOutput(in, {vals[c], vals[a], 3.f});
                            ++n;
                            bool ok = finite3(r1) && finite3(r2) && r1.r >= -1e-6f && r1.r <= 1.0001f && r2.g >= -1e-6f && r2.g <= 1.0001f;
                            if (!ok) ++badc;
                            CHECK(ok, "neg %d proc %d out %d in (%g %g %g) -> (%g %g %g) / (%g %g %g)", neg, proc, int(o), in.r, in.g, in.b,
                                  r1.r, r1.g, r1.b, r2.r, r2.g, r2.b);
                        }
            }
    }
    // Texture-only kernels
    Pipeline P;
    for (OutputSpace o : {OutputSpace::Rec709_G24, OutputSpace::Rec709_sRGB, OutputSpace::Rec2100_PQ, OutputSpace::Rec2100_HLG}) {
        LookParams lp;
        lp.output = o;
        lp.grainAmount = 1.f;
        P.prepare(lp);
        for (int a = 0; a < nv; ++a) {
            CelF3 d = celc_display_decode_px(P.state().data(), cel_f3(vals[a], 0.5f, vals[nv - 1 - a]));
            CelF3 f = celc_display_finish_px(P.state().data(), d, cel_f3(vals[a], 1.f, -2.f), 1);
            bool ok = std::isfinite(d.x) && std::isfinite(d.y) && std::isfinite(d.z) && std::isfinite(f.x) && std::isfinite(f.y) && std::isfinite(f.z);
            ++n;
            if (!ok) ++badc;
            CHECK(ok, "texture-only out %d value %g", int(o), vals[a]);
        }
    }
    std::printf("  %ld evaluations, %ld non-finite / out of range\n", n, badc);
}

// texture-only round trip: display_decode -> display_finish (no grain) is the identity
static void testTextureOnlyIdentity() {
    std::printf("[texture-only identity]\n");
    double worst = 0;
    for (int o = 0; o < int(OutputSpace::Count); ++o) {
        Pipeline P;
        LookParams lp;
        lp.output = OutputSpace(o);
        P.prepare(lp);
        for (int i = 0; i <= 100; ++i) {
            float v = i / 100.f;
            CelF3 d = celc_display_decode_px(P.state().data(), cel_f3(v, v * 0.5f, 1.f - v));
            CelF3 e = celc_display_finish_px(P.state().data(), d, cel_f3(0, 0, 0), 0);
            double err = std::max({std::fabs(e.x - v), std::fabs(e.y - v * 0.5f), std::fabs(e.z - (1.f - v))});
            worst = std::max(worst, err);
        }
    }
    // SDR: grain must never push a legal (<= 1) value above 1
    float mx = 0;
    for (int o = 0; o < 4; ++o) {
        Pipeline P;
        LookParams lp;
        lp.output = OutputSpace(o);
        lp.grainAmount = 3.f;
        P.prepare(lp);
        for (int i = 0; i <= 200; ++i) {
            float v = i / 200.f;
            for (float n : {-4.f, -2.f, 2.f, 4.f}) {
                CelF3 d = celc_display_decode_px(P.state().data(), cel_f3(v, v, v));
                CelF3 e = celc_display_finish_px(P.state().data(), d, cel_f3(n, n, n), 1);
                mx = std::max({mx, e.x, e.y, e.z});
            }
        }
    }
    std::printf("  worst |finish(decode(v)) - v| = %.2e; SDR max with grain %.6f\n", worst, mx);
    CHECK(mx <= 1.0f + 1e-6f, "texture-only SDR grain overshoots: %f", mx);
    CHECK(worst < 2e-5, "texture-only round trip error %.2e", worst);
}

// ----------------------------------------------------------------------- 5. golden (C++ vs NumPy)
static std::map<std::string, double> parseKV(std::istringstream& is) {
    std::map<std::string, double> kv;
    std::string tok;
    while (is >> tok) {
        auto p = tok.find('=');
        if (p != std::string::npos) kv[tok.substr(0, p)] = std::atof(tok.substr(p + 1).c_str());
    }
    return kv;
}

static void testGolden(const std::string& path) {
    std::printf("[C++ vs NumPy reference: %s]\n", path.c_str());
    std::ifstream f(path);
    CHECK(bool(f), "cannot open golden file %s", path.c_str());
    if (!f) return;
    std::string line;
    Pipeline P, Ptx;
    double worst = 0, worstDD = 0, worstDF = 0, sumErr = 0;
    long npx = 0;
    std::string worstCase;
    std::string cur;
    while (std::getline(f, line)) {
        std::istringstream is(line);
        std::string tag;
        is >> tag;
        if (tag == "hash") {
            std::string h;
            is >> h;
            CHECK(h == stocks::libraryHash(), "golden data was generated for stock library %s but C++ tables are %s: "
                  "run python3 reference/gen_tables.py && python3 reference/golden.py", h.c_str(), stocks::libraryHash());
        } else if (tag == "case") {
            is >> cur;
            auto kv = parseKV(is);
            LookParams lp;
            lp.negStock = int(kv["neg"]);
            lp.printStock = int(kv["prt"]);
            lp.process = Process(int(kv["process"]));
            lp.input = InputSpace(int(kv["input"]));
            lp.output = OutputSpace(int(kv["output"]));
            lp.exposure = float(kv["exposure"]);
            lp.pushPull = float(kv["push"]);
            lp.interlayer = float(kv["inter"]);
            lp.negBleach = float(kv["nb"]);
            lp.neutralize = float(kv["neut"]);
            lp.printerR = float(kv["pr"]);
            lp.printerG = float(kv["pg"]);
            lp.printerB = float(kv["pb"]);
            lp.printExposure = float(kv["pe"]);
            lp.printBleach = float(kv["pbl"]);
            lp.hdrPeakNits = float(kv["peak"]);
            lp.hdrUnroll = float(kv["unroll"]);
            lp.grainAmount = float(kv["grain"]);
            lp.projectionWhite = int(kv["pw"]);
            lp.printAim = int(kv["pa"]);
            lp.printRender = int(kv["prd"]);
            lp.printContrast = float(kv["pct"]);
            P.prepare(lp);
            LookParams lt = lp;
            lt.grainAmount = 1.f;
            lt.pushPull = 0.f;
            Ptx.prepare(lt);
        } else if (tag == "px") {
            double v[9];
            for (double& x : v) { std::string t; is >> t; x = std::strtod(t.c_str(), nullptr); }
            Float3 sc = P.decodeToScene({float(v[0]), float(v[1]), float(v[2])});
            Float3 o = P.sceneToOutput(sc, {float(v[3]), float(v[4]), float(v[5])});
            double e = std::max({std::fabs(o.r - v[6]), std::fabs(o.g - v[7]), std::fabs(o.b - v[8])});
            sumErr += e;
            ++npx;
            if (e > worst) { worst = e; worstCase = cur + ": " + line; }
        } else if (tag == "dd") {
            double v[6];
            for (double& x : v) is >> x;
            CelF3 d = celc_display_decode_px(P.state().data(), cel_f3(float(v[0]), float(v[1]), float(v[2])));
            double e = std::max({std::fabs(d.x - v[3]), std::fabs(d.y - v[4]), std::fabs(d.z - v[5])}) /
                       std::max(1.0, std::max({std::fabs(v[3]), std::fabs(v[4]), std::fabs(v[5])}));
            worstDD = std::max(worstDD, e);
        } else if (tag == "txg") {
            double t;
            is >> t;
            CHECK(std::fabs(Ptx.state()[CEL_CS_TX_GSIG] - t) < 1e-6, "txg %f vs %f", Ptx.state()[CEL_CS_TX_GSIG], t);
        } else if (tag == "df") {
            double v[9];
            for (double& x : v) is >> x;
            CelF3 d = celc_display_finish_px(Ptx.state().data(), cel_f3(float(v[0]), float(v[1]), float(v[2])),
                                             cel_f3(float(v[3]), float(v[4]), float(v[5])), 1);
            double e = std::max({std::fabs(d.x - v[6]), std::fabs(d.y - v[7]), std::fabs(d.z - v[8])});
            worstDF = std::max(worstDF, e);
        }
    }
    std::printf("  %ld pixels: max |C++ - NumPy| = %.2e (mean %.2e) on encoded output\n", npx, worst, sumErr / std::max(1L, npx));
    std::printf("  texture-only: display_decode rel %.2e, display_finish %.2e\n", worstDD, worstDF);
    if (worst > 2e-4) std::printf("  worst: %s\n", worstCase.c_str());
    CHECK(worst <= 2e-4, "C++ vs NumPy max abs diff %.2e > 2e-4", worst);
    CHECK(worstDD <= 1e-4, "display_decode C++ vs NumPy %.2e", worstDD);
    CHECK(worstDF <= 1e-4, "display_finish C++ vs NumPy %.2e", worstDF);
}

// ----------------------------------------------------------------------- 6. LUT format
static void testCube(const std::string& dir) {
    std::printf("[.cube export]\n");
    Pipeline P;
    P.prepare(LookParams());
    std::string path = dir + "/cel_test_\xc3\xa4\xc3\xb6\xc3\xbc/Reafilm_test.cube";   // non-ASCII folder (UTF-8)
    std::string err;
    double t0 = nowMs();
    bool ok = writeCubeLUT(P, 33, path, "Reafilm test", &err);
    double t1 = nowMs();
    CHECK(ok, "writeCubeLUT failed: %s", err.c_str());
    std::ifstream f(std::filesystem::u8path(path));   // UTF-8 path (Windows: not the ANSI code page)
    CHECK(bool(f), "cannot read back %s", path.c_str());
    std::string line;
    int size = 0, data = 0;
    bool title = false, dmin = false, dmax = false, badRange = false;
    double maxErr = 0;
    while (std::getline(f, line)) {
        if (line.rfind("TITLE", 0) == 0) title = true;
        else if (line.rfind("LUT_3D_SIZE", 0) == 0) size = std::atoi(line.c_str() + 11);
        else if (line.rfind("DOMAIN_MIN", 0) == 0) dmin = true;
        else if (line.rfind("DOMAIN_MAX", 0) == 0) dmax = true;
        else if (!line.empty() && line[0] != '#') {
            float r, g, b;
            if (std::sscanf(line.c_str(), "%f %f %f", &r, &g, &b) == 3) {
                int i = data, R = i % 33, G = (i / 33) % 33, B = i / (33 * 33);
                Float3 o = P.processColor({R / 32.f, G / 32.f, B / 32.f});
                maxErr = std::max({maxErr, double(std::fabs(o.r - r)), double(std::fabs(o.g - g)), double(std::fabs(o.b - b))});
                if (r < 0 || r > 1 || g < 0 || g > 1 || b < 0 || b > 1) badRange = true;
                ++data;
            }
        }
    }
    CHECK(title && dmin && dmax, "cube header incomplete");
    CHECK(size == 33, "LUT_3D_SIZE %d", size);
    CHECK(data == 33 * 33 * 33, "cube has %d entries", data);
    CHECK(maxErr < 1e-5, "cube entries differ from processColor by %.2e (red-fastest order)", maxErr);
    CHECK(!badRange, "cube values outside [0,1] for SDR");
    CHECK(!writeCubeLUT(P, 20, path, "x", &err), "size 20 must be rejected");
    std::printf("  33^3 export %.1f ms, max deviation %.1e\n", t1 - t0, maxErr);
}

// .cube accuracy: trilinear evaluation of the exported file (incl. the 1D shaper of Linear input)
// against processColor at random inputs, every input space (CN 200T / P1, Rec.709 2.4)
struct CubeFile {
    int n1 = 0, n3 = 0;
    double lo1 = 0, hi1 = 1, lo3 = 0, hi3 = 1;
    std::vector<float> s1, t3;          // shaper (n1 x 3), 3D (n3^3 x 3, red fastest)
};
static bool readCube(const std::string& path, CubeFile& c) {
    std::ifstream f(std::filesystem::u8path(path));
    if (!f) return false;
    std::string line;
    std::vector<float> vals;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        if (line.rfind("LUT_1D_SIZE", 0) == 0) c.n1 = std::atoi(line.c_str() + 11);
        else if (line.rfind("LUT_3D_SIZE", 0) == 0) c.n3 = std::atoi(line.c_str() + 11);
        else if (line.rfind("LUT_1D_INPUT_RANGE", 0) == 0) std::sscanf(line.c_str() + 18, "%lf %lf", &c.lo1, &c.hi1);
        else if (line.rfind("LUT_3D_INPUT_RANGE", 0) == 0) std::sscanf(line.c_str() + 18, "%lf %lf", &c.lo3, &c.hi3);
        else {
            float r, g, b;
            if (std::sscanf(line.c_str(), "%f %f %f", &r, &g, &b) == 3) { vals.push_back(r); vals.push_back(g); vals.push_back(b); }
        }
    }
    const size_t n1 = size_t(c.n1) * 3, n3 = size_t(c.n3) * c.n3 * c.n3 * 3;
    if (vals.size() != n1 + n3 || c.n3 < 2) return false;
    c.s1.assign(vals.begin(), vals.begin() + long(n1));
    c.t3.assign(vals.begin() + long(n1), vals.end());
    return true;
}
static Float3 applyCube(const CubeFile& c, Float3 in) {
    double v[3] = {in.r, in.g, in.b};
    for (int k = 0; k < 3; ++k) {
        if (c.n1 >= 2) {
            double t = std::min(std::max((v[k] - c.lo1) / (c.hi1 - c.lo1), 0.0), 1.0) * (c.n1 - 1);
            int i = std::min(int(t), c.n1 - 2);
            double f = t - i;
            v[k] = c.s1[size_t(i) * 3 + size_t(k)] * (1 - f) + c.s1[size_t(i + 1) * 3 + size_t(k)] * f;
        }
        v[k] = std::min(std::max((v[k] - c.lo3) / (c.hi3 - c.lo3), 0.0), 1.0) * (c.n3 - 1);
    }
    const int N = c.n3;
    int i[3];
    double f[3];
    for (int k = 0; k < 3; ++k) { i[k] = std::min(int(v[k]), N - 2); f[k] = v[k] - i[k]; }
    double o[3] = {0, 0, 0};
    for (int corner = 0; corner < 8; ++corner) {
        int dr = corner & 1, dg = (corner >> 1) & 1, db = (corner >> 2) & 1;
        double w = (dr ? f[0] : 1 - f[0]) * (dg ? f[1] : 1 - f[1]) * (db ? f[2] : 1 - f[2]);
        size_t idx = ((size_t(i[2] + db) * N + size_t(i[1] + dg)) * N + size_t(i[0] + dr)) * 3;
        for (int k = 0; k < 3; ++k) o[k] += w * c.t3[idx + size_t(k)];
    }
    return {float(o[0]), float(o[1]), float(o[2])};
}
static void testCubeAccuracy(const std::string& dir) {
    std::printf("[.cube accuracy: 33^3 trilinear vs processColor, CN 200T / P1, every input space]\n");
    const auto names = inputSpaceNames();
    for (int sp = 0; sp < int(InputSpace::Count); ++sp) {
        LookParams lp;
        lp.input = InputSpace(sp);
        Pipeline P;
        P.prepare(lp);
        const std::string path = dir + "/cel_cube_acc_" + std::to_string(sp) + ".cube";
        std::string err;
        CHECK(writeCubeLUT(P, 33, path, "acc", &err), "writeCubeLUT: %s", err.c_str());
        CubeFile c;
        CHECK(readCube(path, c), "cannot parse %s", path.c_str());
        if (c.n3 == 0) continue;
        const bool lin = InputSpace(sp) == InputSpace::LinearRec2020;
        CHECK(lin == (c.n1 > 0), "space %d: shaper present = %d", sp, int(c.n1 > 0));
        // inputs: code values uniform in [0,1]^3 (log cameras); scene-linear -8..+8 stops around
        // grey per channel (Linear Rec.2020)
        uint32_t rng = 12345u + uint32_t(sp);
        auto uni = [&rng]() { rng = rng * 1664525u + 1013904223u; return double(rng >> 8) / double(1u << 24); };
        std::vector<double> errs;
        const int S = 20000;
        errs.reserve(S);
        for (int k = 0; k < S; ++k) {
            Float3 in;
            if (lin) in = {float(0.18 * std::exp2(16 * uni() - 8)), float(0.18 * std::exp2(16 * uni() - 8)), float(0.18 * std::exp2(16 * uni() - 8))};
            else in = {float(uni()), float(uni()), float(uni())};
            Float3 d = P.processColor(in), l = applyCube(c, in);
            errs.push_back(std::max({std::fabs(d.r - l.r), std::fabs(d.g - l.g), std::fabs(d.b - l.b)}));
        }
        // grey ramp -8..+8 stops (Linear: the shaper must not clip highlights)
        double greyErr = 0;
        for (int k = -80; k <= 80; ++k) {
            float x = float(0.18 * std::exp2(k / 10.0));
            float e = lin ? x : celc_encode_transfer(sp, x);
            if (!lin && (e < 0.f || e > 1.f)) continue;
            Float3 d = P.processColor({e, e, e}), l = applyCube(c, {e, e, e});
            greyErr = std::max(greyErr, double(std::fabs(d.g - l.g)));
        }
        std::sort(errs.begin(), errs.end());
        double mx = errs.back(), p99 = errs[size_t(0.99 * (S - 1))], mean = 0;
        for (double e : errs) mean += e;
        mean /= S;
        std::printf("  %-34s max %.4f  p99 %.4f  mean %.4f  grey ramp %.4f%s\n", names[size_t(sp)].c_str(), mx, p99, mean, greyErr,
                    lin ? "  (1D shaper)" : "");
        // F-Log2 C (primary camera): the no-hold baseline level (review: p99 0.031)
        if (InputSpace(sp) == InputSpace::FLog2C) CHECK(p99 <= 0.035, "F-Log2 C 33^3 LUT p99 error %.4f > 0.035", p99);
        CHECK(p99 <= 0.06, "space %d: 33^3 LUT p99 error %.4f", sp, p99);
        CHECK(greyErr <= 0.015, "space %d: grey ramp LUT error %.4f", sp, greyErr);
        std::error_code ec;
        std::filesystem::remove(std::filesystem::u8path(path), ec);
    }
}

// Hue hold: the chroma hold is monotonic by construction. Along scene saturation ramps at constant
// luminance (12 hues of Rec.2020 and of Rec.709 primaries, -3/0/+3/+6 stops, every stock pair, the
// Lab Scan and reversal), the chroma of the held film colour (CIELAB units, relative to the film's
// own neutral at the same exposure and luminance: the frame the hold works in, so a tinted print
// white does not count as chroma) never falls where the film's own chroma rises (0.2 C* tolerance),
// and where the film itself folds at the gamut edge the hold amplifies that by no more than its
// gain (<= 1.6). (Display C* is not tested: the display gamut mapping alone already folds such
// ramps for saturated blues and greens.)
static double holdChroma(const float* s, CelF3 X, float xref) {
    CelF3 nt = celc_lut1_3(s, celc_i(s, CEL_CS_HOLD_N_OFF), celc_i(s, CEL_CS_HOLD_N_N), CEL_HOLD_X0, CEL_HOLD_X1, cel_f3(xref, xref, xref));
    float fx = celc_labf(X.x / CEL_D65_X), fz = celc_labf(X.z / CEL_D65_Z);
    float nx = celc_labf(nt.x * X.y / CEL_D65_X), nz = celc_labf(nt.z * X.y / CEL_D65_Z);
    return std::hypot(500.0 * (fx - nx), 200.0 * (nz - fz));
}
static void testHoldMonotonic() {
    std::printf("[hue hold: chroma along saturation ramps, every pair / Lab Scan / reversal]\n");
    const auto negs = negativeStocks();
    const int npr = int(printStockNames().size());
    const spec::Vec3 lum = {0.26270021, 0.67799807, 0.05930172};
    // Rec.709 -> Rec.2020 (BT.2087)
    const double M709[9] = {0.627403896, 0.329283039, 0.043313065, 0.069097289, 0.919540395, 0.011362316,
                            0.016391439, 0.088013308, 0.895595253};
    int sweeps = 0, bad = 0;
    double worst = 0;
    for (int ni = 0; ni < int(negs.size()); ++ni) {
        std::vector<std::pair<int, Process>> cfg;
        if (negs[size_t(ni)].reversal) cfg.push_back({0, Process::NegativeToPrint});
        else {
            for (int pj = 0; pj < npr; ++pj) cfg.push_back({pj, Process::NegativeToPrint});
            cfg.push_back({0, Process::NegativeScan});
        }
        for (auto [pj, pr] : cfg) {
            Pipeline P;
            P.prepare(lpFor(ni, pj, pr, OutputSpace::Rec709_G24));
            const float* s = P.state().data();
            if (!(s[CEL_CS_HOLD] > 0.f)) continue;
            for (int set = 0; set < 2; ++set)
                for (int st : {-3, 0, 3, 6})
                    for (int h = 0; h < 360; h += 30) {
                        double hh = h / 60.0, f = hh - std::floor(hh);
                        int seg = int(std::floor(hh)) % 6;
                        const double pr6[6][3] = {{1, f, 0}, {1 - f, 1, 0}, {0, 1, f}, {0, 1 - f, 1}, {f, 0, 1}, {1, 0, 1 - f}};
                        double p[3] = {pr6[seg][0], pr6[seg][1], pr6[seg][2]};
                        if (set == 1) {
                            double q[3];
                            for (int r = 0; r < 3; ++r) q[r] = M709[r * 3] * p[0] + M709[r * 3 + 1] * p[1] + M709[r * 3 + 2] * p[2];
                            for (int r = 0; r < 3; ++r) p[r] = q[r];
                        }
                        double Y = p[0] * lum[0] + p[1] * lum[1] + p[2] * lum[2];
                        double Ys = 0.18 * std::exp2(double(st));
                        double c0 = 0, c1 = 0;
                        ++sweeps;
                        bool fail = false;
                        for (int k = 0; k <= 40; ++k) {
                            double sat = k / 40.0;
                            CelF3 rgb = cel_f3(float(Ys * ((1 - sat) + sat * p[0] / Y)), float(Ys * ((1 - sat) + sat * p[1] / Y)),
                                               float(Ys * ((1 - sat) + sat * p[2] / Y)));
                            CelcFilm fm = celc_film_xyz(s, rgb, cel_f3(0, 0, 0), 0);
                            CelF3 Xh = celc_hold(s, fm.X, fm.xref, rgb);
                            double a0 = holdChroma(s, fm.X, fm.xref), a1 = holdChroma(s, Xh, fm.xref);
                            if (k > 0) {
                                double d0 = a0 - c0, d1 = a1 - c1;
                                double ex = d1 - std::min(1.6 * d0, 0.0);
                                worst = std::min(worst, ex);
                                if (ex < -0.2) fail = true;
                            }
                            c0 = a0;
                            c1 = a1;
                        }
                        if (fail) {
                            ++bad;
                            CHECK(false, "hold folds chroma: neg %d print %d process %d set %d %+d stops hue %d", ni, pj, int(pr), set, st, h);
                        }
                    }
        }
    }
    std::printf("  %d ramps, %d with a hold-made chroma fold (worst step %.3f C*)\n", sweeps, bad, worst);
}

// ----------------------------------------------------------------------- 7. HDR
static void testHDR() {
    std::printf("[HDR PQ / HLG]\n");
    for (float peak : {300.f, 1000.f, 4000.f}) {
        Pipeline P;
        LookParams lp = lpFor(1, 0, Process::NegativeToPrint, OutputSpace::Rec2100_PQ);
        lp.hdrPeakNits = peak;
        P.prepare(lp);
        Float3 g = outGrey(P, 0.18f);
        double gn = celc_pq_dec(g.g);
        double mx = 0, prev = -1;
        bool mono = true;
        for (int k = 0; k <= 400; ++k) {
            float st = -10.f + 26.f * k / 400.f;
            Float3 v = outGrey(P, 0.18f * std::pow(2.f, st));
            double n = celc_pq_dec(std::max({v.r, v.g, v.b}));
            if (n < prev - 1e-3) mono = false;
            prev = n;
            mx = std::max(mx, n);
        }
        Float3 hi = outGrey(P, 0.18f * 4096.f);
        double hiN = celc_pq_dec(hi.g);
        std::printf("  PQ peak %4.0f: grey %.1f nits, +12 stops %.0f nits, max %.0f nits\n", peak, gn, hiN, mx);
        CHECK(std::fabs(gn - 15.0) < 1.5, "PQ grey %.2f nits not ~15", gn);
        CHECK(mono, "PQ ramp not monotonic (peak %.0f)", peak);
        CHECK(mx <= peak * 1.0005, "PQ exceeds peak: %.1f > %.0f", mx, peak);
        CHECK(hiN >= 0.75 * peak, "PQ highlights do not reach the peak region: %.1f", hiN);
    }
    {
        Pipeline P;
        LookParams lp = lpFor(1, 0, Process::NegativeToPrint, OutputSpace::Rec2100_PQ);
        lp.hdrUnroll = 0.f;
        P.prepare(lp);
        double w = celc_pq_dec(outGrey(P, 0.18f * 4096.f).g);
        std::printf("  PQ unroll 0: +12 stops %.0f nits (SDR print look placed in HDR)\n", w);
        CHECK(w < 200, "unroll 0 should keep the SDR highlight level (got %.0f)", w);
    }
    {
        Pipeline P;
        P.prepare(lpFor(1, 0, Process::NegativeToPrint, OutputSpace::Rec2100_HLG));
        Float3 prev = outGrey(P, 0.18f / 1024.f);
        bool mono = true;
        float mx = 0;
        for (int k = 1; k <= 400; ++k) {
            Float3 v = outGrey(P, 0.18f * std::pow(2.f, -10.f + 26.f * k / 400.f));
            if (v.g < prev.g - 1e-6f) mono = false;
            mx = std::max(mx, v.g);
            prev = v;
        }
        Float3 g = outGrey(P, 0.18f);
        std::printf("  HLG: grey code %.4f, max code %.4f\n", g.g, mx);
        CHECK(mono, "HLG not monotonic");
        CHECK(mx <= 1.0f, "HLG code > 1");
    }
}

// ----------------------------------------------------------------------- 8. prepare timing / caching
static void testTiming() {
    std::printf("[prepare() timing]\n");
    bake::clearCache();
    LookParams lp;
    Pipeline P;   // constructor prepares defaults (cold)
    bake::clearCache();
    double t0 = nowMs();
    lp.negStock = 2;
    lp.printStock = 2;
    P.prepare(lp);
    double full = nowMs() - t0;
    // cache hit: only cheap params change
    // (the dev box is shared: every parameter set is timed 3x and the fastest run counts)
    double worstHit = 0, sumHit = 0;
    for (int i = 0; i < 50; ++i) {
        double best = 1e9;
        for (int rep = 0; rep < 3; ++rep) {
            lp.printerR = float(i % 7) - 3.f + 0.001f * rep;
            lp.exposure = 0.01f * i;
            lp.grainAmount = 0.02f * i;
            lp.pushPull = (i % 5) * 0.5f - 1.f;
            lp.interlayer = 0.5f + 0.02f * i;
            lp.neutralize = (i % 3) * 0.4f;
            double a = nowMs();
            P.prepare(lp);
            best = std::min(best, nowMs() - a);
        }
        worstHit = std::max(worstHit, best);
        sumHit += best;
    }
    // switching between two already baked pairs is also a cache hit
    LookParams other;
    P.prepare(other);
    double a = nowMs();
    P.prepare(lp);
    double swap = nowMs() - a;
    std::printf("  full bake (new stock pair): %.1f ms; cache hit: mean %.3f ms, worst %.3f ms; pair switch %.3f ms\n", full,
                sumHit / 50, worstHit, swap);
    CHECK(full < 150.0, "full bake %.1f ms >= 150 ms", full);
    CHECK(worstHit < 2.0, "cache-hit prepare %.3f ms >= 2 ms", worstHit);
    CHECK(swap < 2.0, "switching to a cached pair %.3f ms >= 2 ms", swap);
    // version semantics
    uint64_t v1 = P.stateVersion();
    P.prepare(lp);
    CHECK(P.stateVersion() == v1, "unchanged params must keep the state version");
    lp.printerG += 1.f;
    P.prepare(lp);
    CHECK(P.stateVersion() != v1, "changed params must change the state version");
    Pipeline Q;
    Q.prepare(lp);
    CHECK(Q.stateVersion() != P.stateVersion(), "versions must be unique across Pipeline instances");
    CHECK(Q.state() == P.state(), "same params must give identical state");
    // single-thread full bake (SPEC: one core)
}

// ----------------------------------------------------------------------- 9. behaviour checks
static void testBehaviour() {
    std::printf("[behaviour]\n");
    // printer points: +red light -> less red
    Pipeline A, B;
    LookParams lp;
    A.prepare(lp);
    lp.printerR = 4.f;
    B.prepare(lp);
    Float3 a = outGrey(A, 0.18f), b = outGrey(B, 0.18f);
    CHECK(b.r < a.r - 0.01f, "printer red +4 must reduce red (%.4f -> %.4f)", a.r, b.r);
    lp.printerR = 0.f;
    lp.printExposure = 4.f;
    B.prepare(lp);
    b = outGrey(B, 0.18f);
    CHECK(b.g > a.g + 0.01f, "printExposure +4 must brighten (%.4f -> %.4f)", a.g, b.g);
    std::printf("  printer R+4 / printExposure +4 change grey as expected (G %.4f -> %.4f)\n", a.g, b.g);
    // exposure homogeneity of the spectral stage: x2 scene -> +log10(2) on every layer
    const float* s = A.state().data();
    float worst = 0;
    for (float r : {0.05f, 0.3f, 1.2f})
        for (float g : {0.02f, 0.2f, 0.9f})
            for (float bb : {0.01f, 0.15f, 2.f}) {
                CelF3 e1 = celc_exposures(s, cel_f3(r, g, bb)), e2 = celc_exposures(s, cel_f3(2 * r, 2 * g, 2 * bb));
                worst = std::max({worst, std::fabs(e2.x - e1.x - 0.30103f), std::fabs(e2.y - e1.y - 0.30103f), std::fabs(e2.z - e1.z - 0.30103f)});
            }
    std::printf("  exposure homogeneity error %.2e (log10)\n", worst);
    CHECK(worst < 1e-5, "exposure not homogeneous (%.2e)", worst);
    // +-2 stops camera exposure slides along the curve (monotonic, distinct)
    LookParams e;
    const float gc = celc_encode_transfer(CEL_IN_FLOG2C, 0.18f);
    e.exposure = 2.f;
    B.prepare(e);
    Float3 up = B.processColor({gc, gc, gc});
    e.exposure = -2.f;
    B.prepare(e);
    Float3 dn = B.processColor({gc, gc, gc});
    std::printf("  exposure -2/0/+2: %.3f / %.3f / %.3f\n", dn.g, a.g, up.g);
    CHECK(dn.g < a.g && a.g < up.g, "exposure slide");
    // stocks distinct: max chart difference between any two negatives > threshold
    // (Delta E table is produced by reference/design.py deltae -> docs/STOCKS.md)
    // grain: amount 0 vs noise has no effect; amount > 0 changes output
    LookParams gl;
    gl.grainAmount = 1.f;
    B.prepare(gl);
    Float3 g0 = B.sceneToOutput({0.18f, 0.18f, 0.18f}, {0, 0, 0}), g1 = B.sceneToOutput({0.18f, 0.18f, 0.18f}, {1, 1, 1});
    CHECK(std::fabs(g0.g - g1.g) > 1e-3f, "grain has no effect");
    Float3 n0 = A.sceneToOutput({0.18f, 0.18f, 0.18f}, {1, 1, 1});
    CHECK(std::fabs(n0.g - a.g) < 1e-6f, "grain amount 0 must ignore noise");
    // name lists
    CHECK(inputSpaceNames().size() == size_t(InputSpace::Count), "input names");
    CHECK(outputSpaceNames().size() == size_t(OutputSpace::Count), "output names");
    CHECK(negativeStocks().size() == 26 && printStockNames().size() == 11, "stock counts");
    // v1.2 appended the data-sheet stocks; the shipped-default look is 250D 5207 + P6
    CHECK(negativeStocks()[12].name == "Kodak Vision3 250D 5207" && printStockNames()[8] == "P6 Modern Print", "v1.2 stocks appended");
    CHECK(negativeStocks()[19].name == "Kodak Vision2 500T 5218" && negativeStocks()[21].name == "Kodak Double-X 5222" &&
              negativeStocks()[22].name == "Kodak Vision2 Expression 500T 5229" && negativeStocks()[25].name == "Fujifilm Reala 500D 8592",
          "v1.3 stocks appended");
    // v1.3: print 7 keeps the v1.2 3513DI curve under an honest name; the corrected 3513DI is appended
    CHECK(printStockNames()[7] == "Fujifilm Eterna-CP 3513DI (v1.2 legacy curve)" && printStockNames()[9] == "Fujifilm Eterna-CP 3521XD" &&
              printStockNames()[10] == "Fujifilm Eterna-CP 3513DI",
          "v1.3 prints appended");
    CHECK(negativeStocks()[1].name == "CN 200T Studio Tungsten" && printStockNames()[0] == "P1 Standard Projection", "defaults");
}

// ----------------------------------------------------------------------- v1.3 print lab
// projection white / process aim / print contrast / rendering on the data-sheet prints (2383, 2393, 3513DI):
// monotonic ramps, SDR <= 1, grey on the projection white, aim ordering, the default path unchanged
static void testPrintLab() {
    std::printf("[print lab: projection white x aim x contrast x rendering on 2383 / 2393 / 3513DI]\n");
    auto pn = printStockNames();
    int combos = 0, bad = 0;
    double worstGrey = 0;
    auto slopeAt = [](const Pipeline& P) { return (outGrey(P, 0.36f).g - outGrey(P, 0.09f).g) / 2.f; };
    for (int prt : {5, 6, 7}) {
        CHECK(prt < int(pn.size()), "print %d missing", prt);
        for (int neg : {12, 14, 9})
            for (int w = 0; w < int(ProjectionWhite::Count); ++w)
                for (int a = 0; a < int(PrintAim::Count); ++a)
                    for (int r = 0; r < int(PrintRender::Count); ++r)
                        for (float c : {-1.f, 0.f, 0.7f}) {
                            if (c != 0.f && (a != 0 || w != 0)) continue;
                            LookParams lp = lpFor(neg, prt, Process::NegativeToPrint, OutputSpace::Rec709_G24);
                            lp.projectionWhite = w;
                            lp.printAim = a;
                            lp.printRender = r;
                            lp.printContrast = c;
                            Pipeline P;
                            P.prepare(lp);
                            ++combos;
                            Float3 prev = outGrey(P, 0.18f * std::pow(2.f, -10.f));
                            float mx = 0;
                            for (int k = 1; k <= 400; ++k) {
                                Float3 v = outGrey(P, 0.18f * std::pow(2.f, -10.f + 20.f * k / 400.f));
                                bool ok = finite3(v) && v.r >= prev.r - 5e-6f && v.g >= prev.g - 5e-6f && v.b >= prev.b - 5e-6f;
                                if (!ok) {
                                    ++bad;
                                    CHECK(ok, "print lab non-monotonic neg %d prt %d w %d a %d r %d c %.1f at step %d", neg, prt, w, a, r, c, k);
                                    break;
                                }
                                mx = std::max({mx, v.r, v.g, v.b});
                                prev = v;
                            }
                            CHECK(mx <= 1.0f, "print lab SDR > 1 (%f) neg %d prt %d w %d a %d r %d", mx, neg, prt, w, a, r);
                            Float3 g = outGrey(P, 0.18f);
                            if (w == 0 && neg != 9) {
                                double sp = std::max({g.r, g.g, g.b}) - std::min({g.r, g.g, g.b});
                                worstGrey = std::max(worstGrey, sp);
                                CHECK(sp <= 0.005, "print lab grey not neutral (%.4f) neg %d prt %d a %d r %d c %.1f", sp, neg, prt, a, r, c);
                            }
                            if (w != 0 && neg != 9) CHECK(g.r > g.b + 0.005f, "theatre / D60 white not warmer than D65 neg %d prt %d", neg, prt);
                        }
        // ordering: rich > nominal > soft, print contrast likewise; release softens, telecine sets a deeper black
        auto make = [&](int a, float c, int r) {
            LookParams lp = lpFor(12, prt, Process::NegativeToPrint, OutputSpace::Rec709_G24);
            lp.printAim = a;
            lp.printContrast = c;
            lp.printRender = r;
            Pipeline P;
            P.prepare(lp);
            return P;
        };
        Pipeline N = make(0, 0.f, 0), R = make(1, 0.f, 0), S = make(2, 0.f, 0), Cp = make(0, 0.5f, 0), Rl = make(0, 0.f, 1),
                 Tc = make(0, 0.f, 2);
        CHECK(slopeAt(R) > slopeAt(N) + 1e-3f && slopeAt(N) > slopeAt(S) + 1e-3f, "aim ordering prt %d: %.4f %.4f %.4f", prt, slopeAt(R),
              slopeAt(N), slopeAt(S));
        CHECK(slopeAt(Cp) > slopeAt(N), "print contrast prt %d", prt);
        CHECK(outGrey(R, 0.18f / 256.f).g < outGrey(N, 0.18f / 256.f).g, "rich aim black prt %d", prt);
        CHECK(slopeAt(Rl) < slopeAt(N), "release print softer prt %d", prt);
        CHECK(outGrey(Tc, 0.18f / 256.f).g < outGrey(N, 0.18f / 256.f).g, "telecine black prt %d", prt);
        // default values are the v1.2 path exactly
        LookParams d = lpFor(12, prt, Process::NegativeToPrint, OutputSpace::Rec709_G24);
        Pipeline D;
        D.prepare(d);
        Float3 x = outGrey(D, 0.5f), y = outGrey(N, 0.5f);
        CHECK(x.r == y.r && x.g == y.g && x.b == y.b, "print lab default not identical prt %d", prt);
    }
    CHECK(projectionWhiteNames().size() == size_t(ProjectionWhite::Count) && printAimNames().size() == size_t(PrintAim::Count) &&
              printRenderNames().size() == size_t(PrintRender::Count), "print lab names");
    for (const auto& n : negativeStocks()) CHECK(!n.provenance.empty(), "no provenance for %s", n.name.c_str());
    for (const auto& s : printStockProvenance()) CHECK(!s.empty(), "no print provenance");
    std::printf("  %d combinations, %d non-monotonic, worst D65 grey spread %.5f\n", combos, bad, worstGrey);
}

// v1.3 WP-1: pipeline stage exits (SPEC 4.7)
static void testStages() {
    std::printf("[stage exits: log scan, negative -> scene, print only]\n");
    const float pts[][3] = {{0.18f, 0.18f, 0.18f}, {0.0f, 0.0f, 0.0f}, {0.002f, 0.002f, 0.002f}, {0.02f, 0.02f, 0.02f},
                            {1.5f, 1.5f, 1.5f}, {12.f, 12.f, 12.f}, {0.30f, 0.10f, 0.05f}, {0.05f, 0.12f, 0.30f},
                            {0.10f, 0.30f, 0.08f}, {0.40f, 0.25f, 0.18f}, {0.9f, 0.02f, 0.4f}, {-0.02f, 0.3f, 0.01f}};
    double worstRt = 0;
    for (int neg : {1, 9, 12, 14, 4}) {
        for (int prt : {0, 5, 8}) {
            LookParams p;
            p.input = InputSpace::LinearRec2020;
            p.negStock = neg; p.printStock = prt;
            p.printerR = 1.5f; p.printExposure = -1.f; p.pushPull = 0.5f;
            Pipeline full, ls, po;
            full.prepare(p);
            CHECK(full.state()[CEL_CS_STAGE] == 0.f, "Full chain must not set a stage");
            LookParams q = p;
            q.stage = int(Stage::NegativeLogScan); ls.prepare(q);
            q.stage = int(Stage::PrintOnly); po.prepare(q);
            for (const auto& c : pts) {
                const Float3 in{c[0], c[1], c[2]};
                const Float3 f = full.processColor(in), l = ls.processColor(in), r = po.processColor(l);
                const double e = std::max({std::fabs(f.r - r.r), std::fabs(f.g - r.g), std::fabs(f.b - r.b)});
                worstRt = std::max(worstRt, e);
                // out-of-gamut (negative) or extremely saturated scene values (channel ratio > 20): the hue hold
                // sees the Log Scan's scene estimate, not the scene
                const float lo = std::min({c[0], c[1], c[2]}), hi = std::max({c[0], c[1], c[2]});
                const bool oog = lo < 0.f || hi > 20.f * lo;
                CHECK(e < (oog ? 4e-2 : 2e-3), "neg %d prt %d: Log Scan -> Print Only differs from the full chain by %.2e at (%g %g %g)",
                      neg, prt, e, double(c[0]), double(c[1]), double(c[2]));
                CHECK(std::isfinite(l.r) && std::isfinite(l.g) && std::isfinite(l.b), "log scan not finite");
            }
            // clear film (no exposure) at code 95 (Cineon D-min), grey within the usual printing-density range
            const Float3 b = ls.processColor({0.f, 0.f, 0.f}), g = ls.processColor({0.18f, 0.18f, 0.18f});
            CHECK(std::fabs(b.g - 95.f / 1023.f) < 2e-3, "neg %d: log scan of black %.4f, expected ~95/1023", neg, double(b.g));
            CHECK(g.g > 0.30f && g.g < 0.70f, "neg %d prt %d: log scan grey %.4f out of 0.30 .. 0.70", neg, prt, double(g.g));
        }
    }
    std::printf("  log scan -> print only vs full chain: worst %.2e\n", worstRt);
    // Negative -> Scene: 18 % grey comes back as 18 % grey in every encoding, neutrals stay neutral and
    // monotonic along the grey ramp, everything finite
    for (int neg : {1, 9, 12, 13, 15, 16}) {
        for (int e = 0; e < int(SceneEncoding::Count); ++e) {
            LookParams p;
            p.input = InputSpace::LinearRec2020;
            p.negStock = neg;
            p.process = Process::NegativeScan;   // ignored by the exits
            p.stage = int(Stage::NegativeToScene);
            p.sceneEncoding = e;
            Pipeline ns;
            ns.prepare(p);
            int tf = CEL_IN_LOGC4;
            if (e == 1) tf = CEL_IN_LOGC3; else if (e == 2) tf = CEL_IN_ACESCCT; else if (e == 3) tf = CEL_IN_DAVINCI; else if (e == 4) tf = CEL_IN_LINEAR;
            const Float3 g = ns.processColor({0.18f, 0.18f, 0.18f});
            const float want = celc_encode_transfer(tf, 0.18f);
            CHECK(std::fabs(g.r - want) < 2e-4 && std::fabs(g.g - want) < 2e-4 && std::fabs(g.b - want) < 2e-4,
                  "neg %d enc %d: grey -> (%.5f %.5f %.5f), expected %.5f", neg, e, double(g.r), double(g.g), double(g.b), double(want));
            float prev = -1e9f;
            for (int k = 0; k <= 120; ++k) {
                const float v = 0.18f * std::pow(2.f, -9.f + 0.15f * float(k));
                const Float3 o = ns.processColor({v, v, v});
                const float lin = celc_decode_transfer(tf, o.g);
                const float tol = k >= 30 ? 3e-3f : 8e-3f;   // deep toe (below -4.5 stops): within ~0.1 stop
                CHECK(std::fabs(o.r - o.g) < tol && std::fabs(o.b - o.g) < tol, "neg %d enc %d: neutral %g -> (%g %g %g)",
                      neg, e, double(v), double(o.r), double(o.g), double(o.b));
                CHECK(o.g >= prev - 1e-6f, "neg %d enc %d: grey ramp not monotonic at %g", neg, e, double(v));
                if (k >= 30 && k <= 100)   // -4.5 .. +6 stops: the film keeps the information, the scene comes back
                    CHECK(std::fabs(std::log2(std::max(lin, 1e-9f) / v)) < 0.35, "neg %d enc %d: neutral %g -> %g",
                          neg, e, double(v), double(lin));
                prev = o.g;
            }
            for (const auto& c : pts) {
                const Float3 o = ns.processColor({c[0], c[1], c[2]});
                CHECK(std::isfinite(o.r) && std::isfinite(o.g) && std::isfinite(o.b), "negative -> scene not finite");
            }
        }
    }
    // reversal stocks have no negative stage: every exit renders the full chain
    {
        LookParams p;
        p.input = InputSpace::LinearRec2020;
        p.negStock = 8;
        Pipeline a, b;
        a.prepare(p);
        p.stage = int(Stage::NegativeToScene);
        b.prepare(p);
        const Float3 x = a.processColor({0.3f, 0.2f, 0.1f}), y = b.processColor({0.3f, 0.2f, 0.1f});
        CHECK(x.r == y.r && x.g == y.g && x.b == y.b, "reversal stock: stage exit must render the full chain");
    }
}

// v1.3 Tone (SPEC 4.9): Zones, Filmic Contrast, Film Roll-off, Dye Saturation
static void testTone(bool quick) {
    std::printf("[tone: zones, filmic contrast, film roll-off, dye saturation]\n");
    // 1. neutral values (whatever the companions are) leave the state and the render bit-identical
    {
        LookParams a = lpFor(1, 0, Process::NegativeToPrint, OutputSpace::Rec709_G24), b = a;
        b.zoneShadowsRange = -5.f; b.zoneLightsRange = 0.f; b.zoneHighlightsRange = 1.5f; b.zoneSpecularRange = 8.f;
        b.zoneFalloff = 3.f; b.contrastPivot = 1.5f; b.rolloffStart = 1.f; b.dyeRichness = 0.9f;
        for (Process pr : {Process::NegativeToPrint, Process::NegativeScan}) {
            a.process = b.process = pr;
            Pipeline A, B;
            A.prepare(a); B.prepare(b);
            CHECK(A.state() == B.state(), "neutral tone settings change the state (process %d)", int(pr));
            const float* s = B.state().data();
            CHECK(s[CEL_CS_TONE_ON] == 0.f && s[CEL_CS_DSAT_ON] == 0.f && s[CEL_CS_ROLL_ON] == 0.f, "neutral tone must be off");
        }
    }
    // 2. every setting: neutral ramp monotone and neutral (as neutral as the stock's own ramp), grey kept
    //    where the setting promises it, print white and black unchanged by Dye Saturation
    struct T { const char* name; void (*set)(LookParams&); bool keepsGrey; };
    const T sets[] = {
        {"highlights -1.5 @ +2.5", [](LookParams& p) { p.zoneHighlights = -1.5f; p.zoneHighlightsRange = 2.5f; }, true},
        {"hdr recipe", [](LookParams& p) { p.zoneHighlights = -1.5f; p.zoneHighlightsRange = 2.5f; p.zoneSpecular = -1.f; }, true},
        {"all zones +-4, falloff 0.25", [](LookParams& p) { p.zoneShadows = 4.f; p.zoneLights = -4.f; p.zoneHighlights = -4.f;
                                                           p.zoneSpecular = -4.f; p.zoneFalloff = 0.25f; }, false},
        {"zones crossing", [](LookParams& p) { p.zoneShadows = -4.f; p.zoneShadowsRange = 2.f; p.zoneLights = 4.f;
                                              p.zoneLightsRange = -2.f; p.zoneSpecular = -4.f; p.zoneSpecularRange = 0.f; }, false},
        {"contrast 2", [](LookParams& p) { p.filmContrast = 2.f; }, true},
        {"contrast 0.5 pivot +3", [](LookParams& p) { p.filmContrast = 0.5f; p.contrastPivot = 3.f; }, false},
        {"roll-off +1 @ 0.5", [](LookParams& p) { p.rolloffSoftness = 1.f; p.rolloffStart = 0.5f; }, true},
        {"roll-off -1 @ 0.5", [](LookParams& p) { p.rolloffSoftness = -1.f; p.rolloffStart = 0.5f; }, true},
        {"roll-off +1 @ 8", [](LookParams& p) { p.rolloffSoftness = 1.f; p.rolloffStart = 8.f; }, true},
        {"dye saturation 2", [](LookParams& p) { p.dyeSaturation = 2.f; p.dyeRichness = 1.f; }, true},
        {"dye saturation 0 balance +1", [](LookParams& p) { p.dyeSaturation = 0.f; p.dyeSatBalance = 1.f; }, true},
        {"everything", [](LookParams& p) { p.zoneHighlights = -2.f; p.zoneShadows = 1.f; p.filmContrast = 1.4f;
                                          p.rolloffSoftness = 0.7f; p.dyeSaturation = 1.6f; p.dyeSatBalance = -0.5f; }, false},
    };
    const int negs[] = {1, 15, 8};   // CN 200T, a data-sheet negative (Eterna 250D), a reversal (RV 100 Slide)
    for (const T& t : sets)
        for (int n : negs)
            for (Process pr : {Process::NegativeToPrint, Process::NegativeScan})
                for (OutputSpace o : {OutputSpace::Rec709_G24, OutputSpace::Rec2100_PQ}) {
                    if (quick && (o != OutputSpace::Rec709_G24 || n != 1)) continue;
                    LookParams base = lpFor(n, 0, pr, o);
                    base.input = InputSpace::LinearRec2020;
                    LookParams lp = base;
                    t.set(lp);
                    Pipeline P, R;
                    P.prepare(lp); R.prepare(base);
                    Float3 prev = P.processColor({0.18f / 1024.f, 0.18f / 1024.f, 0.18f / 1024.f});
                    double worstTint = 0, refTint = 0;
                    bool mono = true;
                    for (int k = 1; k <= 1600; ++k) {
                        const float v = 0.18f * std::pow(2.f, -10.f + 22.f * k / 1600.f);
                        // the chain itself is verified up to +12 stops of scene light (PQ flattens above ~+14);
                        // a strong contrast maps +12 in to +24 scene, so stop the ramp there
                        if (P.decodeToScene({v, v, v}).g > 0.18f * 4096.f) break;
                        Float3 a = P.processColor({v, v, v}), r = R.processColor({v, v, v});
                        const float tol = 5e-6f;
                        if (!(a.r >= prev.r - tol && a.g >= prev.g - tol && a.b >= prev.b - tol)) {
                            if (mono) CHECK(false, "%s: neg %d proc %d out %d not monotone at %.2f stops", t.name, n, int(pr), int(o),
                                            -10.f + 22.f * k / 1600.f);
                            mono = false;
                        }
                        CHECK(finite3(a), "%s: non-finite", t.name);
                        prev = a;
                        worstTint = std::max(worstTint, double(std::max({a.r, a.g, a.b}) - std::min({a.r, a.g, a.b})));
                        refTint = std::max(refTint, double(std::max({r.r, r.g, r.b}) - std::min({r.r, r.g, r.b})));
                    }
                    // greys stay as neutral as the stock renders its own grey ramp (+ a small interpolation margin)
                    CHECK(worstTint <= refTint + 0.004, "%s: neg %d proc %d out %d grey ramp tint %.4f (stock %.4f)", t.name, n, int(pr),
                          int(o), worstTint, refTint);
                    if (t.keepsGrey) {
                        Float3 a = P.processColor({0.18f, 0.18f, 0.18f}), r = R.processColor({0.18f, 0.18f, 0.18f});
                        CHECK(std::fabs(a.g - r.g) < 0.004f, "%s: 18%% grey moved %.4f -> %.4f (neg %d proc %d)", t.name, r.g, a.g, n, int(pr));
                    }
                }
    // 3. Highlights pull: a +4 stop sky renders darker (into the print's straight part) while grey stays
    {
        LookParams a = lpFor(1, 0, Process::NegativeToPrint, OutputSpace::Rec709_G24);
        a.input = InputSpace::LinearRec2020;
        LookParams b = a;
        b.zoneHighlights = -1.5f; b.zoneHighlightsRange = 2.5f;
        Pipeline A, B;
        A.prepare(a); B.prepare(b);
        const float sky = 0.18f * 16.f;
        Float3 sa = A.processColor({sky, sky, sky}), sb = B.processColor({sky, sky, sky});
        std::printf("  +4 stops: %.4f -> %.4f with Highlights -1.5\n", sa.g, sb.g);
        CHECK(sb.g < sa.g - 0.05f, "Highlights -1.5 must pull a +4 stop sky down (%.4f -> %.4f)", sa.g, sb.g);
        // ratio-preserving zones: a saturated colour keeps its scene channel ratios through the zone gain
        Float3 c = {0.9f, 0.5f, 0.2f};
        Float3 d = B.decodeToScene(c);
        CHECK(std::fabs(d.r / d.g - c.r / c.g) < 1e-4f && std::fabs(d.b / d.g - c.b / c.g) < 1e-4f, "zones must keep channel ratios");
    }
    // 4. Dye Saturation: a saturated colour gets more saturated and not lighter; neutral white / black kept
    {
        LookParams a = lpFor(1, 0, Process::NegativeToPrint, OutputSpace::Rec709_G24);
        a.input = InputSpace::LinearRec2020;
        LookParams b = a;
        b.dyeSaturation = 1.5f;
        Pipeline A, B;
        A.prepare(a); B.prepare(b);
        auto lum = [](Float3 v) { return 0.2126 * std::pow(v.r, 2.4) + 0.7152 * std::pow(v.g, 2.4) + 0.0722 * std::pow(v.b, 2.4); };
        // purity: 1 - min/max of the display-linear channels (independent of how dark the colour gets)
        auto spread = [](Float3 v) { return 1.f - std::pow(std::min({v.r, v.g, v.b}) / std::max({v.r, v.g, v.b}), 2.4f); };
        // red-orange, blue, green, skin, yellow (scene-linear Rec.2020)
        const Float3 cols[] = {{0.30f, 0.06f, 0.03f}, {0.05f, 0.12f, 0.35f}, {0.08f, 0.25f, 0.05f}, {0.35f, 0.22f, 0.14f}, {0.6f, 0.5f, 0.1f}};
        double l0 = 0, l1 = 0;
        for (Float3 c : cols) {
            Float3 x = A.processColor(c), y = B.processColor(c);
            l0 += lum(x); l1 += lum(y);
            CHECK(spread(y) > std::min(spread(x) + 0.01f, 0.99f), "Dye Saturation 1.5 must saturate (%.3f -> %.3f)", spread(x), spread(y));
            // the extreme red is gamut-mapped on the way to Rec.709, so allow a few percent there
            CHECK(lum(y) <= lum(x) * 1.04, "Dye Saturation 1.5 must not brighten a saturated colour (%.4f -> %.4f)", lum(x), lum(y));
        }
        std::printf("  5 saturated colours: mean display luminance %.4f -> %.4f with Dye Saturation 1.5\n", l0 / 5, l1 / 5);
        CHECK(l1 < l0 * 0.97, "Dye Saturation 1.5 (Richness 0.25) must darken saturated colours on average");
        for (float v : {0.18f / 256.f, 0.18f * 512.f}) {
            Float3 p = A.processColor({v, v, v}), q = B.processColor({v, v, v});
            CHECK(std::fabs(p.r - q.r) < 2e-3f && std::fabs(p.g - q.g) < 2e-3f && std::fabs(p.b - q.b) < 2e-3f,
                  "Dye Saturation moved black / white (%.4f -> %.4f)", p.g, q.g);
        }
    }
}

// v1.3 ALEXA Rendering: the ALEXA Rec.709 stage and ALEXA Colour (SPEC 13, model in reference/alexa_rendering.py)
static void testAlexa() {
    std::printf("[ALEXA Rendering: ALEXA Rec.709 stage, ALEXA Colour]\n");
    auto lumOf = [](Float3 v) { return 0.2126 * v.r + 0.7152 * v.g + 0.0722 * v.b; };
    {   // off = bit-identical state (no AL block, header slots zero)
        LookParams a = lpFor(1, 0, Process::NegativeToPrint, OutputSpace::Rec709_G24), b = a;
        b.alexaColour = 0.f;
        Pipeline A, B;
        A.prepare(a); B.prepare(b);
        CHECK(A.state() == B.state() && A.state()[CEL_CS_AL_OFF] == 0.f, "ALEXA off must not change the state");
    }
    // the stage on every output: neutral ramp monotone and neutral, finite on hostile input, SDR inside [0, 1]
    for (int o = 0; o < int(OutputSpace::Count); ++o) {
        for (int neg : {1, 17}) {   // a negative and a reversal stock (both render the ALEXA stage)
            LookParams p = lpFor(neg, 0, Process::NegativeToPrint, OutputSpace(o));
            p.input = InputSpace::LinearRec2020;
            p.stage = int(Stage::AlexaRec709);
            p.alexaColour = 1.f;   // ignored in the stage
            Pipeline P;
            P.prepare(p);
            CHECK(P.state()[CEL_CS_STAGE] == 4.f && P.state()[CEL_CS_AL_COLOUR] == 0.f, "ALEXA stage state (output %d)", o);
            double prev = -1, worstN = 0;
            for (int k = 0; k <= 320; ++k) {
                const float v = 0.18f * std::exp2(-12.f + k / 16.f);
                Float3 y = P.processColor({v, v, v});
                const double l = lumOf(y);
                CHECK(l >= prev - 1e-6, "ALEXA stage grey ramp not monotone at %.5f (output %d)", v, o);
                prev = std::max(prev, l);
                worstN = std::max(worstN, double(std::max(std::fabs(y.r - y.g), std::fabs(y.b - y.g))));
            }
            // (DCI-P3 encodes the D65 neutral with the DCI white: unequal code values by design)
            CHECK(o == int(OutputSpace::P3DCI_G26) || worstN < 2e-3, "ALEXA stage greys not neutral (%.5f, output %d)", worstN, o);
            const bool sdr = o != int(OutputSpace::Rec2100_PQ) && o != int(OutputSpace::Rec2100_HLG);
            for (Float3 c : {Float3{NAN, 0.2f, 0.1f}, Float3{INFINITY, -INFINITY, 1e30f}, Float3{-1.f, 0.5f, 3.f},
                             Float3{1e6f, 0.f, 0.f}, Float3{0.f, 0.f, 50.f}, Float3{-1e-20f, 0.f, 1e-30f}}) {
                Float3 y = P.processColor(c);
                CHECK(finite3(y), "ALEXA stage not finite (output %d)", o);
                if (sdr) CHECK(y.r >= 0.f && y.r <= 1.f && y.g >= 0.f && y.g <= 1.f && y.b >= 0.f && y.b <= 1.f,
                               "ALEXA stage outside [0, 1] on SDR (output %d)", o);
            }
            if (o == int(OutputSpace::Rec709_G24) && neg == 1) {   // ARRI's LUT: grey 0.18 -> 0.398 (Gamma 2.4 code)
                const Float3 g = P.processColor({0.18f, 0.18f, 0.18f});
                std::printf("  ALEXA Rec.709 grey 18 %% -> %.4f (ARRI LUT 0.398)\n", g.g);
                CHECK(std::fabs(g.g - 0.398f) < 0.01f, "ALEXA Rec.709 grey %.4f, ARRI 0.398", g.g);
            }
        }
    }
    {   // ALEXA Colour (v1.3: fitted through the film): bands exposure-invariant, plus a per-stop tone term that keeps
        // grey luminance, leaves 18 % grey alone, tints the neutral axis only gently and smoothly, changes colour and
        // blends with the strength
        LookParams p = lpFor(1, 0, Process::NegativeToPrint, OutputSpace::Rec709_G24);
        p.input = InputSpace::LinearRec2020;
        Pipeline off, on, half;
        off.prepare(p);
        p.alexaColour = 1.f; on.prepare(p);
        p.alexaColour = 0.5f; half.prepare(p);
        auto lum2020 = [](Float3 v) { return 0.2627 * v.r + 0.6780 * v.g + 0.0593 * v.b; };
        double grey18 = 0, worstY = 0, tint = 0, jump = 0, moved = 0;
        {
            Float3 a = off.decodeToScene({0.18f, 0.18f, 0.18f}), b = on.decodeToScene({0.18f, 0.18f, 0.18f});
            grey18 = std::max({std::fabs(a.r - b.r), std::fabs(a.g - b.g), std::fabs(a.b - b.b)}) / 0.18;
        }
        Float3 prev{};
        for (int i = 0; i <= 18 * 8; ++i) {   // -8 .. +10 stops in 1/8 stop
            const float v = 0.18f * std::exp2(-8.f + float(i) / 8.f);
            Float3 a = off.decodeToScene({v, v, v}), b = on.decodeToScene({v, v, v});
            worstY = std::max(worstY, std::fabs(lum2020(b) / lum2020(a) - 1.0));
            Float3 r{float(b.r / lum2020(b)), float(b.g / lum2020(b)), float(b.b / lum2020(b))};
            tint = std::max(tint, double(std::max({std::fabs(r.r - 1.f), std::fabs(r.g - 1.f), std::fabs(r.b - 1.f)})));
            if (i) jump = std::max(jump, double(std::max({std::fabs(r.r - prev.r), std::fabs(r.g - prev.g), std::fabs(r.b - prev.b)})));
            prev = r;
        }
        for (Float3 c : {Float3{0.30f, 0.10f, 0.05f}, Float3{0.05f, 0.12f, 0.30f}, Float3{0.10f, 0.30f, 0.08f},
                         Float3{0.40f, 0.25f, 0.18f}}) {
            Float3 a = on.decodeToScene(c), z = off.decodeToScene(c), h = half.decodeToScene(c);
            CHECK(finite3(a), "ALEXA Colour not finite");
            moved = std::max(moved, double(std::fabs(a.r - z.r) + std::fabs(a.b - z.b)) / lumOf(z));
            CHECK(std::fabs(h.g - 0.5f * (a.g + z.g)) < 1e-5f + 1e-3f * z.g, "ALEXA Colour 50 %% is not the blend");
        }
        std::printf("  ALEXA Colour: grey 18 %% %.2e, grey luminance %.2e, neutral tint max %.3f (step %.4f / 1/8 stop), "
                    "colour change %.3f\n", grey18, worstY, tint, jump, moved);
        CHECK(grey18 < 1e-3, "ALEXA Colour moved 18 %% grey (%.2e)", grey18);
        CHECK(worstY < 1e-4, "ALEXA Colour changed grey luminance (%.2e)", worstY);
        CHECK(tint < 0.3, "ALEXA Colour tints the neutral axis too much (%.3f)", tint);
        CHECK(jump < 0.05, "ALEXA Colour neutral tint not smooth over exposure (%.4f)", jump);
        CHECK(moved > 0.01, "ALEXA Colour changes nothing (%.4f)", moved);
    }
    {   // Highlight Latitude (v1.3, F-Log2 C): identity below the shoulder, monotone, clipped -> neutral, hue kept
        // for a single clipped channel, finite on hostile input, off outside F-Log2 / F-Log2 C
        LookParams p = lpFor(1, 0, Process::NegativeToPrint, OutputSpace::Rec709_G24);
        p.input = InputSpace::FLog2C;
        Pipeline off, on, nodes;
        off.prepare(p);
        p.latitudeOn = true; on.prepare(p);
        p.latitudeDesaturate = 0.f; p.latitudeReconstruct = 0.f; nodes.prepare(p);
        double below = 0, worstHue = 0, clipSpread = 0; bool mono = true; float prevY = -1.f;
        for (int i = 0; i <= 1000; ++i) {   // grey ramp over the whole code range
            const float c = float(i) / 1000.f;
            Float3 a = off.decodeToScene({c, c, c}), b = on.decodeToScene({c, c, c});
            const float Y = 0.2627f * b.r + 0.6780f * b.g + 0.0593f * b.b;
            if (Y < prevY) mono = false;
            prevY = Y;
            if (c < 0.70f) below = std::max(below, double(std::max({std::fabs(a.r - b.r), std::fabs(a.g - b.g), std::fabs(a.b - b.b)})));
        }
        for (Float3 c : {Float3{0.854f, 0.854f, 0.854f}, Float3{0.854f, 0.854f, 0.80f}, Float3{0.80f, 0.854f, 0.854f},
                         Float3{0.95f, 0.95f, 0.90f}}) {
            Float3 b = on.decodeToScene(c);
            CHECK(finite3(b), "Highlight Latitude not finite");
            clipSpread = std::max(clipSpread, double(std::max({b.r, b.g, b.b}) / std::min({b.r, b.g, b.b}) - 1.f));
        }
        for (Float3 c : {Float3{0.854f, 0.50f, 0.45f}, Float3{0.40f, 0.854f, 0.50f}, Float3{0.45f, 0.42f, 0.90f}}) {
            Float3 a = off.decodeToScene(c), b = nodes.decodeToScene(c);
            const double k = double(b.g) / a.g;
            worstHue = std::max(worstHue, std::max(std::fabs(b.r / (k * a.r) - 1.0), std::fabs(b.b / (k * a.b) - 1.0)));
        }
        for (Float3 c : {Float3{NAN, 0.5f, 0.5f}, Float3{INFINITY, 1.f, 1.f}, Float3{-INFINITY, 0.f, 2.f},
                         Float3{1e6f, 1e6f, -1e6f}, Float3{-0.5f, 0.9f, 0.f}, Float3{1e-30f, 0.f, 1.f}})
            CHECK(finite3(on.decodeToScene(c)), "Highlight Latitude not finite on hostile input");
        std::printf("  Highlight Latitude: below shoulder %.1e, monotone %d, clipped spread %.4f, single-channel hue %.1e\n",
                    below, int(mono), clipSpread, worstHue);
        CHECK(below == 0.0, "Highlight Latitude changed a value below the shoulder (%.2e)", below);
        CHECK(mono, "Highlight Latitude grey ramp not monotone");
        CHECK(clipSpread < 0.02, "Highlight Latitude left a clipped highlight coloured (%.4f)", clipSpread);
        CHECK(worstHue < 1e-4, "Highlight Latitude moved the hue of a single clipped channel (%.2e)", worstHue);
        LookParams q = p; q.input = InputSpace::ArriLogC3; q.latitudeOn = true;
        Pipeline lc, lc0; lc.prepare(q); q.latitudeOn = false; lc0.prepare(q);
        Float3 u = lc.decodeToScene({0.95f, 0.9f, 0.8f}), v = lc0.decodeToScene({0.95f, 0.9f, 0.8f});
        CHECK(u.r == v.r && u.g == v.g && u.b == v.b, "Highlight Latitude active outside F-Log2 / F-Log2 C");
    }
}

int main(int argc, char** argv) {
    std::string golden = CEL_REFERENCE_DIR "/golden/core_golden.txt";
    std::string tmp = "/tmp";
    if (const char* t = std::getenv("TMPDIR")) tmp = t;
    bool quick = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--golden" && i + 1 < argc) golden = argv[++i];
        else if (a == "--quick") quick = true;
        else if (a == "--tmp" && i + 1 < argc) tmp = argv[++i];
    }
    double t0 = nowMs();
    testTransfers();
    testTiming();
    testToneScale();
    testNeutrality();
    if (!quick) testMonotonic();
    if (!quick) testSmoothness();
    testRobustness();
    testTextureOnlyIdentity();
    testHDR();
    testBehaviour();
    testStages();
    testPrintLab();
    testTone(quick);
    testAlexa();
    testGolden(golden);
    testCube(tmp);
    testCubeAccuracy(tmp);
    if (!quick) testHoldMonotonic();
    std::printf("\n%d checks, %d failures (%.1f s)\n", gChecks, gFail, (nowMs() - t0) / 1000.0);
    return gFail == 0 ? 0 : 1;
}
