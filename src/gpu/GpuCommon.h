// Reafilm Pro - GPU backends: API-independent machinery (internal to src/gpu).
//   - FrameSchedule: validates a Plan against the host images, fills the per-pass CelPassArgs
//     (sizes/strides), and assigns internal buffers to physical slots by liveness (internal buffers
//     whose lifetimes do not overlap share memory).
//   - SizedPool: per-queue pool of device buffers keyed by byte size, reused across frames,
//     trimmed when idle.
//   - StateKey: identifies a state buffer content (version + identity + sampled checksum).
//   - GpuBackendBase: stats, debug capture, error bookkeeping shared by all backends.
#pragma once
#include "GpuBackends.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace cel { namespace gpu { namespace detail {

static_assert(sizeof(CelPassArgs) == 220, "CelPassArgs must be 220 bytes (KernelABI.h)");

// --------------------------------------------------------------------------------------------
struct ScheduledPass {
    int kernel = 0;                          // index into KernelSet (count == probe)
    int out = -1;                            // plan buffer index
    int in[CEL_PASS_MAX_INPUTS] = {-1, -1, -1, -1};
    CelPassArgs args{};                      // complete (sizes/strides filled)
};

struct FrameSchedule {
    std::vector<ScheduledPass> passes;
    std::vector<int>    bufferSlot;          // per plan buffer: -1 = host src/dst, -2 = unused, else slot
    std::vector<size_t> slotBytes;           // bytes of every physical internal slot
    std::vector<int>    usedKernels;         // distinct kernel indices used, ascending
    size_t srcBytesNeeded = 0;               // bytes the src / dst device buffers must hold
    size_t dstBytesNeeded = 0;
};

struct ScheduleOptions {
    bool allowNegativeStride = false;        // device pointers (CUDA) can address bottom-up rows
    bool aliasInternal = true;               // share physical slots by liveness
    bool allowProbe = false;                 // accept kernel index == kernelCount (self test probe)
};

// Validates and schedules. Errors are descriptive (pass index, buffer index, sizes).
bool buildSchedule(const engine::Plan& plan, int kernelCount, const engine::HostImage& src,
                   const engine::HostImage& dst, const ScheduleOptions& opt, FrameSchedule* out,
                   std::string* err);

// Bytes a host image with this geometry spans from its data pointer (stride >= 0) or below it
// (stride < 0: rows go towards lower addresses, the span is computed from the lowest row).
size_t imageSpanBytes(int w, int h, int strideFloats);

// --------------------------------------------------------------------------------------------
struct StateKey {
    uint64_t version = 0;
    const void* data = nullptr;
    size_t count = 0;
    uint64_t sample = 0;                     // checksum of a fixed sample of the content
    bool valid = false;
    bool operator==(const StateKey& o) const {
        return valid && o.valid && version == o.version && data == o.data && count == o.count &&
               sample == o.sample;
    }
    bool operator!=(const StateKey& o) const { return !(*this == o); }
};
StateKey makeStateKey(const std::vector<float>& state, uint64_t version);

// --------------------------------------------------------------------------------------------
// Pool of device buffers of handle type H, owned by one queue/stream lane (so reuse across frames
// is ordered by the queue). Not thread-safe: the owner holds its lane lock.
template <class H>
class SizedPool {
public:
    using AllocFn = std::function<bool(size_t bytes, H* out, std::string* err)>;
    using FreeFn = std::function<void(H)>;

    // Provide one buffer per requested size for frame `frame`. On allocation failure, idle buffers
    // are released and the allocation retried once.
    bool acquire(const std::vector<size_t>& sizes, uint64_t frame, std::vector<H>* out,
                 const AllocFn& alloc, const FreeFn& release, BackendStats* st, std::string* err) {
        out->assign(sizes.size(), H());
        std::vector<bool> taken(m_entries.size(), false);
        for (size_t s = 0; s < sizes.size(); ++s) {
            bool found = false;
            for (size_t e = 0; e < m_entries.size(); ++e) {
                if (!taken[e] && m_entries[e].bytes == sizes[s]) {
                    taken[e] = true; m_entries[e].lastFrame = frame; (*out)[s] = m_entries[e].h;
                    found = true; if (st) st->bufferReuses++;
                    break;
                }
            }
            if (found) continue;
            H h = H();
            std::string e1;
            if (!alloc(sizes[s], &h, &e1)) {
                // free everything not taken by this frame, retry once
                releaseUntaken(taken, release);
                if (!alloc(sizes[s], &h, err)) {
                    if (err) *err = "device buffer allocation of " + std::to_string(sizes[s]) +
                                    " bytes failed: " + (err->empty() ? e1 : *err);
                    return false;
                }
            }
            m_entries.push_back(Entry{h, sizes[s], frame});
            taken.push_back(true);
            (*out)[s] = h;
            if (st) st->bufferAllocs++;
        }
        return true;
    }
    // Release buffers not used during the last `maxIdle` frames.
    void trim(uint64_t frame, uint64_t maxIdle, const FreeFn& release) {
        for (size_t e = 0; e < m_entries.size();) {
            if (frame - m_entries[e].lastFrame > maxIdle) {
                release(m_entries[e].h);
                m_entries.erase(m_entries.begin() + std::ptrdiff_t(e));
            } else {
                ++e;
            }
        }
    }
    void clear(const FreeFn& release) {
        for (auto& e : m_entries) release(e.h);
        m_entries.clear();
    }
    size_t bytes() const { size_t b = 0; for (auto& e : m_entries) b += e.bytes; return b; }
    size_t size() const { return m_entries.size(); }

private:
    struct Entry { H h; size_t bytes; uint64_t lastFrame; };
    void releaseUntaken(std::vector<bool>& taken, const FreeFn& release) {
        std::vector<Entry> keep;
        std::vector<bool> keepTaken;
        for (size_t e = 0; e < m_entries.size(); ++e) {
            if (taken[e]) { keep.push_back(m_entries[e]); keepTaken.push_back(true); }
            else release(m_entries[e].h);
        }
        m_entries.swap(keep);
        taken.swap(keepTaken);
    }
    std::vector<Entry> m_entries;
};

// Frames a pooled buffer may stay unused before it is released.
constexpr uint64_t kPoolMaxIdleFrames = 4;
// A lane (per host queue/stream) that has not rendered for this long (10 s) is released completely
// (pool, state buffer, per-queue objects) by the next frame on another lane: hosts may destroy
// queues/streams or switch devices, and a stale lane would otherwise keep a UHD pool alive.
// Tests may shorten it with the environment variable CEL_GPU_LANE_IDLE_MS (read once).
double laneIdleReleaseMs();

// --------------------------------------------------------------------------------------------
class GpuBackendBase : public engine::Backend {
public:
    explicit GpuBackendBase(const KernelSet& ks) : m_ks(ks) {}
    const KernelSet& kernelSet() const { return m_ks; }

    BackendStats stats() const;
    void setDebugCapture(bool on) { std::lock_guard<std::mutex> l(m_statsMutex); m_debugCapture = on; }
    bool debugCapture() const { std::lock_guard<std::mutex> l(m_statsMutex); return m_debugCapture; }
    void setDebugPoison(bool on) { std::lock_guard<std::mutex> l(m_statsMutex); m_debugPoison = on; }
    bool debugPoison() const { std::lock_guard<std::mutex> l(m_statsMutex); return m_debugPoison; }
    bool captured(int idx, std::vector<float>* out, int* w, int* h) const;
    virtual void releaseResources() = 0;

protected:
    // bookkeeping helpers (thread-safe)
    bool fail(std::string* err, const std::string& msg);
    void noteFrame(size_t pooledBytes);
    void noteBuild(double ms);
    void noteStateUpload();
    void noteDevice(const std::string& d);
    void noteLanes(size_t n);
    void notePool(const BackendStats& delta);   // bufferAllocs/bufferReuses deltas
    void storeCapture(int idx, std::vector<float>&& data, int w, int h);
    void clearCapture();

    const KernelSet& m_ks;
    mutable std::mutex m_statsMutex;
    BackendStats m_stats;
    bool m_debugCapture = false;
    bool m_debugPoison = false;
    struct Captured { std::vector<float> data; int w = 0, h = 0; };
    std::map<int, Captured> m_captured;
};

// pack a host image (any stride, incl. negative) into a dense RGBA float vector and back
void packImage(const engine::HostImage& img, std::vector<float>* packed);
void unpackImage(const std::vector<float>& packed, const engine::HostImage& img);

// Registry of the production singletons (for releaseAllResources / backendStats).
GpuBackendBase* productionOpenCLIfCreated();
GpuBackendBase* productionCudaIfCreated();
GpuBackendBase* productionMetalIfCreated();

// Per-API fallback / self-test implementations (defined in each backend TU, or in NullBackends).
SelfTestResult selfTestOpenCL(bool full);
SelfTestResult selfTestCuda(bool full);
SelfTestResult selfTestMetal(bool full);
bool cpuFallbackOpenCL(engine::Backend& cpu, const engine::Plan& plan, const std::vector<float>& st,
                       uint64_t ver, const engine::HostImage& src, const engine::HostImage& dst,
                       const engine::GpuContext& gpu, std::string* err);
bool cpuFallbackCuda(engine::Backend& cpu, const engine::Plan& plan, const std::vector<float>& st,
                     uint64_t ver, const engine::HostImage& src, const engine::HostImage& dst,
                     const engine::GpuContext& gpu, std::string* err);
bool cpuFallbackMetal(engine::Backend& cpu, const engine::Plan& plan, const std::vector<float>& st,
                      uint64_t ver, const engine::HostImage& src, const engine::HostImage& dst,
                      const engine::GpuContext& gpu, std::string* err);

// ---- probe kernel (self test) -----------------------------------------------------------------
// The probe kernel (src/gpu/kernels/Probe.h) exists in every kernel set at index KernelSet::count.
// It writes kProbeWidth x 1 pixels. makeProbePlan builds a 1-pass plan; checkProbe compares the
// device result with the CPU evaluation of the same kernel.
constexpr int kProbeWidth = 16;
engine::Plan makeProbePlan(int kernelCount, std::vector<float>* state, std::vector<float>* srcPixels);
bool checkProbe(const std::vector<float>& deviceOut, const std::vector<float>& state,
                const std::vector<float>& srcPixels, std::string* report);

double nowMs();

}}} // namespace cel::gpu::detail
