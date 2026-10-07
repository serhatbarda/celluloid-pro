// ofxhost - Metal host side (macOS). Compiled without ARC: objects are retained/released explicitly.
#include "MetalHost.h"

#include <cstring>
#include <limits>

#if defined(__APPLE__) && defined(OFXHOST_HAVE_METAL)
#import <Metal/Metal.h>

namespace ofxhost {

bool MetalHost::compiledIn() { return true; }

MetalHost::~MetalHost() {
    if (queue_) [(id)queue_ release];
    if (device_) [(id)device_ release];
}

bool MetalHost::init(std::string& err) {
    id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
    if (!dev) { err = "no Metal device"; return false; }
    id<MTLCommandQueue> q = [dev newCommandQueue];
    if (!q) { [dev release]; err = "newCommandQueue failed"; return false; }
    device_ = (void*)dev;
    queue_ = (void*)q;
    name_ = [[dev name] UTF8String];
    return true;
}

void* MetalHost::createBuffer(size_t bytes, std::string& err) {
    id<MTLBuffer> b = [(id<MTLDevice>)device_ newBufferWithLength:(bytes ? bytes : 16) options:MTLResourceStorageModeShared];
    if (!b) err = "newBufferWithLength failed";
    return (void*)b;
}

bool MetalHost::upload(void* buf, const void* src, size_t bytes, std::string& err) {
    id<MTLBuffer> b = (id<MTLBuffer>)buf;
    if (!b || [b length] < bytes) { err = "upload: buffer too small"; return false; }
    std::memcpy([b contents], src, bytes);
    return true;
}

bool MetalHost::download(void* buf, void* dst, size_t bytes, std::string& err) {
    id<MTLBuffer> b = (id<MTLBuffer>)buf;
    if (!b || [b length] < bytes) { err = "download: buffer too small"; return false; }
    std::memcpy(dst, [b contents], bytes);
    return true;
}

bool MetalHost::fillPattern(void* buf, size_t bytes, std::string& err) {
    id<MTLBuffer> b = (id<MTLBuffer>)buf;
    if (!b || [b length] < bytes) { err = "fill: buffer too small"; return false; }
    float* p = static_cast<float*>([b contents]);
    for (size_t i = 0; i < bytes / sizeof(float); ++i) p[i] = std::numeric_limits<float>::quiet_NaN();
    return true;
}

void MetalHost::release(void* buf) {
    if (buf) [(id)buf release];
}

bool MetalHost::finish(std::string& err) {
    // command buffers of one queue execute in commit order: an empty one completes after them
    id<MTLCommandBuffer> cb = [(id<MTLCommandQueue>)queue_ commandBuffer];
    if (!cb) { err = "commandBuffer failed"; return false; }
    [cb commit];
    [cb waitUntilCompleted];
    if ([cb status] == MTLCommandBufferStatusError) {
        err = std::string("Metal command buffer error: ") + [[[cb error] localizedDescription] UTF8String];
        return false;
    }
    return true;
}

} // namespace ofxhost

#else
#error "MetalHost.mm is built only on Apple platforms with OFXHOST_HAVE_METAL"
#endif
