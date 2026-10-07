// ofxhost - host descriptor, suites, binaries, effects and clips.
#include "Host.h"

#include "ofxGPURender.h"
#include "ofxColour.h"
#include "ofxInteract.h"
#include "ofxMemory.h"
#include "ofxMessage.h"
#include "ofxMultiThread.h"
#include "ofxProgress.h"
#include "ofxTimeLine.h"
#include "ofxDialog.h"
#include "ofxParametricParam.h"

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <malloc.h>
#else
#  include <dlfcn.h>
#endif
#include <filesystem>

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

namespace ofxhost {

// ================================================================== GPU modes
const char* gpuSupportFlag(GpuMode m) {
    switch (m) {
    case GpuMode::OpenCLBuffers: return kOfxImageEffectPropOpenCLRenderSupported;
    case GpuMode::OpenCLImages: return kOfxImageEffectPropOpenCLSupported;
    case GpuMode::Metal: return kOfxImageEffectPropMetalRenderSupported;
    default: return "";
    }
}
const char* gpuModeName(GpuMode m) {
    switch (m) {
    case GpuMode::OpenCLBuffers: return "opencl-buffers";
    case GpuMode::OpenCLImages: return "opencl-images";
    case GpuMode::Metal: return "metal";
    default: return "cpu";
    }
}

// ================================================================== status names
const char* statusName(OfxStatus st) {
    switch (st) {
    case kOfxStatOK: return "kOfxStatOK";
    case kOfxStatFailed: return "kOfxStatFailed";
    case kOfxStatErrFatal: return "kOfxStatErrFatal";
    case kOfxStatErrUnknown: return "kOfxStatErrUnknown";
    case kOfxStatErrMissingHostFeature: return "kOfxStatErrMissingHostFeature";
    case kOfxStatErrUnsupported: return "kOfxStatErrUnsupported";
    case kOfxStatErrExists: return "kOfxStatErrExists";
    case kOfxStatErrFormat: return "kOfxStatErrFormat";
    case kOfxStatErrMemory: return "kOfxStatErrMemory";
    case kOfxStatErrBadHandle: return "kOfxStatErrBadHandle";
    case kOfxStatErrBadIndex: return "kOfxStatErrBadIndex";
    case kOfxStatErrValue: return "kOfxStatErrValue";
    case kOfxStatReplyYes: return "kOfxStatReplyYes";
    case kOfxStatReplyNo: return "kOfxStatReplyNo";
    case kOfxStatReplyDefault: return "kOfxStatReplyDefault";
    case kOfxStatGPUOutOfMemory: return "kOfxStatGPUOutOfMemory";
    case kOfxStatGPURenderFailed: return "kOfxStatGPURenderFailed";
    default: return "kOfxStat(unknown)";
    }
}

// ================================================================== Host
Host& Host::get() {
    static Host h;
    return h;
}

void Host::addMessage(const HostMessage& m0) {
    HostMessage m = m0;
    m.atMs = nowMs();
    std::lock_guard<std::mutex> lk(msgMtx);
    if (messages.size() < 10000) messages.push_back(m);
}

static const void* fetchSuiteC(OfxPropertySetHandle, const char* name, int version) {
    return Host::get().fetchSuite(name, version);
}

void Host::init() {
    if (opt.depths.empty()) opt.depths = {kOfxBitDepthFloat, kOfxBitDepthHalf, kOfxBitDepthShort, kOfxBitDepthByte};
    if (opt.components.empty()) opt.components = {kOfxImageComponentRGBA, kOfxImageComponentRGB, kOfxImageComponentAlpha};
    clAvailable = OpenCLHost::compiledIn();
    mtlAvailable = MetalHost::compiledIn();
    PropertySet& p = props;
    p.defString(kOfxPropType, kOfxTypeImageEffectHost, true);
    p.defString(kOfxPropName, opt.hostName, true);
    p.defString(kOfxPropLabel, opt.hostLabel, true);
    p.defInt(kOfxPropAPIVersion, std::vector<int>{1, 5}, true);
    p.defInt(kOfxPropVersion, std::vector<int>{1, 0, 0}, true);
    p.defString(kOfxPropVersionLabel, "1.0.0", true);
    p.defInt(kOfxImageEffectHostPropIsBackground, 1, true);
    p.defInt(kOfxImageEffectPropSupportsOverlays, 0, true);
    p.defInt(kOfxImageEffectPropSupportsMultiResolution, 1, true);
    p.defInt(kOfxImageEffectPropSupportsTiles, 0, true);
    p.defInt(kOfxImageEffectPropTemporalClipAccess, 1, true);
    p.defString(kOfxImageEffectPropSupportedComponents, opt.components, true);
    p.defString(kOfxImageEffectPropSupportedContexts,
                std::vector<std::string>{kOfxImageEffectContextFilter, kOfxImageEffectContextGeneral}, true);
    p.defString(kOfxImageEffectPropSupportedPixelDepths, opt.depths, true);
    p.defInt(kOfxImageEffectPropSupportsMultipleClipDepths, 0, true);
    p.defInt(kOfxImageEffectPropSupportsMultipleClipPARs, 0, true);
    p.defInt(kOfxImageEffectPropSetableFrameRate, 0, true);
    p.defInt(kOfxImageEffectPropSetableFielding, 0, true);
    p.defInt(kOfxImageEffectInstancePropSequentialRender, 2, true);  // 2 = "can do either"
    p.defInt(kOfxParamHostPropSupportsCustomInteract, 0, true);
    p.defInt(kOfxParamHostPropSupportsStringAnimation, 0, true);
    p.defInt(kOfxParamHostPropSupportsChoiceAnimation, 1, true);
    p.defInt(kOfxParamHostPropSupportsBooleanAnimation, 1, true);
    p.defInt(kOfxParamHostPropSupportsCustomAnimation, 0, true);
    p.defInt(kOfxParamHostPropSupportsStrChoice, 1, true);
    p.defInt(kOfxParamHostPropSupportsStrChoiceAnimation, 0, true);
    p.defInt(kOfxParamHostPropSupportsParametricAnimation, 0, true);
    p.defInt(kOfxParamHostPropMaxParameters, -1, true);
    p.defInt(kOfxParamHostPropMaxPages, 0, true);
    p.defInt(kOfxParamHostPropPageRowColumnCount, std::vector<int>{0, 0}, true);
    p.defPointer(kOfxPropHostOSHandle, nullptr, true);
    p.defString(kOfxImageEffectPropOpenGLRenderSupported, "false", true);
    p.defString(kOfxImageEffectPropCPURenderSupported, "true", true);
    p.defInt(kOfxImageEffectPropRenderQualityDraft, 1, true);
    p.defString(kOfxImageEffectHostPropNativeOrigin, kOfxHostNativeOriginBottomLeft, true);
    p.defString(kOfxImageEffectPropColourManagementStyle, kOfxImageEffectColourManagementNone, true);
    p.defString(kOfxImageEffectPropColourManagementAvailableConfigs, std::vector<std::string>{}, true);
    // GPU render extension: OpenCL when compiled in; CUDA / Metal are not available on this host.
    p.defString(kOfxImageEffectPropOpenCLRenderSupported, (clAvailable && opt.advertiseOpenCLBuffers) ? "true" : "false", true);
    p.defString(kOfxImageEffectPropOpenCLSupported, (clAvailable && opt.advertiseOpenCLImages) ? "true" : "false", true);
    p.defString(kOfxImageEffectPropCudaRenderSupported, "false", true);
    p.defString(kOfxImageEffectPropCudaStreamSupported, "false", true);
    p.defString(kOfxImageEffectPropMetalRenderSupported, mtlAvailable ? "true" : "false", true);
    p.setInstanceSemantics(true);

    ofxHost.host = props.handle();
    ofxHost.fetchSuite = fetchSuiteC;
}

// ================================================================== memory suite
namespace {

OfxStatus memoryAlloc(void*, size_t nBytes, void** data) {
    if (!data) return kOfxStatErrBadHandle;
    void* p = nullptr;
#if defined(_WIN32)
    p = _aligned_malloc(nBytes ? nBytes : 1, 64);
    if (!p) { *data = nullptr; return kOfxStatErrMemory; }
#else
    if (posix_memalign(&p, 64, nBytes ? nBytes : 1) != 0) { *data = nullptr; return kOfxStatErrMemory; }
#endif
    *data = p;
    return kOfxStatOK;
}
OfxStatus memoryFree(void* data) {
#if defined(_WIN32)
    _aligned_free(data);
#else
    std::free(data);
#endif
    return kOfxStatOK;
}
OfxMemorySuiteV1 g_memorySuite = {memoryAlloc, memoryFree};

// ================================================================== multithread suite
thread_local int t_threadIndex = 0;
thread_local bool t_spawned = false;

unsigned numCPUs() {
    int t = Host::get().opt.threads;
    if (t > 0) return unsigned(t);
    unsigned hw = std::thread::hardware_concurrency();
    return hw ? hw : 1;
}

OfxStatus multiThread(OfxThreadFunctionV1 func, unsigned int nThreads, void* customArg) {
    if (!func) return kOfxStatErrBadHandle;
    if (nThreads == 0) nThreads = 1;
    if (nThreads == 1) {
        int pi = t_threadIndex; bool ps = t_spawned;
        t_threadIndex = 0; t_spawned = true;
        func(0, 1, customArg);
        t_threadIndex = pi; t_spawned = ps;
        return kOfxStatOK;
    }
    std::vector<std::thread> pool;
    pool.reserve(nThreads);
    for (unsigned i = 0; i < nThreads; ++i) {
        pool.emplace_back([=]() {
            t_threadIndex = int(i);
            t_spawned = true;
            func(i, nThreads, customArg);
        });
    }
    for (auto& t : pool) t.join();
    return kOfxStatOK;
}
OfxStatus multiThreadNumCPUs(unsigned int* n) {
    if (!n) return kOfxStatErrBadHandle;
    *n = numCPUs();
    return kOfxStatOK;
}
OfxStatus multiThreadIndex(unsigned int* idx) {
    if (!idx) return kOfxStatErrBadHandle;
    *idx = unsigned(t_threadIndex);
    return kOfxStatOK;
}
int multiThreadIsSpawnedThread() { return t_spawned ? 1 : 0; }

struct HostMutex {
    static constexpr unsigned kMagic = 0x0F1A7A7Eu;
    unsigned magic = kMagic;
    std::recursive_mutex m;
};
OfxStatus mutexCreate(OfxMutexHandle* mutex, int lockCount) {
    if (!mutex) return kOfxStatErrBadHandle;
    auto* m = new HostMutex;
    for (int i = 0; i < lockCount; ++i) m->m.lock();
    *mutex = reinterpret_cast<OfxMutexHandle>(m);
    return kOfxStatOK;
}
HostMutex* HM(OfxMutexHandle h) {
    auto* m = reinterpret_cast<HostMutex*>(h);
    return (m && m->magic == HostMutex::kMagic) ? m : nullptr;
}
OfxStatus mutexDestroy(const OfxMutexHandle mutex) {
    HostMutex* m = HM(mutex);
    if (!m) return kOfxStatErrBadHandle;
    m->magic = 0;
    delete m;
    return kOfxStatOK;
}
OfxStatus mutexLock(const OfxMutexHandle mutex) {
    HostMutex* m = HM(mutex);
    if (!m) return kOfxStatErrBadHandle;
    m->m.lock();
    return kOfxStatOK;
}
OfxStatus mutexUnLock(const OfxMutexHandle mutex) {
    HostMutex* m = HM(mutex);
    if (!m) return kOfxStatErrBadHandle;
    m->m.unlock();
    return kOfxStatOK;
}
OfxStatus mutexTryLock(const OfxMutexHandle mutex) {
    HostMutex* m = HM(mutex);
    if (!m) return kOfxStatErrBadHandle;
    return m->m.try_lock() ? kOfxStatOK : kOfxStatFailed;
}
OfxMultiThreadSuiteV1 g_threadSuite = {multiThread, multiThreadNumCPUs, multiThreadIndex, multiThreadIsSpawnedThread,
                                       mutexCreate, mutexDestroy, mutexLock, mutexUnLock, mutexTryLock};

// ================================================================== message suite
std::string vfmt(const char* format, va_list ap) {
    if (!format) return "";
    va_list ap2;
    va_copy(ap2, ap);
    int n = std::vsnprintf(nullptr, 0, format, ap2);
    va_end(ap2);
    if (n < 0) return format;
    std::string s(size_t(n) + 1, '\0');
    std::vsnprintf(&s[0], s.size(), format, ap);
    s.resize(size_t(n));
    return s;
}

OfxStatus postMessage(const char* kind, const char* type, const char* id, const std::string& text) {
    HostMessage m;
    m.kind = kind;
    m.type = type ? type : "";
    m.id = id ? id : "";
    m.text = text;
    Host::get().addMessage(m);
    std::fprintf(stderr, "[plugin %s %s] %s\n", kind, m.type.c_str(), text.c_str());
    if (type && std::strcmp(type, kOfxMessageQuestion) == 0)
        return Host::get().opt.answerYes ? kOfxStatReplyYes : kOfxStatReplyNo;
    return kOfxStatOK;
}

OfxStatus message(void*, const char* messageType, const char* messageId, const char* format, ...) {
    va_list ap; va_start(ap, format);
    std::string s = vfmt(format, ap);
    va_end(ap);
    return postMessage("message", messageType, messageId, s);
}
OfxStatus setPersistentMessage(void*, const char* messageType, const char* messageId, const char* format, ...) {
    va_list ap; va_start(ap, format);
    std::string s = vfmt(format, ap);
    va_end(ap);
    OfxStatus st = postMessage("persistent", messageType, messageId, s);
    return st == kOfxStatReplyYes || st == kOfxStatReplyNo ? kOfxStatOK : st;
}
OfxStatus clearPersistentMessage(void*) {
    HostMessage m;
    m.kind = "clearPersistent";
    Host::get().addMessage(m);
    return kOfxStatOK;
}
OfxMessageSuiteV1 g_messageV1 = {message};
OfxMessageSuiteV2 g_messageV2 = {message, setPersistentMessage, clearPersistentMessage};

// ================================================================== progress suite
OfxStatus progressStartV1(void*, const char* label) {
    HostMessage m; m.kind = "progress"; m.type = "start"; m.text = label ? label : "";
    Host::get().addMessage(m);
    return kOfxStatOK;
}
OfxStatus progressStartV2(void*, const char* message, const char* id) {
    HostMessage m; m.kind = "progress"; m.type = "start"; m.text = message ? message : ""; m.id = id ? id : "";
    Host::get().addMessage(m);
    return kOfxStatOK;
}
OfxStatus progressUpdate(void*, double) { return kOfxStatOK; }
OfxStatus progressEnd(void*) {
    HostMessage m; m.kind = "progress"; m.type = "end";
    Host::get().addMessage(m);
    return kOfxStatOK;
}
OfxProgressSuiteV1 g_progressV1 = {progressStartV1, progressUpdate, progressEnd};
OfxProgressSuiteV2 g_progressV2 = {progressStartV2, progressUpdate, progressEnd};

// ================================================================== timeline suite
OfxStatus tlGetTime(void* instance, double* time) {
    Effect* e = Effect::from(OfxImageEffectHandle(instance));
    if (!time) return kOfxStatErrBadHandle;
    *time = (e && e->instance) ? e->instance->currentTime() : 0.0;
    return kOfxStatOK;
}
OfxStatus tlGotoTime(void*, double time) {
    HostMessage m; m.kind = "log"; m.type = "gotoTime"; m.text = std::to_string(time);
    Host::get().addMessage(m);
    return kOfxStatOK;
}
OfxStatus tlGetTimeBounds(void* instance, double* first, double* last) {
    Effect* e = Effect::from(OfxImageEffectHandle(instance));
    if (!first || !last) return kOfxStatErrBadHandle;
    double d = e ? e->props().getDouble(kOfxImageEffectInstancePropEffectDuration, 0, 1.0) : 1.0;
    *first = 0;
    *last = d - 1;
    return kOfxStatOK;
}
OfxTimeLineSuiteV1 g_timeline = {tlGetTime, tlGotoTime, tlGetTimeBounds};

// ================================================================== interact suite (no UI)
PropertySet g_interactProps("InteractInstance");
OfxStatus interactSwapBuffers(OfxInteractHandle) { return kOfxStatOK; }
OfxStatus interactRedraw(OfxInteractHandle) { return kOfxStatOK; }
OfxStatus interactGetPropertySet(OfxInteractHandle, OfxPropertySetHandle* p) {
    if (!p) return kOfxStatErrBadHandle;
    *p = g_interactProps.handle();
    return kOfxStatOK;
}
OfxInteractSuiteV1 g_interact = {interactSwapBuffers, interactRedraw, interactGetPropertySet};

// ================================================================== OpenCL program suite
OfxStatus clCompileProgram(const char* src, int fOptional, void* result) {
    Host& h = Host::get();
    if (!h.cl || !h.cl->ready()) return fOptional ? kOfxStatReplyDefault : kOfxStatFailed;
    std::string log;
    int r = h.cl->compileProgram(src, result, log);
    if (r != 0) {
        HostMessage m; m.kind = "log"; m.type = "clBuildFailed"; m.text = log;
        h.addMessage(m);
        return kOfxStatFailed;
    }
    return kOfxStatOK;
}
OfxOpenCLProgramSuiteV1 g_clProgram = {clCompileProgram};

} // namespace

// ================================================================== fetchSuite
const void* Host::fetchSuite(const char* name, int version) {
    if (!name) return nullptr;
    std::string n = name;
    const void* r = nullptr;
    if (n == kOfxPropertySuite && version == 1) r = propertySuite();
    else if (n == kOfxImageEffectSuite && version == 1) r = imageEffectSuite();
    else if (n == kOfxParameterSuite && version == 1) r = parameterSuite();
    else if (n == kOfxMemorySuite && version == 1) r = &g_memorySuite;
    else if (n == kOfxMultiThreadSuite && version == 1) r = &g_threadSuite;
    else if (n == kOfxMessageSuite && version == 1) r = &g_messageV1;
    else if (n == kOfxMessageSuite && version == 2) r = &g_messageV2;
    else if (n == kOfxProgressSuite && version == 1) r = &g_progressV1;
    else if (n == kOfxProgressSuite && version == 2) r = &g_progressV2;
    else if (n == kOfxTimeLineSuite && version == 1) r = &g_timeline;
    else if (n == kOfxInteractSuite && version == 1) r = &g_interact;
    else if (n == kOfxOpenCLProgramSuite && version == 1 && clAvailable) r = &g_clProgram;
    std::string tag = n + " v" + std::to_string(version);
    if (r) {
        if (std::find(suitesFetched.begin(), suitesFetched.end(), tag) == suitesFetched.end()) suitesFetched.push_back(tag);
    } else {
        if (std::find(suitesMissing.begin(), suitesMissing.end(), tag) == suitesMissing.end()) suitesMissing.push_back(tag);
    }
    if (opt.trace) std::fprintf(stderr, "[ofxhost] fetchSuite(%s, %d) -> %s\n", name, version, r ? "ok" : "NULL");
    return r;
}

// ================================================================== Clip
Clip::Clip(Effect* e, std::string name, bool descriptor)
    : effect_(e), name_(std::move(name)), props_(descriptor ? "ClipDescriptor:" + name_ : "ClipInstance:" + name_) {
    props_.defString(kOfxPropType, kOfxTypeClip, true);
    props_.defString(kOfxPropName, name_, true);
    props_.defString(kOfxPropLabel, name_);
    props_.defString(kOfxPropShortLabel, name_);
    props_.defString(kOfxPropLongLabel, name_);
    props_.defString(kOfxImageEffectPropSupportedComponents, std::vector<std::string>{});
    props_.defInt(kOfxImageEffectPropTemporalClipAccess, 0);
    props_.defInt(kOfxImageClipPropOptional, 0);
    props_.defString(kOfxImageClipPropFieldExtraction, kOfxImageFieldDoubled);
    props_.defInt(kOfxImageClipPropIsMask, 0);
    props_.defInt(kOfxImageEffectPropSupportsTiles, 1);
}

Clip* Clip::from(OfxImageClipHandle h) {
    auto* c = reinterpret_cast<Clip*>(h);
    return (c && c->magic_ == kMagic) ? c : nullptr;
}

PixelFormat Clip::format() const {
    PixelFormat f;
    f.components = props_.getString(kOfxImageEffectPropComponents, 0, kOfxImageComponentRGBA);
    f.depth = props_.getString(kOfxImageEffectPropPixelDepth, 0, kOfxBitDepthFloat);
    return f;
}

// ================================================================== Effect
Effect::Effect(PluginBinary* bin, int pluginIndex, bool descriptor)
    : bin_(bin), pluginIndex_(pluginIndex), descriptor_(descriptor),
      props_(descriptor ? "EffectDescriptor" : "EffectInstance"), params_(descriptor) {
    std::lock_guard<std::mutex> lk(Host::get().liveMtx);
    Host::get().liveEffects.insert(this);
}

Effect::~Effect() {
    {
        std::lock_guard<std::mutex> lk(Host::get().liveMtx);
        Host::get().liveEffects.erase(this);
    }
    magic_ = 0;
}

Effect* Effect::from(OfxImageEffectHandle h) {
    auto* e = reinterpret_cast<Effect*>(h);
    if (!e) return nullptr;
    {
        std::lock_guard<std::mutex> lk(Host::get().liveMtx);
        if (!Host::get().liveEffects.count(e)) return nullptr;
    }
    return e->magic_ == kMagic ? e : nullptr;
}

Clip* Effect::clip(const std::string& name) const {
    for (auto& c : clips_) if (c->name() == name) return c.get();
    return nullptr;
}

Clip* Effect::defineClip(const std::string& name) {
    if (Clip* c = clip(name)) return c;
    clips_.emplace_back(new Clip(this, name, descriptor_));
    return clips_.back().get();
}

// ================================================================== image effect suite
namespace {

std::mutex g_imgRegMtx;
std::map<OfxPropertySetHandle, HostImage*> g_imgReg;
std::mutex g_memRegMtx;
std::set<HostImageMemory*> g_memReg;

OfxStatus getPropertySet(OfxImageEffectHandle h, OfxPropertySetHandle* propHandle) {
    Effect* e = Effect::from(h);
    if (!e) { diag().badHandles++; return kOfxStatErrBadHandle; }
    if (propHandle) *propHandle = e->props().handle();
    return kOfxStatOK;
}
OfxStatus getParamSet(OfxImageEffectHandle h, OfxParamSetHandle* paramSet) {
    Effect* e = Effect::from(h);
    if (!e) { diag().badHandles++; return kOfxStatErrBadHandle; }
    if (paramSet) *paramSet = e->params().handle();
    return kOfxStatOK;
}
OfxStatus clipDefine(OfxImageEffectHandle h, const char* name, OfxPropertySetHandle* propertySet) {
    Effect* e = Effect::from(h);
    if (!e) { diag().badHandles++; return kOfxStatErrBadHandle; }
    if (!name) return kOfxStatErrValue;
    if (!e->isDescriptor()) {
        diag().warn(std::string("clipDefine on an instance (clip '") + name + "')");
        return kOfxStatErrBadHandle;
    }
    Clip* c = e->defineClip(name);
    if (propertySet) *propertySet = c->props().handle();
    return kOfxStatOK;
}
OfxStatus clipGetHandle(OfxImageEffectHandle h, const char* name, OfxImageClipHandle* clip, OfxPropertySetHandle* propertySet) {
    Effect* e = Effect::from(h);
    if (!e) { diag().badHandles++; return kOfxStatErrBadHandle; }
    if (!name) return kOfxStatErrUnknown;
    Clip* c = e->clip(name);
    if (!c) return kOfxStatErrUnknown;
    if (clip) *clip = c->handle();
    if (propertySet) *propertySet = c->props().handle();
    return kOfxStatOK;
}
OfxStatus clipGetPropertySet(OfxImageClipHandle h, OfxPropertySetHandle* propHandle) {
    Clip* c = Clip::from(h);
    if (!c) { diag().badHandles++; return kOfxStatErrBadHandle; }
    if (propHandle) *propHandle = c->props().handle();
    return kOfxStatOK;
}
OfxStatus clipGetImage(OfxImageClipHandle h, OfxTime time, const OfxRectD* region, OfxPropertySetHandle* imageHandle) {
    Clip* c = Clip::from(h);
    if (!c) { diag().badHandles++; return kOfxStatErrBadHandle; }
    if (!imageHandle) return kOfxStatErrBadHandle;
    Effect* e = c->effect();
    if (!e || !e->instance) {
        diag().warn("clipGetImage on a descriptor clip");
        return kOfxStatErrBadHandle;
    }
    OfxStatus st = e->instance->clipGetImage(c, time, region, imageHandle);
    return st;
}
OfxStatus clipReleaseImage(OfxPropertySetHandle imageHandle) {
    HostImage* img = nullptr;
    {
        std::lock_guard<std::mutex> lk(g_imgRegMtx);
        auto it = g_imgReg.find(imageHandle);
        if (it == g_imgReg.end()) {
            diag().warn("clipReleaseImage on an unknown/already released image handle");
            return kOfxStatErrBadHandle;
        }
        img = it->second;
        g_imgReg.erase(it);
    }
    Effect* e = img->clip ? img->clip->effect() : nullptr;
    if (e && e->instance) return e->instance->clipReleaseImage(img);
    delete img;
    return kOfxStatOK;
}
OfxStatus clipGetRegionOfDefinition(OfxImageClipHandle h, OfxTime time, OfxRectD* bounds) {
    Clip* c = Clip::from(h);
    if (!c) { diag().badHandles++; return kOfxStatErrBadHandle; }
    if (!bounds) return kOfxStatErrBadHandle;
    Effect* e = c->effect();
    if (!e || !e->instance) return kOfxStatErrBadHandle;
    return e->instance->clipGetRoD(c, time, bounds);
}
int abortFn(OfxImageEffectHandle) { return 0; }

OfxStatus imageMemoryAlloc(OfxImageEffectHandle, size_t nBytes, OfxImageMemoryHandle* memoryHandle) {
    if (!memoryHandle) return kOfxStatErrBadHandle;
    auto* m = new HostImageMemory;
    try {
        m->storage.resize(nBytes + 64);
    } catch (...) {
        delete m;
        return kOfxStatErrMemory;
    }
    uintptr_t p = reinterpret_cast<uintptr_t>(m->storage.data());
    m->aligned = m->storage.data() + ((64 - (p & 63)) & 63);
    {
        std::lock_guard<std::mutex> lk(g_memRegMtx);
        g_memReg.insert(m);
    }
    *memoryHandle = reinterpret_cast<OfxImageMemoryHandle>(m);
    return kOfxStatOK;
}
HostImageMemory* IM(OfxImageMemoryHandle h) {
    auto* m = reinterpret_cast<HostImageMemory*>(h);
    std::lock_guard<std::mutex> lk(g_memRegMtx);
    return g_memReg.count(m) ? m : nullptr;
}
OfxStatus imageMemoryFree(OfxImageMemoryHandle h) {
    HostImageMemory* m = IM(h);
    if (!m) return kOfxStatErrBadHandle;
    {
        std::lock_guard<std::mutex> lk(g_memRegMtx);
        g_memReg.erase(m);
    }
    if (m->locks > 0) diag().warn("imageMemoryFree on locked memory");
    delete m;
    return kOfxStatOK;
}
OfxStatus imageMemoryLock(OfxImageMemoryHandle h, void** returnedPtr) {
    HostImageMemory* m = IM(h);
    if (!m) return kOfxStatErrBadHandle;
    m->locks++;
    if (returnedPtr) *returnedPtr = m->aligned;
    return kOfxStatOK;
}
OfxStatus imageMemoryUnlock(OfxImageMemoryHandle h) {
    HostImageMemory* m = IM(h);
    if (!m) return kOfxStatErrBadHandle;
    if (m->locks > 0) m->locks--;
    return kOfxStatOK;
}

OfxImageEffectSuiteV1 g_effectSuite = {
    getPropertySet, getParamSet, clipDefine, clipGetHandle, clipGetPropertySet, clipGetImage, clipReleaseImage,
    clipGetRegionOfDefinition, abortFn, imageMemoryAlloc, imageMemoryFree, imageMemoryLock, imageMemoryUnlock,
};

} // namespace

OfxImageEffectSuiteV1* imageEffectSuite() { return &g_effectSuite; }

void registerImage(HostImage* img) {
    std::lock_guard<std::mutex> lk(g_imgRegMtx);
    g_imgReg[img->props.handle()] = img;
}
void unregisterImage(HostImage* img) {
    std::lock_guard<std::mutex> lk(g_imgRegMtx);
    g_imgReg.erase(img->props.handle());
}

// ================================================================== binaries
static bool isDir(const std::string& p) {
    std::error_code ec;
    return std::filesystem::is_directory(std::filesystem::u8path(p), ec);
}

// OFX bundle architecture folders of the platform this host runs on (OFX "Packaging")
static std::vector<std::string> archDirs() {
#if defined(_WIN32)
    return {"Win64"};
#elif defined(__APPLE__)
    return {"MacOS"};
#else
    return {"Linux-x86-64", "Linux-x86_64", "Linux-aarch64"};
#endif
}

static void* openLibrary(const std::string& path, std::string& err) {
#if defined(_WIN32)
    std::wstring w = std::filesystem::u8path(path).wstring();
    HMODULE h = ::LoadLibraryExW(w.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!h) err = "LoadLibraryExW failed (error " + std::to_string(::GetLastError()) + ")";
    return reinterpret_cast<void*>(h);
#else
    void* h = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!h) err = std::string("dlopen failed: ") + dlerror();
    return h;
#endif
}

static void* librarySymbol(void* lib, const char* name) {
#if defined(_WIN32)
    return reinterpret_cast<void*>(::GetProcAddress(static_cast<HMODULE>(lib), name));
#else
    return dlsym(lib, name);
#endif
}

bool PluginBinary::open(const std::string& pathOrBundle, std::string& err) {
    std::string p = pathOrBundle;
    while (p.size() > 1 && (p.back() == '/' || p.back() == '\\')) p.pop_back();
    if (isDir(p)) {
        bundlePath = p;
        std::string arch;
        for (const std::string& a : archDirs())
            if (isDir(p + "/Contents/" + a)) { arch = p + "/Contents/" + a; break; }
        if (arch.empty()) { err = "bundle has no Contents/" + archDirs()[0] + " directory: " + p; return false; }
        std::error_code ec;
        for (const auto& de : std::filesystem::directory_iterator(std::filesystem::u8path(arch), ec)) {
            std::string n = de.path().filename().u8string();
            if (n.size() > 4 && n.substr(n.size() - 4) == ".ofx") { path = arch + "/" + n; break; }
        }
        if (path.empty()) { err = "no .ofx binary in " + arch; return false; }
    } else {
        path = p;
        // derive the bundle root from .../X.ofx.bundle/Contents/<arch>/X.ofx
        size_t k = p.find(".ofx.bundle");
        if (k != std::string::npos) bundlePath = p.substr(0, k + 11);
    }
    dl = openLibrary(path, err);
    if (!dl) return false;
    using SetHostFn = OfxStatus (*)(const OfxHost*);
    using NumFn = int (*)();
    using GetFn = OfxPlugin* (*)(int);
    auto setHost = reinterpret_cast<SetHostFn>(librarySymbol(dl, "OfxSetHost"));
    auto num = reinterpret_cast<NumFn>(librarySymbol(dl, "OfxGetNumberOfPlugins"));
    auto getp = reinterpret_cast<GetFn>(librarySymbol(dl, "OfxGetPlugin"));
    if (!num || !getp) { err = "binary does not export OfxGetNumberOfPlugins/OfxGetPlugin"; return false; }
    if (setHost) {
        OfxStatus st = setHost(&Host::get().ofxHost);
        if (st != kOfxStatOK && st != kOfxStatReplyDefault) {
            err = std::string("OfxSetHost returned ") + statusName(st);
            return false;
        }
    }
    int n = num();
    for (int i = 0; i < n; ++i) {
        OfxPlugin* pl = getp(i);
        if (!pl) continue;
        PluginInfo info;
        info.plugin = pl;
        info.id = pl->pluginIdentifier ? pl->pluginIdentifier : "";
        info.api = pl->pluginApi ? pl->pluginApi : "";
        info.apiVersion = pl->apiVersion;
        info.vMajor = int(pl->pluginVersionMajor);
        info.vMinor = int(pl->pluginVersionMinor);
        plugins.push_back(std::move(info));
    }
    if (plugins.empty()) { err = "binary contains no plug-ins"; return false; }
    return true;
}

void PluginBinary::close() {
    for (auto& pi : plugins) {
        pi.contexts.clear();
        if (pi.loaded) {
            callAction(pi, kOfxActionUnload, nullptr, nullptr, nullptr);
            pi.loaded = false;
        }
        pi.descriptor.reset();
    }
    plugins.clear();
    // Note: the library is intentionally not dlclose()d: plug-ins built with static C++
    // runtimes may register atexit destructors that would run after unmapping.
    dl = nullptr;
}

OfxStatus callAction(PluginInfo& pi, const char* action, const void* handle, PropertySet* inArgs, PropertySet* outArgs,
                     double* ms) {
    Host& h = Host::get();
    if (h.opt.trace) std::fprintf(stderr, "[ofxhost] -> %s\n", action);
    auto t0 = std::chrono::steady_clock::now();
    OfxStatus st = kOfxStatErrFatal;
    try {
        st = pi.plugin->mainEntry(action, handle, inArgs ? inArgs->handle() : nullptr, outArgs ? outArgs->handle() : nullptr);
    } catch (const std::exception& e) {
        diag().warn(std::string("C++ exception escaped plug-in action ") + action + ": " + e.what());
        st = kOfxStatErrFatal;
    } catch (...) {
        diag().warn(std::string("C++ exception escaped plug-in action ") + action);
        st = kOfxStatErrFatal;
    }
    double dt = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    if (ms) *ms = dt;
    if (h.opt.trace) std::fprintf(stderr, "[ofxhost] <- %s = %s (%.3f ms)\n", action, statusName(st), dt);
    return st;
}

} // namespace ofxhost
