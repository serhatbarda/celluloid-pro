// Celluloid Pro - GPU backends: API-independent machinery. See GpuCommon.h.
#include "GpuCommon.h"
#include "kernels/Probe.h"   // C++ build of the probe kernel (reference for the self test)

#include <algorithm>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <set>

namespace cel { namespace gpu {

const char* apiName(Api api) {
    switch (api) {
        case Api::OpenCL: return "OpenCL";
        case Api::CUDA: return "CUDA";
        case Api::Metal: return "Metal";
    }
    return "?";
}

namespace detail {

double laneIdleReleaseMs() {
    static const double ms = [] {
        const char* e = std::getenv("CEL_GPU_LANE_IDLE_MS");
        const double v = e ? std::atof(e) : 0.0;
        return v > 0.0 ? v : 10000.0;
    }();
    return ms;
}

double nowMs() {
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

size_t imageSpanBytes(int w, int h, int strideFloats) {
    if (w <= 0 || h <= 0) return 0;
    const size_t as = size_t(strideFloats < 0 ? -(int64_t)strideFloats : strideFloats);
    return ((size_t(h) - 1) * as + size_t(w) * 4u) * sizeof(float);
}

static std::string bufName(int b) {
    if (b == engine::BUF_SRC) return "src";
    if (b == engine::BUF_DST) return "dst";
    return "buffer " + std::to_string(b);
}

static bool hostImageOk(const char* what, const engine::HostImage& img, const engine::BufferDesc& d,
                        bool allowNeg, std::string* err) {
    char msg[256];
    if (!img.data) { if (err) *err = std::string(what) + " image has no data"; return false; }
    if (img.w <= 0 || img.h <= 0) {
        std::snprintf(msg, sizeof msg, "%s image has invalid size %dx%d", what, img.w, img.h);
        if (err) *err = msg;
        return false;
    }
    if ((d.w != 0 || d.h != 0) && (d.w != img.w || d.h != img.h)) {
        std::snprintf(msg, sizeof msg, "%s image is %dx%d but the plan expects %dx%d", what, img.w,
                      img.h, d.w, d.h);
        if (err) *err = msg;
        return false;
    }
    const int64_t as = img.rowStrideFloats < 0 ? -(int64_t)img.rowStrideFloats : img.rowStrideFloats;
    if (img.rowStrideFloats < 0 && !allowNeg) {
        std::snprintf(msg, sizeof msg,
                      "%s image has a negative row stride (%d floats); GPU buffers of this API "
                      "must be top-down",
                      what, img.rowStrideFloats);
        if (err) *err = msg;
        return false;
    }
    if (as < int64_t(img.w) * 4) {
        std::snprintf(msg, sizeof msg, "%s image row stride %d floats < width*4 = %d", what,
                      img.rowStrideFloats, img.w * 4);
        if (err) *err = msg;
        return false;
    }
    if ((int64_t(img.h) - 1) * as + int64_t(img.w) * 4 > int64_t(INT_MAX)) {
        std::snprintf(msg, sizeof msg,
                      "%s image %dx%d (stride %d) exceeds the 32-bit index range of the kernels",
                      what, img.w, img.h, img.rowStrideFloats);
        if (err) *err = msg;
        return false;
    }
    return true;
}

bool buildSchedule(const engine::Plan& plan, int kernelCount, const engine::HostImage& src,
                   const engine::HostImage& dst, const ScheduleOptions& opt, FrameSchedule* out,
                   std::string* err) {
    using namespace engine;
    char msg[320];
    const int nb = int(plan.buffers.size());
    if (nb < 2) { if (err) *err = "plan has fewer than 2 buffers (src, dst)"; return false; }
    if (plan.passes.empty()) { if (err) *err = "plan has no passes"; return false; }
    if (!hostImageOk("source", src, plan.buffers[BUF_SRC], opt.allowNegativeStride, err)) return false;
    if (!hostImageOk("destination", dst, plan.buffers[BUF_DST], opt.allowNegativeStride, err)) return false;
    if (src.data == dst.data) {
        if (err) *err = "source and destination are the same buffer (in-place rendering is not supported)";
        return false;
    }
    for (int b = 2; b < nb; ++b) {
        const BufferDesc& d = plan.buffers[size_t(b)];
        if (d.w <= 0 || d.h <= 0 || int64_t(d.w) * int64_t(d.h) * 4 > int64_t(INT_MAX)) {
            std::snprintf(msg, sizeof msg, "internal buffer %d has invalid size %dx%d", b, d.w, d.h);
            if (err) *err = msg;
            return false;
        }
    }
    auto bw = [&](int b) { return b == BUF_SRC ? src.w : b == BUF_DST ? dst.w : plan.buffers[size_t(b)].w; };
    auto bh = [&](int b) { return b == BUF_SRC ? src.h : b == BUF_DST ? dst.h : plan.buffers[size_t(b)].h; };
    auto bs = [&](int b) {
        return b == BUF_SRC ? src.rowStrideFloats
             : b == BUF_DST ? dst.rowStrideFloats : plan.buffers[size_t(b)].w * 4;
    };

    const int np = int(plan.passes.size());
    std::vector<int> firstWrite(size_t(nb), -1), lastUse(size_t(nb), -1);
    std::vector<bool> written(size_t(nb), false);
    written[BUF_SRC] = true;
    std::set<int> kernels;
    out->passes.assign(size_t(np), ScheduledPass());
    for (int p = 0; p < np; ++p) {
        const PassDesc& pd = plan.passes[size_t(p)];
        ScheduledPass& sp = out->passes[size_t(p)];
        const int k = int(pd.kernel);
        const bool probe = opt.allowProbe && k == kernelCount;
        if ((k < 0 || k >= kernelCount) && !probe) {
            std::snprintf(msg, sizeof msg, "pass %d: invalid kernel id %d (kernel set has %d)", p, k, kernelCount);
            if (err) *err = msg;
            return false;
        }
        if (pd.out < BUF_DST || pd.out >= nb) {
            std::snprintf(msg, sizeof msg, "pass %d: invalid output buffer %d", p, pd.out);
            if (err) *err = msg;
            return false;
        }
        sp.kernel = k;
        sp.out = pd.out;
        sp.args = pd.args;
        sp.args.outW = bw(pd.out);
        sp.args.outH = bh(pd.out);
        sp.args.outStride = bs(pd.out);
        for (int j = 0; j < CEL_PASS_MAX_INPUTS; ++j) {
            const int b = pd.in[j];
            sp.in[j] = b;
            if (b < 0) {
                if (b != -1) {
                    std::snprintf(msg, sizeof msg, "pass %d: invalid input %d buffer %d", p, j, b);
                    if (err) *err = msg;
                    return false;
                }
                sp.args.inW[j] = sp.args.inH[j] = sp.args.inStride[j] = 0;
                continue;
            }
            if (b >= nb) {
                std::snprintf(msg, sizeof msg, "pass %d: input %d refers to buffer %d (plan has %d)", p, j, b, nb);
                if (err) *err = msg;
                return false;
            }
            if (b == pd.out) {
                std::snprintf(msg, sizeof msg, "pass %d (%s): input %d aliases its output %s", p,
                              k < kernelCount && k >= 0 ? "kernel" : "probe", j, bufName(b).c_str());
                if (err) *err = msg;
                return false;
            }
            if (!written[size_t(b)]) {
                std::snprintf(msg, sizeof msg, "pass %d: input %d reads %s before any pass wrote it", p, j,
                              bufName(b).c_str());
                if (err) *err = msg;
                return false;
            }
            sp.args.inW[j] = bw(b);
            sp.args.inH[j] = bh(b);
            sp.args.inStride[j] = bs(b);
            lastUse[size_t(b)] = p;
        }
        if (firstWrite[size_t(pd.out)] < 0) firstWrite[size_t(pd.out)] = p;
        lastUse[size_t(pd.out)] = std::max(lastUse[size_t(pd.out)], p);
        written[size_t(pd.out)] = true;
        kernels.insert(k);
    }
    if (!written[BUF_DST]) { if (err) *err = "no pass writes the destination image"; return false; }

    // physical slots for internal buffers (liveness based sharing, best fit: the smallest free slot
    // that is large enough, else the largest free slot grows; internal strides come from the plan,
    // never from the slot size, so a larger slot is safe)
    out->bufferSlot.assign(size_t(nb), -2);
    out->bufferSlot[BUF_SRC] = out->bufferSlot[BUF_DST] = -1;
    out->slotBytes.clear();
    std::vector<int> slotOwner;   // plan buffer currently occupying the slot, -1 = free
    for (int p = 0; p < np; ++p) {
        if (opt.aliasInternal) {
            for (size_t s = 0; s < slotOwner.size(); ++s) {
                const int ob = slotOwner[s];
                if (ob >= 0 && lastUse[size_t(ob)] < p) slotOwner[s] = -1;
            }
        }
        const int b = plan.passes[size_t(p)].out;
        if (b < 2 || firstWrite[size_t(b)] != p) continue;
        const size_t bytes = size_t(plan.buffers[size_t(b)].w) * size_t(plan.buffers[size_t(b)].h) * 16u;
        int slot = -1;
        if (opt.aliasInternal) {
            int grow = -1;
            for (size_t s = 0; s < slotOwner.size(); ++s) {
                if (slotOwner[s] >= 0) continue;
                if (out->slotBytes[s] >= bytes) {
                    if (slot < 0 || out->slotBytes[s] < out->slotBytes[size_t(slot)]) slot = int(s);
                } else if (grow < 0 || out->slotBytes[s] > out->slotBytes[size_t(grow)]) {
                    grow = int(s);
                }
            }
            if (slot < 0 && grow >= 0) {
                slot = grow;
                out->slotBytes[size_t(slot)] = bytes;
            }
        }
        if (slot < 0) {
            slot = int(out->slotBytes.size());
            out->slotBytes.push_back(bytes);
            slotOwner.push_back(-1);
        }
        slotOwner[size_t(slot)] = b;
        out->bufferSlot[size_t(b)] = slot;
    }
    out->usedKernels.assign(kernels.begin(), kernels.end());
    out->srcBytesNeeded = imageSpanBytes(src.w, src.h, src.rowStrideFloats);
    out->dstBytesNeeded = imageSpanBytes(dst.w, dst.h, dst.rowStrideFloats);
    return true;
}

StateKey makeStateKey(const std::vector<float>& state, uint64_t version) {
    StateKey k;
    k.version = version;
    k.data = state.data();
    k.count = state.size();
    k.valid = true;
    uint64_t h = 1469598103934665603ULL;
    auto mix = [&](size_t i) {
        uint32_t bits;
        std::memcpy(&bits, &state[i], 4);
        h ^= bits + 0x9e3779b97f4a7c15ULL + (uint64_t(i) << 1);
        h *= 1099511628211ULL;
    };
    const size_t n = state.size();
    for (size_t i = 0; i < std::min<size_t>(n, 64); ++i) mix(i);
    for (size_t i = n > 32 ? n - 32 : 0; i < n; ++i) mix(i);
    if (n > 0)
        for (size_t s = 0; s < 512; ++s) mix(size_t((uint64_t(s) * 2654435761ULL) % n));
    k.sample = h;
    return k;
}

void packImage(const engine::HostImage& img, std::vector<float>* packed) {
    packed->resize(size_t(img.w) * size_t(img.h) * 4u);
    const float* base = static_cast<const float*>(img.data);
    for (int y = 0; y < img.h; ++y)
        std::memcpy(packed->data() + size_t(y) * size_t(img.w) * 4u,
                    base + std::ptrdiff_t(y) * img.rowStrideFloats, size_t(img.w) * 16u);
}

void unpackImage(const std::vector<float>& packed, const engine::HostImage& img) {
    float* base = static_cast<float*>(img.data);
    for (int y = 0; y < img.h; ++y)
        std::memcpy(base + std::ptrdiff_t(y) * img.rowStrideFloats,
                    packed.data() + size_t(y) * size_t(img.w) * 4u, size_t(img.w) * 16u);
}

// ---------------------------------------------------------------------------------------------
BackendStats GpuBackendBase::stats() const {
    std::lock_guard<std::mutex> l(m_statsMutex);
    return m_stats;
}
bool GpuBackendBase::captured(int idx, std::vector<float>* out, int* w, int* h) const {
    std::lock_guard<std::mutex> l(m_statsMutex);
    auto it = m_captured.find(idx);
    if (it == m_captured.end()) return false;
    if (out) *out = it->second.data;
    if (w) *w = it->second.w;
    if (h) *h = it->second.h;
    return true;
}
bool GpuBackendBase::fail(std::string* err, const std::string& msg) {
    {
        std::lock_guard<std::mutex> l(m_statsMutex);
        m_stats.failures++;
        m_stats.lastError = msg;
    }
    if (err) *err = std::string(name()) + ": " + msg;
    return false;
}
void GpuBackendBase::noteFrame(size_t pooledBytes) {
    std::lock_guard<std::mutex> l(m_statsMutex);
    m_stats.frames++;
    m_stats.bytesPooled = pooledBytes;
}
void GpuBackendBase::noteBuild(double ms) {
    std::lock_guard<std::mutex> l(m_statsMutex);
    m_stats.programBuilds++;
    m_stats.lastBuildMs = ms;
}
void GpuBackendBase::noteStateUpload() {
    std::lock_guard<std::mutex> l(m_statsMutex);
    m_stats.stateUploads++;
}
void GpuBackendBase::noteLanes(size_t n) {
    std::lock_guard<std::mutex> l(m_statsMutex);
    m_stats.lanes = n;
}
void GpuBackendBase::noteDevice(const std::string& d) {
    std::lock_guard<std::mutex> l(m_statsMutex);
    m_stats.lastDevice = d;
}
void GpuBackendBase::notePool(const BackendStats& delta) {
    std::lock_guard<std::mutex> l(m_statsMutex);
    m_stats.bufferAllocs += delta.bufferAllocs;
    m_stats.bufferReuses += delta.bufferReuses;
}
void GpuBackendBase::storeCapture(int idx, std::vector<float>&& data, int w, int h) {
    std::lock_guard<std::mutex> l(m_statsMutex);
    Captured& c = m_captured[idx];
    c.data = std::move(data);
    c.w = w;
    c.h = h;
}
void GpuBackendBase::clearCapture() {
    std::lock_guard<std::mutex> l(m_statsMutex);
    m_captured.clear();
}

// ---------------------------------------------------------------------------------------------
engine::Plan makeProbePlan(int kernelCount, std::vector<float>* state, std::vector<float>* srcPixels) {
    engine::Plan plan;
    plan.buffers.resize(2);
    plan.buffers[0] = {4, 2};
    plan.buffers[1] = {kProbeWidth, 1};
    engine::PassDesc pd;
    pd.kernel = engine::KernelId(kernelCount);
    pd.out = engine::BUF_DST;
    pd.in[0] = engine::BUF_SRC;
    pd.in[1] = engine::BUF_SRC;
    pd.in[3] = engine::BUF_SRC;
    pd.args.f[0] = 0.3f;
    pd.args.f[1] = -1.5e-3f;
    pd.args.f[CEL_PASS_NF - 1] = 12345.5f;
    pd.args.i[0] = -7;
    pd.args.i[3] = 1 << 20;
    pd.args.i[CEL_PASS_NI - 1] = 77;
    plan.passes.push_back(pd);
    state->assign(64, 0.f);
    for (size_t i = 0; i < state->size(); ++i) (*state)[i] = 0.5f + 0.25f * float(i);
    srcPixels->resize(4 * 2 * 4);
    for (size_t i = 0; i < srcPixels->size(); ++i) (*srcPixels)[i] = 0.125f * float(i) - 0.5f;
    return plan;
}

bool checkProbe(const std::vector<float>& deviceOut, const std::vector<float>& state,
                const std::vector<float>& srcPixels, std::string* report) {
    std::vector<float> state2 = state, src2 = srcPixels;
    std::vector<float> dummy;
    engine::Plan plan = makeProbePlan(0, &state2, &src2);
    // reference: run the C++ build of the probe with the args the backend fills in
    CelPassArgs a = plan.passes[0].args;
    a.outW = kProbeWidth; a.outH = 1; a.outStride = kProbeWidth * 4;
    for (int j = 0; j < CEL_PASS_MAX_INPUTS; ++j) {
        const bool used = plan.passes[0].in[j] >= 0;
        a.inW[j] = used ? 4 : 0; a.inH[j] = used ? 2 : 0; a.inStride[j] = used ? 16 : 0;
    }
    std::vector<float> ref(size_t(kProbeWidth) * 4u, 0.f);
    for (int x = 0; x < kProbeWidth; ++x)
        cel_k_gpu_probe(x, 0, &a, ref.data(), srcPixels.data(), srcPixels.data(), nullptr,
                        srcPixels.data(), state.data());
    if (deviceOut.size() < ref.size()) {
        if (report) *report = "probe: device output too small";
        return false;
    }
    double worst = 0.0;
    int worstPx = -1;
    for (int x = 0; x < kProbeWidth; ++x) {
        const bool exact = (x <= 3) || x == 7 || x == 9;
        for (int c = 0; c < 4; ++c) {
            const float r = ref[size_t(x) * 4u + size_t(c)], d = deviceOut[size_t(x) * 4u + size_t(c)];
            double e;
            if (!std::isfinite(r) || !std::isfinite(d)) e = (std::isfinite(r) == std::isfinite(d)) ? 0.0 : 1e30;
            else if (exact) e = (r == d) ? 0.0 : 1e30;
            else e = std::fabs(double(r) - double(d)) / std::max(1.0, std::fabs(double(r)));
            if (x == 7 && c >= 2) e = std::fabs(double(r) - double(d)) / std::max(1.0, std::fabs(double(r)));
            if (e > worst) { worst = e; worstPx = x * 4 + c; }
        }
    }
    const bool ok = worst <= 2e-5;
    if (report) {
        char msg[200];
        if (ok)
            std::snprintf(msg, sizeof msg, "probe OK (worst relative error %.3g)", worst);
        else
            std::snprintf(msg, sizeof msg, "probe MISMATCH at pixel %d channel %d: device %.9g vs cpu %.9g (rel %.3g)",
                          worstPx / 4, worstPx % 4, double(deviceOut[size_t(worstPx)]),
                          double(ref[size_t(worstPx)]), worst);
        *report = msg;
    }
    return ok;
}

} // namespace detail

// ---- public dispatchers --------------------------------------------------------------------------
SelfTestResult selfTest(Api api, bool full) {
    switch (api) {
        case Api::OpenCL: return detail::selfTestOpenCL(full);
        case Api::CUDA: return detail::selfTestCuda(full);
        case Api::Metal: return detail::selfTestMetal(full);
    }
    return SelfTestResult();
}

bool cpuFallback(Api api, engine::Backend& cpu, const engine::Plan& plan, const std::vector<float>& st,
                 uint64_t ver, const engine::HostImage& src, const engine::HostImage& dst,
                 const engine::GpuContext& gpu, std::string* err) {
    switch (api) {
        case Api::OpenCL: return detail::cpuFallbackOpenCL(cpu, plan, st, ver, src, dst, gpu, err);
        case Api::CUDA: return detail::cpuFallbackCuda(cpu, plan, st, ver, src, dst, gpu, err);
        case Api::Metal: return detail::cpuFallbackMetal(cpu, plan, st, ver, src, dst, gpu, err);
    }
    return false;
}

void releaseAllResources() {
    if (auto* b = detail::productionOpenCLIfCreated()) b->releaseResources();
    if (auto* b = detail::productionCudaIfCreated()) b->releaseResources();
    if (auto* b = detail::productionMetalIfCreated()) b->releaseResources();
}

bool backendStats(const engine::Backend* backend, BackendStats* out) {
    auto* b = dynamic_cast<const detail::GpuBackendBase*>(backend);
    if (!b) return false;
    if (out) *out = b->stats();
    return true;
}

void setDebugCapture(engine::Backend* backend, bool on) {
    if (auto* b = dynamic_cast<detail::GpuBackendBase*>(backend)) b->setDebugCapture(on);
}

void setDebugPoison(engine::Backend* backend, bool on) {
    if (auto* b = dynamic_cast<detail::GpuBackendBase*>(backend)) b->setDebugPoison(on);
}

bool debugCapturedBuffer(const engine::Backend* backend, int bufferIndex, std::vector<float>* out, int* w,
                         int* h) {
    auto* b = dynamic_cast<const detail::GpuBackendBase*>(backend);
    return b && b->captured(bufferIndex, out, w, h);
}

}} // namespace cel::gpu
