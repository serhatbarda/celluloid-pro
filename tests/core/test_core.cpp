// Celluloid Pro - core colour science unit tests (plain C++ harness, no framework).
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
                        bool ok = v.r >= prev.r - 2e-6f && v.g >= prev.g - 2e-6f && v.b >= prev.b - 2e-6f;
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
// from its 61-sample running mean by at most 5 % of the local mean |slope| (where the channel
// moves at all), for every print pair and the reversal stock
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
                for (int ch = 0; ch < 3; ++ch) {
                    std::vector<double> sl(size_t(N - 1)), sm(size_t(N - 1), 0.0), am(size_t(N - 1), 0.0);
                    for (int k = 0; k < N - 1; ++k) sl[size_t(k)] = double(o[size_t(k + 1) * 3 + ch]) - o[size_t(k) * 3 + ch];
                    for (int k = Wn / 2; k < N - 1 - Wn / 2; ++k) {
                        double a = 0, b = 0;
                        for (int j = -Wn / 2; j <= Wn / 2; ++j) { a += sl[size_t(k + j)]; b += std::fabs(sl[size_t(k + j)]); }
                        sm[size_t(k)] = a / Wn; am[size_t(k)] = b / Wn;
                    }
                    std::vector<double> sorted(am.begin() + Wn, am.end() - Wn);
                    std::nth_element(sorted.begin(), sorted.begin() + sorted.size() / 2, sorted.end());
                    double med = sorted[sorted.size() / 2];
                    double w = 0;
                    for (int k = Wn; k < N - 1 - Wn; ++k) {
                        float v = o[size_t(k) * 3 + ch];
                        if (v < 0.03f || v > 0.95f || am[size_t(k)] < 0.15 * med || am[size_t(k)] <= 0) continue;
                        w = std::max(w, std::fabs(sl[size_t(k)] - sm[size_t(k)]) / am[size_t(k)]);
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
    std::string path = dir + "/cel_test_\xc3\xa4\xc3\xb6\xc3\xbc/Celluloid_test.cube";   // non-ASCII folder (UTF-8)
    std::string err;
    double t0 = nowMs();
    bool ok = writeCubeLUT(P, 33, path, "Celluloid test", &err);
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
    CHECK(negativeStocks().size() == 11 && printStockNames().size() == 5, "stock counts");
    CHECK(negativeStocks()[1].name == "CN 200T Studio Tungsten" && printStockNames()[0] == "P1 Standard Projection", "defaults");
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
    testGolden(golden);
    testCube(tmp);
    std::printf("\n%d checks, %d failures (%.1f s)\n", gChecks, gFail, (nowMs() - t0) / 1000.0);
    return gFail == 0 ? 0 : 1;
}
