// ofxhost - effect instance lifecycle, user interaction and the render driver.
#include "Host.h"
#include "Png.h"

#include "ofxGPURender.h"
#include "ofxColour.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <thread>

#include <sys/stat.h>

namespace ofxhost {

void registerImage(HostImage* img);
void unregisterImage(HostImage* img);

static double msSince(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

// ================================================================== source
bool SourceSpec::sequence() const { return spec.find('%') != std::string::npos; }

bool SourceSpec::load(double time, RGBAF& out, std::string& err) const {
    if (spec.rfind("pattern:", 0) == 0) return makePattern(spec.substr(8), w, h, time, out, err);
    std::string path = spec;
    if (sequence()) {
        char buf[4096];
        std::snprintf(buf, sizeof(buf), spec.c_str(), int(std::lround(time)));
        path = buf;
    }
    return readF32(path, w, h, out, err);
}

// ================================================================== descriptor helpers
static void initEffectDescriptorProps(PropertySet& p, const std::string& bundlePath) {
    p.defString(kOfxPropType, kOfxTypeImageEffect, true);
    p.defString(kOfxPropLabel, "");
    p.defString(kOfxPropShortLabel, "");
    p.defString(kOfxPropLongLabel, "");
    p.defInt(kOfxPropVersion, std::vector<int>{});
    p.defString(kOfxPropVersionLabel, "");
    p.defString(kOfxPropPluginDescription, "");
    p.defString(kOfxImageEffectPropSupportedContexts, std::vector<std::string>{});
    p.defString(kOfxImageEffectPluginPropGrouping, "");
    p.defInt(kOfxImageEffectPluginPropSingleInstance, 0);
    p.defString(kOfxImageEffectPluginRenderThreadSafety, kOfxImageEffectRenderInstanceSafe);
    p.defInt(kOfxImageEffectPluginPropHostFrameThreading, 1);
    p.defPointer(kOfxImageEffectPluginPropOverlayInteractV1, nullptr);
    p.defPointer(kOfxImageEffectPluginPropOverlayInteractV2, nullptr);
    p.defInt(kOfxImageEffectPropSupportsMultiResolution, 1);
    p.defInt(kOfxImageEffectPropSupportsTiles, 1);
    p.defInt(kOfxImageEffectPropTemporalClipAccess, 0);
    p.defString(kOfxImageEffectPropSupportedPixelDepths, std::vector<std::string>{});
    p.defInt(kOfxImageEffectPluginPropFieldRenderTwiceAlways, 1);
    p.defInt(kOfxImageEffectPropSupportsMultipleClipDepths, 0);
    p.defInt(kOfxImageEffectPropSupportsMultipleClipPARs, 0);
    p.defString(kOfxImageEffectPropClipPreferencesSlaveParam, std::vector<std::string>{});
    p.defString(kOfxImageEffectPropOpenGLRenderSupported, "false");
    p.defString(kOfxImageEffectPropCPURenderSupported, "true");
    p.defString(kOfxImageEffectPropOpenCLRenderSupported, "false");
    p.defString(kOfxImageEffectPropOpenCLSupported, "false");
    p.defString(kOfxImageEffectPropCudaRenderSupported, "false");
    p.defString(kOfxImageEffectPropCudaStreamSupported, "false");
    p.defString(kOfxImageEffectPropMetalRenderSupported, "false");
    p.defString(kOfxOpenGLPropPixelDepth, std::vector<std::string>{});
    p.defString(kOfxImageEffectPropNoSpatialAwareness, "false");
    p.defString(kOfxImageEffectPropColourManagementStyle, kOfxImageEffectColourManagementNone);
    p.defString(kOfxImageEffectPropColourManagementAvailableConfigs, std::vector<std::string>{});
    p.defString(kOfxPluginPropFilePath, bundlePath, true);
}

static bool ensureLoaded(PluginBinary& bin, PluginInfo& pi, std::string& err) {
    if (pi.loaded) return true;
    if (pi.api != kOfxImageEffectPluginApi) { err = "plug-in API '" + pi.api + "' is not " kOfxImageEffectPluginApi; return false; }
    if (pi.apiVersion < 1) { err = "unsupported plug-in API version"; return false; }
    pi.plugin->setHost(&Host::get().ofxHost);
    OfxStatus st = callAction(pi, kOfxActionLoad, nullptr, nullptr, nullptr);
    if (st != kOfxStatOK && st != kOfxStatReplyDefault) { err = std::string("kOfxActionLoad failed: ") + statusName(st); return false; }
    pi.loaded = true;
    (void)bin;
    return true;
}

bool describePlugin(PluginBinary& bin, int idx, std::string& err) {
    PluginInfo& pi = bin.plugins[size_t(idx)];
    if (!ensureLoaded(bin, pi, err)) return false;
    if (pi.described) return true;
    pi.descriptor.reset(new Effect(&bin, idx, true));
    initEffectDescriptorProps(pi.descriptor->props(), bin.bundlePath);
    OfxStatus st = callAction(pi, kOfxActionDescribe, pi.descriptor->handle(), nullptr, nullptr);
    if (st != kOfxStatOK && st != kOfxStatReplyDefault) { err = std::string("kOfxActionDescribe failed: ") + statusName(st); return false; }
    pi.described = true;
    return true;
}

bool describeInContext(PluginBinary& bin, int idx, const std::string& context, std::string& err) {
    PluginInfo& pi = bin.plugins[size_t(idx)];
    if (!describePlugin(bin, idx, err)) return false;
    if (pi.contexts.count(context)) return true;
    auto ctxs = pi.descriptor->props().getStrings(kOfxImageEffectPropSupportedContexts);
    if (std::find(ctxs.begin(), ctxs.end(), context) == ctxs.end()) {
        std::string all;
        for (auto& c : ctxs) all += (all.empty() ? "" : ", ") + c;
        err = "plug-in does not support context " + context + " (supports: " + all + ")";
        return false;
    }
    std::unique_ptr<Effect> d(new Effect(&bin, idx, true));
    d->props() = pi.descriptor->props();
    d->props().setSetName("EffectDescriptor(" + context + ")");
    PropertySet in("DescribeInContext.inArgs");
    in.defString(kOfxImageEffectPropContext, context, true);
    OfxStatus st = callAction(pi, kOfxImageEffectActionDescribeInContext, d->handle(), &in, nullptr);
    if (st != kOfxStatOK && st != kOfxStatReplyDefault) {
        err = std::string("kOfxImageEffectActionDescribeInContext(") + context + ") failed: " + statusName(st);
        return false;
    }
    pi.contexts[context] = std::move(d);
    return true;
}

// ================================================================== Instance
Instance::Instance(PluginBinary* bin, int pluginIndex, const std::string& context)
    : bin_(bin), pluginIndex_(pluginIndex), context_(context) {}

Instance::~Instance() { destroy(); }

PluginInfo& Instance::info() { return bin_->plugins[size_t(pluginIndex_)]; }
Effect* Instance::contextDescriptor() { return info().contexts[context_].get(); }

void Instance::setProject(int w, int h) {
    projW_ = w; projH_ = h;
    source_.w = w; source_.h = h;
    if (!effect_) return;
    PropertySet& p = effect_->props();
    p.defDouble(kOfxImageEffectPropProjectSize, std::vector<double>{double(w), double(h)}, true);
    p.defDouble(kOfxImageEffectPropProjectOffset, std::vector<double>{0.0, 0.0}, true);
    p.defDouble(kOfxImageEffectPropProjectExtent, std::vector<double>{double(w), double(h)}, true);
}

void Instance::setFrameRange(double first, double last) {
    first_ = first; last_ = last;
    if (!effect_) return;
    effect_->props().defDouble(kOfxImageEffectInstancePropEffectDuration, last - first + 1, true);
    for (auto& c : effect_->clips()) {
        c->props().defDouble(kOfxImageEffectPropFrameRange, std::vector<double>{first, last}, true);
        c->props().defDouble(kOfxImageEffectPropUnmappedFrameRange, std::vector<double>{first, last}, true);
    }
}

bool Instance::create(std::string& err) {
    if (!describeInContext(*bin_, pluginIndex_, context_, err)) return false;
    Effect* desc = contextDescriptor();
    Host& H = Host::get();
    effect_.reset(new Effect(bin_, pluginIndex_, false));
    effect_->instance = this;
    PropertySet& p = effect_->props();
    // inherit everything the plug-in described (GPU flags, tiles, ... per OFX 1.4 instance inheritance)
    p = desc->props();
    p.setSetName("EffectInstance");
    p.defString(kOfxPropType, kOfxTypeImageEffectInstance, true);
    p.defString(kOfxImageEffectPropContext, context_, true);
    p.defPointer(kOfxPropInstanceData, nullptr);
    p.defDouble(kOfxImageEffectPropProjectPixelAspectRatio, 1.0, true);
    p.defDouble(kOfxImageEffectPropFrameRate, H.opt.fps, true);
    p.defInt(kOfxPropIsInteractive, H.opt.interactive ? 1 : 0, true);
    p.defInt(kOfxImageEffectInstancePropSequentialRender, desc->props().getInt(kOfxImageEffectInstancePropSequentialRender, 0, 0));
    p.defPointer(kOfxImageEffectPropPluginHandle, info().plugin, true);
    p.defString(kOfxImageEffectPropColourManagementConfig, "", true);
    p.defString(kOfxImageEffectPropDisplayColourspace, "", true);
    p.defString(kOfxImageEffectPropOCIOConfig, "", true);
    p.defString(kOfxImageEffectPropOCIODisplay, "", true);
    p.defString(kOfxImageEffectPropOCIOView, "", true);
    p.setInstanceSemantics(true);
    setProject(projW_, projH_);

    effect_->params().instantiateFrom(desc->params());
    effect_->params().currentTime = [this] { return curTime_; };
    effect_->params().inRender = [this] { return inRender_; };
    effect_->params().onPluginChanged = [this](Param* prm) { onPluginChanged(prm); };

    for (auto& dc : desc->clips()) {
        Clip* c = effect_->defineClip(dc->name());
        c->props() = dc->props();
        c->props().setSetName("ClipInstance:" + dc->name());
        PropertySet& cp = c->props();
        bool optional = cp.getInt(kOfxImageClipPropOptional, 0, 0) != 0;
        bool connected = c->isOutput() || (!optional || dc->name() == kOfxImageEffectSimpleSourceClipName);
        cp.defInt(kOfxImageClipPropConnected, connected ? 1 : 0, true);
        cp.defString(kOfxImageEffectPropPixelDepth, kOfxBitDepthFloat, true);
        cp.defString(kOfxImageEffectPropComponents, kOfxImageComponentRGBA, true);
        cp.defString(kOfxImageClipPropUnmappedPixelDepth, connected ? kOfxBitDepthFloat : kOfxBitDepthNone, true);
        cp.defString(kOfxImageClipPropUnmappedComponents, connected ? kOfxImageComponentRGBA : kOfxImageComponentNone, true);
        cp.defString(kOfxImageEffectPropPreMultiplication, kOfxImagePreMultiplied, true);
        cp.defDouble(kOfxImagePropPixelAspectRatio, 1.0, true);
        cp.defDouble(kOfxImageEffectPropFrameRate, H.opt.fps, true);
        cp.defDouble(kOfxImageEffectPropUnmappedFrameRate, H.opt.fps, true);
        cp.defString(kOfxImageClipPropFieldOrder, kOfxImageFieldNone, true);
        cp.defInt(kOfxImageClipPropContinuousSamples, 0, true);
        cp.defString(kOfxImageClipPropColourspace, "", true);
        cp.defString(kOfxImageClipPropPreferredColourspaces, std::vector<std::string>{}, true);
        cp.setInstanceSemantics(true);
    }
    if (!effect_->clip(kOfxImageEffectOutputClipName)) { err = "plug-in did not define the mandated Output clip"; return false; }
    if (context_ == kOfxImageEffectContextFilter && !effect_->clip(kOfxImageEffectSimpleSourceClipName)) {
        err = "filter context requires a Source clip";
        return false;
    }
    setFrameRange(first_, last_);

    OfxStatus st = callAction(info(), kOfxActionCreateInstance, effect_->handle(), nullptr, nullptr);
    if (st != kOfxStatOK && st != kOfxStatReplyDefault) {
        err = std::string("kOfxActionCreateInstance failed: ") + statusName(st);
        effect_.reset();
        return false;
    }
    created_ = true;
    return clipPreferences(err);
}

void Instance::destroy() {
    if (created_ && effect_) {
        OfxStatus st = callAction(info(), kOfxActionDestroyInstance, effect_->handle(), nullptr, nullptr);
        if (st != kOfxStatOK && st != kOfxStatReplyDefault)
            diag().warn(std::string("kOfxActionDestroyInstance returned ") + statusName(st));
    }
    created_ = false;
    {
        std::lock_guard<std::mutex> lk(imgMtx_);
        for (HostImage* img : images_) { unregisterImage(img); delete img; }
        images_.clear();
    }
    effect_.reset();
}

int Instance::outstandingImages() const {
    std::lock_guard<std::mutex> lk(imgMtx_);
    return int(images_.size());
}

// ================================================================== instance changed
bool Instance::instanceChanged(const std::string& type, const std::string& name, const char* reason, double time,
                               std::string& err) {
    notifyLog.push_back(std::string(reason) + ":" + type + ":" + name + "@" + std::to_string(time));
    notifyDepth_++;
    double saved = curTime_;
    curTime_ = time;
    PropertySet b("BeginInstanceChanged.inArgs");
    b.defString(kOfxPropChangeReason, reason, true);
    OfxStatus st = callAction(info(), kOfxActionBeginInstanceChanged, effect_->handle(), &b, nullptr);
    bool ok = true;
    if (st != kOfxStatOK && st != kOfxStatReplyDefault) {
        err = std::string("kOfxActionBeginInstanceChanged failed: ") + statusName(st);
        ok = false;
    }
    PropertySet a("InstanceChanged.inArgs");
    a.defString(kOfxPropType, type == "clip" ? kOfxTypeClip : kOfxTypeParameter, true);
    a.defString(kOfxPropName, name, true);
    a.defString(kOfxPropChangeReason, reason, true);
    a.defDouble(kOfxPropTime, time, true);
    a.defDouble(kOfxImageEffectPropRenderScale, std::vector<double>{1.0, 1.0}, true);
    st = callAction(info(), kOfxActionInstanceChanged, effect_->handle(), &a, nullptr);
    if (st != kOfxStatOK && st != kOfxStatReplyDefault) {
        err = std::string("kOfxActionInstanceChanged(") + name + ") failed: " + statusName(st);
        ok = false;
    }
    st = callAction(info(), kOfxActionEndInstanceChanged, effect_->handle(), &b, nullptr);
    if (st != kOfxStatOK && st != kOfxStatReplyDefault) {
        err = std::string("kOfxActionEndInstanceChanged failed: ") + statusName(st);
        ok = false;
    }
    curTime_ = saved;
    notifyDepth_--;
    if (notifyDepth_ == 0) flushDeferred();
    return ok;
}

void Instance::onPluginChanged(Param* p) {
    Host& H = Host::get();
    if (!created_ || inRender_) return;
    if (H.opt.pluginEditNotify == NotifyMode::Off) return;
    if (H.opt.pluginEditNotify == NotifyMode::Deferred || notifyDepth_ >= 8) {
        if (notifyDepth_ >= 8 && H.opt.pluginEditNotify == NotifyMode::Sync)
            diag().warn("instanceChanged recursion depth limit reached at '" + p->name() + "', deferring");
        if (std::find(deferred_.begin(), deferred_.end(), p->name()) == deferred_.end()) deferred_.push_back(p->name());
        if (notifyDepth_ == 0) flushDeferred();
        return;
    }
    std::string err;
    if (!instanceChanged("param", p->name(), kOfxChangePluginEdited, curTime_, err)) diag().warn(err);
}

void Instance::flushDeferred() {
    int guard = 0;
    while (!deferred_.empty() && guard++ < 1000) {
        std::string n = deferred_.front();
        deferred_.erase(deferred_.begin());
        std::string err;
        if (!instanceChanged("param", n, kOfxChangePluginEdited, curTime_, err)) diag().warn(err);
    }
}

static std::string suggest(const ParamSet& ps, const std::string& name) {
    std::string out;
    std::string ln = name;
    for (auto& c : ln) c = char(std::tolower(static_cast<unsigned char>(c)));
    for (auto& p : ps.params()) {
        std::string pn = p->name();
        std::string lp = pn;
        for (auto& c : lp) c = char(std::tolower(static_cast<unsigned char>(c)));
        if (lp.find(ln) != std::string::npos || ln.find(lp) != std::string::npos) out += (out.empty() ? "" : ", ") + pn;
    }
    return out.empty() ? "" : " (did you mean: " + out + "?)";
}

bool Instance::userSetParam(const std::string& name, const std::string& value, double time, bool keyframe, std::string& err) {
    Param* p = effect_->params().get(name);
    if (!p) { err = "unknown parameter '" + name + "'" + suggest(effect_->params(), name); return false; }
    if (!p->hasValue()) { err = "parameter '" + name + "' (" + paramKindShort(p->kind()) + ") has no value; use --press for buttons"; return false; }
    ParamValue v;
    if (!parseParamValue(*p, value, v, err)) { err = "--set " + name + ": " + err; return false; }
    if (clampParamValue(*p, v))
        diag().warn("--set " + name + "=" + value + " is outside [min,max]; clamped to " + formatParamValue(*p, v));
    if (!p->props().getInt(kOfxParamPropEnabled, 0, 1))
        diag().warn("--set " + name + ": parameter is currently disabled in the UI");
    if (keyframe) {
        if (!p->animates()) diag().warn("--set " + name + "@t: parameter does not animate; keyframe stored anyway");
        p->setKey(time, v);
    } else {
        p->keys().clear();
        p->updateAnimating();
        p->setStatic(v);
    }
    return instanceChanged("param", name, kOfxChangeUserEdited, time, err);
}

bool Instance::userPressButton(const std::string& name, double time, std::string& err) {
    Param* p = effect_->params().get(name);
    if (!p) { err = "unknown parameter '" + name + "'" + suggest(effect_->params(), name); return false; }
    if (p->kind() != ParamKind::PushButton) diag().warn("--press " + name + ": parameter is not a push button");
    return instanceChanged("param", name, kOfxChangeUserEdited, time, err);
}

bool Instance::resetParams(std::string& err) {
    for (auto& p : effect_->params().params()) {
        if (!p->hasValue()) continue;
        ParamValue d = p->defaultValue();
        if (p->hasKeys() || !(p->staticValue() == d)) {
            p->resetToDefault();
            if (!instanceChanged("param", p->name(), kOfxChangeUserEdited, curTime_, err)) return false;
        }
    }
    return true;
}

// ================================================================== clip preferences
static bool contains(const std::vector<std::string>& v, const std::string& s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}

bool Instance::clipPreferences(std::string& err) {
    Host& H = Host::get();
    Effect* desc = contextDescriptor();
    auto plugDepths = desc->props().getStrings(kOfxImageEffectPropSupportedPixelDepths);
    std::string depth;
    for (auto& d : H.opt.depths) if (plugDepths.empty() || contains(plugDepths, d)) { depth = d; break; }
    if (depth.empty()) {
        err = "no common pixel depth between host and plug-in";
        return false;
    }
    PropertySet out("GetClipPreferences.outArgs");
    for (auto& c : effect_->clips()) {
        auto sup = c->props().getStrings(kOfxImageEffectPropSupportedComponents);
        std::string comp;
        for (auto& hc : H.opt.components)
            if (sup.empty() || contains(sup, hc)) { comp = hc; break; }
        if (comp.empty()) comp = kOfxImageComponentRGBA;
        std::string n = c->name();
        out.defString("OfxImageClipPropComponents_" + n, comp);
        out.defString("OfxImageClipPropDepth_" + n, depth);
        out.defDouble("OfxImageClipPropPAR_" + n, 1.0);
        c->props().setString(kOfxImageEffectPropComponents, 0, comp);
        c->props().setString(kOfxImageEffectPropPixelDepth, 0, depth);
    }
    out.defDouble(kOfxImageEffectPropFrameRate, H.opt.fps);
    out.defString(kOfxImageClipPropFieldOrder, kOfxImageFieldNone);
    out.defString(kOfxImageEffectPropPreMultiplication, kOfxImagePreMultiplied);
    out.defInt(kOfxImageClipPropContinuousSamples, 0);
    out.defInt(kOfxImageEffectFrameVarying, 0);
    OfxStatus st = callAction(info(), kOfxImageEffectActionGetClipPreferences, effect_->handle(), nullptr, &out);
    if (st != kOfxStatOK && st != kOfxStatReplyDefault) {
        err = std::string("kOfxImageEffectActionGetClipPreferences failed: ") + statusName(st);
        return false;
    }
    clipPrefsSummary.clear();
    for (auto& c : effect_->clips()) {
        std::string n = c->name();
        std::string comp = out.getString("OfxImageClipPropComponents_" + n, 0, kOfxImageComponentRGBA);
        std::string dep = out.getString("OfxImageClipPropDepth_" + n, 0, depth);
        if (!contains(H.opt.components, comp)) {
            diag().warn("clip preferences: plug-in asked for unsupported components " + comp + " on " + n);
            comp = kOfxImageComponentRGBA;
        }
        if (!contains(H.opt.depths, dep)) {
            diag().warn("clip preferences: plug-in asked for unsupported depth " + dep + " on " + n);
            dep = depth;
        }
        c->props().setString(kOfxImageEffectPropComponents, 0, comp);
        c->props().setString(kOfxImageEffectPropPixelDepth, 0, dep);
        c->props().setDouble(kOfxImagePropPixelAspectRatio, 0, out.getDouble("OfxImageClipPropPAR_" + n, 0, 1.0));
        if (c->isOutput()) {
            c->props().setString(kOfxImageEffectPropPreMultiplication, 0,
                                 out.getString(kOfxImageEffectPropPreMultiplication, 0, kOfxImagePreMultiplied));
            c->props().setString(kOfxImageClipPropFieldOrder, 0, out.getString(kOfxImageClipPropFieldOrder, 0, kOfxImageFieldNone));
            c->props().setInt(kOfxImageClipPropContinuousSamples, 0, out.getInt(kOfxImageClipPropContinuousSamples, 0, 0));
        }
        clipPrefsSummary[n] = comp + "/" + dep;
    }
    return true;
}

// ================================================================== images
// The source frame at render scale. Static files are cached (already resampled) per path +
// size + mtime + scale, so repeats / batch jobs do not re-read or copy them; patterns and
// sequences are generated per call. `out` points either into the cache or to `storage`.
bool Instance::sourceForTime(double time, double scale, RGBAF& storage, const RGBAF*& out, std::string& err) {
    const int W = std::max(1, int(std::lround(projW_ * scale)));
    const int Hh = std::max(1, int(std::lround(projH_ * scale)));
    if (source_.spec.empty()) { err = "no input (--in) given"; return false; }
    if (!source_.sequence() && source_.spec.rfind("pattern:", 0) != 0) {
        struct stat st {};
        std::string key = source_.spec;
        if (stat(source_.spec.c_str(), &st) == 0)
            key += "|" + std::to_string(st.st_size) + "|" + std::to_string(st.st_mtim.tv_sec) + "." + std::to_string(st.st_mtim.tv_nsec);
        key += "|" + std::to_string(source_.w) + "x" + std::to_string(source_.h) + "@" + std::to_string(scale);
        auto it = sourceFileCache_.find(key);
        if (it == sourceFileCache_.end()) {
            if (sourceFileCache_.size() > 8) sourceFileCache_.clear();
            RGBAF img;
            if (!source_.load(time, img, err)) return false;
            if (scale != 1.0) img = resampleArea(img, W, Hh);
            it = sourceFileCache_.emplace(key, std::move(img)).first;
        }
        out = &it->second;
        return true;
    }
    RGBAF full;
    if (!source_.load(time, full, err)) return false;
    storage = scale == 1.0 ? std::move(full) : resampleArea(full, W, Hh);
    out = &storage;
    return true;
}

static void fillImageProps(PropertySet& p, const PixelFormat& f, const std::string& premult, double scale,
                           int x1, int y1, int x2, int y2, size_t rowBytes, void* data, const std::string& uid) {
    p.defString(kOfxPropType, kOfxTypeImage, true);
    p.defString(kOfxImageEffectPropPixelDepth, f.depth, true);
    p.defString(kOfxImageEffectPropComponents, f.components, true);
    p.defString(kOfxImageEffectPropPreMultiplication, premult, true);
    p.defDouble(kOfxImageEffectPropRenderScale, std::vector<double>{scale, scale}, true);
    p.defDouble(kOfxImagePropPixelAspectRatio, 1.0, true);
    p.defPointer(kOfxImagePropData, data, true);
    p.defInt(kOfxImagePropBounds, std::vector<int>{x1, y1, x2, y2}, true);
    p.defInt(kOfxImagePropRegionOfDefinition, std::vector<int>{x1, y1, x2, y2}, true);
    p.defInt(kOfxImagePropRowBytes, int(rowBytes), true);
    p.defString(kOfxImagePropField, kOfxImageFieldNone, true);
    p.defString(kOfxImagePropUniqueIdentifier, uid, true);
    p.setInstanceSemantics(true);
}

OfxStatus Instance::clipGetImage(Clip* c, double time, const OfxRectD*, OfxPropertySetHandle* out) {
    if (!c->connected()) return kOfxStatFailed;
    Host& H = Host::get();
    std::unique_ptr<HostImage> img(new HostImage);
    img->clip = c;
    PixelFormat f = c->format();
    std::string premult = c->props().getString(kOfxImageEffectPropPreMultiplication, 0, kOfxImagePreMultiplied);
    std::lock_guard<std::mutex> lk(imgMtx_);
    std::string uid = c->name() + "@" + std::to_string(time) + "#" + std::to_string(++imageCounter_);
    double scale = inRender_ ? rs_.scale : 1.0;
    if (c->isOutput()) {
        if (!inRender_) {
            diag().warn("clipGetImage(Output) outside of the render action");
            return kOfxStatFailed;
        }
        if (std::fabs(time - rs_.time) > 1e-9) {
            diag().warn("clipGetImage(Output) at a time other than the render time");
            return kOfxStatFailed;
        }
        ImageMemory& m = *rs_.outMem;
        img->mem = rs_.outMem;
        if (rs_.gpu) {
            img->clMem = rs_.outCl;
            bool images = rs_.gpuMode == GpuMode::OpenCLImages;
            fillImageProps(img->props, f, premult, scale, m.x1, m.y1, m.x2, m.y2, m.rowBytes, images ? nullptr : rs_.outCl, uid);
            if (images) img->props.defPointer(kOfxImageEffectPropOpenCLImage, rs_.outCl, true);
        } else {
            fillImageProps(img->props, f, premult, scale, m.x1, m.y1, m.x2, m.y2, m.rowBytes, m.data, uid);
        }
    } else {
        std::string key = c->name() + "@" + std::to_string(time) + "x" + std::to_string(scale);
        std::shared_ptr<ImageMemory> mem;
        auto it = inRender_ ? rs_.srcCache.find(key) : rs_.srcCache.end();
        if (inRender_ && it != rs_.srcCache.end()) {
            mem = it->second;
        } else {
            RGBAF tmp;
            const RGBAF* src = nullptr;
            std::string err;
            if (!sourceForTime(time, scale, tmp, src, err)) {
                diag().warn("source image for t=" + std::to_string(time) + ": " + err);
                return kOfxStatFailed;
            }
            mem = std::make_shared<ImageMemory>();
            mem->allocate(0, 0, src->w, src->h, f);
            fillImageFromRGBA(*mem, *src);
            if (inRender_) rs_.srcCache[key] = mem;
        }
        img->mem = mem;
        if (inRender_ && rs_.gpu) {
            void* clm = nullptr;
            auto ci = rs_.srcCl.find(key);
            if (ci != rs_.srcCl.end()) {
                clm = ci->second;
            } else {
                std::string err;
                auto t0 = std::chrono::steady_clock::now();
                bool images = rs_.gpuMode == GpuMode::OpenCLImages;
                if (images) {
                    clm = H.cl->createImage2D(mem->width(), mem->height(), f.nComps(), f.depth == kOfxBitDepthFloat || f.depth == kOfxBitDepthHalf,
                                              f.bytesPerComp(), err);
                    if (clm && !H.cl->uploadImage(clm, mem->data, mem->width(), mem->height(), mem->rowBytes, err)) { H.cl->release(clm); clm = nullptr; }
                } else {
                    clm = H.cl->createBuffer(mem->bytes(), err);
                    if (clm && !H.cl->upload(clm, mem->data, mem->bytes(), err)) { H.cl->release(clm); clm = nullptr; }
                }
                rs_.uploadMs += msSince(t0);
                if (!clm) { diag().warn("OpenCL source upload failed: " + err); return kOfxStatErrMemory; }
                rs_.srcCl[key] = clm;
            }
            img->clMem = clm;
            bool images = rs_.gpuMode == GpuMode::OpenCLImages;
            fillImageProps(img->props, f, c->props().getString(kOfxImageEffectPropPreMultiplication, 0, kOfxImagePreMultiplied), scale,
                           mem->x1, mem->y1, mem->x2, mem->y2, mem->rowBytes, images ? nullptr : clm, uid);
            if (images) img->props.defPointer(kOfxImageEffectPropOpenCLImage, clm, true);
        } else {
            fillImageProps(img->props, f, premult, scale, mem->x1, mem->y1, mem->x2, mem->y2, mem->rowBytes, mem->data, uid);
        }
    }
    HostImage* raw = img.release();
    images_.insert(raw);
    registerImage(raw);
    *out = raw->props.handle();
    return kOfxStatOK;
}

OfxStatus Instance::clipReleaseImage(HostImage* img) {
    std::lock_guard<std::mutex> lk(imgMtx_);
    images_.erase(img);
    delete img;
    return kOfxStatOK;
}

OfxStatus Instance::clipGetRoD(Clip*, double, OfxRectD* rod) {
    rod->x1 = 0; rod->y1 = 0; rod->x2 = projW_; rod->y2 = projH_;
    return kOfxStatOK;
}

// ================================================================== render
PropertySet Instance::makeRenderArgs(double time, double scale, const OfxRectI& win, bool gpuEnabled, bool draft) {
    Host& H = Host::get();
    PropertySet a("Render.inArgs");
    a.defDouble(kOfxPropTime, time, true);
    a.defString(kOfxImageEffectPropFieldToRender, kOfxImageFieldNone, true);
    a.defInt(kOfxImageEffectPropRenderWindow, std::vector<int>{win.x1, win.y1, win.x2, win.y2}, true);
    a.defDouble(kOfxImageEffectPropRenderScale, std::vector<double>{scale, scale}, true);
    a.defInt(kOfxImageEffectPropSequentialRenderStatus, 0, true);
    a.defInt(kOfxImageEffectPropInteractiveRenderStatus, H.opt.interactive ? 1 : 0, true);
    a.defInt(kOfxImageEffectPropRenderQualityDraft, draft ? 1 : 0, true);
    a.defInt(kOfxImageEffectPropOpenGLEnabled, 0, true);
    a.defInt(kOfxImageEffectPropOpenCLEnabled, gpuEnabled ? 1 : 0, true);
    a.defPointer(kOfxImageEffectPropOpenCLCommandQueue, gpuEnabled && H.cl ? H.cl->queue() : nullptr, true);
    a.defInt(kOfxImageEffectPropCudaEnabled, 0, true);
    a.defPointer(kOfxImageEffectPropCudaStream, nullptr, true);
    a.defInt(kOfxImageEffectPropMetalEnabled, 0, true);
    a.defPointer(kOfxImageEffectPropMetalCommandQueue, nullptr, true);
    return a;
}

PropertySet Instance::makeSequenceArgs(double first, double last, double scale, bool gpuEnabled) {
    Host& H = Host::get();
    PropertySet a("SequenceRender.inArgs");
    a.defDouble(kOfxImageEffectPropFrameRange, std::vector<double>{first, last}, true);
    a.defDouble(kOfxImageEffectPropFrameStep, 1.0, true);
    a.defInt(kOfxPropIsInteractive, H.opt.interactive ? 1 : 0, true);
    a.defDouble(kOfxImageEffectPropRenderScale, std::vector<double>{scale, scale}, true);
    a.defInt(kOfxImageEffectPropSequentialRenderStatus, 0, true);
    a.defInt(kOfxImageEffectPropInteractiveRenderStatus, H.opt.interactive ? 1 : 0, true);
    a.defInt(kOfxImageEffectPropOpenGLEnabled, 0, true);
    a.defInt(kOfxImageEffectPropOpenCLEnabled, gpuEnabled ? 1 : 0, true);
    a.defPointer(kOfxImageEffectPropOpenCLCommandQueue, gpuEnabled && H.cl ? H.cl->queue() : nullptr, true);
    a.defInt(kOfxImageEffectPropCudaEnabled, 0, true);
    a.defPointer(kOfxImageEffectPropCudaStream, nullptr, true);
    a.defInt(kOfxImageEffectPropMetalEnabled, 0, true);
    a.defPointer(kOfxImageEffectPropMetalCommandQueue, nullptr, true);
    return a;
}

static bool supportsFlag(Effect* inst, Effect* desc, const char* prop) {
    // OFX 1.4: an instance inherits the descriptor value until the plug-in sets it on the instance.
    std::string v = inst->props().getString(prop, 0, desc->props().getString(prop, 0, "false"));
    return v == "true" || v == "needed";
}

bool Instance::render(const RenderRequest& rq, std::vector<FrameResult>& results, std::vector<RGBAF>* keep, std::string& err) {
    Host& H = Host::get();
    if (!created_) { err = "instance not created"; return false; }
    if (rq.times.empty()) { err = "no frames to render"; return false; }
    if (!clipPreferences(err)) return false;
    bool gpu = rq.gpu != GpuMode::None;
    GpuMode gm = rq.gpu;
    if (gpu) {
        if (!H.cl || !H.cl->ready()) { err = "--gpu requested but no OpenCL context is available"; return false; }
        const char* flag = gm == GpuMode::OpenCLBuffers ? kOfxImageEffectPropOpenCLRenderSupported : kOfxImageEffectPropOpenCLSupported;
        if (!supportsFlag(effect_.get(), contextDescriptor(), flag)) {
            std::string what = gm == GpuMode::OpenCLBuffers ? "OpenCL buffers (kOfxImageEffectPropOpenCLRenderSupported)"
                                                            : "OpenCL images (kOfxImageEffectPropOpenCLSupported)";
            if (!rq.gpuFallbackToCpu) { err = "plug-in does not declare " + what; return false; }
            diag().warn("plug-in does not declare " + what + "; rendering on CPU");
            gpu = false;
            gm = GpuMode::None;
        }
    }
    double first = *std::min_element(rq.times.begin(), rq.times.end());
    double last = *std::max_element(rq.times.begin(), rq.times.end());
    PropertySet seq = makeSequenceArgs(first, last, rq.renderScale, gpu);
    OfxStatus st = callAction(info(), kOfxImageEffectActionBeginSequenceRender, effect_->handle(), &seq, nullptr);
    if (st != kOfxStatOK && st != kOfxStatReplyDefault) {
        err = std::string("kOfxImageEffectActionBeginSequenceRender failed: ") + statusName(st);
        return false;
    }
    bool ok = true;
    RenderRequest rq2 = rq;
    rq2.gpu = gm;
    for (size_t fi = 0; fi < rq.times.size(); ++fi) {
        FrameResult fr;
        RGBAF outImg;
        if (rq2.gpu != GpuMode::None) {
            // the plug-in may switch its GPU support off on the instance (OFX 1.4) after a failure
            const char* flag = rq2.gpu == GpuMode::OpenCLBuffers ? kOfxImageEffectPropOpenCLRenderSupported : kOfxImageEffectPropOpenCLSupported;
            if (!supportsFlag(effect_.get(), contextDescriptor(), flag)) {
                if (!rq.gpuFallbackToCpu) {
                    err = std::string("plug-in withdrew ") + flag + " before t=" + std::to_string(rq.times[fi]);
                    ok = false;
                    break;
                }
                diag().warn(std::string("plug-in withdrew ") + flag + "; rendering the remaining frames on the CPU");
                rq2.gpu = GpuMode::None;
            }
        }
        if (!renderFrame(rq2, rq.times[fi], fr, outImg, err)) {
            ok = false;
            results.push_back(fr);
            break;
        }
        if (!rq.outPrefix.empty()) {
            std::string base = rq.outPrefix;
            if (rq.times.size() > 1) {
                char buf[32];
                std::snprintf(buf, sizeof(buf), ".%04d", int(std::lround(rq.times[fi])));
                base += buf;
            }
            std::string e2;
            if (rq.writeF32) {
                if (!writeF32(base + ".f32", outImg, e2)) { err = e2; ok = false; }
                else fr.files.push_back(base + ".f32");
            }
            if (rq.writePng) {
                PngOptions po;
                po.sixteenBit = rq.png16;
                po.alpha = rq.pngAlpha;
                po.text.push_back({"Software", "ofxhost"});
                po.text.push_back({"Comment", "plugin=" + info().id + " time=" + std::to_string(rq.times[fi]) +
                                                  " scale=" + std::to_string(rq.renderScale) + " gpu=" + fr.gpu});
                if (!writePng(base + ".png", outImg, po, e2)) { err = e2; ok = false; }
                else fr.files.push_back(base + ".png");
            }
        }
        if (keep) keep->push_back(std::move(outImg));
        results.push_back(fr);
        if (!ok) break;
    }
    st = callAction(info(), kOfxImageEffectActionEndSequenceRender, effect_->handle(), &seq, nullptr);
    if (st != kOfxStatOK && st != kOfxStatReplyDefault) {
        if (ok) err = std::string("kOfxImageEffectActionEndSequenceRender failed: ") + statusName(st);
        ok = false;
    }
    return ok;
}

bool Instance::renderFrame(const RenderRequest& rq, double time, FrameResult& fr, RGBAF& outRGBA, std::string& err) {
    Host& H = Host::get();
    const double s = rq.renderScale;
    const int pixW = std::max(1, int(std::lround(projW_ * s)));
    const int pixH = std::max(1, int(std::lround(projH_ * s)));
    fr.time = time;
    fr.outW = pixW; fr.outH = pixH;
    curTime_ = time;

    // region of definition
    PropertySet rodIn("GetRegionOfDefinition.inArgs");
    rodIn.defDouble(kOfxPropTime, time, true);
    rodIn.defDouble(kOfxImageEffectPropRenderScale, std::vector<double>{s, s}, true);
    PropertySet rodOut("GetRegionOfDefinition.outArgs");
    rodOut.defDouble(kOfxImageEffectPropRegionOfDefinition, std::vector<double>{0, 0, double(projW_), double(projH_)});
    OfxStatus st = callAction(info(), kOfxImageEffectActionGetRegionOfDefinition, effect_->handle(), &rodIn, &rodOut);
    if (st != kOfxStatOK && st != kOfxStatReplyDefault) {
        err = std::string("kOfxImageEffectActionGetRegionOfDefinition failed: ") + statusName(st);
        fr.status = st;
        return false;
    }
    OfxRectD rod{0, 0, double(projW_), double(projH_)};
    if (st == kOfxStatOK) {
        rod.x1 = rodOut.getDouble(kOfxImageEffectPropRegionOfDefinition, 0, 0);
        rod.y1 = rodOut.getDouble(kOfxImageEffectPropRegionOfDefinition, 1, 0);
        rod.x2 = rodOut.getDouble(kOfxImageEffectPropRegionOfDefinition, 2, projW_);
        rod.y2 = rodOut.getDouble(kOfxImageEffectPropRegionOfDefinition, 3, projH_);
    }
    fr.rod = rod;
    OfxRectI win;
    win.x1 = std::max(0, int(std::floor(rod.x1 * s)));
    win.y1 = std::max(0, int(std::floor(rod.y1 * s)));
    win.x2 = std::min(pixW, int(std::ceil(rod.x2 * s)));
    win.y2 = std::min(pixH, int(std::ceil(rod.y2 * s)));
    if (win.x2 <= win.x1 || win.y2 <= win.y1) {
        // empty RoD: output is black/transparent
        outRGBA.resize(pixW, pixH, 0.f);
        fr.stats = computeStats(outRGBA);
        return true;
    }

    // regions of interest (informational: the host always supplies full source frames)
    PropertySet roiIn("GetRegionsOfInterest.inArgs");
    roiIn.defDouble(kOfxPropTime, time, true);
    roiIn.defDouble(kOfxImageEffectPropRenderScale, std::vector<double>{s, s}, true);
    roiIn.defDouble(kOfxImageEffectPropRegionOfInterest, std::vector<double>{win.x1 / s, win.y1 / s, win.x2 / s, win.y2 / s}, true);
    PropertySet roiOut("GetRegionsOfInterest.outArgs");
    for (auto& c : effect_->clips()) {
        if (c->isOutput()) continue;
        roiOut.defDouble("OfxImageClipPropRoI_" + c->name(), std::vector<double>{win.x1 / s, win.y1 / s, win.x2 / s, win.y2 / s});
    }
    st = callAction(info(), kOfxImageEffectActionGetRegionsOfInterest, effect_->handle(), &roiIn, &roiOut);
    if (st != kOfxStatOK && st != kOfxStatReplyDefault) {
        err = std::string("kOfxImageEffectActionGetRegionsOfInterest failed: ") + statusName(st);
        fr.status = st;
        return false;
    }
    for (auto& c : effect_->clips()) {
        if (c->isOutput()) continue;
        std::string k = "OfxImageClipPropRoI_" + c->name();
        fr.rois[c->name()] = OfxRectD{roiOut.getDouble(k, 0), roiOut.getDouble(k, 1), roiOut.getDouble(k, 2), roiOut.getDouble(k, 3)};
    }

    // identity
    PropertySet idIn("IsIdentity.inArgs");
    idIn.defDouble(kOfxPropTime, time, true);
    idIn.defString(kOfxImageEffectPropFieldToRender, kOfxImageFieldNone, true);
    idIn.defInt(kOfxImageEffectPropRenderWindow, std::vector<int>{win.x1, win.y1, win.x2, win.y2}, true);
    idIn.defDouble(kOfxImageEffectPropRenderScale, std::vector<double>{s, s}, true);
    PropertySet idOut("IsIdentity.outArgs");
    idOut.defString(kOfxPropName, "");
    idOut.defDouble(kOfxPropTime, time);
    st = callAction(info(), kOfxImageEffectActionIsIdentity, effect_->handle(), &idIn, &idOut);
    if (st != kOfxStatOK && st != kOfxStatReplyDefault) {
        err = std::string("kOfxImageEffectActionIsIdentity failed: ") + statusName(st);
        fr.status = st;
        return false;
    }
    if (st == kOfxStatOK && !idOut.getString(kOfxPropName).empty()) {
        fr.identity = true;
        fr.identityClip = idOut.getString(kOfxPropName);
        fr.identityTime = idOut.getDouble(kOfxPropTime, 0, time);
        Clip* ic = effect_->clip(fr.identityClip);
        if (!ic) { err = "IsIdentity returned unknown clip '" + fr.identityClip + "'"; return false; }
        RGBAF tmp;
        const RGBAF* src = nullptr;
        if (!sourceForTime(fr.identityTime, s, tmp, src, err)) return false;
        // pass through the clip's pixel format (quantisation as the plug-in would see it)
        ImageMemory m;
        m.allocate(0, 0, src->w, src->h, ic->format());
        fillImageFromRGBA(m, *src);
        readImageToRGBA(m, outRGBA, pixW, pixH);
        fr.renderMs.push_back(0);
        fr.totalMs.push_back(0);
        fr.gpu = "identity";
        fr.stats = computeStats(outRGBA);
        return true;
    }

    const bool frameThreading = effect_->props().getInt(kOfxImageEffectPluginPropHostFrameThreading, 0,
                                                        contextDescriptor()->props().getInt(kOfxImageEffectPluginPropHostFrameThreading, 0, 1)) != 0;
    const std::string safety = contextDescriptor()->props().getString(kOfxImageEffectPluginRenderThreadSafety, 0, kOfxImageEffectRenderInstanceSafe);
    Clip* outClip = effect_->clip(kOfxImageEffectOutputClipName);

    for (int rep = 0; rep < std::max(1, rq.repeat); ++rep) {
        bool gpu = rq.gpu != GpuMode::None;
        fr.gpu = rq.gpu == GpuMode::OpenCLBuffers ? "opencl-buffers" : (rq.gpu == GpuMode::OpenCLImages ? "opencl-images" : "cpu");
        auto tTotal = std::chrono::steady_clock::now();
        rs_ = RenderState{};
        rs_.time = time;
        rs_.scale = s;
        rs_.gpu = gpu;
        rs_.gpuMode = rq.gpu;
        rs_.outMem = std::make_shared<ImageMemory>();
        rs_.outMem->allocate(win.x1, win.y1, win.x2, win.y2, outClip->format());
        if (H.opt.sentinel) fillSentinel(*rs_.outMem);
        std::string e2;
        if (gpu) {
            const PixelFormat f = outClip->format();
            if (rq.gpu == GpuMode::OpenCLImages)
                rs_.outCl = H.cl->createImage2D(rs_.outMem->width(), rs_.outMem->height(), f.nComps(),
                                                f.depth == kOfxBitDepthFloat || f.depth == kOfxBitDepthHalf, f.bytesPerComp(), e2);
            else
                rs_.outCl = H.cl->createBuffer(rs_.outMem->bytes(), e2);
            if (!rs_.outCl) { err = "OpenCL output allocation failed: " + e2; return false; }
            if (H.opt.sentinel) {
                bool okFill = rq.gpu == GpuMode::OpenCLImages
                                  ? H.cl->uploadImage(rs_.outCl, rs_.outMem->data, rs_.outMem->width(), rs_.outMem->height(), rs_.outMem->rowBytes, e2)
                                  : H.cl->upload(rs_.outCl, rs_.outMem->data, rs_.outMem->bytes(), e2);
                if (!okFill) diag().warn("sentinel upload failed: " + e2);
            }
        }
        inRender_ = true;
        double actMs = 0;
        auto tAct = std::chrono::steady_clock::now();
        const char* gpuFlag = rq.gpu == GpuMode::OpenCLImages ? kOfxImageEffectPropOpenCLSupported : kOfxImageEffectPropOpenCLRenderSupported;
        const bool gpuBefore = gpu && supportsFlag(effect_.get(), contextDescriptor(), gpuFlag);
        if (!gpu && frameThreading && safety != kOfxImageEffectRenderUnsafe && (win.y2 - win.y1) >= 16) {
            unsigned n = std::min<unsigned>(std::max(1u, std::thread::hardware_concurrency()), unsigned(win.y2 - win.y1) / 8u);
            if (H.opt.threads > 0) n = std::min<unsigned>(n, unsigned(H.opt.threads));
            n = std::max(1u, n);
            std::vector<OfxStatus> sts(n, kOfxStatOK);
            std::vector<std::thread> pool;
            for (unsigned k = 0; k < n; ++k) {
                OfxRectI w = win;
                w.y1 = win.y1 + int((long long)(win.y2 - win.y1) * k / n);
                w.y2 = win.y1 + int((long long)(win.y2 - win.y1) * (k + 1) / n);
                pool.emplace_back([&, w, k]() {
                    PropertySet a = makeRenderArgs(time, s, w, false, rq.draft);
                    sts[k] = callAction(info(), kOfxImageEffectActionRender, effect_->handle(), &a, nullptr);
                });
            }
            for (auto& t : pool) t.join();
            st = kOfxStatOK;
            for (auto v : sts) if (v != kOfxStatOK && v != kOfxStatReplyDefault) st = v;
        } else {
            PropertySet a = makeRenderArgs(time, s, win, gpu, rq.draft);
            st = callAction(info(), kOfxImageEffectActionRender, effect_->handle(), &a, nullptr);
        }
        actMs = msSince(tAct);
        inRender_ = false;
        if (gpuBefore && !supportsFlag(effect_.get(), contextDescriptor(), gpuFlag)) {
            fr.gpuWithdrawn = true;
            diag().warn(std::string("plug-in withdrew ") + gpuFlag + " on the instance during the render at t=" + std::to_string(time) +
                        " (it probably failed on the GPU and rendered on the CPU itself)");
        }
        double finMs = 0;
        bool gpuOk = true;
        if (gpu) {
            auto tf = std::chrono::steady_clock::now();
            if (!H.cl->finish(e2)) { diag().warn(e2); gpuOk = false; }
            finMs = msSince(tf);
            if (st == kOfxStatOK && gpuOk) {
                auto td = std::chrono::steady_clock::now();
                bool okd = rq.gpu == GpuMode::OpenCLImages
                               ? H.cl->downloadImage(rs_.outCl, rs_.outMem->data, rs_.outMem->width(), rs_.outMem->height(), rs_.outMem->rowBytes, e2)
                               : H.cl->download(rs_.outCl, rs_.outMem->data, rs_.outMem->bytes(), e2);
                fr.downloadMs = msSince(td);
                if (!okd) { err = "OpenCL download failed: " + e2; gpuOk = false; }
            }
            for (auto& kv : rs_.srcCl) H.cl->release(kv.second);
            H.cl->release(rs_.outCl);
            rs_.srcCl.clear();
            rs_.outCl = nullptr;
            fr.uploadMs = rs_.uploadMs;
        }
        int leaked = outstandingImages();
        if (leaked > 0) {
            diag().warn("plug-in did not release " + std::to_string(leaked) + " image(s) fetched during render");
            std::lock_guard<std::mutex> lk(imgMtx_);
            for (HostImage* img : images_) { unregisterImage(img); delete img; }
            images_.clear();
        }
        if (gpu && (st == kOfxStatGPURenderFailed || st == kOfxStatGPUOutOfMemory) && rq.gpuFallbackToCpu) {
            diag().warn(std::string("GPU render returned ") + statusName(st) + "; retrying on CPU");
            RenderRequest cpu = rq;
            cpu.gpu = GpuMode::None;
            cpu.repeat = 1;
            bool r = renderFrame(cpu, time, fr, outRGBA, err);
            fr.gpuFellBack = true;
            return r;
        }
        if (st == kOfxStatReplyDefault)
            diag().warn("kOfxImageEffectActionRender returned kOfxStatReplyDefault at t=" + std::to_string(time) + " (nothing rendered?)");
        if (st != kOfxStatOK && st != kOfxStatReplyDefault) {
            err = std::string("kOfxImageEffectActionRender failed at t=") + std::to_string(time) + ": " + statusName(st);
            fr.status = st;
            rs_ = RenderState{};
            return false;
        }
        if (!gpuOk) { rs_ = RenderState{}; return false; }
        fr.renderMs.push_back(actMs);
        fr.finishMs.push_back(finMs);
        readImageToRGBA(*rs_.outMem, outRGBA, pixW, pixH);
        fr.totalMs.push_back(msSince(tTotal));
        rs_.srcCache.clear();
        rs_.outMem.reset();
    }
    // unwritten pixels: all four channels still NaN (the sentinel)
    long long unwritten = 0;
    if (H.opt.sentinel) {
        for (int y = 0; y < pixH; ++y)
            for (int x = 0; x < pixW; ++x) {
                int oy = pixH - 1 - y;
                if (x < win.x1 || x >= win.x2 || oy < win.y1 || oy >= win.y2) continue;
                const float* p = outRGBA.at(x, y);
                if (std::isnan(p[0]) && std::isnan(p[1]) && std::isnan(p[2]) && std::isnan(p[3])) ++unwritten;
            }
    }
    fr.unwritten = unwritten;
    fr.stats = computeStats(outRGBA);
    return true;
}

} // namespace ofxhost
