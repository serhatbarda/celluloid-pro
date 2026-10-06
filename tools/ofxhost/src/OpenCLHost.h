// ofxhost - OpenCL side of the host (context, in-order queue, device buffers/images),
// used to exercise a plug-in's OpenCL render path the way DaVinci Resolve does:
// the host owns a cl_context + cl_command_queue, uploads the source frame into a
// cl_mem, passes the cl_mem through kOfxImagePropData (buffers) or
// kOfxImageEffectPropOpenCLImage (images) and the queue through
// kOfxImageEffectPropOpenCLCommandQueue on the render action, then waits on the
// queue (clFinish) and downloads the output.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace ofxhost {

struct ClDeviceInfo {
    int platform = -1, device = -1;
    std::string platformName, deviceName, deviceVersion, driverVersion;
    bool isGpu = false;
};

class OpenCLHost {
public:
    OpenCLHost() = default;
    ~OpenCLHost();
    OpenCLHost(const OpenCLHost&) = delete;
    OpenCLHost& operator=(const OpenCLHost&) = delete;

    static bool compiledIn();
    static std::vector<ClDeviceInfo> enumerate(std::string& err);

    // platform/device < 0: first GPU device, else first device of any type.
    bool init(int platform, int device, std::string& err);
    bool ready() const { return context_ != nullptr; }
    const ClDeviceInfo& info() const { return info_; }

    void* context() const { return context_; }
    void* queue() const { return queue_; }

    // Buffers (cl_mem returned as void*)
    void* createBuffer(size_t bytes, std::string& err);
    void* createImage2D(int w, int h, int channels, bool isFloat, int bytesPerComp, std::string& err);
    bool upload(void* mem, const void* src, size_t bytes, std::string& err);
    bool download(void* mem, void* dst, size_t bytes, std::string& err);
    bool uploadImage(void* mem, const void* src, int w, int h, size_t rowBytes, std::string& err);
    bool downloadImage(void* mem, void* dst, int w, int h, size_t rowBytes, std::string& err);
    bool fillPattern(void* mem, size_t bytes, std::string& err);  // NaN sentinel
    void release(void* mem);
    bool finish(std::string& err);

    // OfxOpenCLProgramSuiteV1::compileProgram implementation (against this context).
    int compileProgram(const char* source, void* resultProgram, std::string& log);

private:
    void* platform_ = nullptr;
    void* device_ = nullptr;
    void* context_ = nullptr;
    void* queue_ = nullptr;
    ClDeviceInfo info_;
};

} // namespace ofxhost
