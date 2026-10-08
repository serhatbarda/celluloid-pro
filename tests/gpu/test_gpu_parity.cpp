// Celluloid Pro - production parity: real plans (engine::buildPlan + core::Pipeline) executed by
//   (a) engine::cpuBackend()                 (texture agent's multithreaded CPU backend)
//   (b) celtest::RefBackend                  (independent single-thread executor, same kernels)
//   (c) every available GPU backend          (OpenCL via POCL here; CUDA when a device exists)
// on synthetic F-Log2 C style frames, for Full / Color Only / Texture Only, texture features on and
// off, trial watermark, odd sizes and UHD-class sizes, and every negative x print stock pair
// (CEL_PARITY_ALL=1; otherwise a representative subset). Targets (SPEC §6): GPU vs CPU max abs
// diff <= 2e-4 on the encoded output. Per-pass diagnostics (debug capture of every internal
// buffer) are printed for the first divergent pass of any failing case.
#include "GpuTestEnv.h"
#include "PerturbedKernels.h"
#include "../../src/kernels/AllKernels.h"

#include "../../src/texture/TextureDesign.h"
#include "../../src/texture/TexturePlan.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <limits>

using namespace celtest;
namespace engine = cel::engine;
namespace core = cel::core;
namespace tex = cel::tex;

namespace {

RefBackend makeProductionRef() {
    return RefBackend({
#define CEL_KERNEL(name) &cel_k_##name,
#include "../../src/kernels/KernelList.h"
#undef CEL_KERNEL
    });
}

enum class PlanKind { Frame, TextureScene, TextureDisplay, TextureDisplayHdr, GrainOnly, DamageOnly };
enum class Scene { Natural, Torture, Display };

struct Case {
    std::string name;
    engine::FrameSettings fs;
    int w, h;
    PlanKind kind = PlanKind::Frame;
    Scene scene = Scene::Natural;
};

// F-Log2 C-like code values: grey ramp, colour patches, skin-ish patch, bright speculars, a few
// out-of-range values, fine noise (to exercise grain/acutance).
HostImg makeScene(int w, int h, int stride, uint32_t seed) {
    HostImg img(w, h, stride);
    uint32_t s = seed * 747796405u + 2891336453u;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            float* p = img.px(x, y);
            const float u = (float(x) + 0.5f) / float(w), v = (float(y) + 0.5f) / float(h);
            float r, g, b;
            if (v < 0.25f) { r = g = b = 0.05f + 0.85f * u; }
            else if (v < 0.5f) {
                const int k = int(u * 8.0f);
                const float pal[8][3] = {{.62f, .38f, .34f}, {.35f, .52f, .38f}, {.33f, .38f, .60f}, {.55f, .48f, .40f},
                                         {.70f, .66f, .30f}, {.30f, .62f, .64f}, {.66f, .30f, .62f}, {.45f, .45f, .45f}};
                r = pal[k][0]; g = pal[k][1]; b = pal[k][2];
            } else if (v < 0.75f) {
                const float d = std::hypot(u - 0.5f, (v - 0.62f) * float(h) / float(w));
                const float spec = d < 0.02f ? 1.15f : d < 0.05f ? 0.85f : 0.30f + 0.1f * u;
                r = spec; g = spec * 0.97f; b = spec * 0.9f;
            } else {
                r = 0.5f + 0.4f * std::sin(u * 40.0f); g = 0.45f + 0.3f * std::cos(v * 37.0f); b = 0.4f;
                if (x % 97 == 3) { r = -0.05f; g = 1.6f; }
            }
            s ^= s << 13; s ^= s >> 17; s ^= s << 5;
            const float n = (float(s >> 8) * (1.0f / 16777216.0f) - 0.5f) * 0.01f;
            p[0] = r + n; p[1] = g - n; p[2] = b + 0.5f * n; p[3] = 0.25f + 0.75f * u;
        }
    return img;
}

// The natural scene with hostile values scattered over it (every channel incl. alpha): NaN,
// +-Inf, +-huge, the half-float limit, negative and > 1 code values, denormals, and a few
// saturated / super-white rows. GPU and CPU must agree bit-for-bit on where NaN lands (none
// should) and within tolerance everywhere else.
HostImg makeTortureScene(int w, int h, int stride, uint32_t seed) {
    HostImg img = makeScene(w, h, stride, seed);
    const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
    const float bad[] = {nan, inf, -inf, 1e30f, -1e30f, 65504.f, -65504.f, -5.f, 3.f, 1e-40f, -0.f, 1e7f};
    const int nb = int(sizeof(bad) / sizeof(bad[0]));
    uint32_t s = seed * 2654435761u + 7u;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            s ^= s << 13; s ^= s >> 17; s ^= s << 5;
            if (s % 23u == 0u) img.px(x, y)[(s >> 8) % 4u] = bad[(s >> 12) % unsigned(nb)];
        }
    for (int x = 0; x < w; ++x) {           // saturated primaries and super-whites, two rows
        float* p = img.px(x, h / 3);
        const int k = (x / 5) % 4;
        p[0] = k == 0 ? 1.2f : 0.0f; p[1] = k == 1 ? 1.2f : 0.0f; p[2] = k == 2 ? 1.2f : 0.0f;
        if (k == 3) p[0] = p[1] = p[2] = 2.5f;
        float* q = img.px(x, (2 * h) / 3);
        q[0] = q[1] = q[2] = 0.9f + 0.1f * float(x % 7);
    }
    return img;
}

engine::FrameSettings baseSettings(int w) {
    engine::FrameSettings fs;
    fs.tex.pixelScale = float(w) / 1920.0f;
    fs.tex.time = 12.0;
    fs.look.negStock = 1;   // CN 200T (SPEC default)
    return fs;
}

void fullTexture(engine::FrameSettings& fs) {
    fs.tex.weaveAmount = 0.6f;
    fs.tex.acutanceAmount = 0.3f;
    fs.tex.acutanceRadius = 2.0f;
    fs.tex.halationAmount = 0.8f;
    fs.tex.halationRadius = 20.0f;
    fs.tex.bloomAmount = 0.15f;
    fs.tex.bloomRadius = 40.0f;
    fs.tex.vignette = 0.3f;
    fs.tex.grainEnabled = true;
    fs.look.grainAmount = 1.0f;
}

// film damage + film blur (v1.1) on a frame where dust, >= 3 scratches (all three scratch tables),
// a hair and a soft moment are all present (searched deterministically)
void filmDamage(engine::FrameSettings& fs, int w, int h, bool needBlur = true) {
    tex::TextureParams& t = fs.tex;
    t.damageEnabled = true; t.dustAmount = 2.0f; t.dustSize = 1.5f; t.scratchAmount = 2.0f; t.hairAmount = 2.0f;
    t.scratchPersistence = 0.3f; t.scratchEmulsion = 0.5f; t.scratchNegative = 0.5f; t.dustNegative = 0.5f;
    t.blurEnabled = needBlur; t.blurFrequency = 0.8f; t.blurRadius = 12.0f; t.blurChromatic = 1.0f; t.blurBreathing = 0.5f;
    const float ps = tex::effectivePixelScale(t, w);
    for (int f = 0; f < 20000; ++f) {
        t.time = f;
        const tex::DamageDesign dd = tex::designDamage(t, w, h, ps);
        if (dd.nScratch >= 5 && dd.nHair > 0 && (!needBlur || tex::designFilmBlur(t, w, h, ps).on)) return;
    }
}

std::vector<Case> makeCases(bool all) {
    std::vector<Case> cs;
    const int W = 331, H = 187;
    auto add = [&](const std::string& name, auto&& edit) {
        Case c{name, baseSettings(W), W, H};
        edit(c);
        cs.push_back(c);
    };
    using OS = core::OutputSpace;
    // ---- modes and texture features
    add("full, all texture", [](Case& c) { fullTexture(c.fs); });
    add("full, texture off", [](Case&) {});
    add("color only", [](Case& c) { c.fs.mode = core::Mode::ColorOnly; });
    add("texture only + grain", [](Case& c) { fullTexture(c.fs); c.fs.mode = core::Mode::TextureOnly; c.scene = Scene::Display; });
    add("texture only, vignette only", [](Case& c) { c.fs.tex.vignette = 0.8f; c.fs.mode = core::Mode::TextureOnly; c.scene = Scene::Display; });
    add("full, vignette + weave only", [](Case& c) { c.fs.tex.vignette = 1.0f; c.fs.tex.weaveAmount = 2.0f; c.fs.tex.weaveSpeed = 4.0f; });
    add("full + watermark", [](Case& c) { fullTexture(c.fs); c.fs.watermark = true; });
    add("PQ + watermark", [](Case& c) { fullTexture(c.fs); c.fs.watermark = true; c.fs.look.output = OS::Rec2100_PQ; });
    add("grain mono, static, S8", [](Case& c) {
        fullTexture(c.fs); c.fs.tex.grainMonochrome = true; c.fs.tex.grainAnimated = false;
        c.fs.tex.grainFormat = tex::GrainFormat::Super8; });
    for (int f = 0; f < int(tex::GrainFormat::Count); ++f)
        add("grain format " + std::to_string(f) + ", size 3, crisp", [f](Case& c) {
            fullTexture(c.fs); c.fs.tex.grainFormat = tex::GrainFormat(f); c.fs.tex.grainSize = f == 4 ? 0.5f : 3.0f;
            c.fs.tex.grainSoftness = f % 2 ? 1.0f : 0.0f; c.fs.tex.grainSeed = 9999; c.fs.look.grainAmount = 3.0f; });
    add("texture extremes (halation/bloom/acutance max)", [](Case& c) {
        fullTexture(c.fs);
        c.fs.tex.halationAmount = 2.0f; c.fs.tex.halationRadius = 80.0f; c.fs.tex.halationThreshold = 0.0f;
        c.fs.tex.halationHue = 1.0f; c.fs.tex.bloomAmount = 2.0f; c.fs.tex.bloomRadius = 200.0f;
        c.fs.tex.acutanceAmount = 2.0f; c.fs.tex.acutanceRadius = 10.0f; });
    add("texture soft (acutance -1, r 0.5, hue -1)", [](Case& c) {
        fullTexture(c.fs); c.fs.tex.acutanceAmount = -1.0f; c.fs.tex.acutanceRadius = 0.5f; c.fs.tex.halationHue = -1.0f;
        c.fs.tex.halationThreshold = 6.0f; });
    add("render scale 0.5 (pixelScale)", [](Case& c) { fullTexture(c.fs); c.fs.tex.pixelScale *= 0.5f; });
    add("pixelScale 3 (tiny frame of a huge one)", [](Case& c) { fullTexture(c.fs); c.fs.tex.pixelScale = 3.0f; });
    add("tiny 7x5", [](Case& c) { c.w = 7; c.h = 5; c.fs = baseSettings(7); fullTexture(c.fs); });
    add("1x1", [](Case& c) { c.w = 1; c.h = 1; c.fs = baseSettings(1); fullTexture(c.fs); });
    add("1x67 column", [](Case& c) { c.w = 1; c.h = 67; c.fs = baseSettings(1); fullTexture(c.fs); });
    add("1920x1080 full", [](Case& c) { c.w = 1920; c.h = 1080; c.fs = baseSettings(1920); fullTexture(c.fs); });
    if (all)
        add("3840x2160 full", [](Case& c) { c.w = 3840; c.h = 2160; c.fs = baseSettings(3840); fullTexture(c.fs); });
    // ---- film damage and film blur (v1.1)
    add("film damage + blur", [](Case& c) { fullTexture(c.fs); filmDamage(c.fs, c.w, c.h); });
    add("film damage, no grain, P3", [](Case& c) { filmDamage(c.fs, c.w, c.h, false); c.fs.look.printStock = 2; });
    add("film damage + blur, pixelScale 3 (pyramid blur)", [](Case& c) { fullTexture(c.fs); c.fs.tex.pixelScale = 3.0f;
        filmDamage(c.fs, c.w, c.h); });
    add("film damage + blur, texture only", [](Case& c) { fullTexture(c.fs); c.fs.mode = core::Mode::TextureOnly; c.scene = Scene::Display;
        filmDamage(c.fs, c.w, c.h); });
    add("film damage + blur, texture only PQ", [](Case& c) { fullTexture(c.fs); c.fs.mode = core::Mode::TextureOnly;
        c.fs.look.output = OS::Rec2100_PQ; c.scene = Scene::Display; filmDamage(c.fs, c.w, c.h); });
    add("film damage + blur, PQ + watermark", [](Case& c) { fullTexture(c.fs); c.fs.look.output = OS::Rec2100_PQ;
        c.fs.watermark = true; filmDamage(c.fs, c.w, c.h); });
    add("film damage + blur, HLG, lab scan", [](Case& c) { fullTexture(c.fs); c.fs.look.output = OS::Rec2100_HLG;
        c.fs.look.process = core::Process::NegativeScan; filmDamage(c.fs, c.w, c.h); });
    for (const char* pre : {"RV", "BW 4"})
        add(std::string("film damage + blur, ") + pre, [pre](Case& c) {
            const auto ns = core::negativeStocks();
            for (size_t n = 0; n < ns.size(); ++n)
                if (ns[n].name.rfind(pre, 0) == 0) { c.fs.look.negStock = int(n); c.fs.tex.grainMonochrome = ns[n].monochrome; }
            fullTexture(c.fs); filmDamage(c.fs, c.w, c.h); });
    add("film damage, 1x1", [](Case& c) { c.w = 1; c.h = 1; c.fs = baseSettings(1); filmDamage(c.fs, c.w, c.h, false); });
    add("film damage + blur, 1920x1080", [](Case& c) { c.w = 1920; c.h = 1080; c.fs = baseSettings(1920); fullTexture(c.fs);
        filmDamage(c.fs, c.w, c.h); });
    add("torture, film damage + blur", [](Case& c) { fullTexture(c.fs); filmDamage(c.fs, c.w, c.h); c.scene = Scene::Torture; });
    add("damage plan", [](Case& c) { filmDamage(c.fs, c.w, c.h, false); c.kind = PlanKind::DamageOnly; });
    add("damage plan, nothing to draw", [](Case& c) { c.kind = PlanKind::DamageOnly; });
    // ---- input spaces (Color Only and Full)
    for (int i = 0; i < int(core::InputSpace::Count); ++i)
        add("input " + core::inputSpaceNames()[size_t(i)], [i](Case& c) {
            c.fs.look.input = core::InputSpace(i);
            if (i % 2) c.fs.mode = core::Mode::ColorOnly; else fullTexture(c.fs); });
    // ---- output spaces (Full and Texture Only)
    for (int o = 0; o < int(OS::Count); ++o) {
        add("output " + core::outputSpaceNames()[size_t(o)], [o](Case& c) { fullTexture(c.fs); c.fs.look.output = OS(o); });
        add("texture only, output " + core::outputSpaceNames()[size_t(o)], [o](Case& c) {
            fullTexture(c.fs); c.fs.look.output = OS(o); c.fs.mode = core::Mode::TextureOnly; c.scene = Scene::Display; });
    }
    // ---- look parameters at their limits
    add("push +3, neg bleach 1, interlayer 2", [](Case& c) {
        fullTexture(c.fs); c.fs.look.pushPull = 3.0f; c.fs.look.negBleach = 1.0f; c.fs.look.interlayer = 2.0f; });
    add("pull -2, neutralize 1, print bleach 1, interlayer 0", [](Case& c) {
        fullTexture(c.fs); c.fs.look.pushPull = -2.0f; c.fs.look.neutralize = 1.0f; c.fs.look.printBleach = 1.0f;
        c.fs.look.interlayer = 0.0f; });
    add("printer lights +12/-12/+6, print exposure -12, exposure +4", [](Case& c) {
        c.fs.look.printerR = 12.0f; c.fs.look.printerG = -12.0f; c.fs.look.printerB = 6.0f;
        c.fs.look.printExposure = -12.0f; c.fs.look.exposure = 4.0f; });
    add("print exposure +12, exposure -4", [](Case& c) { c.fs.look.printExposure = 12.0f; c.fs.look.exposure = -4.0f; });
    add("PQ peak 4000, unroll 0", [](Case& c) { fullTexture(c.fs); c.fs.look.output = OS::Rec2100_PQ;
        c.fs.look.hdrPeakNits = 4000.0f; c.fs.look.hdrUnroll = 0.0f; });
    add("PQ peak 300, unroll 1, exposure +4", [](Case& c) { c.fs.look.output = OS::Rec2100_PQ;
        c.fs.look.hdrPeakNits = 300.0f; c.fs.look.exposure = 4.0f; });
    add("HLG unroll 0.5, push +2", [](Case& c) { fullTexture(c.fs); c.fs.look.output = OS::Rec2100_HLG;
        c.fs.look.hdrUnroll = 0.5f; c.fs.look.pushPull = 2.0f; });
    // ---- hostile inputs
    add("torture, full all texture", [](Case& c) { fullTexture(c.fs); c.scene = Scene::Torture; });
    add("torture, full PQ + watermark", [](Case& c) { fullTexture(c.fs); c.fs.look.output = OS::Rec2100_PQ;
        c.fs.watermark = true; c.scene = Scene::Torture; });
    add("torture, color only, lab scan, linear input", [](Case& c) { c.fs.mode = core::Mode::ColorOnly;
        c.fs.look.process = core::Process::NegativeScan; c.fs.look.input = core::InputSpace::LinearRec2020; c.scene = Scene::Torture; });
    add("torture, texture only HLG", [](Case& c) { fullTexture(c.fs); c.fs.mode = core::Mode::TextureOnly;
        c.fs.look.output = OS::Rec2100_HLG; c.scene = Scene::Torture; });
    add("torture, texture only sRGB", [](Case& c) { fullTexture(c.fs); c.fs.mode = core::Mode::TextureOnly;
        c.fs.look.output = OS::Rec709_sRGB; c.scene = Scene::Torture; });
    // ---- texture-only and grain-only plans (tex::buildTexturePlan / buildGrainPlan, incl. tx_copy)
    add("texture plan, scene-linear", [](Case& c) { fullTexture(c.fs); c.kind = PlanKind::TextureScene; });
    add("texture plan, display-referred SDR", [](Case& c) { fullTexture(c.fs); c.kind = PlanKind::TextureDisplay; c.scene = Scene::Display; });
    add("texture plan, display-referred HDR", [](Case& c) { fullTexture(c.fs); c.kind = PlanKind::TextureDisplayHdr; c.scene = Scene::Display; });
    add("texture plan, all off (tx_copy)", [](Case& c) { c.kind = PlanKind::TextureScene; });
    add("grain plan", [](Case& c) { fullTexture(c.fs); c.kind = PlanKind::GrainOnly; });
    add("grain plan, 16 mm mono", [](Case& c) { fullTexture(c.fs); c.fs.tex.grainFormat = tex::GrainFormat::Mm16;
        c.fs.tex.grainMonochrome = true; c.kind = PlanKind::GrainOnly; });
    // ---- stock pairs: print (every pair with CEL_PARITY_ALL=1) and lab scan (every negative)
    const auto negs = core::negativeStocks();
    const auto prints = core::printStockNames();
    for (size_t n = 0; n < negs.size(); ++n) {
        for (size_t p = 0; p < prints.size(); ++p) {
            if (!all && !(p == 0 || n == p)) continue;
            add("stock " + negs[n].name + " / " + prints[p], [&](Case& c) {
                c.fs.look.negStock = int(n);
                c.fs.look.printStock = int(p);
                fullTexture(c.fs);
                c.fs.tex.grainMonochrome = negs[n].monochrome; });
        }
        add("stock " + negs[n].name + " / Lab Scan", [&](Case& c) {
            c.fs.look.negStock = int(n);
            c.fs.look.process = core::Process::NegativeScan;
            fullTexture(c.fs);
            c.fs.tex.grainMonochrome = negs[n].monochrome; });
    }
    return cs;
}

engine::Plan planFor(const Case& c, const core::Pipeline& pipe) {
    switch (c.kind) {
        case PlanKind::TextureScene: return tex::buildTexturePlan(c.w, c.h, c.fs.tex, false);
        case PlanKind::TextureDisplay: return tex::buildTexturePlan(c.w, c.h, c.fs.tex, true, false);
        case PlanKind::TextureDisplayHdr: return tex::buildTexturePlan(c.w, c.h, c.fs.tex, true, true);
        case PlanKind::GrainOnly: return tex::buildGrainPlan(c.w, c.h, c.fs.tex);
        case PlanKind::DamageOnly: return tex::buildDamagePlan(c.w, c.h, c.fs.tex);
        case PlanKind::Frame: break;
    }
    return engine::buildPlan(c.w, c.h, c.fs, pipe);
}

bool runGpu(Env& env, engine::Backend& be, const engine::Plan& plan, const core::Pipeline& pipe, HostImg& src,
            HostImg& dst, double* ms, std::string* err) {
    void* ds = env.upload(src);
    void* dd = env.allocLike(dst);
    engine::HostImage hs = src.host(), hd = dst.host();
    hs.data = ds; hd.data = dd;
    auto t0 = std::chrono::steady_clock::now();
    const bool ok = be.execute(plan, pipe.state(), pipe.stateVersion(), hs, hd, env.context(), err);
    env.finish();
    *ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    if (ok) env.download(dd, dst);
    env.release(ds);
    env.release(dd);
    return ok;
}

// The plan builders recycle buffers, so the final content of a buffer only shows its last writer.
// For diagnostics every pass gets its own output buffer (inputs remapped to the latest writer).
engine::Plan ssaPlan(const engine::Plan& plan) {
    engine::Plan q;
    q.buffers.assign(plan.buffers.begin(), plan.buffers.begin() + 2);
    std::vector<int> latest(plan.buffers.size(), -1);
    latest[engine::BUF_SRC] = engine::BUF_SRC;
    latest[engine::BUF_DST] = engine::BUF_DST;
    for (engine::PassDesc pd : plan.passes) {
        for (int j = 0; j < CEL_PASS_MAX_INPUTS; ++j)
            if (pd.in[j] >= 0) pd.in[j] = latest[size_t(pd.in[j])];
        if (pd.out >= 2) {
            q.buffers.push_back(plan.buffers[size_t(pd.out)]);
            latest[size_t(pd.out)] = int(q.buffers.size()) - 1;
            pd.out = latest[size_t(pd.out)];
        }
        q.passes.push_back(pd);
    }
    return q;
}

// Re-runs a failing case pass by pass (SSA plan, debug capture) and prints every pass's output
// difference GPU vs reference; the first pass whose difference jumps is the culprit.
void perPassReport(const engine::Plan& plan0, Env& env, engine::Backend& be, const core::Pipeline& pipe,
                   HostImg& src, int stride) {
    const engine::Plan plan = ssaPlan(plan0);
    RefBackend ref = makeProductionRef();
    HostImg refOut(src.w, src.h, stride), gpuOut(src.w, src.h, stride);
    std::string err;
    if (!ref.execute(plan, pipe.state(), pipe.stateVersion(), src.host(), refOut.host(), engine::GpuContext(), &err)) return;
    cel::gpu::setDebugCapture(&be, true);
    double ms = 0;
    if (!runGpu(env, be, plan, pipe, src, gpuOut, &ms, &err)) return;
    for (size_t p = 0; p < plan.passes.size(); ++p) {
        const int b = plan.passes[p].out;
        DiffStats d;
        int w = 0, h = 0;
        if (b < 2) {
            d = compare(refOut, gpuOut); w = src.w; h = src.h;
        } else {
            std::vector<float> cap;
            if (!cel::gpu::debugCapturedBuffer(&be, b, &cap, &w, &h)) continue;
            HostImg ri(w, h, w * 4), gi(w, h, w * 4);
            ri.store = ref.internal(b);
            gi.store = cap;
            d = compare(ri, gi);
        }
        std::printf("        pass %2zu %-18s -> buf %2d %4dx%-4d %s\n", p, engine::kernelName(plan.passes[p].kernel), b, w,
                    h, describe(d).c_str());
    }
}

} // namespace

int main(int argc, char** argv) {
    const bool all = std::getenv("CEL_PARITY_ALL") != nullptr;
    // --model: no GPU device; only CPU vs reference and the worst-case GPU math model
    const bool modelOnly = argc > 1 && std::string(argv[1]) == "--model";
    const double tol = 2e-4;
    std::printf("Celluloid Pro GPU parity (production kernels), tolerance %.1g on encoded output\n", tol);
    std::vector<std::unique_ptr<Env>> envs;
    std::string why;
    if (!modelOnly) {
#if CEL_TEST_HAVE_OPENCL
    if (auto e = makeOpenCLEnv(&why)) envs.push_back(std::move(e)); else std::printf("OpenCL: %s\n", why.c_str());
#endif
#if defined(CEL_TEST_HAVE_CUDA)
    if (auto e = makeCudaEnv(&why)) envs.push_back(std::move(e)); else std::printf("CUDA: %s\n", why.c_str());
#endif
#if defined(CEL_TEST_HAVE_METAL)
    if (auto e = makeMetalEnv(&why)) envs.push_back(std::move(e)); else std::printf("Metal: %s\n", why.c_str());
#endif
    }
    std::vector<std::unique_ptr<engine::Backend>> gpus;
    for (auto& e : envs) {
        const cel::gpu::KernelSet& ks = cel::gpu::productionKernelSet();
        gpus.push_back(e->api() == cel::gpu::Api::CUDA    ? cel::gpu::createCudaBackend(ks)
                       : e->api() == cel::gpu::Api::Metal ? cel::gpu::createMetalBackend(ks)
                                                          : cel::gpu::createOpenCLBackend(ks));
        // pooled internal buffers start every frame as NaN: a kernel reading memory that no pass
        // wrote (uninitialised memory) cannot hide behind a previous frame's data
        cel::gpu::setDebugPoison(gpus.back().get(), true);
        std::printf("device: [%s] %s\n", e->name(), e->device().c_str());
    }
    core::Pipeline pipe;
    std::vector<int> kernelUse(size_t(engine::KID_COUNT), 0);
    double worstGpu = 0.0, worstModel = 0.0;
    for (const Case& c : makeCases(all)) {
        pipe.prepare(c.fs.look);
        const engine::Plan plan = planFor(c, pipe);
        for (const engine::PassDesc& pd : plan.passes) kernelUse[size_t(pd.kernel)]++;
        const int stride = c.w * 4 + (c.w % 3) * 4;
        HostImg src = c.scene == Scene::Torture ? makeTortureScene(c.w, c.h, stride, uint32_t(c.w * 31 + c.h))
                                                : makeScene(c.w, c.h, stride, uint32_t(c.w * 31 + c.h));
        if (c.scene == Scene::Display)   // display code values: compress the scene into [0, 1]
            for (int y = 0; y < c.h; ++y)
                for (int x = 0; x < c.w * 4; ++x) { float& v = src.px(0, y)[x]; v = std::min(1.0f, std::max(0.0f, v * 0.85f)); }
        HostImg cpuOut(c.w, c.h, stride), refOut(c.w, c.h, stride);
        std::string err;
        auto t0 = std::chrono::steady_clock::now();
        const bool okCpu = engine::cpuBackend().execute(plan, pipe.state(), pipe.stateVersion(), src.host(), cpuOut.host(),
                                                        engine::GpuContext(), &err);
        const double cpuMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        CHECK(okCpu, "%s: CPU backend failed: %s", c.name.c_str(), err.c_str());
        RefBackend ref = makeProductionRef();
        const bool okRef = ref.execute(plan, pipe.state(), pipe.stateVersion(), src.host(), refOut.host(),
                                       engine::GpuContext(), &err);
        CHECK(okRef, "%s: reference failed: %s", c.name.c_str(), err.c_str());
        const DiffStats dr = compare(refOut, cpuOut);
        std::printf("%-34s %4dx%-4d %2zu passes  cpu %.1f ms  cpu-vs-ref %s\n", c.name.c_str(), c.w, c.h, plan.passes.size(),
                    cpuMs, describe(dr).c_str());
        CHECK(dr.nanMismatch == 0 && dr.maxAbs <= 1e-6, "%s: CPU backend differs from the reference executor", c.name.c_str());
        {   // worst-case GPU math model (OpenCL 1.2 ulp limits) must stay within the tolerance too
            RefBackend pert(perturbedProductionKernels());
            HostImg pertOut(c.w, c.h, stride);
            setPerturbedMathScale(1.0f);
            const bool okP = pert.execute(plan, pipe.state(), pipe.stateVersion(), src.host(), pertOut.host(),
                                          engine::GpuContext(), &err);
            CHECK(okP, "%s: perturbed reference failed: %s", c.name.c_str(), err.c_str());
            const DiffStats dp = compare(cpuOut, pertOut);
            worstModel = std::max(worstModel, dp.maxAbs);
            std::printf("    %-8s gpu-math model vs cpu %s\n", "model", describe(dp).c_str());
            CHECK(dp.nanMismatch == 0 && dp.maxAbs <= tol, "%s: worst-case GPU math model exceeds the tolerance: %s",
                  c.name.c_str(), describe(dp).c_str());
        }
        if (c.kind == PlanKind::Frame) {   // frame plans never emit NaN/Inf, whatever the input
            long bad = 0;
            for (int y = 0; y < c.h; ++y)
                for (int x = 0; x < c.w * 4; ++x) bad += std::isfinite(cpuOut.px(0, y)[x]) || (x % 4 == 3) ? 0 : 1;
            CHECK(bad == 0, "%s: %ld non-finite colour values in the CPU output", c.name.c_str(), bad);
        }
        for (size_t g = 0; g < envs.size(); ++g) {
            HostImg gpuOut(c.w, c.h, stride);
            double ms = 0;
            const bool big = c.w * c.h > 1000000;
            cel::gpu::setDebugCapture(gpus[g].get(), !big);
            const bool ok = runGpu(*envs[g], *gpus[g], plan, pipe, src, gpuOut, &ms, &err);
            CHECK(ok, "%s: %s execute failed: %s", c.name.c_str(), envs[g]->name(), err.c_str());
            if (!ok) continue;
            const DiffStats d = compare(cpuOut, gpuOut);
            std::printf("    %-8s %.1f ms  gpu-vs-cpu %s\n", envs[g]->name(), ms, describe(d).c_str());
            const bool pass = d.nanMismatch == 0 && d.maxAbs <= tol;
            worstGpu = std::max(worstGpu, d.maxAbs);
            CHECK(pass, "%s: %s parity %s", c.name.c_str(), envs[g]->name(), describe(d).c_str());
            CHECK(gpuOut.paddingIntact(), "%s: %s overwrote destination padding", c.name.c_str(), envs[g]->name());
            const char* trace = std::getenv("CEL_PARITY_TRACE");   // case-name substring: always report
            if ((!pass || (trace && c.name.find(trace) != std::string::npos)) && !big)
                perPassReport(plan, *envs[g], *gpus[g], pipe, src, stride);
        }
    }
    // every kernel of KernelList.h must have been exercised by at least one plan above
    for (int k = 0; k < int(engine::KID_COUNT); ++k)
        CHECK(kernelUse[size_t(k)] > 0, "kernel %s is not covered by any parity case", engine::kernelName(engine::KernelId(k)));
    std::printf("kernel coverage:");
    for (int k = 0; k < int(engine::KID_COUNT); ++k) std::printf(" %s=%d", engine::kernelName(engine::KernelId(k)), kernelUse[size_t(k)]);
    std::printf("\nworst GPU vs CPU max abs diff over all cases: %.3g, worst-case GPU math model: %.3g (tolerance %.1g)\n",
                worstGpu, worstModel, tol);
    std::printf("%d checks, %d failures\n", counters().checks, counters().failures);
    if (counters().failures) return 1;
    if (modelOnly) return 0;
    if (envs.empty()) {
        std::printf("SKIPPED: no GPU device available (CPU vs reference was checked)\n");
        return 77;   // ctest SKIP_RETURN_CODE
    }
    return 0;
}
