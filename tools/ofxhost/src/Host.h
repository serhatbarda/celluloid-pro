// ofxhost - host objects: host descriptor and suites, plug-in binaries, effects
// (descriptors + instances), clips, images and the render driver.
#pragma once

#include "Image.h"
#include "OpenCLHost.h"
#include "Params.h"
#include "Props.h"

#include "ofxCore.h"
#include "ofxImageEffect.h"

#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace ofxhost {

class Effect;
class Instance;
struct PluginBinary;

// ------------------------------------------------------------------ options
enum class NotifyMode { Sync, Deferred, Off };
enum class GpuMode { None, OpenCLBuffers, OpenCLImages };

struct HostOptions {
    std::string hostName = "de.serhatbarda.ofxhost";
    std::string hostLabel = "Celluloid ofxhost (headless test host)";
    int threads = 0;                    // 0 = hardware concurrency
    bool answerYes = true;              // reply to kOfxMessageQuestion
    NotifyMode pluginEditNotify = NotifyMode::Sync;
    bool interactive = false;           // kOfxPropIsInteractive
    bool advertiseOpenCLBuffers = true; // only effective when OpenCL is compiled in
    bool advertiseOpenCLImages = true;
    std::vector<std::string> depths;    // host supported pixel depths, deepest first
    std::vector<std::string> components;
    bool sentinel = true;               // pre-fill outputs with NaN to detect unwritten pixels
    bool verbose = false;
    bool trace = false;                 // trace every action
    double fps = 24.0;
};

struct HostMessage {
    std::string kind;   // "message", "persistent", "clearPersistent", "progress", "log"
    std::string type;   // kOfxMessage* or progress label
    std::string id;
    std::string text;
    double atMs = 0;
};

// ------------------------------------------------------------------ host
class Host {
public:
    static Host& get();
    HostOptions opt;
    PropertySet props{"ImageEffectHost"};
    OfxHost ofxHost{};
    OpenCLHost* cl = nullptr;       // set when a GPU mode is active
    bool clAvailable = false;       // OpenCL host-side compiled in and usable

    void init();  // fills host properties according to opt
    const void* fetchSuite(const char* name, int version);

    std::mutex msgMtx;
    std::vector<HostMessage> messages;
    void addMessage(const HostMessage& m);

    // Effects that are currently alive (for handle validation).
    std::mutex liveMtx;
    std::set<const void*> liveEffects;

    std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
    double nowMs() const {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    }
    std::vector<std::string> suitesFetched;
    std::vector<std::string> suitesMissing;
};

// ------------------------------------------------------------------ clip
class Clip {
public:
    static constexpr unsigned kMagic = 0x0F1AC119u;
    Clip(Effect* e, std::string name, bool descriptor);
    ~Clip() { magic_ = 0; }
    static Clip* from(OfxImageClipHandle h);
    OfxImageClipHandle handle() { return reinterpret_cast<OfxImageClipHandle>(this); }
    const std::string& name() const { return name_; }
    PropertySet& props() { return props_; }
    Effect* effect() const { return effect_; }
    bool isOutput() const { return name_ == kOfxImageEffectOutputClipName; }
    bool connected() const { return props_.getInt(kOfxImageClipPropConnected, 0, 0) != 0; }
    PixelFormat format() const;
private:
    unsigned magic_ = kMagic;
    Effect* effect_;
    std::string name_;
    PropertySet props_;
};

// image handle given to the plug-in by clipGetImage
struct HostImage {
    static constexpr unsigned kMagic = 0x0F1A1A6Eu;
    unsigned magic = kMagic;
    PropertySet props{"Image"};
    std::shared_ptr<ImageMemory> mem;   // CPU memory (null for GPU images)
    void* clMem = nullptr;              // not owned
    Clip* clip = nullptr;
    ~HostImage() { magic = 0; }
};

// image memory allocated via imageMemoryAlloc
struct HostImageMemory {
    static constexpr unsigned kMagic = 0x0F1A3E30u;
    unsigned magic = kMagic;
    std::vector<unsigned char> storage;
    unsigned char* aligned = nullptr;
    int locks = 0;
    ~HostImageMemory() { magic = 0; }
};

// ------------------------------------------------------------------ effect
class Effect {
public:
    static constexpr unsigned kMagic = 0x0F1AEFFEu;
    Effect(PluginBinary* bin, int pluginIndex, bool descriptor);
    ~Effect();
    static Effect* from(OfxImageEffectHandle h);
    OfxImageEffectHandle handle() { return reinterpret_cast<OfxImageEffectHandle>(this); }
    bool isDescriptor() const { return descriptor_; }
    PropertySet& props() { return props_; }
    ParamSet& params() { return params_; }
    Clip* clip(const std::string& name) const;
    Clip* defineClip(const std::string& name);
    const std::vector<std::unique_ptr<Clip>>& clips() const { return clips_; }
    Instance* instance = nullptr;   // set for instances
    PluginBinary* binary() const { return bin_; }
    int pluginIndex() const { return pluginIndex_; }
private:
    unsigned magic_ = kMagic;
    PluginBinary* bin_;
    int pluginIndex_;
    bool descriptor_;
    PropertySet props_;
    ParamSet params_;
    std::vector<std::unique_ptr<Clip>> clips_;
};

// ------------------------------------------------------------------ plug-in binary
struct PluginInfo {
    OfxPlugin* plugin = nullptr;
    std::string id, api;
    int apiVersion = 0, vMajor = 0, vMinor = 0;
    bool loaded = false;        // kOfxActionLoad succeeded
    bool described = false;
    std::unique_ptr<Effect> descriptor;                       // after kOfxActionDescribe
    std::map<std::string, std::unique_ptr<Effect>> contexts;  // after DescribeInContext
};

struct PluginBinary {
    std::string path;        // .ofx file
    std::string bundlePath;  // bundle root (may be empty)
    void* dl = nullptr;
    std::vector<PluginInfo> plugins;
    bool open(const std::string& pathOrBundle, std::string& err);
    void close();
    ~PluginBinary() { close(); }
};

// Call a plug-in action with exception guarding, timing and optional tracing.
OfxStatus callAction(PluginInfo& pi, const char* action, const void* handle, PropertySet* inArgs,
                     PropertySet* outArgs, double* ms = nullptr);
const char* statusName(OfxStatus st);

// ------------------------------------------------------------------ source provider
struct SourceSpec {
    std::string spec;         // "pattern:NAME[:args]" or a .f32 path (may contain printf %d for frame)
    int w = 0, h = 0;         // canonical (full resolution) size
    bool sequence() const;
    bool load(double time, RGBAF& out, std::string& err) const;
};

// ------------------------------------------------------------------ render results
struct FrameResult {
    double time = 0;
    OfxStatus status = kOfxStatOK;
    bool identity = false;
    std::string identityClip;
    double identityTime = 0;
    std::vector<double> renderMs;   // one per repeat: render action wall time
    std::vector<double> finishMs;   // GPU: clFinish after the action
    std::vector<double> totalMs;    // including upload/download/conversion
    double uploadMs = 0, downloadMs = 0;
    long long unwritten = 0;        // pixels still holding the NaN sentinel
    ImageStats stats{};
    int outW = 0, outH = 0;
    std::vector<std::string> files;
    std::string gpu;                 // "none" / "opencl-buffers" / "opencl-images"
    bool gpuFellBack = false;
    bool gpuWithdrawn = false;       // plug-in switched its OpenCL support off on the instance during the render
    OfxRectD rod{0, 0, 0, 0};
    std::map<std::string, OfxRectD> rois;
};

struct RenderRequest {
    std::vector<double> times;
    double renderScale = 1.0;
    GpuMode gpu = GpuMode::None;
    bool gpuFallbackToCpu = false;
    int repeat = 1;
    std::string outPrefix;    // empty -> no files
    bool writeF32 = true, writePng = true, png16 = false, pngAlpha = false;
    bool keepOutputs = false; // keep RGBA of each frame in memory (batch compare)
    bool draft = false;       // kOfxImageEffectPropRenderQualityDraft
};

// ------------------------------------------------------------------ instance
class Instance {
public:
    Instance(PluginBinary* bin, int pluginIndex, const std::string& context);
    ~Instance();
    bool create(std::string& err);   // kOfxActionCreateInstance (+ initial clip preferences)
    void destroy();

    Effect* effect() { return effect_.get(); }
    PluginInfo& info();
    Effect* contextDescriptor();

    // project / clip setup
    void setProject(int w, int h);
    void setFrameRange(double first, double last);
    void setSource(const SourceSpec& s) { source_ = s; }
    const SourceSpec& source() const { return source_; }

    // user interactions (host side), each wrapped in Begin/InstanceChanged/End with kOfxChangeUserEdited
    bool userSetParam(const std::string& name, const std::string& value, double time, bool keyframe, std::string& err);
    bool userPressButton(const std::string& name, double time, std::string& err);
    bool resetParams(std::string& err);   // all params back to defaults (with notifications)

    bool clipPreferences(std::string& err);
    bool render(const RenderRequest& rq, std::vector<FrameResult>& results, std::vector<RGBAF>* keep, std::string& err);

    // suite callbacks
    OfxStatus clipGetImage(Clip* c, double time, const OfxRectD* region, OfxPropertySetHandle* out);
    OfxStatus clipReleaseImage(HostImage* img);
    OfxStatus clipGetRoD(Clip* c, double time, OfxRectD* rod);
    double currentTime() const { return curTime_; }
    bool inRender() const { return inRender_; }

    std::vector<std::string> notifyLog;   // "reason:type:name@time" for every instanceChanged sent
    int outstandingImages() const;
    std::map<std::string, std::string> clipPrefsSummary;

private:
    bool instanceChanged(const std::string& type, const std::string& name, const char* reason, double time, std::string& err);
    void onPluginChanged(Param* p);
    void flushDeferred();
    bool sourceForTime(double time, double scale, RGBAF& storage, const RGBAF*& out, std::string& err);
    PropertySet makeRenderArgs(double time, double scale, const OfxRectI& win, bool gpuEnabled, bool draft);
    PropertySet makeSequenceArgs(double first, double last, double scale, bool gpuEnabled);
    bool renderFrame(const RenderRequest& rq, double time, FrameResult& fr, RGBAF& outRGBA, std::string& err);

    PluginBinary* bin_;
    int pluginIndex_;
    std::string context_;
    std::unique_ptr<Effect> effect_;
    bool created_ = false;
    int projW_ = 0, projH_ = 0;
    double first_ = 0, last_ = 0;
    SourceSpec source_;
    double curTime_ = 0;
    bool inRender_ = false;
    int notifyDepth_ = 0;
    std::vector<std::string> deferred_;

    // per-render state
    struct RenderState {
        double time = 0, scale = 1;
        bool gpu = false;
        GpuMode gpuMode = GpuMode::None;
        std::shared_ptr<ImageMemory> outMem;
        void* outCl = nullptr;
        std::map<std::string, std::shared_ptr<ImageMemory>> srcCache;   // key clip@time
        std::map<std::string, void*> srcCl;
        double uploadMs = 0;
    };
    RenderState rs_;
    mutable std::mutex imgMtx_;
    std::set<HostImage*> images_;
    unsigned long long imageCounter_ = 0;
    std::map<std::string, RGBAF> sourceFileCache_;
};

// The suites
OfxImageEffectSuiteV1* imageEffectSuite();

} // namespace ofxhost
