// Reafilm Pro - CPU backend (texture agent).
//
// Executes any engine::Plan by calling the portable kernel functions (src/kernels/AllKernels.h)
// directly: one switch over KernelId selects a row-loop instantiated per kernel (so each kernel
// is inlined into its own loop), rows are distributed over all hardware threads with
// cel::parallelRows (tiny passes run on the calling thread to avoid thread start-up costs).
//
// Host images: HostImage::data points at pixel (0, 0); row y starts at data + y * rowStrideFloats.
// The stride may be larger than w * 4 (padding) or negative (bottom-up hosts).
//
// Internal buffers come from a process-wide pool keyed by size and are reused across frames and
// across concurrent execute() calls (the pool is mutex protected; buffers are checked out for the
// duration of one execute). Idle memory above kPoolIdleLimit is released, least recently used first.
//
// Thread safety: execute() is re-entrant (no shared mutable state except the locked pool).
// Note for hosts that render several frames concurrently: every execute() uses all hardware
// threads, so the OFX layer should not additionally run many CPU renders in parallel.
#include "Engine.h"
#include "../kernels/AllKernels.h"
#include "../texture/TexturePlan.h"
#include "../common/Common.h"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <utility>

namespace cel { namespace engine {

namespace {

using RowFn = void (*)(const CelPassArgs*, float*, const float*, const float*, const float*,
                       const float*, const float*, int, int);

template <void (*K)(CEL_KERNEL_PARAMS)>
void runRows(const CelPassArgs* a, float* out, const float* in0, const float* in1, const float* in2,
             const float* in3, const float* state, int y0, int y1) {
    const int w = a->outW;
    for (int y = y0; y < y1; ++y)
        for (int x = 0; x < w; ++x) K(x, y, a, out, in0, in1, in2, in3, state);
}

RowFn rowFunction(KernelId id) {
    switch (id) {
#define CEL_KERNEL(name) case KID_##name: return &runRows<&cel_k_##name>;
#include "../kernels/KernelList.h"
#undef CEL_KERNEL
        default: return nullptr;
    }
}

// ---- buffer pool --------------------------------------------------------------------------------
constexpr size_t kPoolIdleLimit = size_t(1536) << 20;   // 1.5 GiB of idle buffers at most

struct PoolBuf {
    std::unique_ptr<float[]> data;
    size_t floats = 0;
    uint64_t lastUse = 0;    // pool clock at release (LRU eviction)
};

class BufferPool {
public:
    PoolBuf acquire(int w, int h) {
        const size_t n = size_t(w) * size_t(h) * 4u;
        {
            std::lock_guard<std::mutex> lk(m_mtx);
            auto it = m_free.find(n);
            if (it != m_free.end() && !it->second.empty()) {
                PoolBuf b = std::move(it->second.back());   // most recently used of this size
                it->second.pop_back();
                m_idleBytes -= n * sizeof(float);
                --m_idleCount;
                noteOut(n);
                return b;
            }
            ++m_allocations;
            noteOut(n);
        }
        PoolBuf b;
        b.data.reset(new float[n]);   // uninitialised: every pass writes its whole output
        b.floats = n;
        return b;
    }
    void release(PoolBuf&& b) {
        if (!b.data) return;
        std::lock_guard<std::mutex> lk(m_mtx);
        b.lastUse = ++m_clock;
        const size_t outBytes = b.floats * sizeof(float);
        m_outBytes = m_outBytes > outBytes ? m_outBytes - outBytes : 0;
        // idle limit: at least the working set of the most recent frame (an 8K Full plan checks out
        // ~2.5 GiB; a fixed 1.5 GiB limit evicted and re-allocated a full-res buffer every frame)
        const size_t limit = std::max(kPoolIdleLimit, std::max(m_recentPeak, m_framePeak));
        if (m_outBytes == 0) {
            m_recentPeak = m_framePeak;
            m_framePeak = 0;
        }
        m_idleBytes += b.floats * sizeof(float);
        ++m_idleCount;
        m_free[b.floats].push_back(std::move(b));   // vectors stay sorted by lastUse
        // trim: evict the least recently used idle buffers first (the working set of the frames
        // being rendered is always the most recent, so it survives)
        while (m_idleBytes > limit) {
            auto oldest = m_free.end();
            for (auto it = m_free.begin(); it != m_free.end(); ++it)
                if (!it->second.empty() && (oldest == m_free.end() || it->second.front().lastUse < oldest->second.front().lastUse))
                    oldest = it;
            if (oldest == m_free.end()) break;
            m_idleBytes -= oldest->second.front().floats * sizeof(float);
            --m_idleCount;
            oldest->second.erase(oldest->second.begin());
            if (oldest->second.empty()) m_free.erase(oldest);
        }
    }
    void trim() {
        std::lock_guard<std::mutex> lk(m_mtx);
        m_free.clear();
        m_idleBytes = 0;
        m_idleCount = 0;
    }
    tex::CpuPoolStats stats() {
        std::lock_guard<std::mutex> lk(m_mtx);
        tex::CpuPoolStats s;
        s.allocations = m_allocations;
        s.idleBuffers = m_idleCount;
        s.idleBytes = m_idleBytes;
        return s;
    }

private:
    void noteOut(size_t floats) {   // under m_mtx
        m_outBytes += floats * sizeof(float);
        m_framePeak = std::max(m_framePeak, m_outBytes);
    }
    std::mutex m_mtx;
    size_t m_outBytes = 0, m_framePeak = 0, m_recentPeak = 0;   // checked-out bytes, peaks
    std::map<size_t, std::vector<PoolBuf>> m_free;   // key = float count
    size_t m_idleBytes = 0, m_idleCount = 0, m_allocations = 0;
    uint64_t m_clock = 0;
};

BufferPool& pool() {
    static BufferPool* p = new BufferPool();   // intentionally leaked: safe at process exit
    return *p;
}

// RAII checkout of the internal buffers of one execute()
struct Checkout {
    std::vector<PoolBuf> bufs;
    ~Checkout() { for (PoolBuf& b : bufs) pool().release(std::move(b)); }
};

constexpr long long kMinParallelPixels = 24 * 1024;   // smaller passes run on the calling thread

std::atomic<bool> g_profile{false};
std::mutex g_profMtx;
std::vector<tex::PassTiming> g_lastProfile;

double processCpuMs() { return 1000.0 * double(std::clock()) / double(CLOCKS_PER_SEC); }

class CpuBackend final : public Backend {
public:
    const char* name() const override { return "CPU"; }

    bool execute(const Plan& plan, const std::vector<float>& stateBuf, uint64_t /*stateVersion*/,
                 const HostImage& src, const HostImage& dst, const GpuContext& /*gpu*/,
                 std::string* err) override {
        auto fail = [&](const std::string& m) { if (err) *err = m; return false; };
        if (plan.buffers.size() < 2) return fail("CPU backend: plan has no src/dst buffers");
        const BufferDesc& sd = plan.buffers[BUF_SRC];
        const BufferDesc& dd = plan.buffers[BUF_DST];
        bool readsSrc = false;
        for (const PassDesc& p : plan.passes)
            for (int k = 0; k < CEL_PASS_MAX_INPUTS; ++k) readsSrc = readsSrc || p.in[k] == BUF_SRC;
        if (readsSrc) {
            if (!src.data) return fail("CPU backend: source image missing");
            if (src.w != sd.w || src.h != sd.h) return fail("CPU backend: source size does not match the plan");
            if (std::llabs((long long)src.rowStrideFloats) < (long long)src.w * 4)
                return fail("CPU backend: source row stride smaller than a row");
        }
        if (!dst.data) return fail("CPU backend: destination image missing");
        if (dst.w != dd.w || dst.h != dd.h) return fail("CPU backend: destination size does not match the plan");
        if (std::llabs((long long)dst.rowStrideFloats) < (long long)dst.w * 4)
            return fail("CPU backend: destination row stride smaller than a row");

        try {
            Checkout co;
            const size_t nb = plan.buffers.size();
            std::vector<float*> ptr(nb, nullptr);
            std::vector<int> stride(nb, 0);
            ptr[BUF_SRC] = static_cast<float*>(src.data);
            stride[BUF_SRC] = src.rowStrideFloats;
            ptr[BUF_DST] = static_cast<float*>(dst.data);
            stride[BUF_DST] = dst.rowStrideFloats;
            co.bufs.reserve(nb);
            for (size_t i = 2; i < nb; ++i) {
                const BufferDesc& b = plan.buffers[i];
                if (b.w <= 0 || b.h <= 0) return fail("CPU backend: invalid internal buffer size");
                co.bufs.push_back(pool().acquire(b.w, b.h));
                ptr[i] = co.bufs.back().data.get();
                stride[i] = b.w * 4;
            }
            const float* state = stateBuf.empty() ? nullptr : stateBuf.data();
            const bool prof = g_profile.load(std::memory_order_relaxed);
            std::vector<tex::PassTiming> profile;
            for (size_t pi = 0; pi < plan.passes.size(); ++pi) {
                auto t0 = std::chrono::steady_clock::now();
                double c0 = prof ? processCpuMs() : 0.0;
                const PassDesc& p = plan.passes[pi];
                RowFn fn = rowFunction(p.kernel);
                if (!fn) return fail("CPU backend: unknown kernel in pass " + std::to_string(pi));
                if (p.out < 0 || size_t(p.out) >= nb || p.out == BUF_SRC)
                    return fail("CPU backend: invalid output buffer in pass " + std::to_string(pi));
                CelPassArgs a = p.args;
                a.outW = plan.buffers[size_t(p.out)].w;
                a.outH = plan.buffers[size_t(p.out)].h;
                a.outStride = stride[size_t(p.out)];
                const float* in[CEL_PASS_MAX_INPUTS] = {nullptr, nullptr, nullptr, nullptr};
                for (int k = 0; k < CEL_PASS_MAX_INPUTS; ++k) {
                    int b = p.in[k];
                    if (b < 0) { a.inW[k] = a.inH[k] = a.inStride[k] = 0; continue; }
                    if (size_t(b) >= nb) return fail("CPU backend: invalid input buffer in pass " + std::to_string(pi));
                    if (b == p.out) return fail("CPU backend: pass " + std::to_string(pi) + " reads its own output");
                    in[k] = ptr[size_t(b)];
                    a.inW[k] = plan.buffers[size_t(b)].w;
                    a.inH[k] = plan.buffers[size_t(b)].h;
                    a.inStride[k] = stride[size_t(b)];
                }
                float* out = ptr[size_t(p.out)];
                const long long px = (long long)a.outW * (long long)a.outH;
                if (px < kMinParallelPixels || a.outH < 2) {
                    fn(&a, out, in[0], in[1], in[2], in[3], state, 0, a.outH);
                } else {
                    parallelRows(a.outH, [&](int y0, int y1) {
                        fn(&a, out, in[0], in[1], in[2], in[3], state, y0, y1);
                    });
                }
                if (prof)
                    profile.push_back({p.kernel, a.outW, a.outH,
                                       std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(),
                                       processCpuMs() - c0});
            }
            if (prof) { std::lock_guard<std::mutex> lk(g_profMtx); g_lastProfile = std::move(profile); }
        } catch (const std::bad_alloc&) {
            return fail("CPU backend: out of memory");
        } catch (const std::exception& e) {
            return fail(std::string("CPU backend: ") + e.what());
        }
        return true;
    }
};

} // namespace

Backend& cpuBackend() {
    static CpuBackend b;
    return b;
}

}} // namespace cel::engine

namespace cel { namespace tex {
CpuPoolStats cpuBackendPoolStats() { return engine::pool().stats(); }
void cpuBackendTrimPool() { engine::pool().trim(); }
void cpuBackendProfile(bool enable) { engine::g_profile.store(enable); }
std::vector<PassTiming> cpuBackendLastProfile() {
    std::lock_guard<std::mutex> lk(engine::g_profMtx);
    return engine::g_lastProfile;
}
}} // namespace cel::tex
