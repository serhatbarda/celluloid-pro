// Reafilm Pro - frame plan builder (texture agent). Implements engine::buildPlan, kernelName and
// Plan::debugString (Engine.h), plus the texture-only plans of src/texture/TexturePlan.h.
//
// PASS GRAPH (SPEC §3).  "full" = frame size w x h, "Ln" = pyramid level n (ceil(w/2^n) x ...).
//
//   Full:        decode(SRC) -> [weave] -> [film resolution] -> [acutance] -> [glow: halation/bloom
//                (+vignette)] -> [vignette] ; [grain grids] ; [damage] ;
//                color(img, coarse grid|-, fine grid|-, damage|-)  (v1.2: grain evaluated in the colour
//                pass from the layer exposures; alpha from img, which every texture pass keeps)
//                -> [film blur] -> [watermark] -> DST
//   ColorOnly:   color(SRC decoded in the kernel, i[1] = 1) -> [watermark] -> DST
//   TextureOnly: display_decode(SRC) -> [weave] -> [tonemap to pseudo scene] -> [film resolution]
//                -> [acutance] -> [glow (maps back to display, vignette)] | [tonemap back] -> [vignette] ;
//                [grain grids] ; [damage] ; display_finish(img, coarse|-, fine|-, damage|-) -> [film blur]
//                -> [watermark] -> DST
//                (SDR outputs: the curve back to display never exceeds 1.0; HDR: exact inverse)
//
// Texture sub-graphs (each only when its amount is non-zero):
//   weave     tx_weave(full; alpha not resampled)
//   film res. tx_blur4 h, v (per channel negative MTF + Integrate print/scan blur; alpha kept)  (v1.2)
//   acutance  L = 0: tx_blur4 h (log2 luma computed from the image on the fly) -> tx_blur4 v
//             L > 0: tx_pyr_down (log2 luma on the fly) -> tx_pyr_down x (L-1) -> tx_blur4 h, v -> tx_up_v
//             -> tx_acut_apply   (L = 0 for radius <= 2.33 px, otherwise the level where the residual
//             blur fits; radius x gauge scale)
//   glow      tx_hb_prefilter_down(full -> L1: bloom source + halation energy, fused) -> tx_pyr_down (L2 .. Ln)
//             for every used level from coarse to fine: tx_blur4 h, v -> tx_pyr_up_add
//             (accumulates B-spline upsampled coarser result + weighted level) -> tx_up_v
//             -> tx_hb_composite
//   vignette  fused into tx_hb_composite when the glow runs, else tx_vignette(full); a mechanical vignette
//             (Lab & Gate) always runs stand-alone tx_vignette (with the cos^4 part)
//   gate      Lab & Gate: camera gate tx_gate(full) on the image right AFTER the weave; printer gate
//             tx_gate(damage field or nothing) -> the colour pass's in3 (an unexposed, clear print border);
//             clean border: tx_gate(1x1 const) [, tx_gate(1x1 border density)] -> colour pass (1x1 reference)
//             -> tx_gate(finished frame, reference) after the film blur (no effect on top of the border)
//   grain     per emulsion (coarse = fast, fine = slow; own grid spacing D >= 1 px):
//             tx_blur4 h (white noise generated on the fly) -> tx_blur4 v [-> tx_up_v]; the colour pass
//             samples both grids (GrainKernels.h). buildGrainPlan adds tx_grain_finish (diagnostic field).
//   damage    [tx_params (8x1 scratch / hair tables)] -> tx_damage(full)   (v1.1, film damage on and
//             something to draw; the field is the colour pass's in3)
//   film blur display_decode(colour result) -> [tx_pyr_down x L] -> tx_blur4 h, v (per channel)
//             [-> tx_pyr_up_add (B-spline, level L -> full res)] -> display_finish(-, -, SRC alpha)
//             (v1.1, only on frames whose blur is >= 0.05 px: projection focus acts on the finished
//             image, so grain and damage soften with it; the watermark stays sharp)
//
// f[] / i[] OF THE COLOUR AND LICENSE PASSES (kernels owned by core / license):
//   decode, display_decode:  f[] = 0, i[] = 0 (everything comes from `state`).
//   color, display_finish:   i[0] = 2 (v1.2 grain): in1 = coarse grid, in2 = fine grid, f[0..23] and
//                            i[3..5] = the GrainKernels.h block, display_finish also f[24..29] = tone
//                            map; alpha = in0.w. i[0] = 0: no grain (in1 / in2 unused). in3 = damage
//                            field or unused. Film blur re-encode: display_finish(img, -, SRC): alpha
//                            from SRC (in2), f[] = 0. (i[0] = 1 = v1.1 finished grain field in in1 is
//                            still understood by the kernels.)
//                            color only: i[1] = 1 = in0 is the encoded host source, decoded in the
//                            kernel (Color Only plan; alpha = in0.w, in2 unused).
//   watermark:               in0 = colour result (dst size) -> DST; f[0] = pixelScale, f[1..4] and
//                            i[0] from lic::fillWatermarkArgs (opacity, size, reference white of
//                            the output transfer SDR/PQ/HLG, band; row 0 = bottom, OFX convention).
// The f[] / i[] of every texture kernel are documented above its body in TextureKernels.h.
//
// Buffers: the builder allocates virtual buffers and then maps them onto a minimal set of
// physical buffers (interval allocation per size; a pass never writes a buffer it reads).
#include "Engine.h"
#include "../texture/TexturePlan.h"
#include "../texture/TextureDesign.h"
#include "../license/Watermark.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <map>
#include <sstream>

namespace cel { namespace engine {

const char* kernelName(KernelId id) {
    static const char* const names[] = {
#define CEL_KERNEL(name) #name,
#include "../kernels/KernelList.h"
#undef CEL_KERNEL
    };
    int i = int(id);
    if (i < 0 || i >= int(KID_COUNT)) return "?";
    return names[i];
}

namespace {

class Builder {
public:
    Builder(int w, int h) { m_vb.push_back({w, h}); m_vb.push_back({w, h}); }

    int buffer(int w, int h) { m_vb.push_back({std::max(1, w), std::max(1, h)}); return int(m_vb.size()) - 1; }
    int width(int b) const { return m_vb[size_t(b)].w; }
    int height(int b) const { return m_vb[size_t(b)].h; }

    PassDesc& pass(KernelId k, int out, std::initializer_list<int> ins) {
        PassDesc p;
        p.kernel = k;
        p.out = out;
        int n = 0;
        for (int b : ins) { if (n < CEL_PASS_MAX_INPUTS) p.in[n++] = b; }
        std::memset(&p.args, 0, sizeof(p.args));
        m_passes.push_back(p);
        return m_passes.back();
    }
    bool empty() const { return m_passes.empty(); }
    // redirect the last pass to write `target` (used to make the final pass write DST)
    bool redirectLast(int from, int target) {
        if (m_passes.empty() || m_passes.back().out != from) return false;
        m_passes.back().out = target;
        return true;
    }

    Plan finish() {
        Plan plan;
        plan.buffers.push_back({m_vb[0].w, m_vb[0].h});
        plan.buffers.push_back({m_vb[1].w, m_vb[1].h});
        const int nv = int(m_vb.size());
        std::vector<int> last(size_t(nv), -1), phys(size_t(nv), -1);
        phys[BUF_SRC] = BUF_SRC; phys[BUF_DST] = BUF_DST;
        for (int p = 0; p < int(m_passes.size()); ++p) {
            const PassDesc& d = m_passes[size_t(p)];
            last[size_t(d.out)] = std::max(last[size_t(d.out)], p);
            for (int k = 0; k < CEL_PASS_MAX_INPUTS; ++k)
                if (d.in[k] >= 0) last[size_t(d.in[k])] = std::max(last[size_t(d.in[k])], p);
        }
        std::map<std::pair<int, int>, std::vector<int>> freeList;   // size -> free physical ids
        for (int p = 0; p < int(m_passes.size()); ++p) {
            PassDesc d = m_passes[size_t(p)];
            int o = d.out;
            if (o >= 2 && phys[size_t(o)] < 0) {
                auto& fl = freeList[{m_vb[size_t(o)].w, m_vb[size_t(o)].h}];
                if (!fl.empty()) { phys[size_t(o)] = fl.back(); fl.pop_back(); }
                else {
                    phys[size_t(o)] = int(plan.buffers.size());
                    plan.buffers.push_back({m_vb[size_t(o)].w, m_vb[size_t(o)].h});
                }
            }
            d.out = phys[size_t(o)];
            for (int k = 0; k < CEL_PASS_MAX_INPUTS; ++k)
                if (d.in[k] >= 0) d.in[k] = phys[size_t(d.in[k])];
            // sizes (backends refresh them; host strides are only known at execute time)
            const BufferDesc& ob = plan.buffers[size_t(d.out)];
            d.args.outW = ob.w; d.args.outH = ob.h; d.args.outStride = ob.w * 4;
            for (int k = 0; k < CEL_PASS_MAX_INPUTS; ++k) {
                if (d.in[k] >= 0) {
                    const BufferDesc& ib = plan.buffers[size_t(d.in[k])];
                    d.args.inW[k] = ib.w; d.args.inH[k] = ib.h; d.args.inStride[k] = ib.w * 4;
                } else {
                    d.args.inW[k] = d.args.inH[k] = d.args.inStride[k] = 0;
                }
            }
            plan.passes.push_back(d);
            // release buffers whose last use is this pass (after the output was assigned, so a
            // pass never writes one of its own inputs)
            auto release = [&](int v) {
                if (v >= 2 && last[size_t(v)] == p && phys[size_t(v)] >= 2) {
                    freeList[{m_vb[size_t(v)].w, m_vb[size_t(v)].h}].push_back(phys[size_t(v)]);
                    last[size_t(v)] = -2;                     // released once
                }
            };
            const PassDesc& orig = m_passes[size_t(p)];
            for (int k = 0; k < CEL_PASS_MAX_INPUTS; ++k) if (orig.in[k] >= 0) release(orig.in[k]);
            release(orig.out);
        }
        return plan;
    }

private:
    struct VB { int w, h; };
    std::vector<VB> m_vb;
    std::vector<PassDesc> m_passes;
};

int bitsToInt(uint32_t u) { int i; std::memcpy(&i, &u, sizeof(i)); return i; }
bool finitePos(float v) { return std::isfinite(v) && v > 0.f; }

void setBlurWeights(PassDesc& p, const tex::Gauss1D g[4], int dir, bool xOnly) {
    int R = 0;
    for (int c = 0; c < 4; ++c) {
        R = std::max(R, g[c].radius);
        for (int k = 0; k < 8; ++k) p.args.f[c * 8 + k] = g[c].w[k];
    }
    p.args.i[0] = R;
    p.args.i[1] = dir;
    p.args.i[2] = xOnly ? 1 : 0;
}
int maxRadius(const tex::Gauss1D g[4]) {
    return std::max(std::max(g[0].radius, g[1].radius), std::max(g[2].radius, g[3].radius));
}
// separable blur on buffer `src`; returns the blurred buffer (or src when the kernel is identity)
int addBlur(Builder& B, int src, const tex::Gauss1D g[4], bool xOnly = false) {
    if (maxRadius(g) == 0) return src;
    int w = B.width(src), h = B.height(src);
    int t = B.buffer(w, h);
    setBlurWeights(B.pass(KID_tx_blur4, t, {src}), g, 0, xOnly);
    int o = B.buffer(w, h);
    setBlurWeights(B.pass(KID_tx_blur4, o, {t}), g, 1, xOnly);
    return o;
}
// vertical half of a separable B-spline upsample to `targetH` rows: row y samples src at
// (y + 0.5) * scale + offset (consumers then sample columns with 4 taps)
int addUpV(Builder& B, int src, int targetH, double scale, double offset) {
    int o = B.buffer(B.width(src), targetH);
    PassDesc& p = B.pass(KID_tx_up_v, o, {src});
    p.args.f[0] = float(scale);
    p.args.f[1] = float(offset);
    return o;
}

void setVignette(float* f, float amount, int w, int h) {
    f[0] = std::min(1.f, std::max(0.f, amount));
    f[1] = tex::kVignetteCornerTan;
    f[2] = 0.5f * float(w);
    f[3] = 0.5f * float(h);
    f[4] = float(1.0 / (0.5 * std::sqrt(double(w) * w + double(h) * h)));
}

bool weaveOn(const tex::TextureParams& tp) { return finitePos(tp.weaveAmount); }
bool acutOn(const tex::TextureParams& tp) { return std::isfinite(tp.acutanceAmount) && std::fabs(tp.acutanceAmount) > 1e-4f; }
bool halOn(const tex::TextureParams& tp) { return finitePos(tp.halationAmount); }
bool bloomFilterOn(const tex::TextureParams& tp) { return tex::bloomFilterFraction(tp.bloomFilter, tp.bloomFilterGrade) > 0.0; }
// the bloom runs when the classic amount is non-zero or (v1.3) a diffusion filter is chosen; the Plugin
// zeroes bloomAmount when the module is off and then also passes bloomFilter 0
bool bloomOn(const tex::TextureParams& tp) { return tp.bloomFilter > 0 ? bloomFilterOn(tp) : finitePos(tp.bloomAmount); }
bool sharpRemovalOn(const tex::TextureParams& tp, float ps) {
    return std::isfinite(tp.sharpenRemoval) && tp.sharpenRemoval > 1e-4f && tex::kSharpRemovalSigma1920 * ps >= 0.25;
}
bool vigOn(const tex::TextureParams& tp) { return finitePos(tp.vignette); }
bool mechVigOn(const tex::TextureParams& tp) { return finitePos(tp.vignetteMech); }   // Lab & Gate
// Lab & Gate film gate: the camera gate masks the scene light first (before the weave, so the frame line
// weaves with the picture and halates / grains like it); the printer gate is a density in the damage field
bool cameraGateOn(const tex::TextureParams& tp) { return tp.gateEnabled && tp.gateSource != int(tex::GateSource::Printer); }
bool printerGateOn(const tex::TextureParams& tp) { return tp.gateEnabled && tp.gateSource == int(tex::GateSource::Printer); }
void setGateArgs(PassDesc& p, const tex::GateDesign& gd, int mode, bool hasIn) {
    for (int k = 0; k < 32; ++k) p.args.f[k] = gd.f[k];
    p.args.i[0] = mode;
    p.args.i[1] = hasIn ? 1 : 0;
    p.args.i[2] = mode == 4 ? gd.edge : 0;   // v1.6 Film Edge (clean border only)
    p.args.i[3] = gd.frameNumber;
}
// printer gate: the damage field (or a new one) with the unexposed print border's density added
int addPrinterGate(Builder& B, int damage, const tex::TextureParams& tp, int w, int h, float ps) {
    if (!printerGateOn(tp)) return damage;
    const tex::GateDesign gd = tex::designGate(tp, w, h, ps);
    if (!gd.on) return damage;
    const int o = B.buffer(w, h);
    setGateArgs(damage >= 0 ? B.pass(KID_tx_gate, o, {damage}) : B.pass(KID_tx_gate, o, {}), gd, 1, damage >= 0);
    return o;
}
// clean film gate border: the bare border (no light, plus the printer gate's border density) rendered by the same
// colour pass as a 1 x 1 reference, laid over the finished frame outside the gate (tx_gate mode 4), so no grain,
// glow, damage, weave or film blur sits on top of it. `printer` = the printer gate is on the colour pass's path.
int addCleanGate(Builder& B, int src, int target, const tex::TextureParams& tp, KernelId colorK, bool printer,
                 int w, int h, float ps) {
    const tex::GateDesign gd = tex::designGate(tp, w, h, ps);
    if (!gd.on) return src;
    auto constant = [&](float r, float g, float b, float al) {
        int o = B.buffer(1, 1);
        PassDesc& p = B.pass(KID_tx_gate, o, {});
        setGateArgs(p, gd, 3, false);
        p.args.f[24] = r; p.args.f[25] = g; p.args.f[26] = b; p.args.f[27] = al;
        return o;
    };
    const int black = constant(0.f, 0.f, 0.f, 1.f);
    const int dens = printer ? constant(gd.f[6], 0.f, 0.f, 0.f) : -1;
    const int ref = B.buffer(1, 1);
    B.pass(colorK, ref, {black, -1, -1, dens});
    if (gd.edge == 0) {
        setGateArgs(B.pass(KID_tx_gate, target, {src, ref}), gd, 4, true);
        return target;
    }
    // v1.6 Film Edge: perforations pass the scanner's light unfiltered (the brightest white of the rendering);
    // the edge print is a warm exposure on the rebate. Rendered by the same colour pass so they follow the look and
    // the output (HDR included); Texture Only has no film, so they are display values there.
    int hole, text;
    if (colorK == KID_color) {
        hole = B.buffer(1, 1);
        B.pass(colorK, hole, {constant(64.f, 64.f, 64.f, 1.f), -1, -1, -1});
        text = B.buffer(1, 1);
        B.pass(colorK, text, {constant(2.4f, 1.3f, 0.45f, 1.f), -1, -1, -1});
    } else {
        hole = constant(0.92f, 0.92f, 0.92f, 1.f);
        text = constant(0.86f, 0.58f, 0.26f, 1.f);
    }
    setGateArgs(B.pass(KID_tx_gate, target, {src, ref, hole, text}), gd, 4, true);
    return target;
}
bool auraOn(const tex::TextureParams& tp) { return tp.auraEnabled && finitePos(tp.auraStrength); }

// Image texture chain; returns the buffer holding the result (== img when nothing is on).
void setToneMap(float* f, const tex::ToneMap& tm) {
    f[0] = tm.x0; f[1] = tm.p; f[2] = tm.yk; f[3] = tm.xk; f[4] = tm.slope; f[5] = tm.sdr;
}

// dr = display-referred input (Texture-Only); hdr = the output is PQ/HLG (values > 1 legal)
int addImageTexture(Builder& B, int img, const tex::TextureParams& tp, bool dr, bool hdr, int w, int h, float ps) {
    int cur = img;
    const tex::ToneMap tm = tex::toneMap(!hdr);
    // ---- gate weave
    if (weaveOn(tp)) {
        tex::WeaveOffset wo = tex::weaveOffset(tp.time, tp.weaveAmount, tp.weaveSpeed, tp.weaveStyle);
        int o = B.buffer(w, h);
        PassDesc& p = B.pass(KID_tx_weave, o, {cur});
        p.args.f[0] = float(wo.dx * ps);
        p.args.f[1] = float(wo.dy * ps);
        p.args.f[2] = float(std::cos(wo.rot));
        p.args.f[3] = float(std::sin(wo.rot));
        p.args.f[4] = 0.5f * float(w);
        p.args.f[5] = 0.5f * float(h);
        cur = o;
    }
    // ---- Lab & Gate: camera gate (the aperture holds back the scene light; the clear negative prints black).
    // After the weave: the picture weaves under a steady frame line, and nothing outside the gate feeds the glow
    if (cameraGateOn(tp)) {
        const tex::GateDesign gd = tex::designGate(tp, w, h, ps);
        if (gd.on) {
            int o = B.buffer(w, h);
            setGateArgs(B.pass(KID_tx_gate, o, {cur}), gd, 0, true);
            cur = o;
        }
    }
    // ---- v1.10 Light Leaks: stray light on the negative, added to the scene light after the weave and the
    // gate (it moves with the film, and the clean gate border stays clean); it then halates and grains like light
    {
        const tex::LightLeak lk = tex::lightLeakAt(tp, w, h);
        if (lk.n > 0) {
            int o = B.buffer(w, h);
            PassDesc& p = B.pass(KID_tx_leak, o, {cur});
            for (int j = 0; j < lk.n; ++j)
                for (int c = 0; c < 7; ++c) p.args.f[7 * j + c] = lk.g[j][c];
            p.args.i[0] = lk.n;
            p.args.i[1] = dr ? 1 : 0;
            cur = o;
        }
    }
    const bool hal = halOn(tp), bloom = bloomOn(tp);
    // Texture-Only: acutance and glow work in the film-like pseudo scene (log exposure semantics
    // and display-referred thresholds); the way back goes through the same curve, which for SDR
    // outputs never exceeds 1.0.
    const bool pseudo = dr && (acutOn(tp) || hal || bloom || tex::designMtf(tp, ps).on || sharpRemovalOn(tp, ps));
    if (pseudo) {
        int o = B.buffer(w, h);
        PassDesc& p = B.pass(KID_tx_tonemap, o, {cur});
        setToneMap(p.args.f, tm);
        p.args.i[0] = 0;
        cur = o;
    }
    // ---- v1.3 Digital Sharpening Removal (before the film MTF): the inverse of the camera's unsharp mask,
    // in log luminance at the camera's pixel scale (tx_acut_apply with a negative, nearly linear gain)
    if (sharpRemovalOn(tp, ps)) {
        tex::AcutDesign ad = tex::designAcutance(tex::kSharpRemovalSigma1920 * ps);
        tex::Gauss1D g[4] = {ad.g, tex::Gauss1D{}, tex::Gauss1D{}, tex::Gauss1D{}};
        g[1].w[0] = g[2].w[0] = g[3].w[0] = 0.f;
        int t = B.buffer(w, h);
        PassDesc& ph = B.pass(KID_tx_blur4, t, {cur});
        setBlurWeights(ph, g, 0, true);
        ph.args.i[6] = 1;
        ph.args.f[8] = ad.eps;
        int blurred = B.buffer(w, h);
        setBlurWeights(B.pass(KID_tx_blur4, blurred, {t}), g, 1, true);
        int o = B.buffer(w, h);
        PassDesc& p = B.pass(KID_tx_acut_apply, o, {cur, blurred});
        const float k = 4.f;   // log2 units: linear for every real halo (|d| << k)
        p.args.f[0] = -std::min(1.5f, tp.sharpenRemoval) * tex::kSharpRemovalGain;
        p.args.f[1] = k;
        p.args.f[2] = 1.f;
        p.args.f[3] = ad.eps;
        p.args.f[4] = 1.f;
        p.args.f[5] = 0.f;
        p.args.i[0] = 0;
        cur = o;
    }
    // ---- film resolution (v1.2): negative MTF + Integrate, per channel, alpha kept
    {
        const tex::MtfDesign md = tex::designMtf(tp, ps);
        if (md.on) {
            for (int n = 0; n < md.passes; ++n) {   // v1.3: cascaded when a sigma exceeds the 7-tap kernel
                int t = B.buffer(w, h);
                PassDesc& ph = B.pass(KID_tx_blur4, t, {cur});
                setBlurWeights(ph, md.g, 0, false);
                ph.args.i[5] = 1;
                int o = B.buffer(w, h);
                PassDesc& pv = B.pass(KID_tx_blur4, o, {t});
                setBlurWeights(pv, md.g, 1, false);
                pv.args.i[5] = 1;
                cur = o;
            }
        }
    }
    // ---- acutance (log2 luminance computed on the fly by the first blur / downsample pass)
    if (acutOn(tp)) {
        double r = std::isfinite(tp.acutanceRadius) ? std::max(0.05, double(tp.acutanceRadius)) : 2.0;
        tex::AcutDesign ad = tex::designAcutance(r * ps * tex::gaugeRadiusScale(tp.grainFormat));
        tex::Gauss1D g[4] = {ad.g, tex::Gauss1D{}, tex::Gauss1D{}, tex::Gauss1D{}};
        g[1].w[0] = g[2].w[0] = g[3].w[0] = 0.f;      // unused channels: zero
        int blurred;
        if (ad.level == 0) {
            int t = B.buffer(w, h);
            PassDesc& ph = B.pass(KID_tx_blur4, t, {cur});
            setBlurWeights(ph, g, 0, true);
            ph.args.i[6] = 1;
            ph.args.f[8] = ad.eps;
            blurred = B.buffer(w, h);
            setBlurWeights(B.pass(KID_tx_blur4, blurred, {t}), g, 1, true);
        } else {
            int lb = B.buffer(tex::levelSize(w, 1), tex::levelSize(h, 1));
            PassDesc& pd = B.pass(KID_tx_pyr_down, lb, {cur});
            pd.args.i[0] = 1;
            pd.args.f[0] = ad.eps;
            for (int l = 2; l <= ad.level; ++l) {
                int d = B.buffer(tex::levelSize(w, l), tex::levelSize(h, l));
                B.pass(KID_tx_pyr_down, d, {lb});
                lb = d;
            }
            blurred = addBlur(B, lb, g, true);
            blurred = addUpV(B, blurred, h, std::ldexp(1.0, -ad.level), 0.0);
        }
        int o = B.buffer(w, h);
        PassDesc& p = B.pass(KID_tx_acut_apply, o, {cur, blurred});
        p.args.f[0] = std::min(2.f, std::max(-1.f, tp.acutanceAmount));
        p.args.f[1] = ad.k;
        p.args.f[2] = ad.gmax;
        p.args.f[3] = ad.eps;
        p.args.f[4] = float(std::ldexp(1.0, -ad.level));
        p.args.f[5] = float(0.18 * std::exp2(tex::kAcutToeStops));   // v1.3: weaker in the negative's toe
        p.args.i[0] = ad.level > 0 ? 1 : 0;
        cur = o;
    }
    // ---- halation + bloom (+ vignette fused)
    bool vigDone = false, backToDisplay = false;
    if (hal || bloom) {
        double hr = (std::isfinite(tp.halationRadius) ? std::max(0.1, double(tp.halationRadius)) : 20.0) * ps
                    * tex::gaugeRadiusScale(tp.grainFormat);
        double br = (std::isfinite(tp.bloomRadius) ? std::max(0.1, double(tp.bloomRadius)) : 40.0) * ps;
        tex::HalationLook hl = tex::halationLook(tp.halationAmount, tp.halationThreshold, tp.halationHue, dr);
        // v1.3 P1 bloom threshold (stops over grey -> linear: 0.18 (2^s - 1), 0 = none) and Keep Highlights
        const float bth = bloom && std::isfinite(tp.bloomThreshold) && tp.bloomThreshold > 0.f
                              ? float(0.18 * (std::exp2(double(std::min(8.f, tp.bloomThreshold))) - 1.0)) : 0.f;
        const int bkeep = bloom && std::isfinite(tp.bloomKeep)
                              ? int(std::lround(1000.0 * double(std::min(1.f, std::max(0.f, tp.bloomKeep))))) : 0;
        const int bf = bloom && bloomFilterOn(tp) ? tp.bloomFilter : 0;   // v1.3 Bloom Filter (0 = Classic)
        tex::GlowDesign gd = tex::designGlow(bloom, br, hal, hr, w, h, hl.ring, bf);
        // v1.3 aura: a second, wide base-reflection lobe on the same pyramid (halation channel only)
        const bool aura = hal && auraOn(tp);
        tex::GlowDesign ga;
        if (aura) {
            double ar = (std::isfinite(tp.auraRadius) ? std::max(1.0, double(tp.auraRadius)) : 150.0) * ps
                        * tex::gaugeRadiusScale(tp.grainFormat);
            ga = tex::designAura(ar, w, h);
        }
        const int nDown = std::max(gd.nLevels, ga.nLevels);
        if (gd.nLevels >= 1) {
            std::vector<int> down(size_t(nDown) + 1, -1);
            {   // glow source + level 1 in one pass (tx_hb_prefilter -> tx_pyr_down fused)
                int d1 = B.buffer(tex::levelSize(w, 1), tex::levelSize(h, 1));
                PassDesc& p = B.pass(KID_tx_hb_prefilter_down, d1, {cur});
                p.args.f[0] = hl.threshold; p.args.f[1] = hl.knee; p.args.f[2] = hl.emax;
                p.args.f[3] = tex::kBloomBMax;
                p.args.f[4] = hl.srcW[0]; p.args.f[5] = hl.srcW[1]; p.args.f[6] = hl.srcW[2];
                p.args.f[7] = bth;
                down[1] = d1;
            }
            for (int l = 2; l <= nDown; ++l) {
                int d = B.buffer(tex::levelSize(w, l), tex::levelSize(h, l));
                B.pass(KID_tx_pyr_down, d, {down[size_t(l - 1)]});
                down[size_t(l)] = d;
            }
            int acc = -1, accLevel = 0;
            for (int l = gd.nLevels; l >= 1; --l) {
                const tex::GlowLevel& L = gd.level[size_t(l)];
                if (!L.used) continue;
                tex::Gauss1D g[4] = {L.gBloom, L.gBloom, L.gBloom, L.gHal};
                if (L.wBloom <= 0.f) { for (int c = 0; c < 3; ++c) { g[c] = tex::Gauss1D{}; g[c].w[0] = 0.f; } }
                if (L.wHal <= 0.f) { g[3] = tex::Gauss1D{}; g[3].w[0] = 0.f; }
                int b = addBlur(B, down[size_t(l)], g);
                int A = B.buffer(tex::levelSize(w, l), tex::levelSize(h, l));
                PassDesc& p = B.pass(KID_tx_pyr_up_add, A, {acc, b});
                p.args.f[0] = p.args.f[1] = p.args.f[2] = L.wBloom;
                p.args.f[3] = L.wHal;
                p.args.f[4] = acc >= 0 ? float(std::ldexp(1.0, -(accLevel - l))) : 1.f;
                p.args.i[0] = acc >= 0 ? 1 : 0;
                p.args.i[1] = 1;
                acc = A; accLevel = l;
            }
            int accUp = addUpV(B, acc, h, std::ldexp(1.0, -gd.finest), 0.0);
            int auraUp = -1, auraLevel = 0;
            if (ga.nLevels >= 1) {
                int aacc = -1, aLevel = 0;
                for (int l = ga.nLevels; l >= 1; --l) {
                    const tex::GlowLevel& L = ga.level[size_t(l)];
                    if (!L.used || L.wHal <= 0.f) continue;
                    tex::Gauss1D g[4] = {tex::Gauss1D{}, tex::Gauss1D{}, tex::Gauss1D{}, L.gHal};
                    for (int c = 0; c < 3; ++c) g[c].w[0] = 0.f;
                    int b = addBlur(B, down[size_t(l)], g);
                    int A = B.buffer(tex::levelSize(w, l), tex::levelSize(h, l));
                    PassDesc& p = B.pass(KID_tx_pyr_up_add, A, {aacc, b});
                    p.args.f[0] = p.args.f[1] = p.args.f[2] = 0.f;
                    p.args.f[3] = L.wHal;
                    p.args.f[4] = aacc >= 0 ? float(std::ldexp(1.0, -(aLevel - l))) : 1.f;
                    p.args.i[0] = aacc >= 0 ? 1 : 0;
                    p.args.i[1] = 1;
                    aacc = A; aLevel = l;
                }
                if (aacc >= 0) { auraUp = addUpV(B, aacc, h, std::ldexp(1.0, -aLevel), 0.0); auraLevel = aLevel; }
            }
            int o = B.buffer(w, h);
            PassDesc& p = auraUp >= 0 ? B.pass(KID_tx_hb_composite, o, {cur, accUp, auraUp})
                                      : B.pass(KID_tx_hb_composite, o, {cur, accUp});
            float* f = p.args.f;
            f[0] = !bloom ? 0.f
                   : bf > 0 ? float(tex::bloomFilterFraction(bf, tp.bloomFilterGrade))
                            : std::min(2.f, tp.bloomAmount) * tex::kBloomMixPerAmount;
            f[1] = hl.gain;
            f[2] = hl.tint[0]; f[3] = hl.tint[1]; f[4] = hl.tint[2];
            f[5] = hl.threshold; f[6] = hl.knee; f[7] = hl.emax; f[8] = tex::kBloomBMax;
            f[9] = hl.srcW[0]; f[10] = hl.srcW[1]; f[11] = hl.srcW[2];
            f[12] = float(std::ldexp(1.0, -gd.finest));
            if (vigOn(tp) && !mechVigOn(tp)) { setVignette(f + 13, tp.vignette, w, h); vigDone = true; }
            // setVignette writes f[13..17]: amount, k, cx, cy, 1/halfdiag
            setToneMap(f + 18, tm);
            f[24] = hal ? hl.chroma : 0.f; f[25] = hl.chromaRef;
            p.args.i[0] = dr ? 1 : 0;
            backToDisplay = dr;
            p.args.i[1] = hal ? 1 : 0;
            p.args.i[2] = bloom ? 1 : 0;
            p.args.i[3] = vigDone ? 1 : 0;
            f[31] = bth; p.args.i[6] = bkeep;   // v1.3 P1 bloom threshold / Keep Highlights (0 = as before)
            if (hal && tex::halationChromaActive(tp)) {   // v1.3 chroma halation
                p.args.i[4] = 1;
                f[26] = std::isfinite(tp.halationSaturation) ? std::min(2.f, std::max(0.f, tp.halationSaturation)) : 1.f;
                f[27] = std::isfinite(tp.halationBackground) ? std::min(1.f, std::max(-1.f, tp.halationBackground)) : 0.f;
            }
            if (auraUp >= 0) {                            // v1.3 aura
                p.args.i[5] = 1;
                float st = std::isfinite(tp.auraStrength) ? std::min(2.f, std::max(0.f, tp.auraStrength)) : 0.f;
                f[28] = hl.gain * st * float(tex::kAuraGainPerStrength);
                f[29] = std::isfinite(tp.auraGreen) ? std::min(1.f, std::max(0.f, tp.auraGreen)) : 0.35f;
                f[30] = float(std::ldexp(1.0, -auraLevel));
            }
            cur = o;
        }
    }
    if (pseudo && !backToDisplay) {
        int o = B.buffer(w, h);
        PassDesc& p = B.pass(KID_tx_tonemap, o, {cur});
        setToneMap(p.args.f, tm);
        p.args.i[0] = 1;
        cur = o;
    }
    // ---- vignette (stand-alone)
    if ((vigOn(tp) || mechVigOn(tp)) && !vigDone) {
        int o = B.buffer(w, h);
        PassDesc& p = B.pass(KID_tx_vignette, o, {cur});
        setVignette(p.args.f, vigOn(tp) ? tp.vignette : 0.f, w, h);
        if (mechVigOn(tp)) {   // Lab & Gate mechanical vignette
            p.args.i[0] = 1;
            p.args.f[5] = std::min(1.f, tp.vignetteMech);
            p.args.f[6] = std::isfinite(tp.vignetteMechSize) ? std::min(1.5f, std::max(0.5f, tp.vignetteMechSize)) : 1.f;
            p.args.f[7] = std::isfinite(tp.vignetteMechSoft) ? std::min(1.f, std::max(0.05f, tp.vignetteMechSoft)) : 0.35f;
        }
        cur = o;
    }
    return cur;
}

// one emulsion's band-limited sensitivity field: white noise generated inside the horizontal blur
// Grain white noise is generated inside the horizontal blur (tx_blur4 i[3]) only for short kernels:
// each output texel hashes 2R + 1 noise texels, so for wide kernels a tx_grain_gen pass (one hash set
// per texel, one extra write + read) is cheaper on both the CPU and Apple-class GPUs.
constexpr int kGrainFuseMaxRadius = 2;

// gw / gh: the grid buffer size (>= o.gw x o.gh; the extra right / bottom texels are never read)
int addGrainOctave(Builder& B, const tex::GrainOctave& o, int h, int gw, int gh) {
    int b;
    const int R = maxRadius(o.g);
    if (R == 0) {
        b = B.buffer(gw, gh);
        B.pass(KID_tx_grain_gen, b, {}).args.i[0] = bitsToInt(o.seed);
    } else {
        int t = B.buffer(gw, gh);
        if (R <= kGrainFuseMaxRadius) {
            PassDesc& ph = B.pass(KID_tx_blur4, t, {});
            setBlurWeights(ph, o.g, 0, false);
            ph.args.i[3] = 1;
            ph.args.i[4] = bitsToInt(o.seed);
        } else {
            int n = B.buffer(gw, gh);
            B.pass(KID_tx_grain_gen, n, {}).args.i[0] = bitsToInt(o.seed);
            setBlurWeights(B.pass(KID_tx_blur4, t, {n}), o.g, 0, false);
        }
        b = B.buffer(gw, gh);
        setBlurWeights(B.pass(KID_tx_blur4, b, {t}), o.g, 1, false);
    }
    if (o.spacing > 1.0) b = addUpV(B, b, h, 1.0 / o.spacing, double(o.margin));
    return b;
}

struct GrainGrids { int coarse = -1, fine = -1; tex::GrainDesign d; };
GrainGrids addGrain(Builder& B, const tex::TextureParams& tp, int w, int h, float ps) {
    GrainGrids g;
    g.d = tex::designGrain(tp, w, h, ps);
    const tex::GrainOctave &c = g.d.coarse, &f = g.d.fine;
    // full-res grids of both octaves get one size, so the fine octave reuses the coarse octave's
    // horizontal-pass buffer (bounded memory)
    const bool same = c.spacing <= 1.0 && f.spacing <= 1.0;
    const int gw = std::max(c.gw, f.gw), gh = std::max(c.gh, f.gh);
    g.coarse = addGrainOctave(B, c, h, same ? gw : c.gw, same ? gh : c.gh);
    g.fine = addGrainOctave(B, f, h, same ? gw : f.gw, same ? gh : f.gh);
    return g;
}
void setGrainArgs(PassDesc& p, const tex::GrainDesign& d) {
    for (int k = 0; k < 24; ++k) p.args.f[k] = d.f[k];
    p.args.i[0] = d.i[0];
    for (int k = 3; k < 6; ++k) p.args.i[k] = d.i[k];
    if (d.tonal) {                       // v1.3 tonal balance
        p.args.i[6] = 1;
        p.args.f[30] = d.toneShadow;
        p.args.f[31] = d.toneHigh;
    }
}

bool hdrOutput(const FrameSettings& fs) {
    return fs.look.output == core::OutputSpace::Rec2100_PQ || fs.look.output == core::OutputSpace::Rec2100_HLG;
}

bool grainOn(const FrameSettings& fs) {
    return fs.tex.grainEnabled && std::isfinite(fs.look.grainAmount) && fs.look.grainAmount > 0.f;
}

// film damage field (tx_damage, preceded by its parameter tables); -1 when there is nothing to draw
int addDamage(Builder& B, const tex::TextureParams& tp, int w, int h, float ps) {
    if (!tex::damageActive(tp)) return -1;
    const tex::DamageDesign dd = tex::designDamage(tp, w, h, ps);
    if (!dd.any()) return -1;
    int tables[4] = {-1, -1, -1, -1};
    for (int t = 0; t < 3 && 2 * t < dd.nScratch; ++t) {
        tables[t] = B.buffer(8, 1);
        PassDesc& p = B.pass(KID_tx_params, tables[t], {});
        for (int k = 0; k < 2 && 2 * t + k < dd.nScratch; ++k)
            for (int j = 0; j < 12; ++j) p.args.f[k * 12 + j] = dd.scratch[2 * t + k][j];
    }
    if (dd.nHair > 0) {
        tables[3] = B.buffer(8, 1);
        PassDesc& p = B.pass(KID_tx_params, tables[3], {});
        for (int k = 0; k < dd.nHair; ++k)
            for (int j = 0; j < 16; ++j) p.args.f[k * 16 + j] = dd.hair[k][j];
    }
    const int o = B.buffer(w, h);
    PassDesc& p = B.pass(KID_tx_damage, o, {tables[0], tables[1], tables[2], tables[3]});
    for (int k = 0; k < CEL_PASS_NF; ++k) p.args.f[k] = dd.f[k];
    for (int k = 0; k < CEL_PASS_NI; ++k) p.args.i[k] = dd.i[k];
    return o;
}

// film blur of the encoded result `src` into `target` (see the pass graph above)
void addFilmBlur(Builder& B, int src, int target, const tex::FilmBlurDesign& fb, int w, int h) {
    int cur = B.buffer(w, h);
    B.pass(KID_display_decode, cur, {src});
    for (int l = 1; l <= fb.level; ++l) {
        int d = B.buffer(tex::levelSize(w, l), tex::levelSize(h, l));
        B.pass(KID_tx_pyr_down, d, {cur});
        cur = d;
    }
    tex::Gauss1D g[4] = {fb.g[0], fb.g[1], fb.g[2], fb.g[3]};
    cur = addBlur(B, cur, g);
    if (fb.level > 0) {
        int up = B.buffer(w, h);
        PassDesc& p = B.pass(KID_tx_pyr_up_add, up, {cur, -1});
        p.args.f[4] = float(std::ldexp(1.0, -fb.level));
        p.args.i[0] = 1;
        p.args.i[1] = 0;
        cur = up;
    }
    B.pass(KID_display_finish, target, {cur, -1, BUF_SRC});
}

// watermark of the licensed build (also on the diagnostic views)
void addWatermark(Builder& B, int src, const FrameSettings& fs, float ps) {
    PassDesc& wp = B.pass(KID_watermark, BUF_DST, {src});
    wp.args.f[0] = ps;
    const core::OutputSpace os = fs.look.output;
    lic::fillWatermarkArgs(wp.args, os == core::OutputSpace::Rec2100_PQ ? lic::WatermarkTransfer::PQ
                                    : os == core::OutputSpace::Rec2100_HLG ? lic::WatermarkTransfer::HLG
                                                                            : lic::WatermarkTransfer::SDR);
}

// v1.3 diagnostic views (the modules' "Show" controls): the real texture chain WITH and WITHOUT the
// module, from the same decoded source, compared by tx_diag. Only built when a view is selected, so
// the production plan (buildPlan below) is untouched and costs nothing extra when every view is off.
Plan buildDiagPlan(int w, int h, const FrameSettings& fs) {
    using tex::DiagView;
    const tex::TextureParams& tp = fs.tex;
    const float ps = tex::effectivePixelScale(tp, w);
    const bool dr = fs.mode == core::Mode::TextureOnly;
    const bool hdr = hdrOutput(fs);
    const DiagView v = DiagView(tp.diagView);
    Builder B(w, h);
    int src = B.buffer(w, h);
    B.pass(dr ? KID_display_decode : KID_decode, src, {BUF_SRC});
    const int out = fs.watermark ? B.buffer(w, h) : int(BUF_DST);
    // the colour pass (film rendering, or Texture-Only re-encode) of `img`, with or without grain / damage
    auto colour = [&](int img, bool withGrain, bool withDamage) {
        GrainGrids g;
        if (withGrain && grainOn(fs)) g = addGrain(B, tp, w, h, ps);
        const int dmg = withDamage ? addPrinterGate(B, addDamage(B, tp, w, h, ps), tp, w, h, ps) : -1;
        int o = B.buffer(w, h);
        PassDesc& cp = B.pass(dr ? KID_display_finish : KID_color, o, {img, g.coarse, g.fine, dmg});
        if (g.coarse >= 0) {
            setGrainArgs(cp, g.d);
            if (dr) setToneMap(cp.args.f + 24, tex::toneMap(!hdr));
        }
        return o;
    };
    tex::TextureParams off = tp;
    off.diagView = 0; off.diagWipe = 0.f;
    int a = -1, b = -1, mode = 0;
    float gain = 1.f, eps = 0.f;
    PassDesc* dp = nullptr;
    switch (v) {
        case DiagView::FilmResolution:      // detail removed: without minus with (log luminance), x4
            off.filmResolution = false; off.integrate = 0.f; off.sharpenRemoval = 0.f;
            a = addImageTexture(B, src, off, dr, hdr, w, h, ps);
            b = addImageTexture(B, src, tp, dr, hdr, w, h, ps);
            mode = 1; gain = 4.f; eps = dr ? 0.01f : 0.002f;
            break;
        case DiagView::Acutance:            // edge contrast added (+) / taken (-), log luminance, x4
            off.acutanceAmount = 0.f;
            a = addImageTexture(B, src, tp, dr, hdr, w, h, ps);
            b = addImageTexture(B, src, off, dr, hdr, w, h, ps);
            mode = 1; gain = 4.f; eps = dr ? 0.01f : 0.002f;
            break;
        case DiagView::HalationOnly:        // the added halation (and aura) light, x8 on black
            off.halationAmount = 0.f;
            a = addImageTexture(B, src, tp, dr, hdr, w, h, ps);
            b = addImageTexture(B, src, off, dr, hdr, w, h, ps);
            mode = 0; gain = 8.f;
            break;
        case DiagView::BloomOnly:           // where bloom / a filter brightens (shadow lift) or takes light, in stops
            off.bloomAmount = 0.f; off.bloomFilter = 0;
            a = addImageTexture(B, src, tp, dr, hdr, w, h, ps);
            b = addImageTexture(B, src, off, dr, hdr, w, h, ps);
            mode = 1; gain = 0.5f; eps = dr ? 0.01f : 0.002f;
            break;
        case DiagView::HalationMask: {
            const tex::HalationLook hl = tex::halationLook(tp.halationAmount, tp.halationThreshold, tp.halationHue, dr);
            dp = &B.pass(KID_tx_diag, out, {src});
            dp->args.i[0] = 3;
            dp->args.f[2] = hl.threshold; dp->args.f[3] = hl.knee; dp->args.f[4] = hl.emax;
            dp->args.f[5] = hl.srcW[0]; dp->args.f[6] = hl.srcW[1]; dp->args.f[7] = hl.srcW[2];
            break;
        }
        case DiagView::GrainOnly: {         // the grain of the colour pass, around mid grey, x4
            const int img = addImageTexture(B, src, tp, dr, hdr, w, h, ps);
            a = colour(img, true, false);
            b = colour(img, false, false);
            mode = 2; gain = 4.f;
            break;
        }
        case DiagView::GateMask: {          // Lab & Gate: the gate's transmission, white = picture
            dp = &B.pass(KID_tx_gate, out, {src});
            setGateArgs(*dp, tex::designGate(tp, w, h, ps), 2, true);
            break;
        }
        case DiagView::TextureDiff:         // final minus the texture-off picture, x4
        default: {
            const int img = addImageTexture(B, src, tp, dr, hdr, w, h, ps);
            a = colour(img, true, true);
            b = colour(src, false, false);
            mode = 2; gain = 4.f;
            break;
        }
    }
    if (!dp) {
        dp = &B.pass(KID_tx_diag, out, {a, b});
        dp->args.i[0] = mode;
        dp->args.f[0] = gain;
        dp->args.f[1] = eps;
    }
    if (fs.watermark) addWatermark(B, out, fs, ps);
    return B.finish();
}

} // namespace

Plan buildPlan(int w, int h, const FrameSettings& fs, const core::Pipeline& pipe) {
    (void)pipe;   // colour state is passed to the backend separately (Pipeline::state())
    w = std::max(1, w); h = std::max(1, h);
    const tex::TextureParams& tp = fs.tex;
    if (fs.mode != core::Mode::ColorOnly && tp.diagView > 0 && tp.diagView < int(tex::DiagView::Count))
        return buildDiagPlan(w, h, fs);   // v1.3 diagnostic view (a module's "Show" control)
    const float ps = tex::effectivePixelScale(tp, w);
    const bool wipe = std::isfinite(tp.diagWipe) && tp.diagWipe > 0.f;   // v1.3 before / after wipe
    Builder B(w, h);
    int img = -1, damage = -1;
    GrainGrids grain;
    KernelId colorK = KID_color;
    switch (fs.mode) {
        case core::Mode::ColorOnly: {
            img = int(BUF_SRC);   // color decodes the source itself (i[1] = 1): one full-res pass less
            break;
        }
        case core::Mode::TextureOnly: {
            int dl = B.buffer(w, h);
            B.pass(KID_display_decode, dl, {BUF_SRC});
            img = addImageTexture(B, dl, tp, true, hdrOutput(fs), w, h, ps);
            if (grainOn(fs)) grain = addGrain(B, tp, w, h, ps);
            damage = addPrinterGate(B, addDamage(B, tp, w, h, ps), tp, w, h, ps);
            colorK = KID_display_finish;
            break;
        }
        case core::Mode::Full:
        default: {
            int sc = B.buffer(w, h);
            B.pass(KID_decode, sc, {BUF_SRC});
            img = addImageTexture(B, sc, tp, false, hdrOutput(fs), w, h, ps);
            if (grainOn(fs)) grain = addGrain(B, tp, w, h, ps);
            damage = addDamage(B, tp, w, h, ps);
            // Lab & Gate printer gate: a print exists only on the print path (not at the negative stage exits)
            if (fs.look.stage != int(core::Stage::NegativeLogScan) && fs.look.stage != int(core::Stage::NegativeToScene))
                damage = addPrinterGate(B, damage, tp, w, h, ps);
            break;
        }
    }
    const bool fusedDecode = fs.mode == core::Mode::ColorOnly;
    // v1.3 stage exits Log Scan / Negative -> Scene end before the projector: no projection focus (film blur)
    const bool beforeProjection = fs.mode != core::Mode::TextureOnly &&
                                  (fs.look.stage == int(core::Stage::NegativeLogScan) || fs.look.stage == int(core::Stage::NegativeToScene));
    const tex::FilmBlurDesign fb = (fusedDecode || beforeProjection) ? tex::FilmBlurDesign{} : tex::designFilmBlur(tp, w, h, ps);
    // Lab & Gate: the clean gate border goes over the finished frame (printer gate only on the print path)
    const bool printPath = fs.mode == core::Mode::TextureOnly ||
                           (fs.look.stage != int(core::Stage::NegativeLogScan) && fs.look.stage != int(core::Stage::NegativeToScene));
    const bool cleanGate = fs.mode != core::Mode::ColorOnly && (cameraGateOn(tp) || (printerGateOn(tp) && printPath)) &&
                           tex::designGate(tp, w, h, ps).on;
    int colorOut = (fs.watermark || fb.on || wipe || cleanGate) ? B.buffer(w, h) : int(BUF_DST);
    PassDesc& cp = fusedDecode ? B.pass(colorK, colorOut, {img, -1, -1})
                               : B.pass(colorK, colorOut, {img, grain.coarse, grain.fine, damage});
    if (grain.coarse >= 0) {
        setGrainArgs(cp, grain.d);
        if (colorK == KID_display_finish) setToneMap(cp.args.f + 24, tex::toneMap(!hdrOutput(fs)));
    }
    cp.args.i[1] = fusedDecode ? 1 : 0;
    int finalOut = colorOut;
    if (fb.on) {
        finalOut = (fs.watermark || wipe || cleanGate) ? B.buffer(w, h) : int(BUF_DST);
        addFilmBlur(B, colorOut, finalOut, fb, w, h);
    }
    if (cleanGate) {
        const int go = (fs.watermark || wipe) ? B.buffer(w, h) : int(BUF_DST);
        finalOut = addCleanGate(B, finalOut, go, tp, colorK, printerGateOn(tp), w, h, ps);
    }
    if (wipe) {   // the source (before) left of the line, the result (after) right of it
        // 1.4: "before" is the camera source through the ALEXA Rec.709 rendering in the output space, not the log
        // source (Texture Only works on a display-referred source, which is already a viewable before)
        int before = int(BUF_SRC);
        if (fs.mode != core::Mode::TextureOnly && fs.look.stage != int(core::Stage::PrintOnly)) {
            before = B.buffer(w, h);
            B.pass(KID_color, before, {int(BUF_SRC), -1, -1}).args.i[1] = 2;
        }
        const int wo = fs.watermark ? B.buffer(w, h) : int(BUF_DST);
        PassDesc& dp = B.pass(KID_tx_diag, wo, {finalOut, before});
        dp.args.i[0] = 4;
        dp.args.f[2] = std::min(1.f, tp.diagWipe);
        finalOut = wo;
    }
    if (fs.watermark) addWatermark(B, finalOut, fs, ps);
    return B.finish();
}

std::string Plan::debugString() const {
    std::ostringstream os;
    os << "Plan: " << buffers.size() << " buffers, " << passes.size() << " passes\n";
    size_t bytes = 0;
    for (size_t i = 0; i < buffers.size(); ++i) {
        os << "  buf " << i << (i == BUF_SRC ? " (src)" : i == BUF_DST ? " (dst)" : "") << ": "
           << buffers[i].w << "x" << buffers[i].h << "\n";
        if (i >= 2) bytes += size_t(buffers[i].w) * size_t(buffers[i].h) * 16u;
    }
    os << "  internal memory: " << (bytes / (1024.0 * 1024.0)) << " MiB\n";
    for (size_t p = 0; p < passes.size(); ++p) {
        const PassDesc& d = passes[p];
        os << "  " << p << ": " << kernelName(d.kernel) << " -> " << d.out << " (" << d.args.outW << "x"
           << d.args.outH << ") in[";
        for (int k = 0; k < CEL_PASS_MAX_INPUTS; ++k) os << (k ? "," : "") << d.in[k];
        os << "]";
        int lastF = -1, lastI = -1;
        for (int k = 0; k < CEL_PASS_NF; ++k) if (d.args.f[k] != 0.f) lastF = k;
        for (int k = 0; k < CEL_PASS_NI; ++k) if (d.args.i[k] != 0) lastI = k;
        if (lastF >= 0) {
            os << " f[";
            for (int k = 0; k <= lastF; ++k) { char b[32]; std::snprintf(b, sizeof b, "%s%.6g", k ? " " : "", double(d.args.f[k])); os << b; }
            os << "]";
        }
        if (lastI >= 0) {
            os << " i[";
            for (int k = 0; k <= lastI; ++k) os << (k ? " " : "") << d.args.i[k];
            os << "]";
        }
        os << "\n";
    }
    return os.str();
}

}} // namespace cel::engine

namespace cel { namespace tex {

float effectivePixelScale(const TextureParams& tp, int w) {
    if (std::isfinite(tp.pixelScale) && tp.pixelScale > 1e-4f && tp.pixelScale < 1e4f) return tp.pixelScale;
    return float(std::max(1, w)) / 1920.f;
}

engine::Plan buildTexturePlan(int w, int h, const TextureParams& tp, bool displayReferred, bool hdrOutput) {
    using namespace engine;
    w = std::max(1, w); h = std::max(1, h);
    Builder B(w, h);
    int r = addImageTexture(B, BUF_SRC, tp, displayReferred, hdrOutput, w, h, effectivePixelScale(tp, w));
    if (r == BUF_SRC || !B.redirectLast(r, BUF_DST)) B.pass(KID_tx_copy, BUF_DST, {r});
    return B.finish();
}

engine::Plan buildDamagePlan(int w, int h, const TextureParams& tp) {
    using namespace engine;
    w = std::max(1, w); h = std::max(1, h);
    Builder B(w, h);
    int r = addDamage(B, tp, w, h, effectivePixelScale(tp, w));
    if (r < 0) {   // nothing to draw: a zero field (tx_damage with no dust, scratches or hairs)
        r = B.buffer(w, h);
        PassDesc& p = B.pass(KID_tx_damage, r, {});
        const DamageDesign dd = designDamage(TextureParams{}, w, h, effectivePixelScale(tp, w));
        for (int k = 0; k < CEL_PASS_NF; ++k) p.args.f[k] = dd.f[k];
    }
    B.redirectLast(r, BUF_DST);
    return B.finish();
}

engine::Plan buildGrainPlan(int w, int h, const TextureParams& tp, float stops) {
    using namespace engine;
    w = std::max(1, w); h = std::max(1, h);
    Builder B(w, h);
    GrainGrids g = addGrain(B, tp, w, h, effectivePixelScale(tp, w));
    PassDesc& p = B.pass(KID_tx_grain_finish, BUF_DST, {g.coarse, g.fine});
    setGrainArgs(p, g.d);
    p.args.f[24] = std::isfinite(stops) ? stops : 0.f;
    return B.finish();
}

}} // namespace cel::tex
