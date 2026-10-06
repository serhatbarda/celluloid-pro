// Celluloid Pro - the OFX plug-in: parameters, presets, license UI, LUT export and render
// orchestration (FrameSettings -> core::Pipeline -> engine::Plan -> Backend).
//
// ---------------------------------------------------------------------------------------------
// Threading model
//   * Render thread safety = eRenderInstanceSafe, host frame threading OFF. The host may render
//     different instances concurrently, never one instance on two threads at once.
//   * Each instance owns one core::Pipeline (+ the LookParams it was prepared with) guarded by a
//     mutex. prepare() runs only when the LookParams actually changed; the expensive spectral
//     bakes are cached inside core, so slider drags of cheap params stay interactive.
//   * CPU frames: the CPU backend already spreads one frame over all hardware threads
//     (cel::parallelRows). To avoid oversubscription (N host threads x M internal threads) and to
//     protect the backend's shared buffer pool, CPU frames of ALL instances are serialised on one
//     process-wide mutex. One frame at a time on all cores has the best latency and throughput.
//   * GPU frames: the backend only enqueues work on the host's queue/stream (no blocking), under
//     a per-API mutex because the backend objects (program/pipeline caches, buffer pools) are
//     process-wide singletons.
//
// GPU strategy (SPEC §2/§7)
//   * describe(): OpenCL (buffers), CUDA (+ streams) and Metal render support are declared for
//     each backend that is compiled into this binary AND passes the GPU module's quick load-time
//     self test (runtime library loads, >= 1 suitable device: gpu::selfTest(api, false)).
//   * render(): the API is taken from the render arguments (Metal > CUDA > OpenCL enabled flag),
//     the host's queue/stream is used, nothing blocks the host thread beyond the enqueue.
//   * A failing GPU frame is first re-rendered on the CPU while the images stay on the device
//     (gpu::cpuFallback: download src -> CPU backend -> upload dst on the host's queue/stream).
//     Only if that also fails do we return kOfxStatGPURenderFailed / kOfxStatGPUOutOfMemory,
//     which tells the host to retry the frame with GPU rendering disabled. After a non-memory GPU
//     failure the instance sets its own <API>RenderSupported property to "false" (instances may
//     override the descriptor per ofxGPURender.h) so the host stops offering that API to it.
//   * Environment overrides for support cases: CELLULOID_DISABLE_GPU=1|all|opencl,cuda,metal
//     (do not declare those APIs), CELLULOID_LOG=1 (diagnostics on stderr).
//   * Images: y = 0 is the first row in memory, i.e. the BOTTOM row in OFX's y-up convention,
//     identically on CPU and GPU (GPU buffers cannot be offset), so CPU/GPU renders are
//     bit-comparable. Row stride = kOfxImagePropRowBytes / 4 (CPU rows may be negative).
// ---------------------------------------------------------------------------------------------
#include "CelluloidPlugin.h"
#include "Params.h"
#include "Platform.h"
#include "Presets.h"
#include "CelVersion.h"

#include "ofxGPURender.h"
#include "ofxsImageEffect.h"

#include "../common/Common.h"
#include "../core/Core.h"
#include "../engine/Engine.h"
#include "../license/License.h"
#include "../texture/Texture.h"
#include "../texture/TextureDesign.h"   // grainFormatAmplitude()
#if defined(CEL_HAVE_GPU_API) && CEL_HAVE_GPU_API
#  include "../gpu/GpuBackends.h"
#  define CEL_GPU_EXTRAS 1
#else
#  define CEL_GPU_EXTRAS 0
#endif

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <exception>
#include <initializer_list>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace cel { namespace ofxp {

namespace {

// ------------------------------------------------------------------------------- utilities

bool logEnabled() {
    static const bool on = !envVar("CELLULOID_LOG").empty() && envVar("CELLULOID_LOG") != "0";
    return on;
}

void logf(const std::string& msg) {
    if (logEnabled()) std::fprintf(stderr, "[Celluloid Pro] %s\n", msg.c_str());
}

std::string lowerAscii(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return s;
}

enum class Api : int { CPU = 0, OpenCL = 1, CUDA = 2, Metal = 3 };
const char* apiName(Api a) {
    switch (a) {
    case Api::OpenCL: return "OpenCL";
    case Api::CUDA: return "CUDA";
    case Api::Metal: return "Metal";
    default: return "CPU";
    }
}

// true if the GPU API is disabled by CELLULOID_DISABLE_GPU
bool gpuDisabledByEnv(Api a) {
    const std::string v = lowerAscii(envVar("CELLULOID_DISABLE_GPU"));
    if (v.empty() || v == "0" || v == "false" || v == "no") return false;
    if (v == "1" || v == "all" || v == "true" || v == "yes") return true;
    return v.find(lowerAscii(apiName(a))) != std::string::npos;
}

engine::Backend* gpuBackend(Api a) {
    switch (a) {
    case Api::OpenCL: return engine::openclBackend();
    case Api::CUDA: return engine::cudaBackend();
    case Api::Metal: return engine::metalBackend();
    default: return nullptr;
    }
}

#if CEL_GPU_EXTRAS
gpu::Api toGpuApi(Api a) {
    switch (a) {
    case Api::CUDA: return gpu::Api::CUDA;
    case Api::Metal: return gpu::Api::Metal;
    default: return gpu::Api::OpenCL;
    }
}
#endif

// Declared at describe time: compiled in, not disabled by the environment, and (when the GPU
// module provides it) the quick load-time self test found a usable runtime + device.
bool gpuAvailable(Api a) {
    if (gpuDisabledByEnv(a) || gpuBackend(a) == nullptr) return false;
#if CEL_GPU_EXTRAS
    try {
        const gpu::SelfTestResult r = gpu::selfTest(toGpuApi(a), false);
        logf(std::string(apiName(a)) + " self test: " + (r.ok ? "ok" : "not usable") + " - " + r.message);
        return r.ok;
    } catch (...) {
        return false;
    }
#else
    return true;
#endif
}

std::mutex& cpuFrameMutex() {
    static std::mutex m;
    return m;
}
std::mutex& gpuMutex(Api a) {
    static std::mutex m[4];
    return m[int(a) & 3];
}

const std::vector<core::NegativeStockInfo>& negStocks() {
    static const std::vector<core::NegativeStockInfo> s = core::negativeStocks();
    return s;
}

const core::NegativeStockInfo* stockInfo(int idx) {
    const auto& s = negStocks();
    if (s.empty()) return nullptr;
    return &s[size_t(std::min(std::max(idx, 0), int(s.size()) - 1))];
}

bool sameLook(const core::LookParams& a, const core::LookParams& b) {
    return a.input == b.input && a.output == b.output && a.exposure == b.exposure && a.negStock == b.negStock &&
           a.pushPull == b.pushPull && a.interlayer == b.interlayer && a.negBleach == b.negBleach &&
           a.neutralize == b.neutralize && a.process == b.process && a.printStock == b.printStock &&
           a.printerR == b.printerR && a.printerG == b.printerG && a.printerB == b.printerB &&
           a.printExposure == b.printExposure && a.printBleach == b.printBleach && a.hdrPeakNits == b.hdrPeakNits &&
           a.hdrUnroll == b.hdrUnroll && a.grainAmount == b.grainAmount;
}

std::string versionString() {
    std::string s = std::string(CEL_PRODUCT_NAME) + " " + CEL_VERSION_STRING;
    std::string build = CEL_BUILD_ID;
    if (!build.empty()) s += " (build " + build + ", " + CEL_BUILD_DATE + ")";
    return s;
}

std::string licenseStatusText(const lic::LicenseInfo& li) {
    if (!li.message.empty()) return li.message;   // the license module's wording ("Licensed to ...", "Trial ...")
    return lic::needsWatermark(li) ? "Trial - renders carry a watermark" : "Licensed";
}

[[noreturn]] void fail(OfxStatus st) { throw OFX::Exception::Suite(st); }

// Copies a block of rows between two images with possibly different layouts.
// comps = 3 or 4 floats per pixel; missing alpha becomes 1.
void copyPixels(const float* srcBase, std::ptrdiff_t srcStrideFloats, int srcComps, float* dstBase, std::ptrdiff_t dstStrideFloats,
                int dstComps, int w, int h) {
    cel::parallelRows(h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            const float* s = srcBase + std::ptrdiff_t(y) * srcStrideFloats;
            float* d = dstBase + std::ptrdiff_t(y) * dstStrideFloats;
            if (srcComps == dstComps) {
                std::memcpy(d, s, size_t(w) * size_t(srcComps) * sizeof(float));
            } else if (srcComps == 3) {   // RGB -> RGBA
                for (int x = 0; x < w; ++x) {
                    d[4 * x + 0] = s[3 * x + 0];
                    d[4 * x + 1] = s[3 * x + 1];
                    d[4 * x + 2] = s[3 * x + 2];
                    d[4 * x + 3] = 1.f;
                }
            } else {                       // RGBA -> RGB
                for (int x = 0; x < w; ++x) {
                    d[3 * x + 0] = s[4 * x + 0];
                    d[3 * x + 1] = s[4 * x + 1];
                    d[3 * x + 2] = s[4 * x + 2];
                }
            }
        }
    });
}

int componentCount(OFX::PixelComponentEnum c) {
    switch (c) {
    case OFX::ePixelComponentRGBA: return 4;
    case OFX::ePixelComponentRGB: return 3;
    default: return 0;
    }
}

} // namespace

// =============================================================================== the effect

class CelluloidEffect : public OFX::ImageEffect {
public:
    explicit CelluloidEffect(OfxImageEffectHandle handle);

    void render(const OFX::RenderArguments& args) override;
    bool isIdentity(const OFX::IsIdentityArguments& args, OFX::Clip*& identityClip, double& identityTime) override;
    void changedParam(const OFX::InstanceChangedArgs& args, const std::string& paramName) override;
    void getClipPreferences(OFX::ClipPreferencesSetter& prefs) override;

private:
    // ---- parameter access (by registry id) ----
    double num(const std::string& pid, double t) const;
    double num(const std::string& pid) const;
    int choice(const std::string& pid, double t) const { return int(std::lround(num(pid, t))); }
    bool flag(const std::string& pid, double t) const { return num(pid, t) != 0.0; }
    std::string str(const std::string& pid) const;
    void setNum(const std::string& pid, double v);
    void setStr(const std::string& pid, const std::string& v);   // only writes when different
    void setEnabled(const std::string& pid, bool on);

    // ---- settings ----
    core::LookParams lookAt(double t) const;
    engine::FrameSettings frameSettingsAt(double t, int fullWidthPx) const;

    // ---- UI logic ----
    void applyPreset(int choiceIdx);
    void refreshPresetStatus(double t);
    void refreshEnabledness(double t);
    void refreshLicenseStatus();
    void exportLut(double t);
    void activateLicense();
    void deactivateLicense();

    // ---- render paths ----
    void renderCpu(const OFX::RenderArguments& args, OFX::Image* src, OFX::Image& dst, const engine::Plan& plan);
    void renderGpu(Api api, const OFX::RenderArguments& args, OFX::Image* src, OFX::Image& dst,
                   const engine::Plan& plan);
    void disableGpuForInstance(Api api);
    void setRenderError(const std::string& msg);
    void clearRenderError();

    OFX::Clip* m_dstClip = nullptr;
    OFX::Clip* m_srcClip = nullptr;

    std::map<std::string, OFX::DoubleParam*> m_double;
    std::map<std::string, OFX::IntParam*> m_int;
    std::map<std::string, OFX::BooleanParam*> m_bool;
    std::map<std::string, OFX::ChoiceParam*> m_choice;
    std::map<std::string, OFX::StringParam*> m_string;

    // pipeline cache (one per instance)
    std::mutex m_pipeMutex;
    core::Pipeline m_pipe;
    core::LookParams m_pipeLook;
    bool m_pipePrepared = false;

    // staging buffers for RGB images / mismatched bounds (CPU only), reused across frames
    std::vector<float> m_stageSrc, m_stageDst;

    std::atomic<bool> m_hasRenderError{false};
    std::atomic<bool> m_gpuWarned{false};
    bool m_applyingPreset = false;
};

CelluloidEffect::CelluloidEffect(OfxImageEffectHandle handle) : OFX::ImageEffect(handle) {
    m_dstClip = fetchClip(kOfxImageEffectOutputClipName);
    m_srcClip = fetchClip(kOfxImageEffectSimpleSourceClipName);

    for (const ParamDef& d : paramDefs()) {
        switch (d.type) {
        case PType::Double: m_double[d.id] = fetchDoubleParam(d.id); break;
        case PType::Int: m_int[d.id] = fetchIntParam(d.id); break;
        case PType::Bool: m_bool[d.id] = fetchBooleanParam(d.id); break;
        case PType::Choice: m_choice[d.id] = fetchChoiceParam(d.id); break;
        case PType::String:
        case PType::Label: m_string[d.id] = fetchStringParam(d.id); break;
        default: break;   // groups, push buttons: nothing to read
        }
    }

    // Derived, non-persistent labels (setting params is allowed in kOfxActionCreateInstance).
    try {
        setStr(id::aboutVersion, versionString());
        refreshLicenseStatus();
        refreshPresetStatus(0.0);
        refreshEnabledness(0.0);
    } catch (const std::exception& e) {
        logf(std::string("instance setup: ") + e.what());
    }
}

// ------------------------------------------------------------------------- param helpers

double CelluloidEffect::num(const std::string& pid, double t) const {
    if (auto it = m_double.find(pid); it != m_double.end()) {
        double v = 0;
        it->second->getValueAtTime(t, v);
        return v;
    }
    if (auto it = m_int.find(pid); it != m_int.end()) {
        int v = 0;
        it->second->getValueAtTime(t, v);
        return v;
    }
    if (auto it = m_bool.find(pid); it != m_bool.end()) {
        bool v = false;
        it->second->getValueAtTime(t, v);
        return v ? 1.0 : 0.0;
    }
    if (auto it = m_choice.find(pid); it != m_choice.end()) {
        int v = 0;
        it->second->getValueAtTime(t, v);
        return v;
    }
    const ParamDef* d = findParam(pid);
    return d ? d->def : 0.0;
}

double CelluloidEffect::num(const std::string& pid) const {
    if (auto it = m_double.find(pid); it != m_double.end()) return it->second->getValue();
    if (auto it = m_int.find(pid); it != m_int.end()) return it->second->getValue();
    if (auto it = m_bool.find(pid); it != m_bool.end()) return it->second->getValue() ? 1.0 : 0.0;
    if (auto it = m_choice.find(pid); it != m_choice.end()) {
        int v = 0;
        it->second->getValue(v);
        return v;
    }
    const ParamDef* d = findParam(pid);
    return d ? d->def : 0.0;
}

std::string CelluloidEffect::str(const std::string& pid) const {
    auto it = m_string.find(pid);
    if (it == m_string.end()) return std::string();
    std::string v;
    it->second->getValue(v);
    return v;
}

void CelluloidEffect::setNum(const std::string& pid, double v) {
    if (auto it = m_double.find(pid); it != m_double.end()) { it->second->setValue(v); return; }
    if (auto it = m_int.find(pid); it != m_int.end()) { it->second->setValue(int(std::lround(v))); return; }
    if (auto it = m_bool.find(pid); it != m_bool.end()) { it->second->setValue(v != 0.0); return; }
    if (auto it = m_choice.find(pid); it != m_choice.end()) { it->second->setValue(int(std::lround(v))); return; }
}

void CelluloidEffect::setStr(const std::string& pid, const std::string& v) {
    auto it = m_string.find(pid);
    if (it == m_string.end()) return;
    std::string cur;
    it->second->getValue(cur);
    if (cur != v) it->second->setValue(v);
}

void CelluloidEffect::setEnabled(const std::string& pid, bool on) {
    OFX::Param* p = nullptr;
    if (auto it = m_double.find(pid); it != m_double.end()) p = it->second;
    else if (auto it2 = m_int.find(pid); it2 != m_int.end()) p = it2->second;
    else if (auto it3 = m_bool.find(pid); it3 != m_bool.end()) p = it3->second;
    else if (auto it4 = m_choice.find(pid); it4 != m_choice.end()) p = it4->second;
    if (p && p->getIsEnable() != on) p->setEnabled(on);
}

// ------------------------------------------------------------------------- settings

core::LookParams CelluloidEffect::lookAt(double t) const {
    core::LookParams lp;
    auto clampIdx = [](int v, int count) { return std::min(std::max(v, 0), std::max(count - 1, 0)); };
    lp.input = core::InputSpace(clampIdx(choice(id::inputSpace, t), int(core::InputSpace::Count)));
    lp.output = core::OutputSpace(clampIdx(choice(id::outputSpace, t), int(core::OutputSpace::Count)));
    lp.exposure = float(num(id::exposure, t));
    lp.negStock = clampIdx(choice(id::negStock, t), int(negStocks().size()));
    lp.pushPull = float(num(id::pushPull, t));
    lp.interlayer = float(num(id::interlayer, t));
    lp.negBleach = float(num(id::negBleach, t));
    lp.neutralize = float(num(id::neutralize, t));
    lp.process = core::Process(clampIdx(choice(id::process, t), int(core::Process::Count)));
    lp.printStock = clampIdx(choice(id::printStock, t), int(core::printStockNames().size()));
    lp.printerR = float(num(id::printerR, t));
    lp.printerG = float(num(id::printerG, t));
    lp.printerB = float(num(id::printerB, t));
    lp.printExposure = float(num(id::printExposure, t));
    lp.printBleach = float(num(id::printBleach, t));
    lp.hdrPeakNits = float(num(id::hdrPeakNits, t));
    lp.hdrUnroll = float(num(id::hdrUnroll, t));
    lp.grainAmount = flag(id::grainEnable, t) ? float(num(id::grainAmount, t)) : 0.f;
    return lp;
}

engine::FrameSettings CelluloidEffect::frameSettingsAt(double t, int fullWidthPx) const {
    engine::FrameSettings fs;
    fs.look = lookAt(t);
    fs.mode = core::Mode(std::min(std::max(choice(id::mode, t), 0), int(core::Mode::Count) - 1));

    const core::NegativeStockInfo* st = stockInfo(fs.look.negStock);
    const float halScale = st ? st->halationScale : 1.f;
    const float grainScale = st ? st->grainSizeScale : 1.f;
    const float bloomScale = st ? st->bloomScale : 1.f;
    const bool mono = st ? st->monochrome : false;

    tex::TextureParams& tp = fs.tex;
    tp.time = t;
    // all texture sizes are specified at 1920 px image width; the rendered image width already
    // includes the host's render scale (proxy renders), so this is render-scale correct.
    tp.pixelScale = float(std::max(1, fullWidthPx)) / 1920.f;
    tp.weaveAmount = float(num(id::weaveAmount, t));
    tp.weaveSpeed = float(num(id::weaveSpeed, t));
    tp.acutanceAmount = float(num(id::acutanceAmount, t));
    tp.acutanceRadius = float(num(id::acutanceRadius, t));
    tp.halationAmount = float(num(id::halationAmount, t)) * halScale;
    tp.halationRadius = float(num(id::halationRadius, t));
    tp.halationThreshold = float(num(id::halationThreshold, t));
    tp.halationHue = float(num(id::halationHue, t));
    tp.bloomAmount = float(num(id::bloomAmount, t)) * bloomScale;
    tp.bloomRadius = float(num(id::bloomRadius, t));
    tp.vignette = float(num(id::vignette, t));
    tp.grainEnabled = flag(id::grainEnable, t) && fs.look.grainAmount > 0.f;
    tp.grainFormat = tex::GrainFormat(std::min(std::max(choice(id::grainFormat, t), 0), int(tex::GrainFormat::Count) - 1));
    tp.grainSize = float(num(id::grainSize, t)) * grainScale;
    tp.grainSoftness = float(num(id::grainSoftness, t));
    tp.grainAnimated = flag(id::grainAnimated, t);
    tp.grainSeed = int(std::lround(num(id::grainSeed, t)));
    tp.grainMonochrome = mono;
    // The grain field is unit variance per pixel for every format (cel::tex); visible granularity
    // grows with the enlargement of the gate, so the density amplitude carries the per-format
    // factor (Super 8 ~2x ... 65 mm ~0.7x of Super 35). Applied here, exactly once.
    if (tp.grainEnabled) fs.look.grainAmount *= tex::grainFormatAmplitude(tp.grainFormat);

    if (fs.mode == core::Mode::ColorOnly) {   // colour pipeline only: exactly what a LUT contains
        tp.grainEnabled = false;
        fs.look.grainAmount = 0.f;
    }
    fs.watermark = lic::needsWatermark(lic::current());
    return fs;
}

// ------------------------------------------------------------------------- render

bool CelluloidEffect::isIdentity(const OFX::IsIdentityArguments&, OFX::Clip*&, double&) { return false; }

void CelluloidEffect::getClipPreferences(OFX::ClipPreferencesSetter& prefs) {
    // Grain/weave make the output change every frame even for a still source.
    const bool texture = choice(id::mode, 0.0) != int(core::Mode::ColorOnly);
    const bool animatedGrain = flag(id::grainEnable, 0.0) && flag(id::grainAnimated, 0.0) && num(id::grainAmount) > 0.0;
    const bool weave = num(id::weaveAmount) > 0.0;
    prefs.setOutputFrameVarying(texture && (animatedGrain || weave));
}

void CelluloidEffect::render(const OFX::RenderArguments& args) {
    if (!m_dstClip) fail(kOfxStatFailed);
    if (m_dstClip->getPixelDepth() != OFX::eBitDepthFloat) fail(kOfxStatErrUnsupported);
    if (componentCount(m_dstClip->getPixelComponents()) == 0) fail(kOfxStatErrUnsupported);

    Api api = Api::CPU;
    if (args.isEnabledMetalRender) api = Api::Metal;
    else if (args.isEnabledCudaRender) api = Api::CUDA;
    else if (args.isEnabledOpenCLRender) api = Api::OpenCL;

    std::unique_ptr<OFX::Image> dst(m_dstClip->fetchImage(args.time));
    if (!dst) fail(kOfxStatFailed);
    if (dst->getPixelDepth() != OFX::eBitDepthFloat || componentCount(dst->getPixelComponents()) == 0)
        fail(kOfxStatErrImageFormat);

    std::unique_ptr<OFX::Image> src;
    if (m_srcClip && m_srcClip->isConnected()) src.reset(m_srcClip->fetchImage(args.time));
    if (src && (src->getPixelDepth() != OFX::eBitDepthFloat || componentCount(src->getPixelComponents()) == 0))
        fail(kOfxStatErrImageFormat);

    const OfxRectI db = dst->getBounds();
    const int w = db.x2 - db.x1, h = db.y2 - db.y1;
    if (w <= 0 || h <= 0) return;

    engine::FrameSettings fs = frameSettingsAt(args.time, w);
    if (abort()) return;

    std::lock_guard<std::mutex> lock(m_pipeMutex);
    try {
        if (!m_pipePrepared || !sameLook(m_pipeLook, fs.look)) {
            m_pipe.prepare(fs.look);
            m_pipeLook = fs.look;
            m_pipePrepared = true;
        }
    } catch (const std::exception& e) {
        m_pipePrepared = false;
        setRenderError(std::string("Colour pipeline setup failed: ") + e.what());
        fail(kOfxStatFailed);
    }

    engine::Plan plan;
    try {
        plan = engine::buildPlan(w, h, fs, m_pipe);
    } catch (const std::exception& e) {
        setRenderError(std::string("Frame plan failed: ") + e.what());
        fail(kOfxStatFailed);
    }

    if (api == Api::CPU) renderCpu(args, src.get(), *dst, plan);
    else renderGpu(api, args, src.get(), *dst, plan);
}

void CelluloidEffect::renderCpu(const OFX::RenderArguments&, OFX::Image* src, OFX::Image& dst,
                                const engine::Plan& plan) {
    const OfxRectI db = dst.getBounds();
    const int w = db.x2 - db.x1, h = db.y2 - db.y1;
    const int dstComps = componentCount(dst.getPixelComponents());
    const int dstRowBytes = dst.getRowBytes();
    float* dstData = static_cast<float*>(dst.getPixelData());
    if (!dstData) fail(kOfxStatFailed);

    // No source (unconnected General-context input): transparent black.
    if (!src || !src->getPixelData()) {
        for (int y = 0; y < h; ++y) {
            float* row = reinterpret_cast<float*>(reinterpret_cast<char*>(dstData) + std::ptrdiff_t(y) * dstRowBytes);
            std::memset(row, 0, size_t(w) * size_t(dstComps) * sizeof(float));
        }
        return;
    }

    // ---- source: use the host buffer directly when it already matches, otherwise stage ----
    engine::HostImage hs;
    const OfxRectI sb = src->getBounds();
    const int srcComps = componentCount(src->getPixelComponents());
    const int srcRowBytes = src->getRowBytes();
    const bool srcDirect = srcComps == 4 && sb.x1 == db.x1 && sb.y1 == db.y1 && sb.x2 == db.x2 && sb.y2 == db.y2 &&
                           srcRowBytes % int(sizeof(float)) == 0;
    if (srcDirect) {
        hs.data = src->getPixelData();
        hs.w = w;
        hs.h = h;
        hs.rowStrideFloats = srcRowBytes / int(sizeof(float));
    } else {
        // dst-sized packed RGBA, the source placed at its own bounds, transparent black elsewhere
        m_stageSrc.assign(size_t(w) * size_t(h) * 4u, 0.f);
        const int x1 = std::max(sb.x1, db.x1), x2 = std::min(sb.x2, db.x2);
        const int y1 = std::max(sb.y1, db.y1), y2 = std::min(sb.y2, db.y2);
        if (x2 > x1 && y2 > y1) {
            const char* sbase = static_cast<const char*>(src->getPixelData());
            const float* s0 = reinterpret_cast<const float*>(sbase + std::ptrdiff_t(y1 - sb.y1) * srcRowBytes) +
                              std::ptrdiff_t(x1 - sb.x1) * srcComps;
            float* d0 = m_stageSrc.data() + (size_t(y1 - db.y1) * size_t(w) + size_t(x1 - db.x1)) * 4u;
            if (srcRowBytes % int(sizeof(float)) != 0) fail(kOfxStatErrImageFormat);
            copyPixels(s0, srcRowBytes / std::ptrdiff_t(sizeof(float)), srcComps, d0, std::ptrdiff_t(w) * 4, 4, x2 - x1, y2 - y1);
        }
        hs.data = m_stageSrc.data();
        hs.w = w;
        hs.h = h;
        hs.rowStrideFloats = w * 4;
    }

    // ---- destination ----
    engine::HostImage hd;
    const bool dstDirect = dstComps == 4 && dstRowBytes % int(sizeof(float)) == 0;
    if (dstDirect) {
        hd.data = dstData;
        hd.w = w;
        hd.h = h;
        hd.rowStrideFloats = dstRowBytes / int(sizeof(float));
    } else {
        if (dstRowBytes % int(sizeof(float)) != 0) fail(kOfxStatErrImageFormat);
        m_stageDst.assign(size_t(w) * size_t(h) * 4u, 0.f);
        hd.data = m_stageDst.data();
        hd.w = w;
        hd.h = h;
        hd.rowStrideFloats = w * 4;
    }

    std::string err;
    bool ok = false;
    {
        std::lock_guard<std::mutex> lock(cpuFrameMutex());
        try {
            ok = engine::cpuBackend().execute(plan, m_pipe.state(), m_pipe.stateVersion(), hs, hd,
                                              engine::GpuContext{}, &err);
        } catch (const std::bad_alloc&) {
            err = "out of memory";
        } catch (const std::exception& e) {
            err = e.what();
        }
    }
    if (!ok) {
        setRenderError("Render failed (CPU): " + (err.empty() ? std::string("unknown error") : err));
        fail(err == "out of memory" ? kOfxStatErrMemory : kOfxStatFailed);
    }
    if (!dstDirect) copyPixels(m_stageDst.data(), std::ptrdiff_t(w) * 4, 4, dstData, dstRowBytes / std::ptrdiff_t(sizeof(float)), dstComps, w, h);
    clearRenderError();
}

void CelluloidEffect::renderGpu(Api api, const OFX::RenderArguments& args, OFX::Image* src, OFX::Image& dst,
                                const engine::Plan& plan) {
    engine::Backend* be = gpuBackend(api);
    if (!be || gpuDisabledByEnv(api)) {
        logf(std::string(apiName(api)) + " render requested but this backend is not available");
        disableGpuForInstance(api);
        fail(kOfxStatGPURenderFailed);
    }
    // GPU buffers cannot be staged/offset here: only the direct case is supported; anything else
    // is handed back to the host for a CPU render of this frame.
    const OfxRectI db = dst.getBounds();
    const int w = db.x2 - db.x1, h = db.y2 - db.y1;
    if (!src || !src->getPixelData() || !dst.getPixelData()) fail(kOfxStatGPURenderFailed);
    const OfxRectI sb = src->getBounds();
    const bool layoutOk = componentCount(dst.getPixelComponents()) == 4 &&
                          componentCount(src->getPixelComponents()) == 4 && sb.x1 == db.x1 && sb.y1 == db.y1 &&
                          sb.x2 == db.x2 && sb.y2 == db.y2 && src->getRowBytes() > 0 && dst.getRowBytes() > 0 &&
                          src->getRowBytes() % 16 == 0 && dst.getRowBytes() % 16 == 0;
    if (!layoutOk) {
        logf(std::string(apiName(api)) + ": unsupported image layout for GPU render, host falls back to CPU");
        fail(kOfxStatGPURenderFailed);
    }

    engine::HostImage hs, hd;
    hs.data = src->getPixelData();
    hs.w = w; hs.h = h; hs.rowStrideFloats = src->getRowBytes() / int(sizeof(float));
    hd.data = dst.getPixelData();
    hd.w = w; hd.h = h; hd.rowStrideFloats = dst.getRowBytes() / int(sizeof(float));

    engine::GpuContext gpu;
    gpu.openclCommandQueue = args.pOpenCLCmdQ;
    gpu.cudaStream = args.pCudaStream;
    gpu.metalCommandQueue = args.pMetalCmdQ;
    if ((api == Api::OpenCL && !gpu.openclCommandQueue) || (api == Api::Metal && !gpu.metalCommandQueue)) {
        logf(std::string(apiName(api)) + ": host did not supply a command queue");
        fail(kOfxStatGPURenderFailed);
    }

    std::string err;
    bool ok = false;
    {
        std::lock_guard<std::mutex> lock(gpuMutex(api));
        try {
            ok = be->execute(plan, m_pipe.state(), m_pipe.stateVersion(), hs, hd, gpu, &err);
        } catch (const std::bad_alloc&) {
            err = "out of memory";
        } catch (const std::exception& e) {
            err = e.what();
        }
    }
    if (!ok) {
        const std::string le = lowerAscii(err);
        const bool oom = le.find("memory") != std::string::npos || le.find("mem_object_allocation") != std::string::npos ||
                         le.find("out_of_resources") != std::string::npos;
#if CEL_GPU_EXTRAS
        // First choice: render this frame on the CPU while the images stay on the GPU
        // (download src -> CPU backend -> upload dst, ordered on the host's queue/stream).
        std::string ferr;
        bool fallbackOk = false;
        {
            std::lock_guard<std::mutex> cpuLock(cpuFrameMutex());
            std::lock_guard<std::mutex> gpuLock(gpuMutex(api));
            try {
                fallbackOk = gpu::cpuFallback(toGpuApi(api), engine::cpuBackend(), plan, m_pipe.state(),
                                              m_pipe.stateVersion(), hs, hd, gpu, &ferr);
            } catch (const std::exception& e) {
                ferr = e.what();
            }
        }
        if (fallbackOk) {
            if (!m_gpuWarned.exchange(true))
                std::fprintf(stderr, "[Celluloid Pro] %s render failed (%s); rendered on the CPU instead.\n",
                             apiName(api), err.empty() ? "unknown error" : err.c_str());
            if (!oom) disableGpuForInstance(api);   // future frames go straight to the CPU path
            clearRenderError();
            return;
        }
        err += "; CPU fallback: " + (ferr.empty() ? std::string("failed") : ferr);
#endif
        std::fprintf(stderr, "[Celluloid Pro] %s render failed (%s); asking the host to render this frame on the CPU.\n",
                     apiName(api), err.empty() ? "unknown error" : err.c_str());
        if (!oom) disableGpuForInstance(api);
        fail(oom ? kOfxStatGPUOutOfMemory : kOfxStatGPURenderFailed);
    }
    clearRenderError();
}

void CelluloidEffect::disableGpuForInstance(Api api) {
    const char* prop = nullptr;
    switch (api) {
    case Api::OpenCL: prop = kOfxImageEffectPropOpenCLRenderSupported; break;
    case Api::CUDA: prop = kOfxImageEffectPropCudaRenderSupported; break;
    case Api::Metal: prop = kOfxImageEffectPropMetalRenderSupported; break;
    default: return;
    }
    try {
        getPropertySet().propSetString(prop, "false", false);
    } catch (...) {
        // hosts may refuse instance-level changes; the per-frame GPURenderFailed still works
    }
}

void CelluloidEffect::setRenderError(const std::string& msg) {
    std::fprintf(stderr, "[Celluloid Pro] %s\n", msg.c_str());
    try {
        setPersistentMessage(OFX::Message::eMessageError, "", "Celluloid Pro: " + msg);
        m_hasRenderError = true;
    } catch (...) {
    }
}

void CelluloidEffect::clearRenderError() {
    if (!m_hasRenderError.exchange(false)) return;
    try {
        clearPersistentMessage();
    } catch (...) {
    }
}

// ------------------------------------------------------------------------- UI logic

void CelluloidEffect::changedParam(const OFX::InstanceChangedArgs& args, const std::string& name) {
    try {
        if (name == id::lookPresetStatus || name == id::licenseStatus || name == id::aboutVersion ||
            name == id::aboutManual)
            return;   // our own read-only labels

        if (name == id::lookPreset) {
            if (args.reason == OFX::eChangeUserEdit && !m_applyingPreset) applyPreset(choice(id::lookPreset, args.time));
            refreshPresetStatus(args.time);
            refreshEnabledness(args.time);
            return;
        }
        if (name == id::lutExport) { exportLut(args.time); return; }
        if (name == id::licenseActivate) { activateLicense(); return; }
        if (name == id::licenseRemove) { deactivateLicense(); return; }

        const ParamDef* d = findParam(name);
        if (d && d->look && !m_applyingPreset) refreshPresetStatus(args.time);
        if (name == id::negStock || name == id::process || name == id::mode || name == id::outputSpace ||
            name == id::grainEnable)
            refreshEnabledness(args.time);
    } catch (const OFX::Exception::Suite&) {
        throw;
    } catch (const std::exception& e) {
        sendMessage(OFX::Message::eMessageError, "", std::string("Celluloid Pro: ") + e.what());
    }
}

void CelluloidEffect::applyPreset(int choiceIdx) {
    if (choiceIdx <= 0) return;   // "Custom": keep everything
    std::vector<std::string> problems;
    const std::vector<ResolvedValue> values = resolvePreset(size_t(choiceIdx - 1), &problems);
    for (const std::string& p : problems) logf("preset: " + p);
    if (values.empty()) return;
    m_applyingPreset = true;
    try {
        beginEditBlock("Apply Look Preset");
        for (const ResolvedValue& rv : values) setNum(rv.param, rv.value);
        endEditBlock();
    } catch (...) {
        m_applyingPreset = false;
        throw;
    }
    m_applyingPreset = false;
}

void CelluloidEffect::refreshPresetStatus(double t) {
    const int idx = choice(id::lookPreset, t);
    const auto& presets = lookPresets();
    if (idx <= 0 || size_t(idx - 1) >= presets.size()) {
        setStr(id::lookPresetStatus, "Custom");
        return;
    }
    bool modified = false;
    for (const ResolvedValue& rv : resolvePreset(size_t(idx - 1))) {
        const double cur = num(rv.param, t);
        if (std::fabs(cur - rv.value) > 1e-4 * std::max(1.0, std::fabs(rv.value))) {
            modified = true;
            break;
        }
    }
    const std::string& name = presets[size_t(idx - 1)].name;
    setStr(id::lookPresetStatus, modified ? name + " (modified)" : name);
}

void CelluloidEffect::refreshEnabledness(double t) {
    const core::Mode mode = core::Mode(choice(id::mode, t));
    const core::NegativeStockInfo* st = stockInfo(choice(id::negStock, t));
    const bool reversal = st && st->reversal;
    const bool mono = st && st->monochrome;
    const bool scan = core::Process(choice(id::process, t)) == core::Process::NegativeScan;
    const bool colour = mode != core::Mode::TextureOnly;
    const bool texture = mode != core::Mode::ColorOnly;
    const core::OutputSpace out = core::OutputSpace(choice(id::outputSpace, t));
    const bool pq = out == core::OutputSpace::Rec2100_PQ;
    const bool hdr = pq || out == core::OutputSpace::Rec2100_HLG;
    const bool grainOn = flag(id::grainEnable, t);

    // colour (negative/print)
    setEnabled(id::inputSpace, colour);
    setEnabled(id::exposure, colour);
    setEnabled(id::pushPull, colour);
    setEnabled(id::interlayer, colour && !mono);
    setEnabled(id::negBleach, colour);
    setEnabled(id::neutralize, colour && !mono);
    setEnabled(id::process, colour && !reversal);
    setEnabled(id::printStock, colour && !reversal && !scan);
    setEnabled(id::printerR, colour && !reversal && !mono);
    setEnabled(id::printerG, colour && !reversal && !mono);
    setEnabled(id::printerB, colour && !reversal && !mono);
    setEnabled(id::printExposure, colour && !reversal);
    setEnabled(id::printBleach, colour && !reversal && !scan);
    setEnabled(id::hdrPeakNits, pq);
    setEnabled(id::hdrUnroll, colour && hdr);
    // texture
    for (const char* p : {id::halationAmount, id::halationRadius, id::halationThreshold, id::halationHue,
                          id::bloomAmount, id::bloomRadius, id::grainEnable, id::acutanceAmount, id::acutanceRadius,
                          id::weaveAmount, id::weaveSpeed, id::vignette})
        setEnabled(p, texture);
    for (const char* p : {id::grainFormat, id::grainAmount, id::grainSize, id::grainSoftness, id::grainAnimated,
                          id::grainSeed})
        setEnabled(p, texture && grainOn);
}

void CelluloidEffect::refreshLicenseStatus() { setStr(id::licenseStatus, licenseStatusText(lic::current())); }

void CelluloidEffect::activateLicense() {
    std::string key = normalizeUserPath(str(id::licenseKey));   // trims white space / quotes
    if (key.empty()) {
        sendMessage(OFX::Message::eMessageError, "", "Please paste your Celluloid Pro license key into the License Key field first.");
        return;
    }
    const lic::LicenseInfo info = lic::verifyKey(key, int64_t(std::time(nullptr)));
    if (!info.valid) {
        sendMessage(OFX::Message::eMessageError, "",
                    "This license key could not be activated. " +
                        (info.message.empty()
                             ? std::string("Please check that you pasted the complete key (it starts with CELL1-).")
                             : info.message));
        return;
    }
    std::string err;
    if (!lic::saveKey(key, &err)) {
        sendMessage(OFX::Message::eMessageError, "", "The key is valid, but it could not be stored: " + err);
        return;
    }
    setStr(id::licenseKey, "");
    refreshLicenseStatus();
    const lic::LicenseInfo cur = lic::current();
    sendMessage(OFX::Message::eMessageMessage, "",
                "Celluloid Pro is activated. " + (cur.message.empty() ? info.message : cur.message) +
                    "\nThank you for choosing Celluloid Pro.");
}

void CelluloidEffect::deactivateLicense() {
    std::string err;
    if (!lic::removeKey(&err)) {
        sendMessage(OFX::Message::eMessageError, "", "The license could not be removed: " + err);
        return;
    }
    refreshLicenseStatus();
    // removeKey() succeeds but fills err with a note when another license (machine-wide file or
    // CELLULOID_LICENSE_KEY) still applies after the per-user key is gone: show that note.
    const lic::LicenseInfo cur = lic::current();
    if (!lic::needsWatermark(cur)) {
        sendMessage(OFX::Message::eMessageMessage, "",
                    err.empty() ? "Your license key was removed, but another license on this computer still applies: " +
                                      cur.message
                                : err);
        return;
    }
    std::string msg = "The license was removed from this computer. Celluloid Pro now runs as a trial (watermarked renders).";
    if (!err.empty()) msg += "\n" + err;
    sendMessage(OFX::Message::eMessageMessage, "", msg);
}

void CelluloidEffect::exportLut(double t) {
    const int sizeIdx = std::min(std::max(choice(id::lutSize, t), 0), 2);
    const int size = kLutSizes[sizeIdx];

    core::LookParams lp = lookAt(t);
    lp.grainAmount = 0.f;   // a LUT is the Color Only pipeline

    std::string dir = normalizeUserPath(str(id::lutPath));
    if (dir.empty()) dir = defaultLutDirectory();
    std::string err;
    if (!ensureDirectory(dir, &err)) {
        sendMessage(OFX::Message::eMessageError, "", "LUT export failed: " + err);
        return;
    }

    const core::NegativeStockInfo* st = stockInfo(lp.negStock);
    const std::vector<std::string> prints = core::printStockNames();
    const std::vector<std::string> outs = core::outputSpaceNames();
    const std::vector<std::string> ins = core::inputSpaceNames();
    const std::string negName = st ? st->name : "Negative";
    std::string printName;
    if (st && st->reversal) printName = "Reversal";
    else if (lp.process == core::Process::NegativeScan) printName = "Lab Scan";
    else printName = size_t(lp.printStock) < prints.size() ? prints[size_t(lp.printStock)] : "Print";
    const std::string outName = size_t(lp.output) < outs.size() ? outs[size_t(lp.output)] : "Output";
    const std::string inName = size_t(lp.input) < ins.size() ? ins[size_t(lp.input)] : "Input";

    const std::string file = "Celluloid_" + fileComponent(negName) + "_" + fileComponent(printName) + "_" +
                             fileComponent(outName) + ".cube";
    const std::string path = joinPath(dir, file);
    const std::string title = std::string("Celluloid Pro ") + CEL_VERSION_STRING + " - " + negName + " / " + printName +
                              " - " + inName + " to " + outName;
    try {
        core::Pipeline pipe;
        pipe.prepare(lp);
        if (!core::writeCubeLUT(pipe, size, path, title, &err)) {
            sendMessage(OFX::Message::eMessageError, "", "LUT export failed: " + (err.empty() ? path : err));
            return;
        }
    } catch (const std::exception& e) {
        sendMessage(OFX::Message::eMessageError, "", std::string("LUT export failed: ") + e.what());
        return;
    }
    std::string note;
    if (choice(id::mode, t) == int(core::Mode::TextureOnly))
        note = "\nNote: Texture Only mode is active in the node; the LUT contains the colour pipeline of the current "
               "settings (as in Color Only mode).";
    sendMessage(OFX::Message::eMessageMessage, "",
                "LUT exported (" + std::to_string(size) + "x" + std::to_string(size) + "x" + std::to_string(size) +
                    "):\n" + path + "\nInput: " + inName + "  -  Output: " + outName +
                    "\nGrain, halation and other texture effects cannot be stored in a LUT." + note);
}

// =============================================================================== factory

CelluloidFactory::CelluloidFactory()
    : OFX::PluginFactoryHelper<CelluloidFactory>(CEL_PLUGIN_IDENTIFIER, CEL_VERSION_MAJOR, CEL_VERSION_MINOR) {}

void CelluloidFactory::load() {
    // Validate the data-driven preset table once (diagnostics only; invalid entries are skipped).
    std::vector<std::string> problems;
    if (!validatePresets(&problems))
        for (const std::string& p : problems) logf("preset table: " + p);
}

void CelluloidFactory::unload() {
    try {
        cel::shutdownParallelPool();   // join the CPU worker threads before the module goes away
    } catch (...) {
    }
#if CEL_GPU_EXTRAS
    try {
        gpu::releaseAllResources();   // programs, pipelines, pooled buffers of the GPU backends
    } catch (...) {
    }
#endif
}

void CelluloidFactory::describe(OFX::ImageEffectDescriptor& desc) {
    desc.setLabels(CEL_PRODUCT_NAME, CEL_PRODUCT_NAME, CEL_PRODUCT_NAME);
    desc.setPluginGrouping("Celluloid");
    desc.setPluginDescription(
        "Photochemical film emulation: spectral negative and print stocks, push/pull and bleach bypass "
        "development, printer lights, halation, bloom, film grain, acutance, gate weave and vignetting, "
        "with SDR and HDR outputs and LUT export. " + versionString());

    desc.addSupportedContext(OFX::eContextFilter);
    desc.addSupportedContext(OFX::eContextGeneral);
    desc.addSupportedBitDepth(OFX::eBitDepthFloat);

    desc.setSingleInstance(false);
    desc.setHostFrameThreading(false);
    desc.setRenderThreadSafety(OFX::eRenderInstanceSafe);
    desc.setSupportsTiles(false);
    desc.setSupportsMultiResolution(false);
    desc.setSupportsMultipleClipPARs(false);
    desc.setSupportsMultipleClipDepths(false);
    desc.setTemporalClipAccess(false);
    desc.setRenderTwiceAlways(false);

    // GPU render support (see the strategy at the top of this file).
    const bool cl = gpuAvailable(Api::OpenCL);
    const bool cuda = gpuAvailable(Api::CUDA);
    const bool metal = gpuAvailable(Api::Metal);
    desc.setSupportsOpenCLBuffersRender(cl);
    desc.setSupportsOpenCLImagesRender(false);   // we work on cl_mem buffers only
    desc.setSupportsCudaRender(cuda);
    desc.setSupportsCudaStream(cuda);
    desc.setSupportsMetalRender(metal);
    logf(std::string("GPU support declared: OpenCL=") + (cl ? "yes" : "no") + " CUDA=" + (cuda ? "yes" : "no") +
         " Metal=" + (metal ? "yes" : "no"));
}

void CelluloidFactory::describeInContext(OFX::ImageEffectDescriptor& desc, OFX::ContextEnum) {
    OFX::ClipDescriptor* src = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
    src->addSupportedComponent(OFX::ePixelComponentRGBA);
    src->addSupportedComponent(OFX::ePixelComponentRGB);
    src->setTemporalClipAccess(false);
    src->setSupportsTiles(false);
    src->setIsMask(false);

    OFX::ClipDescriptor* dst = desc.defineClip(kOfxImageEffectOutputClipName);
    dst->addSupportedComponent(OFX::ePixelComponentRGBA);
    dst->addSupportedComponent(OFX::ePixelComponentRGB);
    dst->setSupportsTiles(false);

    OFX::PageParamDescriptor* page = desc.definePageParam("Controls");
    std::map<std::string, OFX::GroupParamDescriptor*> groups;

    for (const ParamDef& d : paramDefs()) {
        OFX::ParamDescriptor* pd = nullptr;
        switch (d.type) {
        case PType::Group: {
            OFX::GroupParamDescriptor* g = desc.defineGroupParam(d.id);
            g->setOpen(d.groupOpen);
            groups[d.id] = g;
            pd = g;
            break;
        }
        case PType::Double: {
            OFX::DoubleParamDescriptor* p = desc.defineDoubleParam(d.id);
            p->setDoubleType(OFX::eDoubleTypePlain);
            p->setRange(d.min, d.max);
            p->setDisplayRange(d.min, d.max);
            p->setDefault(d.def);
            p->setIncrement(d.increment);
            p->setDigits(d.digits);
            p->setAnimates(d.animates);
            pd = p;
            break;
        }
        case PType::Int: {
            OFX::IntParamDescriptor* p = desc.defineIntParam(d.id);
            p->setRange(int(d.min), int(d.max));
            p->setDisplayRange(int(d.min), int(d.max));
            p->setDefault(int(d.def));
            p->setAnimates(d.animates);
            pd = p;
            break;
        }
        case PType::Bool: {
            OFX::BooleanParamDescriptor* p = desc.defineBooleanParam(d.id);
            p->setDefault(d.def != 0.0);
            p->setAnimates(d.animates);
            pd = p;
            break;
        }
        case PType::Choice: {
            OFX::ChoiceParamDescriptor* p = desc.defineChoiceParam(d.id);
            for (const std::string& o : d.options) p->appendOption(o);
            p->setDefault(int(d.def));
            p->setAnimates(d.animates);
            pd = p;
            break;
        }
        case PType::String:
        case PType::Label: {
            OFX::StringParamDescriptor* p = desc.defineStringParam(d.id);
            if (d.type == PType::Label) p->setStringType(OFX::eStringTypeLabel);
            else if (d.strKind == StrKind::Directory) {
                p->setStringType(OFX::eStringTypeDirectoryPath);
                p->setFilePathExists(false);
            } else p->setStringType(OFX::eStringTypeSingleLine);
            p->setDefault(d.id == id::aboutVersion ? versionString() : d.defString);
            p->setAnimates(false);
            pd = p;
            break;
        }
        case PType::Push: {
            pd = desc.definePushButtonParam(d.id);
            break;
        }
        }
        if (!pd) continue;
        pd->setLabels(d.label, d.label, d.label);
        pd->setScriptName(d.id);
        if (!d.hint.empty()) pd->setHint(d.hint);
        if (d.type != PType::Group) {
            if (OFX::ValueParamDescriptor* vp = dynamic_cast<OFX::ValueParamDescriptor*>(pd)) {
                vp->setEvaluateOnChange(d.evaluateOnChange);
                vp->setIsPersistant(d.persistent);
            }
        }
        if (!d.group.empty()) {
            auto it = groups.find(d.group);
            if (it != groups.end()) pd->setParent(*it->second);
        } else if (page) {
            page->addChild(*pd);
        }
    }
}

OFX::ImageEffect* CelluloidFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum) {
    return new CelluloidEffect(handle);
}

}} // namespace cel::ofxp

// =============================================================================== registration

namespace OFX { namespace Plugin {
void getPluginIDs(OFX::PluginFactoryArray& ids) {
    static cel::ofxp::CelluloidFactory factory;
    ids.push_back(&factory);
}
}} // namespace OFX::Plugin
