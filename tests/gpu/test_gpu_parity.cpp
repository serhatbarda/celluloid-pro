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
#include "../../src/kernels/AllKernels.h"

#include <chrono>
#include <cstdlib>

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

struct Case {
    std::string name;
    engine::FrameSettings fs;
    int w, h;
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

engine::FrameSettings baseSettings(int w) {
    engine::FrameSettings fs;
    fs.tex.pixelScale = float(w) / 1920.0f;
    fs.tex.time = 12.0;
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

std::vector<Case> makeCases(bool all) {
    std::vector<Case> cs;
    const int W = 331, H = 187;
    { Case c{"full, all texture", baseSettings(W), W, H}; fullTexture(c.fs); cs.push_back(c); }
    { Case c{"full, texture off", baseSettings(W), W, H}; cs.push_back(c); }
    { Case c{"color only", baseSettings(W), W, H}; c.fs.mode = core::Mode::ColorOnly; cs.push_back(c); }
    { Case c{"texture only + grain", baseSettings(W), W, H}; fullTexture(c.fs); c.fs.mode = core::Mode::TextureOnly; cs.push_back(c); }
    { Case c{"full + watermark", baseSettings(W), W, H}; fullTexture(c.fs); c.fs.watermark = true; cs.push_back(c); }
    { Case c{"grain mono, static, S8", baseSettings(W), W, H}; fullTexture(c.fs);
      c.fs.tex.grainMonochrome = true; c.fs.tex.grainAnimated = false; c.fs.tex.grainFormat = tex::GrainFormat::Super8; cs.push_back(c); }
    { Case c{"tiny 7x5", baseSettings(7), 7, 5}; fullTexture(c.fs); cs.push_back(c); }
    { Case c{"1920x1080 full", baseSettings(1920), 1920, 1080}; fullTexture(c.fs); cs.push_back(c); }
    // stock pairs (colour path): representative subset, or every pair with CEL_PARITY_ALL=1
    const auto negs = core::negativeStocks();
    const auto prints = core::printStockNames();
    for (size_t n = 0; n < negs.size(); ++n)
        for (size_t p = 0; p < prints.size(); ++p) {
            if (!all && !(p == 0 || n == p)) continue;
            Case c{"stock " + negs[n].name + " / " + prints[p], baseSettings(W), W, H};
            c.fs.look.negStock = int(n);
            c.fs.look.printStock = int(p);
            fullTexture(c.fs);
            c.fs.tex.grainMonochrome = negs[n].monochrome;
            cs.push_back(c);
        }
    return cs;
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

void perPassReport(const engine::Plan& plan, const RefBackend& ref, const engine::Backend& be) {
    for (size_t p = 0; p < plan.passes.size(); ++p) {
        const int b = plan.passes[p].out;
        if (b < 2) continue;
        std::vector<float> cap;
        int w = 0, h = 0;
        if (!cel::gpu::debugCapturedBuffer(&be, b, &cap, &w, &h)) continue;
        HostImg ri(w, h, w * 4), gi(w, h, w * 4);
        ri.store = ref.internal(b);
        gi.store = cap;
        DiffStats d = compare(ri, gi);
        std::printf("        pass %2zu %-18s -> buf %2d %4dx%-4d %s\n", p, engine::kernelName(plan.passes[p].kernel), b, w,
                    h, describe(d).c_str());
    }
}

} // namespace

int main() {
    const bool all = std::getenv("CEL_PARITY_ALL") != nullptr;
    const double tol = 2e-4;
    std::printf("Celluloid Pro GPU parity (production kernels), tolerance %.1g on encoded output\n", tol);
    std::vector<std::unique_ptr<Env>> envs;
    std::string why;
#if CEL_TEST_HAVE_OPENCL
    if (auto e = makeOpenCLEnv(&why)) envs.push_back(std::move(e)); else std::printf("OpenCL: %s\n", why.c_str());
#endif
#if defined(CEL_TEST_HAVE_CUDA)
    if (auto e = makeCudaEnv(&why)) envs.push_back(std::move(e)); else std::printf("CUDA: %s\n", why.c_str());
#endif
#if defined(CEL_TEST_HAVE_METAL)
    if (auto e = makeMetalEnv(&why)) envs.push_back(std::move(e)); else std::printf("Metal: %s\n", why.c_str());
#endif
    std::vector<std::unique_ptr<engine::Backend>> gpus;
    for (auto& e : envs) {
        const cel::gpu::KernelSet& ks = cel::gpu::productionKernelSet();
        gpus.push_back(e->api() == cel::gpu::Api::CUDA    ? cel::gpu::createCudaBackend(ks)
                       : e->api() == cel::gpu::Api::Metal ? cel::gpu::createMetalBackend(ks)
                                                          : cel::gpu::createOpenCLBackend(ks));
        std::printf("device: [%s] %s\n", e->name(), e->device().c_str());
    }
    core::Pipeline pipe;
    for (const Case& c : makeCases(all)) {
        pipe.prepare(c.fs.look);
        const engine::Plan plan = engine::buildPlan(c.w, c.h, c.fs, pipe);
        const int stride = c.w * 4 + (c.w % 3) * 4;
        HostImg src = makeScene(c.w, c.h, stride, uint32_t(c.w * 31 + c.h));
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
            CHECK(pass, "%s: %s parity %s", c.name.c_str(), envs[g]->name(), describe(d).c_str());
            CHECK(gpuOut.paddingIntact(), "%s: %s overwrote destination padding", c.name.c_str(), envs[g]->name());
            if (!pass && !big) perPassReport(plan, ref, *gpus[g]);
        }
    }
    std::printf("%d checks, %d failures\n", counters().checks, counters().failures);
    if (counters().failures) return 1;
    if (envs.empty()) {
        std::printf("SKIPPED: no GPU device available (CPU vs reference was checked)\n");
        return 77;   // ctest SKIP_RETURN_CODE
    }
    return 0;
}
