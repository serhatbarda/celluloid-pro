// Celluloid Pro - CUDA backend (owned by the gpu role). See src/gpu/README.md.
//
// The host (DaVinci Resolve) makes its CUDA context current on the render thread and passes
// device pointers + (optionally) its cudaStream_t. We link the CUDA runtime statically; the
// runtime adopts the context current on the thread (the device's primary context), so device
// pointers and the stream are directly usable.
//
//   Lane per (device, stream): internal buffer pool, device state buffer + content key, pinned
//        staging blocks for asynchronous state uploads, per-kernel launch configuration.
//   Ordering: everything is enqueued on the host's stream; with a stream we never synchronise
//        (OFX: "SHOULD NOT wait"). Without a stream (legacy default stream) the OFX rule is that
//        the work must be complete on return, so we synchronise the default stream at the end.
//   Memory: stream-ordered allocation (cudaMallocAsync/cudaFreeAsync) where the device supports
//        memory pools, so neither allocation nor release ever synchronises the device; plain
//        cudaMalloc/cudaFree otherwise (release only when a pool trims, i.e. on size changes).
#include "../GpuCommon.h"

#include <cuda_runtime_api.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace cel { namespace gpu { namespace detail {

namespace {

std::string cuErr(const char* what, cudaError_t e) {
    return std::string(what) + " failed: " + cudaGetErrorName(e) + " (" + cudaGetErrorString(e) + ")";
}

struct DeviceInfo {
    bool memPools = false;
    std::string name;
};

} // namespace

class CudaBackend final : public GpuBackendBase {
public:
    explicit CudaBackend(const KernelSet& ks) : GpuBackendBase(ks) {}
    ~CudaBackend() override { releaseResources(); }
    const char* name() const override { return "CUDA"; }

    bool execute(const engine::Plan& plan, const std::vector<float>& stateBuf, uint64_t stateVersion,
                 const engine::HostImage& src, const engine::HostImage& dst, const engine::GpuContext& gpu,
                 std::string* err) override {
        return executeImpl(plan, stateBuf, stateVersion, src, dst, gpu, false, err);
    }
    bool executeImpl(const engine::Plan& plan, const std::vector<float>& stateBuf, uint64_t stateVersion,
                     const engine::HostImage& src, const engine::HostImage& dst,
                     const engine::GpuContext& gpu, bool allowProbe, std::string* err);
    void releaseResources() override;

private:
    struct Staging {
        void* host = nullptr;           // pinned
        size_t cap = 0;
        cudaEvent_t ev = nullptr;
        bool used = false;              // an upload was recorded on ev
    };
    struct Lane {
        int device = 0;
        cudaStream_t stream = nullptr;
        std::mutex m;
        bool memPools = false;
        SizedPool<void*> pool;
        void* state = nullptr;
        size_t stateBytes = 0;
        StateKey stateKey;
        std::vector<Staging> staging;
        std::vector<int> block;          // per kernel: block.y (block.x = 32); 0 = not queried
        uint64_t frame = 0;
        double lastUseMs = 0.0;
    };
    bool deviceAlloc(Lane& l, size_t bytes, void** out, std::string* err);
    void deviceFree(Lane& l, void* p);
    bool uploadState(Lane& l, const std::vector<float>& st, std::string* err);
    void releaseLane(Lane& l);
    const DeviceInfo& deviceInfo(int dev);

    std::mutex m_mapMutex;
    std::map<std::pair<int, cudaStream_t>, std::shared_ptr<Lane>> m_lanes;
    std::map<int, DeviceInfo> m_devices;
};

const DeviceInfo& CudaBackend::deviceInfo(int dev) {
    std::lock_guard<std::mutex> lk(m_mapMutex);
    auto it = m_devices.find(dev);
    if (it != m_devices.end()) return it->second;
    DeviceInfo di;
    cudaDeviceProp prop;
    if (cudaGetDeviceProperties(&prop, dev) == cudaSuccess) {
        char b[64];
        std::snprintf(b, sizeof b, " (sm_%d%d, %d MB)", prop.major, prop.minor, int(prop.totalGlobalMem >> 20));
        di.name = std::string(prop.name) + b;
    }
#if CUDART_VERSION >= 11020
    int pools = 0;
    if (cudaDeviceGetAttribute(&pools, cudaDevAttrMemoryPoolsSupported, dev) == cudaSuccess) di.memPools = pools != 0;
#endif
    cudaGetLastError();
    return m_devices.emplace(dev, di).first->second;
}

bool CudaBackend::deviceAlloc(Lane& l, size_t bytes, void** out, std::string* err) {
    cudaError_t e;
#if CUDART_VERSION >= 11020
    if (l.memPools) e = cudaMallocAsync(out, bytes, l.stream);
    else
#endif
        e = cudaMalloc(out, bytes);
    if (e != cudaSuccess) {
        cudaGetLastError();   // clear the (non-sticky) allocation error
        *out = nullptr;
        if (err) *err = cuErr("cudaMalloc", e);
        return false;
    }
    return true;
}

void CudaBackend::deviceFree(Lane& l, void* p) {
    if (!p) return;
#if CUDART_VERSION >= 11020
    if (l.memPools) { cudaFreeAsync(p, l.stream); return; }
#endif
    cudaFree(p);
}

bool CudaBackend::uploadState(Lane& l, const std::vector<float>& st, std::string* err) {
    const size_t bytes = st.size() * sizeof(float);
    if (bytes == 0) return true;
    // a staging block whose previous upload has completed (or was never used) and is big enough
    Staging* s = nullptr;
    for (auto& c : l.staging) {
        if (c.cap >= bytes && (!c.used || cudaEventQuery(c.ev) == cudaSuccess)) { s = &c; break; }
    }
    cudaGetLastError();   // cudaEventQuery(cudaErrorNotReady) is not an error
    if (!s) {
        if (l.staging.size() >= 4) {
            // bound pinned memory: wait for the oldest block (only if 4 uploads are in flight)
            Staging& o = l.staging.front();
            cudaEventSynchronize(o.ev);
            if (o.cap < bytes) {
                cudaFreeHost(o.host);
                o.host = nullptr;
                o.cap = 0;
                if (cudaMallocHost(&o.host, bytes) != cudaSuccess) { cudaGetLastError(); o.host = nullptr; }
                else o.cap = bytes;
            }
            if (o.host) s = &o;
        } else {
            Staging n;
            const size_t cap = bytes + bytes / 8;   // headroom for small layout growth
            if (cudaMallocHost(&n.host, cap) == cudaSuccess &&
                cudaEventCreateWithFlags(&n.ev, cudaEventDisableTiming) == cudaSuccess) {
                n.cap = cap;
                l.staging.push_back(n);
                s = &l.staging.back();
            } else {
                cudaGetLastError();
                if (n.host) cudaFreeHost(n.host);
            }
        }
    }
    cudaError_t e;
    if (s) {
        std::memcpy(s->host, st.data(), bytes);
        e = cudaMemcpyAsync(l.state, s->host, bytes, cudaMemcpyHostToDevice, l.stream);
        if (e == cudaSuccess) e = cudaEventRecord(s->ev, l.stream);
        s->used = true;
    } else {
        // no pinned memory available: pageable async copy (the runtime stages it before returning)
        e = cudaMemcpyAsync(l.state, st.data(), bytes, cudaMemcpyHostToDevice, l.stream);
    }
    if (e != cudaSuccess) {
        if (err) *err = cuErr("cudaMemcpyAsync(state)", e);
        return false;
    }
    return true;
}

void CudaBackend::releaseLane(Lane& l) {
    int prev = 0;
    cudaGetDevice(&prev);
    cudaSetDevice(l.device);
    for (auto& s : l.staging) {
        if (s.ev) { cudaEventSynchronize(s.ev); cudaEventDestroy(s.ev); }
        if (s.host) cudaFreeHost(s.host);
    }
    l.staging.clear();
    l.pool.clear([&](void* p) { deviceFree(l, p); });
    deviceFree(l, l.state);
    l.state = nullptr;
    l.stateBytes = 0;
    l.stateKey = StateKey();
    l.block.clear();
    cudaSetDevice(prev);
    cudaGetLastError();
}

void CudaBackend::releaseResources() {
    std::lock_guard<std::mutex> lk(m_mapMutex);
    for (auto& kv : m_lanes) {
        std::lock_guard<std::mutex> ll(kv.second->m);
        releaseLane(*kv.second);
    }
    m_lanes.clear();
}

bool CudaBackend::executeImpl(const engine::Plan& plan, const std::vector<float>& stateBuf,
                              uint64_t stateVersion, const engine::HostImage& src,
                              const engine::HostImage& dst, const engine::GpuContext& gpu,
                              bool allowProbe, std::string* err) {
    if (!m_ks.cudaEntries) return fail(err, "no CUDA kernels compiled into this build");
    cudaStream_t stream = static_cast<cudaStream_t>(gpu.cudaStream);
    int dev = 0;
    cudaError_t e = cudaGetDevice(&dev);
    if (e != cudaSuccess) return fail(err, cuErr("cudaGetDevice", e));

    FrameSchedule fs;
    ScheduleOptions so;
    so.allowNegativeStride = true;   // device pointers address rows in both directions
    so.allowProbe = allowProbe;
    so.aliasInternal = !debugCapture();
    std::string serr;
    if (!buildSchedule(plan, m_ks.count, src, dst, so, &fs, &serr)) return fail(err, "invalid plan: " + serr);

    // the images must be device memory of the current device (catches CPU pointers)
    for (int i = 0; i < 2; ++i) {
        const void* p = i == 0 ? src.data : dst.data;
        cudaPointerAttributes pa;
        std::memset(&pa, 0, sizeof pa);
        e = cudaPointerGetAttributes(&pa, p);
        if (e != cudaSuccess) {
            cudaGetLastError();
            return fail(err, cuErr(i == 0 ? "cudaPointerGetAttributes(source)" : "cudaPointerGetAttributes(destination)", e));
        }
#if CUDART_VERSION >= 10000
        const bool deviceMem = pa.type == cudaMemoryTypeDevice || pa.type == cudaMemoryTypeManaged;
#else
        const bool deviceMem = pa.memoryType == cudaMemoryTypeDevice;
#endif
        if (!deviceMem)
            return fail(err, std::string(i == 0 ? "source" : "destination") + " image is not CUDA device memory");
        if (pa.device != dev)
            return fail(err, std::string(i == 0 ? "source" : "destination") + " image lives on CUDA device " +
                                 std::to_string(pa.device) + " but device " + std::to_string(dev) + " is current");
    }

    const DeviceInfo& di = deviceInfo(dev);
    std::shared_ptr<Lane> lane;
    {
        std::lock_guard<std::mutex> lk(m_mapMutex);
        auto& slot = m_lanes[std::make_pair(dev, stream)];
        if (!slot) {
            slot = std::make_shared<Lane>();
            slot->device = dev;
            slot->stream = stream;
            slot->memPools = di.memPools;
            noteDevice(di.name);
        }
        lane = slot;
    }

    std::lock_guard<std::mutex> ll(lane->m);
    Lane& l = *lane;
    l.frame++;
    l.lastUseMs = nowMs();
    if (l.block.empty()) l.block.assign(size_t(m_ks.count) + 1, 0);

    // launch configuration per kernel: 32 x N threads within the kernel's register budget
    for (int kid : fs.usedKernels) {
        if (l.block[size_t(kid)]) continue;
        cudaFuncAttributes fa;
        e = cudaFuncGetAttributes(&fa, m_ks.cudaEntries[kid]);
        if (e != cudaSuccess) {
            cudaGetLastError();
            return fail(err, cuErr("cudaFuncGetAttributes", e) + " for " + m_ks.names[kid] +
                                 " (no kernel image for this GPU architecture?)");
        }
        l.block[size_t(kid)] = std::max(1, std::min(8, fa.maxThreadsPerBlock / 32));
    }

    std::vector<void*> slots;
    BackendStats delta;
    auto allocFn = [&](size_t bytes, void** out, std::string* aerr) { return deviceAlloc(l, bytes, out, aerr); };
    auto freeFn = [&](void* p) { deviceFree(l, p); };
    if (!l.pool.acquire(fs.slotBytes, l.frame, &slots, allocFn, freeFn, &delta, &serr)) return fail(err, serr);
    notePool(delta);

    const StateKey key = makeStateKey(stateBuf, stateVersion);
    const size_t stateBytes = std::max<size_t>(16, stateBuf.size() * sizeof(float));
    if (!l.state || l.stateBytes < stateBytes) {
        deviceFree(l, l.state);   // stream ordered (or synchronous cudaFree): never in use afterwards
        l.state = nullptr;
        l.stateBytes = 0;
        l.stateKey = StateKey();
        if (!deviceAlloc(l, stateBytes, &l.state, &serr)) return fail(err, "state buffer: " + serr);
        l.stateBytes = stateBytes;
    }
    if (l.stateKey != key) {
        if (!uploadState(l, stateBuf, &serr)) { l.stateKey = StateKey(); return fail(err, serr); }
        l.stateKey = key;
        noteStateUpload();
    }

    auto ptrOf = [&](int b) -> void* {
        if (b == engine::BUF_SRC) return src.data;
        if (b == engine::BUF_DST) return dst.data;
        return slots[size_t(fs.bufferSlot[size_t(b)])];
    };
    for (size_t p = 0; p < fs.passes.size(); ++p) {
        const ScheduledPass& sp = fs.passes[p];
        CelPassArgs args = sp.args;
        float* outP = static_cast<float*>(ptrOf(sp.out));
        const float* inP[CEL_PASS_MAX_INPUTS];
        for (int j = 0; j < CEL_PASS_MAX_INPUTS; ++j)
            inP[j] = sp.in[j] >= 0 ? static_cast<const float*>(ptrOf(sp.in[j])) : nullptr;
        const float* stP = static_cast<const float*>(l.state);
        void* kargs[7] = {&args, &outP, &inP[0], &inP[1], &inP[2], &inP[3], &stP};
        const unsigned by = unsigned(l.block[size_t(sp.kernel)]);
        const dim3 block(32, by, 1);
        const dim3 grid((unsigned(args.outW) + 31u) / 32u, (unsigned(args.outH) + by - 1u) / by, 1);
        e = cudaLaunchKernel(m_ks.cudaEntries[sp.kernel], grid, block, kargs, 0, stream);
        if (e != cudaSuccess) {
            cudaGetLastError();
            return fail(err, cuErr("cudaLaunchKernel", e) + " for " + m_ks.names[sp.kernel] + " (" +
                                 std::to_string(args.outW) + "x" + std::to_string(args.outH) + ")");
        }
    }
    if (!stream) {
        // no host stream: OFX requires the work to be complete when render returns
        e = cudaStreamSynchronize(nullptr);
        if (e != cudaSuccess) return fail(err, cuErr("cudaStreamSynchronize", e));
    } else {
        e = cudaPeekAtLastError();
        if (e != cudaSuccess) return fail(err, cuErr("CUDA error after launch", e));
    }

    if (debugCapture()) {
        clearCapture();
        cudaStreamSynchronize(stream);
        for (size_t b = 1; b < fs.bufferSlot.size(); ++b) {
            if (b >= 2 && fs.bufferSlot[b] < 0) continue;
            const int w = b == 1 ? dst.w : plan.buffers[b].w, h = b == 1 ? dst.h : plan.buffers[b].h;
            const int stride = b == 1 ? dst.rowStrideFloats : w * 4;
            std::vector<float> data(size_t(w) * size_t(h) * 4u);
            bool ok = true;
            for (int y = 0; y < h && ok; ++y)
                ok = cudaMemcpy(data.data() + size_t(y) * size_t(w) * 4u,
                                static_cast<const float*>(ptrOf(int(b))) + std::ptrdiff_t(y) * stride,
                                size_t(w) * 16u, cudaMemcpyDeviceToHost) == cudaSuccess;
            if (ok) storeCapture(int(b), std::move(data), w, h);
        }
    }
    l.pool.trim(l.frame, kPoolMaxIdleFrames, freeFn);
    noteFrame(l.pool.bytes());
    return true;
}

// ---------------------------------------------------------------------------------------------
namespace {
std::atomic<CudaBackend*> g_production{nullptr};
}
GpuBackendBase* productionCudaIfCreated() { return g_production.load(); }

engine::Backend* productionCuda() {
    // Intentionally leaked (see OpenCL): the CUDA runtime tears itself down at exit.
    static CudaBackend* b = [] {
        auto* p = new CudaBackend(productionKernelSet());
        g_production.store(p);
        return p;
    }();
    return b;
}

std::unique_ptr<engine::Backend> createCudaBackendImpl(const KernelSet& ks) {
    return std::unique_ptr<engine::Backend>(new CudaBackend(ks));
}

SelfTestResult selfTestCuda(bool full) {
    SelfTestResult r;
    r.compiledIn = true;
    const double t0 = nowMs();
    int n = 0;
    cudaError_t e = cudaGetDeviceCount(&n);
    if (e != cudaSuccess || n <= 0) {
        int drv = 0;
        cudaDriverGetVersion(&drv);
        r.message = e != cudaSuccess ? cuErr("cudaGetDeviceCount", e) : std::string("no CUDA device");
        char b[96];
        std::snprintf(b, sizeof b, " (driver supports CUDA %d.%d, runtime %d.%d)", drv / 1000, (drv % 1000) / 10,
                      CUDART_VERSION / 1000, (CUDART_VERSION % 1000) / 10);
        r.message += b;
        cudaGetLastError();
        r.ms = nowMs() - t0;
        return r;
    }
    std::string listing;
    for (int d = 0; d < n; ++d) {
        cudaDeviceProp prop;
        if (cudaGetDeviceProperties(&prop, d) == cudaSuccess) {
            char b[64];
            std::snprintf(b, sizeof b, " (sm_%d%d)", prop.major, prop.minor);
            listing += (listing.empty() ? "" : "; ") + std::string(prop.name) + b;
        }
    }
    r.message = std::to_string(n) + " CUDA device(s): " + listing;
    if (!full) { r.ok = true; r.ms = nowMs() - t0; return r; }

    int prev = 0;
    cudaGetDevice(&prev);
    cudaSetDevice(0);
    std::string report;
    bool ok = false;
    cudaStream_t s = nullptr;
    void *sm = nullptr, *dm = nullptr;
    std::vector<float> state, srcPx;
    engine::Plan plan = makeProbePlan(productionKernelSet().count, &state, &srcPx);
    if ((e = cudaStreamCreateWithFlags(&s, cudaStreamNonBlocking)) == cudaSuccess &&
        (e = cudaMalloc(&sm, srcPx.size() * 4)) == cudaSuccess &&
        (e = cudaMalloc(&dm, size_t(kProbeWidth) * 16u)) == cudaSuccess &&
        (e = cudaMemcpy(sm, srcPx.data(), srcPx.size() * 4, cudaMemcpyHostToDevice)) == cudaSuccess) {
        CudaBackend be(productionKernelSet());
        engine::HostImage hs, hd;
        hs.data = sm; hs.w = 4; hs.h = 2; hs.rowStrideFloats = 16;
        hd.data = dm; hd.w = kProbeWidth; hd.h = 1; hd.rowStrideFloats = kProbeWidth * 4;
        engine::GpuContext g;
        g.cudaStream = s;
        std::string eerr;
        if (be.executeImpl(plan, state, 1, hs, hd, g, true, &eerr)) {
            std::vector<float> outPx(size_t(kProbeWidth) * 4u);
            e = cudaStreamSynchronize(s);
            if (e == cudaSuccess) e = cudaMemcpy(outPx.data(), dm, outPx.size() * 4, cudaMemcpyDeviceToHost);
            if (e == cudaSuccess) ok = checkProbe(outPx, state, srcPx, &report);
            else report = cuErr("probe readback", e);
        } else {
            report = eerr;
        }
    } else {
        report = cuErr("CUDA setup", e);
    }
    if (sm) cudaFree(sm);
    if (dm) cudaFree(dm);
    if (s) cudaStreamDestroy(s);
    cudaGetLastError();
    cudaSetDevice(prev);
    r.ok = ok;
    r.message += " | full test on device 0: " + report;
    r.ms = nowMs() - t0;
    return r;
}

static bool copyRows(void* dstBase, int dstStride, const void* srcBase, int srcStride, int w, int h,
                     cudaMemcpyKind kind, cudaStream_t s) {
    const size_t rowBytes = size_t(w) * 16u;
    if (dstStride > 0 && srcStride > 0)
        return cudaMemcpy2DAsync(dstBase, size_t(dstStride) * 4u, srcBase, size_t(srcStride) * 4u, rowBytes,
                                 size_t(h), kind, s) == cudaSuccess;
    for (int y = 0; y < h; ++y)
        if (cudaMemcpyAsync(static_cast<float*>(dstBase) + std::ptrdiff_t(y) * dstStride,
                            static_cast<const float*>(srcBase) + std::ptrdiff_t(y) * srcStride, rowBytes, kind,
                            s) != cudaSuccess)
            return false;
    return true;
}

bool cpuFallbackCuda(engine::Backend& cpu, const engine::Plan& plan, const std::vector<float>& st, uint64_t ver,
                     const engine::HostImage& src, const engine::HostImage& dst, const engine::GpuContext& gpu,
                     std::string* err) {
    auto bad = [&](const std::string& m) { cudaGetLastError(); if (err) *err = "CUDA CPU fallback: " + m; return false; };
    if (!src.data || !dst.data) return bad("missing images");
    cudaStream_t s = static_cast<cudaStream_t>(gpu.cudaStream);
    std::vector<float> hs(size_t(src.w) * size_t(src.h) * 4u), hd(size_t(dst.w) * size_t(dst.h) * 4u, 0.f);
    if (!copyRows(hs.data(), src.w * 4, src.data, src.rowStrideFloats, src.w, src.h, cudaMemcpyDeviceToHost, s))
        return bad("download failed");
    cudaError_t e = cudaStreamSynchronize(s);
    if (e != cudaSuccess) return bad(cuErr("cudaStreamSynchronize", e));
    engine::HostImage cs, cd;
    cs.data = hs.data(); cs.w = src.w; cs.h = src.h; cs.rowStrideFloats = src.w * 4;
    cd.data = hd.data(); cd.w = dst.w; cd.h = dst.h; cd.rowStrideFloats = dst.w * 4;
    std::string cerr;
    if (!cpu.execute(plan, st, ver, cs, cd, engine::GpuContext(), &cerr)) return bad("CPU backend: " + cerr);
    if (!copyRows(dst.data, dst.rowStrideFloats, hd.data(), dst.w * 4, dst.w, dst.h, cudaMemcpyHostToDevice, s))
        return bad("upload failed");
    e = cudaStreamSynchronize(s);   // the host vector dies on return
    if (e != cudaSuccess) return bad(cuErr("cudaStreamSynchronize", e));
    return true;
}

}}} // namespace cel::gpu::detail
