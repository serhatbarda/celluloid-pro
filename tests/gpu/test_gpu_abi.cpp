// Reafilm Pro - GPU backend tests over the ABI test kernel set (tests/gpu/abi) plus the
// production self tests. Every available device API (OpenCL here; CUDA when compiled in and a
// device exists) is checked against the independent reference executor (RefBackend.h):
//   strides/padding, odd sizes, 4 inputs of different sizes, f[]/i[] layout, state buffer
//   (large, upload-by-version), bit-exact integer hashing, math parity, internal buffer
//   aliasing + pool reuse across frames, debug capture of every internal buffer, error paths,
//   concurrent rendering on two queues, out-of-order queues, CPU fallback on device images.
// Exit code 0 = all checks passed.
#include "GpuTestEnv.h"
#include "abi/AbiKernels.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <thread>

namespace cel { namespace gpu { namespace embedded {
#if CEL_TEST_HAVE_OPENCL
extern const char cel_opencl_abi[];
extern const std::size_t cel_opencl_abi_len;
extern const char cel_opencl_abi_sha256[];
#endif
#if CEL_TEST_HAVE_METAL
extern const char cel_metal_abi[];
extern const std::size_t cel_metal_abi_len;
extern const char cel_metal_abi_sha256[];
#endif
}}} // namespace cel::gpu::embedded
#if defined(CEL_TEST_HAVE_CUDA)
extern const void* const cel_cuda_abi_entries[];
#endif

using namespace celtest;
using cel::engine::Plan;
using cel::engine::PassDesc;
using cel::engine::KernelId;

namespace {

enum AbiId {
#define CEL_KERNEL(name) A_##name,
#include "abi/AbiKernelList.h"
#undef CEL_KERNEL
    A_COUNT
};
const char* const kAbiNames[] = {
#define CEL_KERNEL(name) "cel_" #name,
#include "abi/AbiKernelList.h"
#undef CEL_KERNEL
    "cel_gpu_probe"};

const cel::gpu::KernelSet& abiKernelSet() {
    static const cel::gpu::KernelSet ks = [] {
        cel::gpu::KernelSet k;
        k.name = "abi-test";
        k.count = A_COUNT;
        k.names = kAbiNames;
#if CEL_TEST_HAVE_OPENCL
        k.openclSource = cel::gpu::embedded::cel_opencl_abi;
        k.openclSourceLen = cel::gpu::embedded::cel_opencl_abi_len;
        k.openclSha256 = cel::gpu::embedded::cel_opencl_abi_sha256;
#endif
#if CEL_TEST_HAVE_METAL
        k.metalSource = cel::gpu::embedded::cel_metal_abi;
        k.metalSourceLen = cel::gpu::embedded::cel_metal_abi_len;
        k.metalSha256 = cel::gpu::embedded::cel_metal_abi_sha256;
#endif
#if defined(CEL_TEST_HAVE_CUDA)
        k.cudaEntries = cel_cuda_abi_entries;
#endif
        return k;
    }();
    return ks;
}

RefBackend makeRef() {
    return RefBackend({
#define CEL_KERNEL(name) &cel_k_##name,
#include "abi/AbiKernelList.h"
#undef CEL_KERNEL
    });
}

PassDesc pass(int kernel, int out, int i0 = -1, int i1 = -1, int i2 = -1, int i3 = -1) {
    PassDesc p;
    p.kernel = KernelId(kernel);
    p.out = out;
    p.in[0] = i0; p.in[1] = i1; p.in[2] = i2; p.in[3] = i3;
    return p;
}

struct Frame {
    HostImg src, dst;
};

// Runs `plan` on the GPU (src uploaded, dst pre-filled with the sentinel) and on the reference.
// Returns false on execute failure (err set).
bool runBoth(Env& env, cel::engine::Backend& be, const Plan& plan, const std::vector<float>& state, uint64_t ver,
             HostImg& src, HostImg& gpuDst, HostImg& refDst, std::string* err) {
    RefBackend ref = makeRef();
    std::string rerr;
    if (!ref.execute(plan, state, ver, src.host(), refDst.host(), cel::engine::GpuContext(), &rerr)) {
        *err = "reference failed: " + rerr;
        return false;
    }
    void* ds = env.upload(src);
    void* dd = env.allocLike(gpuDst);
    cel::engine::HostImage hs = src.host(), hd = gpuDst.host();
    hs.data = ds;
    hd.data = dd;
    const bool ok = be.execute(plan, state, ver, hs, hd, env.context(), err);
    env.finish();
    if (ok) env.download(dd, gpuDst);
    env.release(ds);
    env.release(dd);
    return ok;
}

void expectClose(const char* what, const HostImg& ref, const HostImg& gpu, double tol) {
    DiffStats d = compare(ref, gpu);
    std::printf("    %-28s %s\n", what, describe(d).c_str());
    CHECK(d.nanMismatch == 0 && d.maxRel <= tol, "%s: %s (tol %.1g)", what, describe(d).c_str(), tol);
    CHECK(gpu.paddingIntact(), "%s: destination padding was overwritten", what);
}

// ---------------------------------------------------------------------------------------------
void testGradient(Env& env, cel::engine::Backend& be) {
    for (int variant = 0; variant < 3; ++variant) {
        const int w = variant == 0 ? 37 : variant == 1 ? 1 : 257, h = variant == 0 ? 23 : variant == 1 ? 1 : 3;
        const int stride = variant == 2 ? w * 4 : w * 4 + 12;
        Plan plan;
        plan.buffers = {{w, h}, {w, h}};
        PassDesc p = pass(A_t_gradient, 1);
        p.args.f[0] = 0.5f; p.args.f[1] = -3.25f; p.args.f[2] = 1.0f / 3.0f; p.args.f[3] = 1e6f;
        p.args.i[0] = 7; p.args.i[1] = -11;
        plan.passes = {p};
        HostImg src(w, h, stride), g(w, h, stride), r(w, h, stride);
        src.fillPattern(1, 0, 1);
        std::string err;
        const bool ok = runBoth(env, be, plan, {}, 1, src, g, r, &err);
        CHECK(ok, "gradient execute: %s", err.c_str());
        if (ok) expectClose(variant == 0 ? "gradient 37x23 padded" : variant == 1 ? "gradient 1x1" : "gradient 257x3", r, g, 1e-6);
    }
}

Plan chainPlan(int w, int h) {
    Plan plan;
    plan.buffers = {{w, h}, {w, h}, {w, h}, {w / 2, h / 2}, {w, h}, {w / 4, h / 4}};
    PassDesc pm = pass(A_t_math, 2, 0);
    PassDesc pd = pass(A_t_down2, 3, 2);
    PassDesc pb = pass(A_t_bilinear, 4, 3);
    pb.args.f[0] = 0.5f; pb.args.f[1] = 0.5f; pb.args.f[2] = 0.13f; pb.args.f[3] = -0.21f;
    PassDesc pd2 = pass(A_t_down2, 5, 3);
    PassDesc pa = pass(A_t_add4, 1, 0, 2, 4, 5);
    plan.passes = {pm, pd, pb, pd2, pa};
    return plan;
}

void testChain(Env& env, cel::engine::Backend& be) {
    const int w = 66, h = 50;
    Plan plan = chainPlan(w, h);
    HostImg src(w, h, w * 4 + 4), g(w, h, w * 4 + 8), r(w, h, w * 4 + 8);
    src.fillPattern(2, -1.0f, 2.0f);
    // debug capture: compare every internal buffer, not only the result
    cel::gpu::setDebugCapture(&be, true);
    std::string err;
    RefBackend ref = makeRef();
    ref.execute(plan, {}, 1, src.host(), r.host(), cel::engine::GpuContext(), &err);
    const bool ok = runBoth(env, be, plan, {}, 1, src, g, r, &err);
    CHECK(ok, "chain execute: %s", err.c_str());
    if (ok) {
        expectClose("chain (5 passes, 4 inputs)", r, g, 1e-5);
        for (int b = 2; b < 6; ++b) {
            std::vector<float> cap;
            int cw = 0, chh = 0;
            const bool have = cel::gpu::debugCapturedBuffer(&be, b, &cap, &cw, &chh);
            CHECK(have && cw == plan.buffers[size_t(b)].w && chh == plan.buffers[size_t(b)].h, "capture of buffer %d", b);
            if (!have) continue;
            HostImg ri(cw, chh, cw * 4), gi(cw, chh, cw * 4);
            ri.store = ref.internal(b);
            gi.store = cap;
            char what[64];
            std::snprintf(what, sizeof what, "  internal buffer %d (%dx%d)", b, cw, chh);
            expectClose(what, ri, gi, 1e-5);
        }
    }
    cel::gpu::setDebugCapture(&be, false);
}

void testArgsAndUnused(Env& env, cel::engine::Backend& be) {
    const int w = 41, h = 9;
    Plan plan;
    plan.buffers = {{w, h}, {w, h}, {13, 5}};
    PassDesc g0 = pass(A_t_gradient, 2);
    g0.args.f[0] = 1; g0.args.f[2] = 1;
    PassDesc pa = pass(A_t_args, 1, 0, -1, 2, 0);
    for (int i = 0; i < CEL_PASS_NF; ++i) pa.args.f[i] = float(i) * 1.5f - 7.25f;
    for (int i = 0; i < CEL_PASS_NI; ++i) pa.args.i[i] = (i + 1) * (i % 2 ? -1000 : 77);
    plan.passes = {g0, pa};
    HostImg src(w, h, w * 4 + 16), g(w, h, w * 4), r(w, h, w * 4);
    src.fillPattern(3, 0, 1);
    std::string err;
    bool ok = runBoth(env, be, plan, {}, 1, src, g, r, &err);
    CHECK(ok, "args execute: %s", err.c_str());
    if (ok) expectClose("args layout echo", r, g, 0.0);

    Plan p2;
    p2.buffers = {{w, h}, {w, h}, {7, 3}};
    p2.passes = {pass(A_t_gradient, 2), pass(A_t_unused, 1, -1, 0, -1, 2)};
    HostImg g2(w, h, w * 4), r2(w, h, w * 4);
    ok = runBoth(env, be, p2, {}, 1, src, g2, r2, &err);
    CHECK(ok, "unused execute: %s", err.c_str());
    if (ok) {
        expectClose("unused inputs", r2, g2, 0.0);
        CHECK(g2.px(3, 2)[1] == 5.0f, "unused mask %g (expect 5: in0+in2 unused)", double(g2.px(3, 2)[1]));
    }
}

void testState(Env& env, cel::engine::Backend& be) {
    const int w = 50, h = 30, n = 1 << 20;
    std::vector<float> state(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) state[size_t(i)] = std::sin(float(i) * 0.001f) * 3.0f;
    Plan plan;
    plan.buffers = {{w, h}, {w, h}};
    PassDesc p = pass(A_t_state, 1, 0);
    p.args.i[0] = 1000; p.args.i[1] = 500000; p.args.i[2] = n - 1;
    plan.passes = {p};
    HostImg src(w, h, w * 4);
    src.fillPattern(4, -1, 1);
    cel::gpu::BackendStats s0, s1, s2, s3;
    cel::gpu::backendStats(&be, &s0);
    std::string err;
    HostImg g(w, h, w * 4), r(w, h, w * 4);
    bool ok = runBoth(env, be, plan, state, 7, src, g, r, &err);
    CHECK(ok, "state execute: %s", err.c_str());
    if (ok) expectClose("state 4 MiB, version 7", r, g, 1e-6);
    cel::gpu::backendStats(&be, &s1);
    // same version + content: no upload
    HostImg g2(w, h, w * 4), r2(w, h, w * 4);
    ok = runBoth(env, be, plan, state, 7, src, g2, r2, &err);
    cel::gpu::backendStats(&be, &s2);
    CHECK(ok && s2.stateUploads == s1.stateUploads, "unchanged state re-uploaded (%llu -> %llu)",
          (unsigned long long)s1.stateUploads, (unsigned long long)s2.stateUploads);
    // new content + version: upload and new results
    for (auto& v : state) v = -v + 0.5f;
    HostImg g3(w, h, w * 4), r3(w, h, w * 4);
    ok = runBoth(env, be, plan, state, 8, src, g3, r3, &err);
    cel::gpu::backendStats(&be, &s3);
    CHECK(ok && s3.stateUploads == s2.stateUploads + 1, "changed state not uploaded once");
    if (ok) expectClose("state version 8", r3, g3, 1e-6);
    // content changed but version reused (must still be detected via the sampled checksum)
    for (size_t i = 0; i < state.size(); i += 4096) state[i] += 1.0f;
    HostImg g4(w, h, w * 4), r4(w, h, w * 4);
    ok = runBoth(env, be, plan, state, 8, src, g4, r4, &err);
    if (ok) expectClose("state edited, same version", r4, g4, 1e-6);
}

void testHash(Env& env, cel::engine::Backend& be) {
    const int w = 128, h = 64;
    Plan plan;
    plan.buffers = {{w, h}, {w, h}};
    PassDesc p = pass(A_t_hash, 1);
    p.args.i[0] = 424242;
    plan.passes = {p};
    HostImg src(w, h, w * 4), g(w, h, w * 4), r(w, h, w * 4);
    std::string err;
    const bool ok = runBoth(env, be, plan, {}, 1, src, g, r, &err);
    CHECK(ok, "hash execute: %s", err.c_str());
    if (ok) expectClose("integer hash (bit exact)", r, g, 0.0);
}

void testPoolAndAliasing(Env& env, cel::engine::Backend& be) {
    const int w = 96, h = 64;
    // 8 same-size internal buffers, each consumed only by the next pass -> 2 physical slots suffice
    Plan plan;
    plan.buffers = {{w, h}, {w, h}};
    for (int i = 0; i < 8; ++i) plan.buffers.push_back({w, h});
    plan.passes.push_back(pass(A_t_math, 2, 0));
    for (int i = 3; i < 10; ++i) {
        PassDesc p = pass(A_t_bilinear, i, i - 1);
        p.args.f[0] = 1.0f; p.args.f[1] = 1.0f; p.args.f[2] = 0.25f; p.args.f[3] = 0.125f;
        plan.passes.push_back(p);
    }
    plan.passes.push_back(pass(A_t_add4, 1, 9, 0, 9, 0));
    HostImg src(w, h, w * 4);
    src.fillPattern(5, 0, 1);
    cel::gpu::BackendStats a, b;
    std::string err;
    for (int f = 0; f < 4; ++f) {
        HostImg g(w, h, w * 4), r(w, h, w * 4);
        const bool ok = runBoth(env, be, plan, {}, 1, src, g, r, &err);
        CHECK(ok, "alias execute: %s", err.c_str());
        if (ok && f == 0) expectClose("aliased chain (8 internal)", r, g, 1e-5);
        if (f == 0) cel::gpu::backendStats(&be, &a);
    }
    cel::gpu::backendStats(&be, &b);
    const uint64_t slotBytes = uint64_t(w) * h * 16u;
    std::printf("    pool: %llu bytes pooled (%.1f full-size slots), allocs %llu -> %llu, reuses %llu\n",
                (unsigned long long)b.bytesPooled, double(b.bytesPooled) / double(slotBytes),
                (unsigned long long)a.bufferAllocs, (unsigned long long)b.bufferAllocs,
                (unsigned long long)b.bufferReuses);
    CHECK(b.bufferAllocs == a.bufferAllocs, "pool allocated again on identical frames");
    CHECK(b.bytesPooled <= 2 * slotBytes, "aliasing should need 2 slots, pool holds %llu bytes",
          (unsigned long long)b.bytesPooled);
}

void testErrors(Env& env, cel::engine::Backend& be) {
    const int w = 16, h = 8;
    HostImg src(w, h, w * 4), dst(w, h, w * 4);
    void* ds = env.upload(src);
    void* dd = env.allocLike(dst);
    cel::engine::HostImage hs = src.host(), hd = dst.host();
    hs.data = ds; hd.data = dd;
    struct Case { const char* what; Plan plan; };
    std::vector<Case> cases;
    {
        Plan p; p.buffers = {{w, h}, {w, h}, {w, h}};
        p.passes = {pass(A_t_math, 2, 2)};
        cases.push_back({"input aliases output", p});
    }
    {
        Plan p; p.buffers = {{w, h}, {w, h}, {w, h}};
        p.passes = {pass(A_t_math, 1, 2)};
        cases.push_back({"reads unwritten buffer", p});
    }
    {
        Plan p; p.buffers = {{w, h}, {w, h}};
        p.passes = {pass(A_COUNT + 5, 1, 0)};
        cases.push_back({"invalid kernel id", p});
    }
    {
        Plan p; p.buffers = {{w, h}, {w, h}, {w, h}};
        p.passes = {pass(A_t_math, 2, 0)};
        cases.push_back({"dst never written", p});
    }
    {
        Plan p; p.buffers = {{w, h}, {w, h}};
        p.passes = {pass(A_t_math, 0, 1)};
        cases.push_back({"writes src", p});
    }
    {
        Plan p; p.buffers = {{w + 1, h}, {w, h}};
        p.passes = {pass(A_t_math, 1, 0)};
        cases.push_back({"plan/host size mismatch", p});
    }
    {
        Plan p; p.buffers = {{w, h}, {w, h}};
        cases.push_back({"empty plan", p});
    }
    for (auto& c : cases) {
        std::string err;
        const bool ok = be.execute(c.plan, {}, 1, hs, hd, env.context(), &err);
        std::printf("    error case %-24s -> %s\n", c.what, ok ? "ACCEPTED" : err.c_str());
        CHECK(!ok && !err.empty(), "%s should fail", c.what);
    }
    // device buffer too small for the claimed geometry
    {
        Plan p; p.buffers = {{w, h * 2}, {w, h}};
        p.passes = {pass(A_t_math, 1, 0)};
        cel::engine::HostImage big = hs;
        big.h = h * 2;
        std::string err;
        const bool ok = be.execute(p, {}, 1, big, hd, env.context(), &err);
        std::printf("    error case %-24s -> %s\n", "src buffer too small", ok ? "ACCEPTED" : err.c_str());
        if (env.api() != cel::gpu::Api::CUDA) CHECK(!ok && !err.empty(), "too-small source buffer should fail");
    }
    if (!env.supportsNegativeStride()) {
        Plan p; p.buffers = {{w, h}, {w, h}};
        p.passes = {pass(A_t_math, 1, 0)};
        cel::engine::HostImage neg = hd;
        neg.rowStrideFloats = -w * 4;
        std::string err;
        const bool ok = be.execute(p, {}, 1, hs, neg, env.context(), &err);
        std::printf("    error case %-24s -> %s\n", "negative stride", ok ? "ACCEPTED" : err.c_str());
        CHECK(!ok && !err.empty(), "negative stride should be rejected for %s", env.name());
    }
    {
        std::string err;
        Plan p; p.buffers = {{w, h}, {w, h}};
        p.passes = {pass(A_t_math, 1, 0)};
        const bool ok = be.execute(p, {}, 1, hs, hd, cel::engine::GpuContext(), &err);
        std::printf("    error case %-24s -> %s\n", "no queue/stream", ok ? "ACCEPTED" : err.c_str());
        if (env.api() != cel::gpu::Api::CUDA) CHECK(!ok && !err.empty(), "missing queue should fail");
    }
    env.release(ds);
    env.release(dd);
    // the backend still works after errors
    testHash(env, be);
}

void testNegativeStride(Env& env, cel::engine::Backend& be) {
    if (!env.supportsNegativeStride()) return;
    const int w = 33, h = 17;
    Plan plan = chainPlan(w, h);
    HostImg src(w, h, -(w * 4 + 4)), g(w, h, -(w * 4)), r(w, h, -(w * 4));
    src.fillPattern(9, -1, 2);
    std::string err;
    const bool ok = runBoth(env, be, plan, {}, 1, src, g, r, &err);
    CHECK(ok, "negative stride execute: %s", err.c_str());
    if (ok) expectClose("bottom-up (negative stride)", r, g, 1e-5);
}

void testTwoQueues(Env& env, cel::engine::Backend& be) {
    if (!env.makeSecondQueue()) { std::printf("    (second queue unavailable)\n"); return; }
    const int w = 80, h = 60;
    Plan plan = chainPlan(w, h);
    HostImg src(w, h, w * 4), r(w, h, w * 4);
    src.fillPattern(6, -1, 2);
    RefBackend ref = makeRef();
    std::string err;
    ref.execute(plan, {}, 1, src.host(), r.host(), cel::engine::GpuContext(), &err);
    std::atomic<int> bad{0}, done{0};
    auto worker = [&](cel::engine::GpuContext ctx) {
        void* ds = env.upload(src);
        for (int f = 0; f < 12; ++f) {
            HostImg g(w, h, w * 4);
            void* dd = env.allocLike(g);
            cel::engine::HostImage hs = src.host(), hd = g.host();
            hs.data = ds; hd.data = dd;
            std::string e;
            if (!be.execute(plan, {}, 1, hs, hd, ctx, &e)) { bad++; std::printf("    thread error: %s\n", e.c_str()); }
            env.finish();
            env.download(dd, g);
            env.release(dd);
            DiffStats d = compare(r, g);
            if (d.maxRel > 1e-5 || d.nanMismatch) bad++;
            done++;
        }
        env.release(ds);
    };
    std::thread t1(worker, env.context()), t2(worker, env.secondContext());
    t1.join();
    t2.join();
    std::printf("    two queues x 12 frames concurrently: %d frames, %d bad\n", done.load(), bad.load());
    CHECK(bad == 0, "concurrent rendering on two queues produced %d bad frames", bad.load());
}

// Lanes of queues/streams that stopped rendering are released by later frames on other lanes
// (CEL_GPU_LANE_IDLE_MS is shortened to 150 ms in main), and a retired lane is recreated
// transparently when its queue renders again.
void testIdleLanes(Env& env, cel::engine::Backend& be) {
    if (!env.makeSecondQueue()) return;
    const int w = 64, h = 48;
    Plan plan = chainPlan(w, h);
    HostImg src(w, h, w * 4), r(w, h, w * 4);
    src.fillPattern(11, -1, 2);
    RefBackend ref = makeRef();
    std::string err;
    ref.execute(plan, {}, 1, src.host(), r.host(), cel::engine::GpuContext(), &err);
    auto frame = [&](cel::engine::GpuContext ctx, const char* what) {
        HostImg g(w, h, w * 4);
        void* ds = env.upload(src);
        void* dd = env.allocLike(g);
        cel::engine::HostImage hs = src.host(), hd = g.host();
        hs.data = ds; hd.data = dd;
        std::string e;
        const bool ok = be.execute(plan, {}, 1, hs, hd, ctx, &e);
        env.finish();
        env.download(dd, g);
        env.release(ds);
        env.release(dd);
        CHECK(ok, "%s: %s", what, e.c_str());
        if (ok) expectClose(what, r, g, 1e-5);
    };
    auto lanes = [&]() { cel::gpu::BackendStats st; cel::gpu::backendStats(&be, &st); return st.lanes; };
    frame(env.context(), "idle lanes: queue 1");
    frame(env.secondContext(), "idle lanes: queue 2");
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    frame(env.context(), "idle lanes: queue 1 after idle");          // retires queue 2's lane
    const uint64_t afterTrim = lanes();
    frame(env.secondContext(), "idle lanes: queue 2 recreated");    // new lane for queue 2
    std::printf("    idle lanes: %llu live lane(s) after the trim (expected 1)\n", (unsigned long long)afterTrim);
    CHECK(afterTrim == 1, "idle lane was not released (%llu live lanes)", (unsigned long long)afterTrim);
}

void testFallback(Env& env) {
    const int w = 45, h = 31;
    Plan plan = chainPlan(w, h);
    HostImg src(w, h, w * 4 + 8), g(w, h, w * 4 + 4), r(w, h, w * 4 + 4);
    src.fillPattern(7, -1, 2);
    RefBackend ref = makeRef();
    std::string err;
    ref.execute(plan, {}, 1, src.host(), r.host(), cel::engine::GpuContext(), &err);
    void* ds = env.upload(src);
    void* dd = env.allocLike(g);
    cel::engine::HostImage hs = src.host(), hd = g.host();
    hs.data = ds; hd.data = dd;
    RefBackend cpu = makeRef();
    const bool ok = cel::gpu::cpuFallback(env.api(), cpu, plan, {}, 1, hs, hd, env.context(), &err);
    env.finish();
    env.download(dd, g);
    env.release(ds);
    env.release(dd);
    CHECK(ok, "cpuFallback: %s", err.c_str());
    if (ok) expectClose("CPU fallback on device images", r, g, 0.0);
}

void testTiming(Env& env, cel::engine::Backend& be) {
    const int w = 1920, h = 1080;
    Plan plan = chainPlan(w, h);
    HostImg src(w, h, w * 4), g(w, h, w * 4);
    src.fillPattern(8, 0, 1);
    void* ds = env.upload(src);
    void* dd = env.allocLike(g);
    cel::engine::HostImage hs = src.host(), hd = g.host();
    hs.data = ds; hd.data = dd;
    std::string err;
    be.execute(plan, {}, 1, hs, hd, env.context(), &err);   // warm-up (allocations)
    env.finish();
    const int frames = 5;
    auto t0 = std::chrono::steady_clock::now();
    for (int f = 0; f < frames; ++f) be.execute(plan, {}, 1, hs, hd, env.context(), &err);
    env.finish();
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / frames;
    std::printf("    1920x1080 5-pass chain: %.2f ms/frame on %s\n", ms, env.device().c_str());
    env.release(ds);
    env.release(dd);
}

void runAll(Env& env, cel::engine::Backend& be) {
    std::printf("  [%s] %s\n", env.name(), env.device().c_str());
    testGradient(env, be);
    testChain(env, be);
    testArgsAndUnused(env, be);
    testState(env, be);
    testHash(env, be);
    testPoolAndAliasing(env, be);
    testErrors(env, be);
    testNegativeStride(env, be);
    testTwoQueues(env, be);
    testIdleLanes(env, be);
    testFallback(env);
    testTiming(env, be);
    cel::gpu::BackendStats st;
    cel::gpu::backendStats(&be, &st);
    std::printf("    stats: frames %llu failures %llu builds %llu (last %.0f ms) stateUploads %llu allocs %llu reuses %llu\n",
                (unsigned long long)st.frames, (unsigned long long)st.failures, (unsigned long long)st.programBuilds,
                st.lastBuildMs, (unsigned long long)st.stateUploads, (unsigned long long)st.bufferAllocs,
                (unsigned long long)st.bufferReuses);
    CHECK(st.programBuilds == 1, "program built %llu times (expected once per device)", (unsigned long long)st.programBuilds);
}

} // namespace

int main() {
    std::printf("Reafilm Pro GPU ABI tests\n");
    // idle lanes are released after 150 ms instead of 10 s (testIdleLanes); read once by the module
#if defined(_WIN32)
    _putenv_s("CEL_GPU_LANE_IDLE_MS", "150");
#else
    setenv("CEL_GPU_LANE_IDLE_MS", "150", 1);
#endif
    int apis = 0;
    // self tests of the production backends
    for (cel::gpu::Api api : {cel::gpu::Api::OpenCL, cel::gpu::Api::CUDA, cel::gpu::Api::Metal}) {
        cel::gpu::SelfTestResult q = cel::gpu::selfTest(api, false);
        std::printf("selfTest(%s, quick): compiled=%d ok=%d %.1f ms: %s\n", cel::gpu::apiName(api), q.compiledIn, q.ok,
                    q.ms, q.message.c_str());
        if (q.compiledIn && q.ok) {
            cel::gpu::SelfTestResult f = cel::gpu::selfTest(api, true);
            std::printf("selfTest(%s, full):  ok=%d %.1f ms: %s\n", cel::gpu::apiName(api), f.ok, f.ms, f.message.c_str());
            CHECK(f.ok, "full self test of the production %s backend failed", cel::gpu::apiName(api));
        }
    }
    CHECK(cel::engine::openclBackend() != nullptr || !CEL_TEST_HAVE_OPENCL, "openclBackend() missing");

#if CEL_TEST_HAVE_OPENCL
    {
        std::string why;
        auto env = makeOpenCLEnv(&why);
        if (env) {
            auto be = cel::gpu::createOpenCLBackend(abiKernelSet());
            runAll(*env, *be);
            ++apis;
            auto ooo = makeOpenCLEnv(&why, true);
            if (ooo) {
                std::printf("  [OpenCL out-of-order queue]\n");
                auto be2 = cel::gpu::createOpenCLBackend(abiKernelSet());
                testChain(*ooo, *be2);
                testState(*ooo, *be2);
                testPoolAndAliasing(*ooo, *be2);
            } else {
                std::printf("  (out-of-order queue not supported: %s)\n", why.c_str());
            }
        } else {
            std::printf("  OpenCL environment unavailable: %s\n", why.c_str());
        }
    }
#endif
#if defined(CEL_TEST_HAVE_CUDA)
    {
        std::string why;
        auto env = makeCudaEnv(&why);
        if (env) {
            auto be = cel::gpu::createCudaBackend(abiKernelSet());
            runAll(*env, *be);
            ++apis;
        } else {
            std::printf("  CUDA environment unavailable: %s (CUDA backend compiled, not executed)\n", why.c_str());
        }
    }
#endif
#if CEL_TEST_HAVE_METAL
    {
        std::string why;
        auto env = makeMetalEnv(&why);
        if (env) {
            auto be = cel::gpu::createMetalBackend(abiKernelSet());
            runAll(*env, *be);
            ++apis;
        } else {
            std::printf("  Metal environment unavailable: %s\n", why.c_str());
        }
    }
#endif
    std::printf("%d checks, %d failures, %d device API(s) exercised\n", counters().checks, counters().failures, apis);
    if (counters().failures) return 1;
    if (apis == 0) {
        std::printf("SKIPPED: no GPU device API could be exercised on this machine\n");
        return 77;   // ctest SKIP_RETURN_CODE
    }
    return 0;
}
