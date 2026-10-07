// ofxhost - headless OpenFX test host (command line driver).
//
// Run `ofxhost --help` for the full option reference.
#include "Host.h"
#include "Json.h"
#include "Png.h"

#include "ofxGPURender.h"

#include <algorithm>
#include <cfloat>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>

using namespace ofxhost;

namespace ofxhost {
bool describePlugin(PluginBinary& bin, int idx, std::string& err);
bool describeInContext(PluginBinary& bin, int idx, const std::string& context, std::string& err);
}

namespace {

const char* kUsage = R"(ofxhost - headless OpenFX image effect host (test tool)

usage: ofxhost <plugin.ofx.bundle | plugin.ofx> [options]

plug-in selection
  --plugin ID|INDEX         plug-in to use from the binary (default 0)
  --context filter|general  context to instantiate (default filter)
  --list-plugins            list the plug-ins in the binary and exit

inspection
  --list-params             print all parameters (type, default, range, options, group, hint)
  --list-params-json FILE   write the plug-in description + parameters as JSON ("-" = stdout)
  --list-cl-devices         list OpenCL platforms/devices and exit

parameters (applied in command line order, each wrapped in Begin/InstanceChanged/End
with kOfxChangeUserEdited, like a user edit in a host UI)
  --set NAME=VALUE          double/int: number (multi-dim: a,b,c); bool: true/false/1/0/on/off;
                            choice: index or option label (forms index:N / label:TEXT force one);
                            strchoice: enum or label; string: verbatim
  --set NAME@T=VALUE        set a keyframe at time T (linear interpolation for numbers)
  --press NAME              press a push button (instance changed at the job time)
  --reset-params            reset every parameter to its default

input / render
  --in FILE.f32             raw RGBA float32, top-down rows, no header (needs --width/--height)
  --in 'seq.%04d.f32'       per-frame raw input (printf pattern on the frame number)
  --in pattern:NAME[:args]  ramp, vramp, gradient, bars[:level], grey[:v], white, black,
                            checker[:n], noise[:seed], zone[:mid]
  --width W --height H      project / input size in pixels (canonical, render scale 1)
  --time T                  render one frame at time T (default 0)
  --frames A:B[:STEP]       render frames A..B inclusive (BeginSequenceRender spans all)
  --render-scale S          render scale (images are S*W x S*H; source is area-downsampled)
  --gpu none|opencl|opencl-images|metal
                            OpenCL render like Resolve: host context + in-order queue,
                            cl_mem buffers in kOfxImagePropData (opencl) or cl_mem images in
                            kOfxImageEffectPropOpenCLImage (opencl-images); metal (macOS): host
                            MTLCommandQueue + MTLBuffers in kOfxImagePropData
  --gpu-fallback            render on CPU if the plug-in lacks / fails the GPU path
  --cl-platform N --cl-device N   pick the OpenCL device (default: first GPU, else first)
  --repeat N                render each frame N times (timing statistics)
  --render                  force a render in batch lines without --out
  --draft                   set kOfxImageEffectPropRenderQualityDraft on the render action
  --out PREFIX              write PREFIX.f32 (+ PREFIX.png); multi-frame: PREFIX.NNNN.*
  --no-f32 / --no-png / --png16 / --png-alpha

host behaviour
  --threads N               CPUs reported by the multithread suite (default: all)
  --answer yes|no           reply to kOfxMessageQuestion (default yes)
  --plugin-edit-notify sync|deferred|off
                            instanceChanged(kOfxChangePluginEdited) for values the plug-in
                            sets itself (default sync = re-entrant, like Resolve/Nuke)
  --host-name NAME          kOfxPropName of the host (default de.serhatbarda.ofxhost)
  --depths LIST             host pixel depths, deepest first (default float,half,short,byte)
  --interactive             report an interactive (viewer) render
  --fps F                   frame rate (default 24)
  --no-sentinel             do not pre-fill outputs with NaN (unwritten pixel detection)

automation
  --batch FILE              run each non-empty line of FILE as a job on the SAME instance
                            (job options: --set --press --reset-params --in --width --height
                            --time --frames --render-scale --gpu --gpu-fallback --repeat
                            --out --render --draft --no-f32 --no-png --png16 --png-alpha --label
                            --list-params-json); '#' starts a comment
  --label TEXT              name of the job in the report
  --report FILE.json        machine readable report (timings, stats, messages, warnings)
  --keep-going              in batch mode, continue with the next job after a failed one
                            (the exit code still reports the first failure)
  --strict                  exit 4 on host-detected protocol violations or unwritten pixels
  -v / --verbose, --trace (every action and suite fetch), --trace-props (unknown props)

exit codes: 0 ok, 1 action/render failure, 2 usage error, 3 load/describe failure, 4 strict
)";

struct Op {
    enum Kind { Set, Press, Reset } kind = Set;
    std::string name, value;
    bool hasTime = false;
    double time = 0;
};

struct Job {
    std::string label;
    std::vector<Op> ops;
    bool haveIn = false, haveSize = false;
    std::string in;
    int w = 0, h = 0;
    bool haveTimes = false;
    std::vector<double> times;
    double scale = 1.0;
    GpuMode gpu = GpuMode::None;
    bool gpuFallback = false;
    int repeat = 1;
    std::string out;
    bool forceRender = false;
    bool draft = false;
    bool f32 = true, png = true, png16 = false, pngAlpha = false;
    std::string paramsJson;
    bool listParams = false;
};

struct Global {
    std::string bundle;
    std::string plugin = "0";
    std::string context = kOfxImageEffectContextFilter;
    bool listPlugins = false, listCl = false;
    int clPlatform = -1, clDevice = -1;
    std::string report;
    std::string batch;
    bool strict = false;
    bool keepGoing = false;
};

bool parseFrames(const std::string& s, std::vector<double>& out) {
    double a = 0, b = 0, st = 1;
    int n = std::sscanf(s.c_str(), "%lf:%lf:%lf", &a, &b, &st);
    if (n < 2) return false;
    if (st <= 0 || b < a) return false;
    out.clear();
    for (double t = a; t <= b + 1e-9 && out.size() < 100000; t += st) out.push_back(t);
    return true;
}

std::vector<std::string> splitArgs(const std::string& line) {
    std::vector<std::string> r;
    std::string cur;
    bool inQ = false, any = false;
    char q = 0;
    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (inQ) {
            if (c == q) inQ = false;
            else if (c == '\\' && q == '"' && i + 1 < line.size()) cur += line[++i];
            else cur += c;
        } else if (c == '"' || c == '\'') { inQ = true; q = c; any = true; }
        else if (c == '#' && cur.empty() && !any) break;
        else if (c == ' ' || c == '\t') { if (!cur.empty() || any) { r.push_back(cur); cur.clear(); any = false; } }
        else { cur += c; }
    }
    if (!cur.empty() || any) r.push_back(cur);
    return r;
}

// Parses args[i..] into job / global. Returns false on error.
bool parseArgs(const std::vector<std::string>& args, size_t start, Job& job, Global* g, std::string& err) {
    HostOptions& o = Host::get().opt;
    for (size_t i = start; i < args.size(); ++i) {
        const std::string& a = args[i];
        auto need = [&](const char* what) -> const std::string* {
            if (i + 1 >= args.size()) { err = std::string(a) + " needs " + what; return nullptr; }
            return &args[++i];
        };
        const std::string* v = nullptr;
        if (a == "--set") {
            if (!(v = need("NAME=VALUE"))) return false;
            size_t eq = v->find('=');
            if (eq == std::string::npos || eq == 0) { err = "--set expects NAME=VALUE, got '" + *v + "'"; return false; }
            Op op;
            op.kind = Op::Set;
            op.name = v->substr(0, eq);
            op.value = v->substr(eq + 1);
            size_t at = op.name.find('@');
            if (at != std::string::npos) {
                op.hasTime = true;
                op.time = std::atof(op.name.c_str() + at + 1);
                op.name = op.name.substr(0, at);
            }
            job.ops.push_back(op);
        } else if (a == "--press") {
            if (!(v = need("NAME"))) return false;
            Op op; op.kind = Op::Press; op.name = *v;
            size_t at = op.name.find('@');
            if (at != std::string::npos) { op.hasTime = true; op.time = std::atof(op.name.c_str() + at + 1); op.name = op.name.substr(0, at); }
            job.ops.push_back(op);
        } else if (a == "--reset-params") {
            Op op; op.kind = Op::Reset; job.ops.push_back(op);
        } else if (a == "--in") {
            if (!(v = need("FILE|pattern:NAME"))) return false;
            job.in = *v; job.haveIn = true;
        } else if (a == "--width") {
            if (!(v = need("W"))) return false;
            job.w = std::atoi(v->c_str()); job.haveSize = true;
        } else if (a == "--height") {
            if (!(v = need("H"))) return false;
            job.h = std::atoi(v->c_str()); job.haveSize = true;
        } else if (a == "--time") {
            if (!(v = need("T"))) return false;
            job.times = {std::atof(v->c_str())}; job.haveTimes = true;
        } else if (a == "--frames") {
            if (!(v = need("A:B[:STEP]"))) return false;
            if (!parseFrames(*v, job.times)) { err = "bad --frames '" + *v + "'"; return false; }
            job.haveTimes = true;
        } else if (a == "--render-scale") {
            if (!(v = need("S"))) return false;
            job.scale = std::atof(v->c_str());
            if (!(job.scale > 0 && job.scale <= 1.0)) { err = "--render-scale must be in (0,1]"; return false; }
        } else if (a == "--gpu") {
            if (!(v = need("none|opencl|opencl-images"))) return false;
            if (*v == "none" || *v == "cpu") job.gpu = GpuMode::None;
            else if (*v == "opencl" || *v == "opencl-buffers") job.gpu = GpuMode::OpenCLBuffers;
            else if (*v == "opencl-images") job.gpu = GpuMode::OpenCLImages;
            else if (*v == "metal") job.gpu = GpuMode::Metal;
            else { err = "unknown --gpu mode '" + *v + "' (none, opencl, opencl-images, metal)"; return false; }
        } else if (a == "--gpu-fallback") {
            job.gpuFallback = true;
        } else if (a == "--repeat") {
            if (!(v = need("N"))) return false;
            job.repeat = std::max(1, std::atoi(v->c_str()));
        } else if (a == "--out") {
            if (!(v = need("PREFIX"))) return false;
            job.out = *v;
        } else if (a == "--render") {
            job.forceRender = true;
        } else if (a == "--draft") {
            job.draft = true;
        } else if (a == "--no-f32") { job.f32 = false; }
        else if (a == "--no-png") { job.png = false; }
        else if (a == "--png16") { job.png16 = true; }
        else if (a == "--png-alpha") { job.pngAlpha = true; }
        else if (a == "--label") {
            if (!(v = need("TEXT"))) return false;
            job.label = *v;
        } else if (a == "--list-params-json") {
            if (!(v = need("FILE"))) return false;
            job.paramsJson = *v;
        } else if (a == "--list-params") {
            job.listParams = true;
        } else if (!g) {
            err = "option '" + a + "' is not allowed in a batch line";
            return false;
        } else if (a == "--plugin") {
            if (!(v = need("ID|INDEX"))) return false;
            g->plugin = *v;
        } else if (a == "--context") {
            if (!(v = need("filter|general"))) return false;
            if (*v == "filter") g->context = kOfxImageEffectContextFilter;
            else if (*v == "general") g->context = kOfxImageEffectContextGeneral;
            else { err = "unsupported context '" + *v + "'"; return false; }
        } else if (a == "--list-plugins") { g->listPlugins = true; }
        else if (a == "--list-cl-devices") { g->listCl = true; }
        else if (a == "--cl-platform") { if (!(v = need("N"))) return false; g->clPlatform = std::atoi(v->c_str()); }
        else if (a == "--cl-device") { if (!(v = need("N"))) return false; g->clDevice = std::atoi(v->c_str()); }
        else if (a == "--threads") { if (!(v = need("N"))) return false; o.threads = std::max(0, std::atoi(v->c_str())); }
        else if (a == "--answer") {
            if (!(v = need("yes|no"))) return false;
            o.answerYes = (*v != "no");
        } else if (a == "--plugin-edit-notify") {
            if (!(v = need("sync|deferred|off"))) return false;
            if (*v == "sync") o.pluginEditNotify = NotifyMode::Sync;
            else if (*v == "deferred") o.pluginEditNotify = NotifyMode::Deferred;
            else if (*v == "off") o.pluginEditNotify = NotifyMode::Off;
            else { err = "bad --plugin-edit-notify"; return false; }
        } else if (a == "--host-name") { if (!(v = need("NAME"))) return false; o.hostName = *v; }
        else if (a == "--depths") {
            if (!(v = need("LIST"))) return false;
            o.depths.clear();
            std::stringstream ss(*v);
            std::string t;
            while (std::getline(ss, t, ',')) {
                if (t == "float") o.depths.push_back(kOfxBitDepthFloat);
                else if (t == "half") o.depths.push_back(kOfxBitDepthHalf);
                else if (t == "short") o.depths.push_back(kOfxBitDepthShort);
                else if (t == "byte") o.depths.push_back(kOfxBitDepthByte);
                else { err = "unknown depth '" + t + "'"; return false; }
            }
        } else if (a == "--interactive") { o.interactive = true; }
        else if (a == "--fps") { if (!(v = need("F"))) return false; o.fps = std::atof(v->c_str()); }
        else if (a == "--no-sentinel") { o.sentinel = false; }
        else if (a == "--report") { if (!(v = need("FILE"))) return false; g->report = *v; }
        else if (a == "--batch") { if (!(v = need("FILE"))) return false; g->batch = *v; }
        else if (a == "--strict") { g->strict = true; }
        else if (a == "--keep-going") { g->keepGoing = true; }
        else if (a == "-v" || a == "--verbose") { o.verbose = true; diag().verbose = true; }
        else if (a == "--trace") { o.trace = true; }
        else if (a == "--trace-props") { diag().traceProps = true; }
        else if (a == "-h" || a == "--help") { std::fputs(kUsage, stdout); std::exit(0); }
        else if (g->bundle.empty() && !a.empty() && a[0] != '-') { g->bundle = a; }
        else { err = "unknown option '" + a + "' (see --help)"; return false; }
    }
    return true;
}

// ---------------------------------------------------------------- description output
std::string boolFlag(Effect* inst, Effect* desc, const char* prop) {
    std::string d = desc->props().getString(prop, 0, "false");
    return inst ? inst->props().getString(prop, 0, d) : d;
}

void writeParamJson(Json& j, Param& p) {
    PropertySet& ps = p.props();
    j.beginObject();
    j.kv("name", p.name());
    j.kv("type", std::string(paramKindShort(p.kind())));
    j.kv("label", ps.getString(kOfxPropLabel));
    j.kv("hint", ps.getString(kOfxParamPropHint));
    j.kv("parent", ps.getString(kOfxParamPropParent));
    j.kv("scriptName", ps.getString(kOfxParamPropScriptName));
    j.kv("secret", ps.getInt(kOfxParamPropSecret) != 0);
    j.kv("enabled", ps.getInt(kOfxParamPropEnabled, 0, 1) != 0);
    if (p.hasValue()) {
        j.kv("animates", ps.getInt(kOfxParamPropAnimates) != 0);
        j.kv("evaluateOnChange", ps.getInt(kOfxParamPropEvaluateOnChange, 0, 1) != 0);
        j.kv("dimension", p.dim());
        ParamValue d = p.defaultValue();
        ParamValue cur = p.valueAt(0);
        auto emitVal = [&](const char* k, const ParamValue& v) {
            j.key(k);
            if (p.isStringValued()) { j.value(v.str); return; }
            if (p.dim() == 1) {
                if (p.kind() == ParamKind::Boolean) j.value(v.num[0] != 0);
                else if (p.isIntValued()) j.value(static_cast<long long>(v.num[0]));
                else j.value(v.num[0]);
                return;
            }
            j.beginArray();
            for (double x : v.num) { if (p.isIntValued()) j.value(static_cast<long long>(x)); else j.value(x); }
            j.endArray();
        };
        emitVal("default", d);
        emitVal("value", cur);
        j.kv("formatted", formatParamValue(p, cur));
        if (p.kind() == ParamKind::Choice || p.kind() == ParamKind::StrChoice) {
            j.kv("options", ps.getStrings(kOfxParamPropChoiceOption));
            if (p.kind() == ParamKind::StrChoice) j.kv("enums", ps.getStrings(kOfxParamPropChoiceEnum));
            if (p.kind() == ParamKind::Choice) {
                auto ord = ps.getInts(kOfxParamPropChoiceOrder);
                j.key("order"); j.beginArray(); for (int o : ord) j.value(o); j.endArray();
            }
        }
        bool numeric = !p.isStringValued() && p.kind() != ParamKind::Boolean && p.kind() != ParamKind::Choice;
        if (numeric) {
            auto arr = [&](const char* k, const char* prop, double lim) {
                j.key(k);
                j.beginArray();
                for (int c = 0; c < p.dim(); ++c) {
                    double x = p.isIntValued() ? double(ps.getInt(prop, c, 0)) : ps.getDouble(prop, c, 0);
                    if (std::fabs(x) >= lim) j.null(); else j.value(x);
                }
                j.endArray();
            };
            double lim = p.isIntValued() ? double(INT_MAX) : DBL_MAX;
            arr("min", kOfxParamPropMin, lim);
            arr("max", kOfxParamPropMax, lim);
            arr("displayMin", kOfxParamPropDisplayMin, lim);
            arr("displayMax", kOfxParamPropDisplayMax, lim);
            if (!p.isIntValued()) {
                j.kv("increment", ps.getDouble(kOfxParamPropIncrement, 0, 1));
                j.kv("digits", ps.getInt(kOfxParamPropDigits, 0, 2));
                j.kv("doubleType", ps.getString(kOfxParamPropDoubleType));
            }
        }
        if (p.kind() == ParamKind::String) {
            j.kv("stringMode", ps.getString(kOfxParamPropStringMode));
            j.kv("filePathExists", ps.getInt(kOfxParamPropStringFilePathExists, 0, 1) != 0);
        }
        j.kv("keys", int(p.keys().size()));
    }
    if (p.kind() == ParamKind::Page) j.kv("children", ps.getStrings(kOfxParamPropPageChild));
    if (p.kind() == ParamKind::Group) j.kv("groupOpen", ps.getInt(kOfxParamPropGroupOpen, 0, 1) != 0);
    j.endObject();
}

std::string groupPath(ParamSet& ps, Param& p) {
    std::string path;
    std::string par = p.props().getString(kOfxParamPropParent);
    int guard = 0;
    while (!par.empty() && guard++ < 32) {
        Param* g = ps.get(par);
        std::string label = g ? g->props().getString(kOfxPropLabel, 0, par) : par;
        path = path.empty() ? label : label + "/" + path;
        par = g ? g->props().getString(kOfxParamPropParent) : "";
    }
    return path;
}

void writeDescriptionJson(Json& j, PluginInfo& pi, Effect* desc, Instance* inst) {
    Effect* ie = inst ? inst->effect() : nullptr;
    PropertySet& dp = desc->props();
    j.key("plugin").beginObject();
    j.kv("id", pi.id);
    j.kv("versionMajor", pi.vMajor);
    j.kv("versionMinor", pi.vMinor);
    j.kv("label", dp.getString(kOfxPropLabel));
    j.kv("grouping", dp.getString(kOfxImageEffectPluginPropGrouping));
    j.kv("description", dp.getString(kOfxPropPluginDescription));
    j.kv("versionLabel", dp.getString(kOfxPropVersionLabel));
    j.kv("contexts", dp.getStrings(kOfxImageEffectPropSupportedContexts));
    j.kv("pixelDepths", dp.getStrings(kOfxImageEffectPropSupportedPixelDepths));
    j.kv("supportsTiles", dp.getInt(kOfxImageEffectPropSupportsTiles, 0, 1) != 0);
    j.kv("supportsMultiResolution", dp.getInt(kOfxImageEffectPropSupportsMultiResolution, 0, 1) != 0);
    j.kv("temporalClipAccess", dp.getInt(kOfxImageEffectPropTemporalClipAccess) != 0);
    j.kv("renderThreadSafety", dp.getString(kOfxImageEffectPluginRenderThreadSafety));
    j.kv("hostFrameThreading", dp.getInt(kOfxImageEffectPluginPropHostFrameThreading, 0, 1) != 0);
    j.kv("singleInstance", dp.getInt(kOfxImageEffectPluginPropSingleInstance) != 0);
    j.key("gpu").beginObject();
    j.kv("openclBuffers", boolFlag(ie, desc, kOfxImageEffectPropOpenCLRenderSupported));
    j.kv("openclImages", boolFlag(ie, desc, kOfxImageEffectPropOpenCLSupported));
    j.kv("cuda", boolFlag(ie, desc, kOfxImageEffectPropCudaRenderSupported));
    j.kv("cudaStream", boolFlag(ie, desc, kOfxImageEffectPropCudaStreamSupported));
    j.kv("metal", boolFlag(ie, desc, kOfxImageEffectPropMetalRenderSupported));
    j.kv("opengl", boolFlag(ie, desc, kOfxImageEffectPropOpenGLRenderSupported));
    j.kv("cpu", boolFlag(ie, desc, kOfxImageEffectPropCPURenderSupported));
    j.endObject();
    j.endObject();
    j.key("clips").beginArray();
    for (auto& c : desc->clips()) {
        j.beginObject();
        j.kv("name", c->name());
        j.kv("components", c->props().getStrings(kOfxImageEffectPropSupportedComponents));
        j.kv("optional", c->props().getInt(kOfxImageClipPropOptional) != 0);
        j.kv("isMask", c->props().getInt(kOfxImageClipPropIsMask) != 0);
        j.kv("temporalAccess", c->props().getInt(kOfxImageEffectPropTemporalClipAccess) != 0);
        if (inst && inst->clipPrefsSummary.count(c->name())) j.kv("chosen", inst->clipPrefsSummary[c->name()]);
        j.endObject();
    }
    j.endArray();
    ParamSet& ps = ie ? ie->params() : desc->params();
    j.kv("pageOrder", ps.props().getStrings(kOfxPluginPropParamPageOrder));
    j.key("params").beginArray();
    for (auto& p : ps.params()) {
        // groupPath is emitted in a wrapper to keep writeParamJson generic
        std::string gp = groupPath(ps, *p);
        Json pj;
        writeParamJson(pj, *p);
        std::string s = pj.str();
        s.insert(s.size() - 1, ",\"groupPath\":" + Json::quote(gp));
        j.raw(s);
    }
    j.endArray();
}

void printParamsText(PluginInfo& pi, Effect* desc, Instance* inst) {
    Effect* ie = inst ? inst->effect() : nullptr;
    PropertySet& dp = desc->props();
    std::printf("Plug-in  %s v%d.%d  \"%s\"  grouping \"%s\"\n", pi.id.c_str(), pi.vMajor, pi.vMinor,
                dp.getString(kOfxPropLabel).c_str(), dp.getString(kOfxImageEffectPluginPropGrouping).c_str());
    std::string ctx, dep;
    for (auto& c : dp.getStrings(kOfxImageEffectPropSupportedContexts)) ctx += " " + c;
    for (auto& d : dp.getStrings(kOfxImageEffectPropSupportedPixelDepths)) dep += " " + d;
    std::printf("Contexts%s\nDepths  %s\n", ctx.c_str(), dep.c_str());
    std::printf("Tiles %d  MultiRes %d  ThreadSafety %s  HostFrameThreading %d\n",
                dp.getInt(kOfxImageEffectPropSupportsTiles, 0, 1), dp.getInt(kOfxImageEffectPropSupportsMultiResolution, 0, 1),
                dp.getString(kOfxImageEffectPluginRenderThreadSafety).c_str(), dp.getInt(kOfxImageEffectPluginPropHostFrameThreading, 0, 1));
    std::printf("GPU     OpenCL(buffers)=%s OpenCL(images)=%s CUDA=%s CUDAstream=%s Metal=%s OpenGL=%s CPU=%s\n",
                boolFlag(ie, desc, kOfxImageEffectPropOpenCLRenderSupported).c_str(),
                boolFlag(ie, desc, kOfxImageEffectPropOpenCLSupported).c_str(),
                boolFlag(ie, desc, kOfxImageEffectPropCudaRenderSupported).c_str(),
                boolFlag(ie, desc, kOfxImageEffectPropCudaStreamSupported).c_str(),
                boolFlag(ie, desc, kOfxImageEffectPropMetalRenderSupported).c_str(),
                boolFlag(ie, desc, kOfxImageEffectPropOpenGLRenderSupported).c_str(),
                boolFlag(ie, desc, kOfxImageEffectPropCPURenderSupported).c_str());
    std::printf("Clips  ");
    for (auto& c : desc->clips()) {
        std::string comps;
        for (auto& s : c->props().getStrings(kOfxImageEffectPropSupportedComponents)) comps += (comps.empty() ? "" : ",") + s.substr(std::min<size_t>(s.size(), 17));
        std::printf(" %s[%s]%s", c->name().c_str(), comps.c_str(), c->props().getInt(kOfxImageClipPropOptional) ? "(optional)" : "");
    }
    std::printf("\n");
    ParamSet& ps = ie ? ie->params() : desc->params();
    std::printf("Parameters (%zu)\n", ps.params().size());
    for (auto& p : ps.params()) {
        PropertySet& pp = p->props();
        std::string gp = groupPath(ps, *p);
        std::printf("  %-22s %-10s", p->name().c_str(), paramKindShort(p->kind()));
        if (!gp.empty()) std::printf(" [%s]", gp.c_str());
        std::printf(" \"%s\"", pp.getString(kOfxPropLabel).c_str());
        if (p->hasValue()) {
            std::printf(" default=%s", formatParamValue(*p, p->defaultValue()).c_str());
            ParamValue cur = p->valueAt(0);
            if (!(cur == p->defaultValue())) std::printf(" value=%s", formatParamValue(*p, cur).c_str());
            bool numeric = !p->isStringValued() && p->kind() != ParamKind::Boolean && p->kind() != ParamKind::Choice;
            if (numeric) {
                for (int c = 0; c < p->dim(); ++c) {
                    double lo = pp.getDouble(kOfxParamPropMin, c), hi = pp.getDouble(kOfxParamPropMax, c);
                    double dlo = pp.getDouble(kOfxParamPropDisplayMin, c), dhi = pp.getDouble(kOfxParamPropDisplayMax, c);
                    auto f = [](double x) {
                        char b[32];
                        if (std::fabs(x) >= 2e9) return std::string(x < 0 ? "-inf" : "inf");
                        std::snprintf(b, sizeof(b), "%g", x);
                        return std::string(b);
                    };
                    std::printf(" range%s=[%s,%s] display=[%s,%s]", p->dim() > 1 ? std::to_string(c).c_str() : "",
                                f(lo).c_str(), f(hi).c_str(), f(dlo).c_str(), f(dhi).c_str());
                }
            }
            if (!pp.getInt(kOfxParamPropAnimates)) std::printf(" static");
        }
        if (pp.getInt(kOfxParamPropSecret)) std::printf(" secret");
        if (!pp.getInt(kOfxParamPropEnabled, 0, 1)) std::printf(" disabled");
        std::printf("\n");
        if (p->kind() == ParamKind::Choice || p->kind() == ParamKind::StrChoice) {
            auto op = pp.getStrings(kOfxParamPropChoiceOption);
            auto en = pp.getStrings(kOfxParamPropChoiceEnum);
            for (size_t k = 0; k < op.size(); ++k)
                std::printf("      %2zu: %s%s\n", k, op[k].c_str(), k < en.size() ? (" [" + en[k] + "]").c_str() : "");
        }
        std::string hint = pp.getString(kOfxParamPropHint);
        if (!hint.empty()) std::printf("      hint: %s\n", hint.c_str());
    }
}

// ---------------------------------------------------------------- helpers
double median(std::vector<double> v) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    size_t n = v.size();
    return n % 2 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

struct JobRecord {
    Job job;
    std::vector<FrameResult> frames;
    std::string error;
    bool rendered = false;
};

void writeFrameJson(Json& j, const FrameResult& f) {
    j.beginObject();
    j.kv("time", f.time);
    j.kv("status", std::string(statusName(f.status)));
    j.kv("identity", f.identity);
    if (f.identity) { j.kv("identityClip", f.identityClip); j.kv("identityTime", f.identityTime); }
    j.kv("gpu", f.gpu);
    j.kv("gpuFellBack", f.gpuFellBack);
    j.kv("gpuWithdrawn", f.gpuWithdrawn);
    j.kv("width", f.outW);
    j.kv("height", f.outH);
    j.kv("renderMs", f.renderMs);
    j.kv("finishMs", f.finishMs);
    j.kv("totalMs", f.totalMs);
    j.kv("renderMsMedian", median(f.renderMs));
    j.kv("renderMsMin", f.renderMs.empty() ? 0.0 : *std::min_element(f.renderMs.begin(), f.renderMs.end()));
    {
        // device-complete time: render action + wait for the queue (what a GPU host frame really costs)
        std::vector<double> rf;
        for (size_t k = 0; k < f.renderMs.size(); ++k) rf.push_back(f.renderMs[k] + (k < f.finishMs.size() ? f.finishMs[k] : 0.0));
        j.kv("renderFinishMsMedian", median(rf));
        j.kv("renderFinishMsMin", rf.empty() ? 0.0 : *std::min_element(rf.begin(), rf.end()));
    }
    j.kv("uploadMs", f.uploadMs);
    j.kv("downloadMs", f.downloadMs);
    j.kv("unwrittenPixels", f.unwritten);
    j.key("rod").beginArray().value(f.rod.x1).value(f.rod.y1).value(f.rod.x2).value(f.rod.y2).endArray();
    j.key("stats").beginObject();
    j.key("min").beginArray(); for (double x : f.stats.min) j.value(x); j.endArray();
    j.key("max").beginArray(); for (double x : f.stats.max) j.value(x); j.endArray();
    j.key("mean").beginArray(); for (double x : f.stats.mean) j.value(x); j.endArray();
    j.kv("nan", f.stats.nan);
    j.kv("inf", f.stats.inf);
    j.endObject();
    j.kv("files", f.files);
    j.endObject();
}

} // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args(argv, argv + argc);
    Global g;
    Job first;
    std::string err;
    if (argc < 2) { std::fputs(kUsage, stderr); return 2; }
    if (!parseArgs(args, 1, first, &g, err)) { std::fprintf(stderr, "ofxhost: %s\n", err.c_str()); return 2; }
    Host& H = Host::get();
    H.init();

    if (g.listCl) {
        auto devs = OpenCLHost::enumerate(err);
        if (devs.empty()) { std::fprintf(stderr, "ofxhost: %s\n", err.c_str()); return 1; }
        for (auto& d : devs)
            std::printf("platform %d device %d: %s / %s (%s; driver %s)%s\n", d.platform, d.device, d.platformName.c_str(),
                        d.deviceName.c_str(), d.deviceVersion.c_str(), d.driverVersion.c_str(), d.isGpu ? " [GPU]" : "");
        return 0;
    }
    if (g.bundle.empty()) { std::fprintf(stderr, "ofxhost: no plug-in given (see --help)\n"); return 2; }

    PluginBinary bin;
    if (!bin.open(g.bundle, err)) { std::fprintf(stderr, "ofxhost: %s\n", err.c_str()); return 3; }
    if (g.listPlugins) {
        for (size_t i = 0; i < bin.plugins.size(); ++i) {
            auto& p = bin.plugins[i];
            std::printf("%zu: %s v%d.%d (%s v%d)\n", i, p.id.c_str(), p.vMajor, p.vMinor, p.api.c_str(), p.apiVersion);
        }
        return 0;
    }
    int idx = -1;
    {
        char* end = nullptr;
        long n = std::strtol(g.plugin.c_str(), &end, 10);
        if (end && *end == 0 && n >= 0 && size_t(n) < bin.plugins.size()) idx = int(n);
        for (size_t i = 0; i < bin.plugins.size() && idx < 0; ++i) if (bin.plugins[i].id == g.plugin) idx = int(i);
        if (idx < 0) { std::fprintf(stderr, "ofxhost: plug-in '%s' not found (use --list-plugins)\n", g.plugin.c_str()); return 3; }
    }

    // jobs
    std::vector<Job> jobs;
    jobs.push_back(first);
    if (!g.batch.empty()) {
        std::ifstream f(g.batch);
        if (!f) { std::fprintf(stderr, "ofxhost: cannot read batch file %s\n", g.batch.c_str()); return 2; }
        std::string line;
        int ln = 0;
        while (std::getline(f, line)) {
            ++ln;
            auto a = splitArgs(line);
            if (a.empty()) continue;
            Job j;
            if (!parseArgs(a, 0, j, nullptr, err)) {
                std::fprintf(stderr, "ofxhost: %s:%d: %s\n", g.batch.c_str(), ln, err.c_str());
                return 2;
            }
            if (j.label.empty()) j.label = "line " + std::to_string(ln);
            jobs.push_back(j);
        }
    }

    // OpenCL context (only if some job wants it)
    std::unique_ptr<OpenCLHost> cl;
    std::unique_ptr<MetalHost> mtl;
    bool wantCl = false, wantMtl = false;
    for (auto& j : jobs) {
        wantCl = wantCl || j.gpu == GpuMode::OpenCLBuffers || j.gpu == GpuMode::OpenCLImages;
        wantMtl = wantMtl || j.gpu == GpuMode::Metal;
    }
    if (wantMtl) {
        mtl.reset(new MetalHost);
        if (!mtl->init(err)) {
            std::fprintf(stderr, "ofxhost: Metal init failed: %s\n", err.c_str());
            return 1;
        }
        H.mtl = mtl.get();
        std::printf("Metal: %s\n", mtl->deviceName().c_str());
    }
    if (wantCl) {
        cl.reset(new OpenCLHost);
        if (!cl->init(g.clPlatform, g.clDevice, err)) {
            std::fprintf(stderr, "ofxhost: OpenCL init failed: %s\n", err.c_str());
            return 1;
        }
        H.cl = cl.get();
        std::printf("OpenCL: %s / %s (%s)\n", cl->info().platformName.c_str(), cl->info().deviceName.c_str(), cl->info().deviceVersion.c_str());
    }

    int exitCode = 0;
    std::vector<JobRecord> records;
    double tLoad = H.nowMs();
    if (!describeInContext(bin, idx, g.context, err)) {
        std::fprintf(stderr, "ofxhost: %s\n", err.c_str());
        exitCode = 3;
    }
    double describeMs = H.nowMs() - tLoad;
    std::unique_ptr<Instance> inst;
    double createMs = 0;
    if (exitCode == 0) {
        inst.reset(new Instance(&bin, idx, g.context));
        Job& j0 = jobs[0];
        // project size: first job that names one
        int W = 0, Hh = 0;
        for (auto& j : jobs) if (j.haveSize) { W = j.w; Hh = j.h; break; }
        if (W <= 0 || Hh <= 0) { W = 1920; Hh = 1080; }
        inst->setProject(W, Hh);
        std::vector<double> times = j0.haveTimes ? j0.times : std::vector<double>{0.0};
        double lo = 0, hi = 0;
        for (auto& j : jobs) for (double t : j.times) { lo = std::min(lo, t); hi = std::max(hi, t); }
        (void)times;
        inst->setFrameRange(lo, std::max(hi, lo));
        double t0 = H.nowMs();
        if (!inst->create(err)) {
            std::fprintf(stderr, "ofxhost: %s\n", err.c_str());
            exitCode = 3;
        }
        createMs = H.nowMs() - t0;
    }

    if (exitCode == 0) {
        int curW = 0, curH = 0;
        std::string curIn;
        for (size_t ji = 0; ji < jobs.size(); ++ji) {
            Job& j = jobs[ji];
            JobRecord rec;
            rec.job = j;
            if (j.haveSize) {
                if (j.w > 0) curW = j.w;
                if (j.h > 0) curH = j.h;
            }
            if (curW == 0 || curH == 0) {
                for (auto& jj : jobs) if (jj.haveSize) { curW = jj.w; curH = jj.h; break; }
                if (curW <= 0 || curH <= 0) { curW = 1920; curH = 1080; }
            }
            if (j.haveIn) curIn = j.in;
            inst->setProject(curW, curH);
            SourceSpec src;
            src.spec = curIn; src.w = curW; src.h = curH;
            inst->setSource(src);
            double jobTime = j.haveTimes && !j.times.empty() ? j.times[0] : 0.0;
            bool ok = true;
            for (auto& op : j.ops) {
                double t = op.hasTime ? op.time : jobTime;
                if (op.kind == Op::Set) ok = inst->userSetParam(op.name, op.value, t, op.hasTime, err);
                else if (op.kind == Op::Press) ok = inst->userPressButton(op.name, t, err);
                else ok = inst->resetParams(err);
                if (!ok) break;
            }
            if (!ok) {
                rec.error = err;
                std::fprintf(stderr, "ofxhost: %s%s\n", j.label.empty() ? "" : (j.label + ": ").c_str(), err.c_str());
                if (!exitCode) exitCode = (err.find("unknown parameter") != std::string::npos || err.find("--set") == 0) ? 2 : 1;
                records.push_back(rec);
                if (g.keepGoing) continue;
                break;
            }
            if (j.listParams) printParamsText(inst->info(), inst->contextDescriptor(), inst.get());
            if (!j.paramsJson.empty()) {
                Json pj;
                pj.beginObject();
                writeDescriptionJson(pj, inst->info(), inst->contextDescriptor(), inst.get());
                pj.endObject();
                if (j.paramsJson == "-") std::printf("%s\n", pj.str().c_str());
                else {
                    std::ofstream f(j.paramsJson);
                    f << pj.str() << "\n";
                    if (!f) { std::fprintf(stderr, "ofxhost: cannot write %s\n", j.paramsJson.c_str()); if (!exitCode) exitCode = 1; }
                }
            }
            bool doRender = !j.out.empty() || j.forceRender || (ji == 0 && g.batch.empty() && j.haveIn) || j.repeat > 1;
            if (doRender) {
                if (curIn.empty()) {
                    rec.error = "render requested but no --in given";
                    std::fprintf(stderr, "ofxhost: %s\n", rec.error.c_str());
                    if (!exitCode) exitCode = 2;
                    records.push_back(rec);
                    if (g.keepGoing) continue;
                    break;
                }
                RenderRequest rq;
                rq.times = j.haveTimes ? j.times : std::vector<double>{0.0};
                rq.renderScale = j.scale;
                rq.gpu = j.gpu;
                rq.gpuFallbackToCpu = j.gpuFallback;
                rq.repeat = j.repeat;
                rq.outPrefix = j.out;
                rq.writeF32 = j.f32;
                rq.writePng = j.png;
                rq.png16 = j.png16;
                rq.pngAlpha = j.pngAlpha;
                rq.draft = j.draft;
                rec.rendered = true;
                if (!inst->render(rq, rec.frames, nullptr, err)) {
                    rec.error = err;
                    std::fprintf(stderr, "ofxhost: %s%s\n", j.label.empty() ? "" : (j.label + ": ").c_str(), err.c_str());
                    if (!exitCode) exitCode = 1;
                }
                for (auto& f : rec.frames) {
                    if (f.status != kOfxStatOK && f.renderMs.empty()) continue;
                    double med = median(f.renderMs);
                    double mn = f.renderMs.empty() ? 0 : *std::min_element(f.renderMs.begin(), f.renderMs.end());
                    std::printf("%sframe %g [%s%s] %dx%d render %.3f ms", j.label.empty() ? "" : (j.label + ": ").c_str(), f.time,
                                f.gpu.c_str(), f.gpuFellBack ? "->cpu" : "", f.outW, f.outH, med);
                    if (f.renderMs.size() > 1) std::printf(" (min %.3f, n=%zu)", mn, f.renderMs.size());
                    if (!f.finishMs.empty() && f.gpu.rfind("opencl", 0) == 0) std::printf(" finish %.3f ms upload %.3f download %.3f", median(f.finishMs), f.uploadMs, f.downloadMs);
                    if (!f.totalMs.empty()) std::printf(" total %.3f ms", median(f.totalMs));
                    std::printf(" | mean %.5f %.5f %.5f %.5f", f.stats.mean[0], f.stats.mean[1], f.stats.mean[2], f.stats.mean[3]);
                    if (f.stats.nan || f.stats.inf) std::printf(" NaN=%lld Inf=%lld", f.stats.nan, f.stats.inf);
                    if (f.unwritten) std::printf(" UNWRITTEN=%lld", f.unwritten);
                    if (f.identity) std::printf(" identity(%s@%g)", f.identityClip.c_str(), f.identityTime);
                    if (f.gpuWithdrawn) std::printf(" GPU-WITHDRAWN-BY-PLUGIN");
                    std::printf("\n");
                }
            }
            records.push_back(rec);
            if (exitCode != 0 && !g.keepGoing) break;
        }
    }

    // strict checks
    std::vector<std::string> warnings;
    {
        std::lock_guard<std::mutex> lk(diag().mtx);
        warnings = diag().warnings;
    }
    long long unwrittenTotal = 0;
    for (auto& r : records) for (auto& f : r.frames) unwrittenTotal += f.unwritten;
    if (!warnings.empty() && !H.opt.verbose) {
        std::fprintf(stderr, "ofxhost: %zu host warning(s):\n", warnings.size());
        for (size_t i = 0; i < warnings.size() && i < 20; ++i) std::fprintf(stderr, "  - %s\n", warnings[i].c_str());
    }
    if (g.strict && exitCode == 0 && (!warnings.empty() || unwrittenTotal > 0 || diag().badHandles > 0)) {
        std::fprintf(stderr, "ofxhost: strict mode: %zu warning(s), %lld unwritten pixel(s), %d bad handle(s)\n", warnings.size(),
                     unwrittenTotal, int(diag().badHandles));
        exitCode = 4;
    }

    if (!g.report.empty()) {
        Json j;
        j.beginObject();
        j.kv("tool", "ofxhost");
        j.kv("exitCode", exitCode);
        j.kv("bundle", g.bundle);
        j.kv("binary", bin.path);
        j.kv("context", g.context);
        j.key("host").beginObject();
        j.kv("name", H.opt.hostName);
        j.kv("threads", H.opt.threads);
        j.kv("pluginEditNotify", std::string(H.opt.pluginEditNotify == NotifyMode::Sync ? "sync" : H.opt.pluginEditNotify == NotifyMode::Deferred ? "deferred" : "off"));
        j.kv("depths", H.opt.depths);
        if (cl) {
            j.key("opencl").beginObject();
            j.kv("platform", cl->info().platformName);
            j.kv("device", cl->info().deviceName);
            j.kv("version", cl->info().deviceVersion);
            j.endObject();
        }
        j.endObject();
        j.kv("describeMs", describeMs);
        j.kv("createInstanceMs", createMs);
        if (inst && inst->effect()) writeDescriptionJson(j, inst->info(), inst->contextDescriptor(), inst.get());
        j.key("jobs").beginArray();
        for (auto& r : records) {
            j.beginObject();
            j.kv("label", r.job.label);
            j.kv("error", r.error);
            j.kv("renderScale", r.job.scale);
            j.kv("gpuRequested", std::string(r.job.gpu == GpuMode::None ? "none" : r.job.gpu == GpuMode::OpenCLBuffers ? "opencl" : gpuModeName(r.job.gpu)));
            j.key("ops").beginArray();
            for (auto& op : r.job.ops) {
                j.beginObject();
                j.kv("op", std::string(op.kind == Op::Set ? "set" : op.kind == Op::Press ? "press" : "reset"));
                j.kv("name", op.name);
                if (op.kind == Op::Set) j.kv("value", op.value);
                if (op.hasTime) j.kv("time", op.time);
                j.endObject();
            }
            j.endArray();
            j.key("frames").beginArray();
            for (auto& f : r.frames) writeFrameJson(j, f);
            j.endArray();
            j.endObject();
        }
        j.endArray();
        j.key("messages").beginArray();
        {
            std::lock_guard<std::mutex> lk(H.msgMtx);
            for (auto& m : H.messages) {
                j.beginObject();
                j.kv("kind", m.kind); j.kv("type", m.type); j.kv("id", m.id); j.kv("text", m.text); j.kv("atMs", m.atMs);
                j.endObject();
            }
        }
        j.endArray();
        j.kv("warnings", warnings);
        j.kv("badHandles", int(diag().badHandles));
        j.key("unknownPropertyGets").beginObject();
        {
            std::lock_guard<std::mutex> lk(diag().mtx);
            for (auto& kv : diag().unknownGets) j.kv(kv.first, kv.second);
        }
        j.endObject();
        j.key("pluginCreatedProperties").beginObject();
        {
            std::lock_guard<std::mutex> lk(diag().mtx);
            for (auto& kv : diag().dynamicSets) j.kv(kv.first, kv.second);
        }
        j.endObject();
        j.kv("suitesFetched", H.suitesFetched);
        j.kv("suitesMissing", H.suitesMissing);
        if (inst) j.kv("instanceChangedLog", inst->notifyLog);
        j.endObject();
        std::ofstream f(g.report);
        f << j.str() << "\n";
        if (!f) { std::fprintf(stderr, "ofxhost: cannot write report %s\n", g.report.c_str()); if (!exitCode) exitCode = 1; }
    }

    inst.reset();
    bin.close();
    return exitCode;
}
