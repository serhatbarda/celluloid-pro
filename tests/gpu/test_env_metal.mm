// Reafilm Pro - GPU tests: Metal environment (own device queue, shared-storage images), ARC.
// Built only on Apple with CEL_ENABLE_METAL; lets `ctest -R gpu_` exercise the Metal backend on a
// Mac (including Apple-silicon CI runners, which expose a Metal device).
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include "GpuTestEnv.h"

#include <cstring>
#include <map>

#if !__has_feature(objc_arc)
#error "test_env_metal.mm must be compiled with -fobjc-arc"
#endif

namespace celtest {
namespace {
class MetalEnv : public Env {
public:
    bool init(std::string* why) {
        m_dev = MTLCreateSystemDefaultDevice();
        if (!m_dev) { *why = "no Metal device"; return false; }
        m_q = [m_dev newCommandQueue];
        return m_q != nil;
    }
    const char* name() const override { return "Metal"; }
    cel::gpu::Api api() const override { return cel::gpu::Api::Metal; }
    std::string device() const override { return std::string(m_dev.name.UTF8String); }
    void* upload(const HostImg& img) override {
        id<MTLBuffer> b = [m_dev newBufferWithBytes:img.store.data() length:img.bytes() options:MTLResourceStorageModeShared];
        if (!b) return nullptr;
        void* h = (__bridge void*)b;
        m_bufs[h] = b;   // keeps it alive until release()
        return h;
    }
    void* allocLike(const HostImg& img) override { return upload(img); }
    bool download(void* h, HostImg& img) override {
        id<MTLBuffer> src = m_bufs[h];
        id<MTLBuffer> rb = [m_dev newBufferWithLength:img.bytes() options:MTLResourceStorageModeShared];
        id<MTLCommandBuffer> cb = [m_q commandBuffer];
        id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
        [blit copyFromBuffer:src sourceOffset:0 toBuffer:rb destinationOffset:0 size:img.bytes()];
        [blit endEncoding];
        [cb commit];
        [cb waitUntilCompleted];
        if (cb.status != MTLCommandBufferStatusCompleted) return false;
        std::memcpy(img.store.data(), rb.contents, img.bytes());
        return true;
    }
    void release(void* h) override { m_bufs.erase(h); }
    cel::engine::GpuContext context() override {
        cel::engine::GpuContext g;
        g.metalCommandQueue = (__bridge void*)m_q;
        return g;
    }
    void finish() override {
        id<MTLCommandQueue> qs[2] = {m_q, m_q2};
        for (int i = 0; i < 2; ++i) {
            id<MTLCommandQueue> q = qs[i];
            if (!q) continue;
            id<MTLCommandBuffer> cb = [q commandBuffer];
            [cb commit];
            [cb waitUntilCompleted];
        }
    }
    bool makeSecondQueue() override {
        if (!m_q2) m_q2 = [m_dev newCommandQueue];
        return m_q2 != nil;
    }
    cel::engine::GpuContext secondContext() override {
        cel::engine::GpuContext g;
        g.metalCommandQueue = (__bridge void*)m_q2;
        return g;
    }

private:
    id<MTLDevice> m_dev = nil;
    id<MTLCommandQueue> m_q = nil, m_q2 = nil;
    std::map<void*, id<MTLBuffer>> m_bufs;
};
} // namespace

std::unique_ptr<Env> makeMetalEnv(std::string* why) {
    std::unique_ptr<MetalEnv> e(new MetalEnv());
    if (!e->init(why)) return nullptr;
    return std::unique_ptr<Env>(e.release());
}
} // namespace celtest
