// ofxhost - Metal side of the host (macOS): device, command queue and MTLBuffers, used to exercise
// a plug-in's Metal render path the way DaVinci Resolve does: the host owns an MTLCommandQueue,
// passes MTLBuffers through kOfxImagePropData and the queue through
// kOfxImageEffectPropMetalCommandQueue on the render action, then waits for the queue to drain
// and reads the output back. Buffers use shared storage, so upload / download are memcpy.
#pragma once

#include <cstddef>
#include <string>

namespace ofxhost {

class MetalHost {
public:
    MetalHost() = default;
    ~MetalHost();
    MetalHost(const MetalHost&) = delete;
    MetalHost& operator=(const MetalHost&) = delete;

    static bool compiledIn();
    bool init(std::string& err);
    bool ready() const { return queue_ != nullptr; }
    const std::string& deviceName() const { return name_; }
    void* queue() const { return queue_; }    // id<MTLCommandQueue> (retained by this object)

    void* createBuffer(size_t bytes, std::string& err);   // id<MTLBuffer>, +1 retained
    bool upload(void* buf, const void* src, size_t bytes, std::string& err);
    bool download(void* buf, void* dst, size_t bytes, std::string& err);
    bool fillPattern(void* buf, size_t bytes, std::string& err);   // NaN sentinel
    void release(void* buf);
    bool finish(std::string& err);   // waits until every command buffer committed so far completed

private:
    void* device_ = nullptr;
    void* queue_ = nullptr;
    std::string name_;
};

} // namespace ofxhost
