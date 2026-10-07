// Celluloid Pro - Metal backend (owned by the gpu role). See src/gpu/README.md.
//
// Memory management: ARC (this file must be compiled with -fobjc-arc; enforced below). Objective-C
// objects held in C++ structs/containers are __strong and released automatically. Handles passed
// by the host (id<MTLCommandQueue>, id<MTLBuffer> as void*) are bridged with __bridge (no
// ownership transfer) and never stored beyond the render call.
//
//   Library   per MTLDevice: newLibraryWithSource from the embedded flattened MSL, compiled once
//             with safe math (mathMode = safe on macOS 15+, fastMathEnabled = NO before), plus
//             one MTLComputePipelineState per kernel (immutable, thread-safe, shared by lanes).
//   Lane      per host MTLCommandQueue: private-storage internal buffer pool, private state
//             buffer + content key, a dummy buffer bound to unused inputs.
//   Frame     one command buffer on the host queue: [blit state upload if the key changed]
//             + one serial compute encoder with one dispatch per pass (Metal's hazard tracking
//             orders dependent dispatches; tracked resources). Committed, never waited on (OFX).
//             State uploads go through a shared staging buffer that the command buffer retains
//             until completion, so host memory is never read asynchronously.
//   Errors    command buffer errors are reported asynchronously by a completion handler and
//             surface in the stats and as the failure of the next execute() on that queue.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include "../GpuCommon.h"

#include <algorithm>
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#if !__has_feature(objc_arc)
#error "src/gpu/metal/MetalBackend.mm must be compiled with -fobjc-arc"
#endif

namespace cel { namespace gpu { namespace detail {

namespace {

std::string nsErr(NSError* e) {
    if (!e) return std::string("unknown error");
    NSString* d = e.localizedDescription;
    return d ? std::string(d.UTF8String) : std::string("unknown error");
}

// shared between a lane and the completion handlers of its command buffers
struct AsyncError {
    std::mutex m;
    std::string message;
};

} // namespace

class MetalBackend final : public GpuBackendBase {
public:
    explicit MetalBackend(const KernelSet& ks) : GpuBackendBase(ks) {}
    ~MetalBackend() override { releaseResources(); }
    const char* name() const override { return "Metal"; }

    bool execute(const engine::Plan& plan, const std::vector<float>& stateBuf, uint64_t stateVersion,
                 const engine::HostImage& src, const engine::HostImage& dst, const engine::GpuContext& gpu,
                 std::string* err) override {
        @autoreleasepool {
            return executeImpl(plan, stateBuf, stateVersion, src, dst, gpu, false, err);
        }
    }
    bool executeImpl(const engine::Plan& plan, const std::vector<float>& stateBuf, uint64_t stateVersion,
                     const engine::HostImage& src, const engine::HostImage& dst,
                     const engine::GpuContext& gpu, bool allowProbe, std::string* err);
    void releaseResources() override;

private:
    struct Library {
        id<MTLDevice> device = nil;
        std::mutex m;
        bool built = false, ok = false;
        id<MTLLibrary> lib = nil;
        std::vector<id<MTLComputePipelineState>> pipes;   // per kernel, lazily created
        std::string error;
    };
    struct Lane {
        id<MTLDevice> device = nil;
        std::mutex m;
        SizedPool<id<MTLBuffer>> pool;
        id<MTLBuffer> state = nil;
        size_t stateBytes = 0;
        StateKey stateKey;
        id<MTLBuffer> dummy = nil;
        std::shared_ptr<AsyncError> asyncError = std::make_shared<AsyncError>();
        uint64_t frame = 0;
        std::atomic<double> lastUseMs{0.0};   // set when the lane is taken from the map
        bool retired = false;                 // released by trimIdleLanes (under m); take a new lane
    };
    static void releaseLane(Lane& l) {        // ARC frees; in-flight command buffers retain theirs
        l.pool.clear([](id<MTLBuffer>) {});
        l.state = nil;
        l.dummy = nil;
        l.stateBytes = 0;
        l.stateKey = StateKey();
        l.retired = true;
    }
    void trimIdleLanes(Lane* current);

    std::shared_ptr<Library> library(id<MTLDevice> device);
    bool build(Library& L, std::string* err);
    id<MTLComputePipelineState> pipeline(Library& L, int kid, std::string* err);

    std::mutex m_mapMutex;
    std::map<void*, std::shared_ptr<Library>> m_libs;   // key: (__bridge void*)device (retained in Library)
    std::map<void*, std::shared_ptr<Lane>> m_lanes;     // key: (__bridge void*)queue
};

std::shared_ptr<MetalBackend::Library> MetalBackend::library(id<MTLDevice> device) {
    std::lock_guard<std::mutex> lk(m_mapMutex);
    auto& L = m_libs[(__bridge void*)device];
    if (!L) {
        L = std::make_shared<Library>();
        L->device = device;
    }
    return L;
}

bool MetalBackend::build(Library& L, std::string* err) {
    std::lock_guard<std::mutex> lk(L.m);
    if (L.built) {
        if (!L.ok && err) *err = L.error;
        return L.ok;
    }
    const double t0 = nowMs();
    L.built = true;
    if (!m_ks.metalSource || m_ks.metalSourceLen == 0) {
        L.error = "no Metal kernel source embedded in this build";
    } else {
        NSString* source = [[NSString alloc] initWithBytes:m_ks.metalSource
                                                    length:m_ks.metalSourceLen
                                                  encoding:NSUTF8StringEncoding];
        MTLCompileOptions* opts = [[MTLCompileOptions alloc] init];
        // CPU parity: no fast math. mathMode exists from the macOS 15 SDK / runtime on.
#if defined(__MAC_OS_X_VERSION_MAX_ALLOWED) && __MAC_OS_X_VERSION_MAX_ALLOWED >= 150000
        if (@available(macOS 15.0, *)) {
            opts.mathMode = MTLMathModeSafe;
            opts.mathFloatingPointFunctions = MTLMathFloatingPointFunctionsPrecise;
        } else
#endif
        {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
            opts.fastMathEnabled = NO;
#pragma clang diagnostic pop
        }
        opts.languageVersion = MTLLanguageVersion2_3;   // macOS 11 baseline (deployment target)
        NSError* e = nil;
        L.lib = source ? [L.device newLibraryWithSource:source options:opts error:&e] : nil;
        if (!L.lib) {
            std::string log = source ? nsErr(e) : std::string("kernel source is not valid UTF-8");
            if (log.size() > 8000) log = log.substr(0, 8000) + "\n...(truncated)";
            L.error = std::string("newLibraryWithSource failed on ") + L.device.name.UTF8String + ":\n" + log;
        } else {
            L.ok = true;
            L.pipes.assign(size_t(m_ks.count) + 1, nil);
        }
    }
    noteBuild(nowMs() - t0);
    noteDevice(L.device.name ? std::string(L.device.name.UTF8String) : std::string("Metal device"));
    if (!L.ok && err) *err = L.error;
    return L.ok;
}

id<MTLComputePipelineState> MetalBackend::pipeline(Library& L, int kid, std::string* err) {
    std::lock_guard<std::mutex> lk(L.m);
    if (L.pipes[size_t(kid)]) return L.pipes[size_t(kid)];
    NSString* fname = [NSString stringWithUTF8String:m_ks.names[kid]];
    id<MTLFunction> fn = [L.lib newFunctionWithName:fname];
    if (!fn) {
        if (err) *err = std::string("Metal library has no function '") + m_ks.names[kid] + "'";
        return nil;
    }
    NSError* e = nil;
    id<MTLComputePipelineState> pso = [L.device newComputePipelineStateWithFunction:fn error:&e];
    if (!pso) {
        if (err) *err = std::string("newComputePipelineState(") + m_ks.names[kid] + ") failed: " + nsErr(e);
        return nil;
    }
    L.pipes[size_t(kid)] = pso;
    return pso;
}

void MetalBackend::releaseResources() {
    std::lock_guard<std::mutex> lk(m_mapMutex);
    for (auto& kv : m_lanes) {
        std::lock_guard<std::mutex> ll(kv.second->m);
        releaseLane(*kv.second);
    }
    m_lanes.clear();
    m_libs.clear();   // ARC releases libraries, pipelines and devices
}

void MetalBackend::trimIdleLanes(Lane* current) {
    // Lanes of queues that have not rendered for laneIdleReleaseMs() are dropped (the host may have
    // released the queue; a new queue could even reuse its address). Command buffers still in flight
    // keep their own references to the buffers, so dropping ours is always safe.
    const double now = nowMs();
    std::vector<std::shared_ptr<Lane>> lanes;
    {
        std::lock_guard<std::mutex> lk(m_mapMutex);
        for (auto it = m_lanes.begin(); it != m_lanes.end();) {
            if (it->second.get() != current && now - it->second->lastUseMs > laneIdleReleaseMs()) {
                lanes.push_back(it->second);
                it = m_lanes.erase(it);
            } else {
                ++it;
            }
        }
        noteLanes(m_lanes.size());
    }
    for (auto& l : lanes) {
        std::lock_guard<std::mutex> ll(l->m);
        releaseLane(*l);
    }
}

bool MetalBackend::executeImpl(const engine::Plan& plan, const std::vector<float>& stateBuf,
                               uint64_t stateVersion, const engine::HostImage& src,
                               const engine::HostImage& dst, const engine::GpuContext& gpu,
                               bool allowProbe, std::string* err) {
    if (!gpu.metalCommandQueue) return fail(err, "no Metal command queue supplied by the host");
    id<MTLCommandQueue> queue = (__bridge id<MTLCommandQueue>)gpu.metalCommandQueue;
    id<MTLDevice> device = queue.device;
    if (!device) return fail(err, "Metal command queue has no device");

    FrameSchedule fs;
    ScheduleOptions so;
    so.allowNegativeStride = false;
    so.allowProbe = allowProbe;
    so.aliasInternal = !debugCapture();
    std::string serr;
    if (!buildSchedule(plan, m_ks.count, src, dst, so, &fs, &serr)) return fail(err, "invalid plan: " + serr);

    id<MTLBuffer> srcBuf = (__bridge id<MTLBuffer>)src.data;
    id<MTLBuffer> dstBuf = (__bridge id<MTLBuffer>)dst.data;
    if (srcBuf.device != device || dstBuf.device != device)
        return fail(err, "image buffers belong to a different Metal device than the command queue");
    if (size_t(srcBuf.length) < fs.srcBytesNeeded)
        return fail(err, "source buffer holds " + std::to_string(size_t(srcBuf.length)) + " bytes, image needs " +
                             std::to_string(fs.srcBytesNeeded));
    if (size_t(dstBuf.length) < fs.dstBytesNeeded)
        return fail(err, "destination buffer holds " + std::to_string(size_t(dstBuf.length)) +
                             " bytes, image needs " + std::to_string(fs.dstBytesNeeded));

    std::shared_ptr<Library> L = library(device);
    if (!build(*L, &serr)) return fail(err, serr);
    std::vector<id<MTLComputePipelineState>> psos(size_t(m_ks.count) + 1, nil);
    for (int kid : fs.usedKernels) {
        psos[size_t(kid)] = pipeline(*L, kid, &serr);
        if (!psos[size_t(kid)]) return fail(err, serr);
    }

    std::shared_ptr<Lane> lane;
    std::unique_lock<std::mutex> laneLock;
    for (;;) {   // retry if trimIdleLanes retired the lane between the lookup and the lock
        {
            std::lock_guard<std::mutex> lk(m_mapMutex);
            auto& slot = m_lanes[(__bridge void*)queue];
            if (slot && slot->device != device) slot.reset();   // handle reused for another device
            if (!slot) {
                slot = std::make_shared<Lane>();
                slot->device = device;
            }
            slot->lastUseMs = nowMs();
            lane = slot;
        }
        laneLock = std::unique_lock<std::mutex>(lane->m);
        if (!lane->retired) break;
        laneLock.unlock();
    }
    Lane& l = *lane;
    {
        std::lock_guard<std::mutex> el(l.asyncError->m);
        if (!l.asyncError->message.empty()) {
            std::string m = "previous frame failed on the GPU: " + l.asyncError->message;
            l.asyncError->message.clear();
            l.stateKey = StateKey();   // that frame's state upload may never have executed
            return fail(err, m);
        }
    }
    l.frame++;

    std::vector<id<MTLBuffer>> slots;
    BackendStats delta;
    // `__strong`: under ARC an unqualified `id*` parameter would be `__autoreleasing`
    auto allocFn = [&](size_t bytes, __strong id<MTLBuffer>* out, std::string* aerr) {
        *out = [device newBufferWithLength:bytes options:MTLResourceStorageModePrivate];
        if (!*out) { if (aerr) *aerr = "newBufferWithLength failed (out of GPU memory?)"; return false; }
        return true;
    };
    auto freeFn = [](id<MTLBuffer>) {};   // ARC: dropping the reference releases the buffer
    if (!l.pool.acquire(fs.slotBytes, l.frame, &slots, allocFn, freeFn, &delta, &serr)) return fail(err, serr);
    notePool(delta);
    if (!l.dummy) {
        l.dummy = [device newBufferWithLength:16 options:MTLResourceStorageModePrivate];
        if (!l.dummy) return fail(err, "newBufferWithLength(dummy) failed");
    }

    id<MTLCommandBuffer> cb = [queue commandBuffer];
    if (!cb) return fail(err, "could not create a command buffer on the host queue");
    cb.label = @"Celluloid Pro";

    // The state key is committed to the lane only after [cb commit] (an early return before the
    // commit leaves the blit unexecuted: the key must not claim the upload happened)
    const StateKey key = makeStateKey(stateBuf, stateVersion);
    bool stateEncoded = false;
    const size_t stateBytes = std::max<size_t>(16, stateBuf.size() * sizeof(float));
    if (!l.state || l.stateBytes < stateBytes) {
        l.state = [device newBufferWithLength:stateBytes options:MTLResourceStorageModePrivate];
        l.stateBytes = l.state ? stateBytes : 0;
        l.stateKey = StateKey();
        if (!l.state) return fail(err, "newBufferWithLength(state) failed");
    }
    if (l.stateKey != key && !stateBuf.empty()) {
        const size_t n = stateBuf.size() * sizeof(float);
        id<MTLBuffer> staging = [device newBufferWithBytes:stateBuf.data() length:n options:MTLResourceStorageModeShared];
        if (!staging) return fail(err, "newBufferWithBytes(state staging) failed");
        id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
        [blit copyFromBuffer:staging sourceOffset:0 toBuffer:l.state destinationOffset:0 size:n];
        [blit endEncoding];
        l.stateKey = StateKey();   // set to `key` after the commit (the command buffer retains `staging`)
        stateEncoded = true;
        noteStateUpload();
    }

    auto bufOf = [&](int b) -> id<MTLBuffer> {
        if (b == engine::BUF_SRC) return srcBuf;
        if (b == engine::BUF_DST) return dstBuf;
        return slots[size_t(fs.bufferSlot[size_t(b)])];
    };
    if (debugPoison() && !slots.empty()) {   // test hook: NaN in every internal buffer before the first pass
        id<MTLBlitCommandEncoder> pb = [cb blitCommandEncoder];
        for (size_t s = 0; s < slots.size(); ++s)
            [pb fillBuffer:slots[s] range:NSMakeRange(0, NSUInteger(fs.slotBytes[s])) value:0xFF];
        [pb endEncoding];
    }
    id<MTLComputeCommandEncoder> enc = [cb computeCommandEncoder];   // serial dispatch type
    if (!enc) return fail(err, "could not create a compute command encoder");
    enc.label = @"Celluloid Pro passes";
    for (size_t p = 0; p < fs.passes.size(); ++p) {
        const ScheduledPass& sp = fs.passes[p];
        id<MTLComputePipelineState> pso = psos[size_t(sp.kernel)];
        [enc setComputePipelineState:pso];
        [enc setBytes:&sp.args length:sizeof(CelPassArgs) atIndex:0];
        [enc setBuffer:bufOf(sp.out) offset:0 atIndex:1];
        for (int j = 0; j < CEL_PASS_MAX_INPUTS; ++j)
            [enc setBuffer:(sp.in[j] >= 0 ? bufOf(sp.in[j]) : l.dummy) offset:0 atIndex:NSUInteger(2 + j)];
        [enc setBuffer:l.state offset:0 atIndex:6];
        const NSUInteger tw = std::max<NSUInteger>(1, pso.threadExecutionWidth);
        NSUInteger th = std::max<NSUInteger>(1, pso.maxTotalThreadsPerThreadgroup / tw);
        th = std::min<NSUInteger>(th, std::max<NSUInteger>(1, 256 / tw));
        const MTLSize tg = MTLSizeMake(tw, th, 1);
        const MTLSize groups = MTLSizeMake((NSUInteger(sp.args.outW) + tw - 1) / tw,
                                           (NSUInteger(sp.args.outH) + th - 1) / th, 1);
        [enc dispatchThreadgroups:groups threadsPerThreadgroup:tg];
    }
    [enc endEncoding];

    std::shared_ptr<AsyncError> sink = l.asyncError;
    [cb addCompletedHandler:^(id<MTLCommandBuffer> done) {
        if (done.status == MTLCommandBufferStatusError) {
            std::lock_guard<std::mutex> el(sink->m);
            sink->message = nsErr(done.error);   // the next frame resets the lane's state key
        }
    }];
    [cb commit];   // no waitUntilCompleted: the host orders its own work after ours on the queue
    if (stateEncoded) l.stateKey = key;

    if (debugCapture()) {
        clearCapture();
        [cb waitUntilCompleted];
        for (size_t b = 1; b < fs.bufferSlot.size(); ++b) {
            if (b >= 2 && fs.bufferSlot[b] < 0) continue;
            const int w = b == 1 ? dst.w : plan.buffers[b].w, h = b == 1 ? dst.h : plan.buffers[b].h;
            const int stride = b == 1 ? dst.rowStrideFloats : w * 4;
            const size_t span = imageSpanBytes(w, h, stride);
            id<MTLBuffer> rb = [device newBufferWithLength:span options:MTLResourceStorageModeShared];
            id<MTLCommandBuffer> cb2 = [queue commandBuffer];
            id<MTLBlitCommandEncoder> blit = [cb2 blitCommandEncoder];
            [blit copyFromBuffer:bufOf(int(b)) sourceOffset:0 toBuffer:rb destinationOffset:0 size:span];
            [blit endEncoding];
            [cb2 commit];
            [cb2 waitUntilCompleted];
            std::vector<float> data(size_t(w) * size_t(h) * 4u);
            const float* base = static_cast<const float*>(rb.contents);
            for (int y = 0; y < h; ++y)
                std::copy(base + size_t(y) * size_t(stride), base + size_t(y) * size_t(stride) + size_t(w) * 4u,
                          data.begin() + std::ptrdiff_t(size_t(y) * size_t(w) * 4u));
            storeCapture(int(b), std::move(data), w, h);
        }
    }
    l.pool.trim(l.frame, kPoolMaxIdleFrames, freeFn);
    noteFrame(l.pool.bytes());
    l.lastUseMs = nowMs();
    laneLock.unlock();
    trimIdleLanes(lane.get());
    return true;
}

// ---------------------------------------------------------------------------------------------
namespace {
std::atomic<MetalBackend*> g_production{nullptr};
}
GpuBackendBase* productionMetalIfCreated() { return g_production.load(); }

engine::Backend* productionMetal() {
    // Intentionally leaked (process exit / bundle unload); releaseAllResources() frees GPU objects.
    static MetalBackend* b = [] {
        auto* p = new MetalBackend(productionKernelSet());
        g_production.store(p);
        return p;
    }();
    return b;
}

std::unique_ptr<engine::Backend> createMetalBackendImpl(const KernelSet& ks) {
    return std::unique_ptr<engine::Backend>(new MetalBackend(ks));
}

SelfTestResult selfTestMetal(bool full) {
    SelfTestResult r;
    r.compiledIn = true;
    const double t0 = nowMs();
    @autoreleasepool {
        NSArray<id<MTLDevice>>* devices = MTLCopyAllDevices();
        if (!devices || devices.count == 0) {
            r.message = "no Metal device";
            r.ms = nowMs() - t0;
            return r;
        }
        std::string listing;
        for (id<MTLDevice> d in devices)
            listing += (listing.empty() ? "" : "; ") + std::string(d.name.UTF8String);
        r.message = std::to_string(size_t(devices.count)) + " Metal device(s): " + listing;
        if (!full) { r.ok = true; r.ms = nowMs() - t0; return r; }

        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) device = devices[0];
        id<MTLCommandQueue> queue = [device newCommandQueue];
        std::vector<float> state, srcPx;
        engine::Plan plan = makeProbePlan(productionKernelSet().count, &state, &srcPx);
        id<MTLBuffer> sb = [device newBufferWithBytes:srcPx.data() length:srcPx.size() * 4 options:MTLResourceStorageModeShared];
        id<MTLBuffer> db = [device newBufferWithLength:size_t(kProbeWidth) * 16u options:MTLResourceStorageModeShared];
        std::string report;
        bool ok = false;
        if (queue && sb && db) {
            MetalBackend be(productionKernelSet());
            engine::HostImage hs, hd;
            hs.data = (__bridge void*)sb; hs.w = 4; hs.h = 2; hs.rowStrideFloats = 16;
            hd.data = (__bridge void*)db; hd.w = kProbeWidth; hd.h = 1; hd.rowStrideFloats = kProbeWidth * 4;
            engine::GpuContext g;
            g.metalCommandQueue = (__bridge void*)queue;
            std::string eerr;
            if (be.executeImpl(plan, state, 1, hs, hd, g, true, &eerr)) {
                // a later command buffer on the same queue that reads db is ordered after the
                // probe by hazard tracking; waiting for it means the probe output is visible
                id<MTLBuffer> rb = [device newBufferWithLength:size_t(kProbeWidth) * 16u options:MTLResourceStorageModeShared];
                id<MTLCommandBuffer> cb = [queue commandBuffer];
                id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
                [blit copyFromBuffer:db sourceOffset:0 toBuffer:rb destinationOffset:0 size:size_t(kProbeWidth) * 16u];
                [blit endEncoding];
                [cb commit];
                [cb waitUntilCompleted];
                if (cb.status == MTLCommandBufferStatusCompleted) {
                    const float* p = static_cast<const float*>(rb.contents);
                    std::vector<float> outPx(p, p + size_t(kProbeWidth) * 4u);
                    ok = checkProbe(outPx, state, srcPx, &report);
                } else {
                    report = "probe command buffer failed: " + nsErr(cb.error);
                }
                BackendStats st = be.stats();
                report += " (library build " + std::to_string(int(st.lastBuildMs)) + " ms)";
            } else {
                report = eerr;
            }
        } else {
            report = "could not create Metal queue/buffers";
        }
        r.ok = ok;
        r.message += " | full test on " + std::string(device.name.UTF8String) + ": " + report;
    }
    r.ms = nowMs() - t0;
    return r;
}

bool cpuFallbackMetal(engine::Backend& cpu, const engine::Plan& plan, const std::vector<float>& st, uint64_t ver,
                      const engine::HostImage& src, const engine::HostImage& dst, const engine::GpuContext& gpu,
                      std::string* err) {
    @autoreleasepool {
        auto bad = [&](const std::string& m) { if (err) *err = "Metal CPU fallback: " + m; return false; };
        if (!gpu.metalCommandQueue || !src.data || !dst.data) return bad("missing queue or images");
        if (src.rowStrideFloats < src.w * 4 || dst.rowStrideFloats < dst.w * 4) return bad("unsupported row stride");
        id<MTLCommandQueue> queue = (__bridge id<MTLCommandQueue>)gpu.metalCommandQueue;
        id<MTLDevice> device = queue.device;
        id<MTLBuffer> srcBuf = (__bridge id<MTLBuffer>)src.data;
        id<MTLBuffer> dstBuf = (__bridge id<MTLBuffer>)dst.data;
        const size_t sspan = imageSpanBytes(src.w, src.h, src.rowStrideFloats);
        const size_t dspan = imageSpanBytes(dst.w, dst.h, dst.rowStrideFloats);
        if (size_t(srcBuf.length) < sspan || size_t(dstBuf.length) < dspan) return bad("image buffers too small");
        id<MTLBuffer> down = [device newBufferWithLength:sspan options:MTLResourceStorageModeShared];
        id<MTLBuffer> up = [device newBufferWithLength:dspan options:MTLResourceStorageModeShared];
        if (!down || !up) return bad("staging allocation failed");
        id<MTLCommandBuffer> cb = [queue commandBuffer];
        id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
        [blit copyFromBuffer:srcBuf sourceOffset:0 toBuffer:down destinationOffset:0 size:sspan];
        [blit endEncoding];
        [cb commit];
        [cb waitUntilCompleted];
        if (cb.status != MTLCommandBufferStatusCompleted) return bad("download failed: " + nsErr(cb.error));
        std::vector<float> hs(size_t(src.w) * size_t(src.h) * 4u), hd(size_t(dst.w) * size_t(dst.h) * 4u, 0.f);
        const float* sp = static_cast<const float*>(down.contents);
        for (int y = 0; y < src.h; ++y)
            std::copy(sp + size_t(y) * size_t(src.rowStrideFloats),
                      sp + size_t(y) * size_t(src.rowStrideFloats) + size_t(src.w) * 4u,
                      hs.begin() + std::ptrdiff_t(size_t(y) * size_t(src.w) * 4u));
        engine::HostImage cs, cd;
        cs.data = hs.data(); cs.w = src.w; cs.h = src.h; cs.rowStrideFloats = src.w * 4;
        cd.data = hd.data(); cd.w = dst.w; cd.h = dst.h; cd.rowStrideFloats = dst.w * 4;
        std::string cerr;
        if (!cpu.execute(plan, st, ver, cs, cd, engine::GpuContext(), &cerr)) return bad("CPU backend: " + cerr);
        float* dp = static_cast<float*>(up.contents);
        for (int y = 0; y < dst.h; ++y)
            std::copy(hd.begin() + std::ptrdiff_t(size_t(y) * size_t(dst.w) * 4u),
                      hd.begin() + std::ptrdiff_t(size_t(y + 1) * size_t(dst.w) * 4u),
                      dp + size_t(y) * size_t(dst.rowStrideFloats));
        id<MTLCommandBuffer> cb2 = [queue commandBuffer];
        id<MTLBlitCommandEncoder> blit2 = [cb2 blitCommandEncoder];
        const size_t rowBytes = size_t(dst.w) * 16u, pitch = size_t(dst.rowStrideFloats) * 4u;
        if (pitch == rowBytes) {
            [blit2 copyFromBuffer:up sourceOffset:0 toBuffer:dstBuf destinationOffset:0 size:dspan];
        } else {
            for (int y = 0; y < dst.h; ++y)   // keep the host's row padding untouched
                [blit2 copyFromBuffer:up sourceOffset:size_t(y) * pitch toBuffer:dstBuf
                    destinationOffset:size_t(y) * pitch size:rowBytes];
        }
        [blit2 endEncoding];
        [cb2 commit];   // `up` is retained by cb2 until it completes; the host orders after it
        return true;
    }
}

}}} // namespace cel::gpu::detail
