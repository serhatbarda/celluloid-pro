// Celluloid Pro - frame plan builder (texture agent). Implements engine::buildPlan, kernelName and
// Plan::debugString (Engine.h), plus the texture-only plans of src/texture/TexturePlan.h.
//
// PASS GRAPH (SPEC §3).  "full" = frame size w x h, "Ln" = pyramid level n (ceil(w/2^n) x ...).
//
//   Full:        decode(SRC) -> [weave] -> [acutance] -> [glow: halation/bloom (+vignette)]
//                -> [vignette] ; [grain] ; color(img, grain|-, SRC) -> [watermark] -> DST
//   ColorOnly:   color(SRC decoded in the kernel, i[1] = 1) -> [watermark] -> DST
//   TextureOnly: display_decode(SRC) -> [weave] -> [tonemap to pseudo scene] -> [acutance]
//                -> [glow (maps back to display, vignette)] | [tonemap back] -> [vignette] ;
//                [grain] ; display_finish(img, grain|-, SRC) -> [watermark] -> DST
//                (SDR outputs: the curve back to display never exceeds 1.0; HDR: exact inverse)
//
// Texture sub-graphs (each only when its amount is non-zero):
//   weave     tx_weave(full)
//   acutance  tx_acut_luma(full) -> tx_pyr_down x L -> tx_blur4 h, v (L) -> [tx_up_v] -> tx_acut_apply
//             (L = 0 for radius <= 2.33 px, otherwise the level where the residual blur fits)
//   glow      tx_hb_prefilter(full: bloom source + halation energy) -> tx_pyr_down (L1 .. Ln)
//             for every used level from coarse to fine: tx_blur4 h, v -> tx_pyr_up_add
//             (accumulates B-spline upsampled coarser result + weighted level) -> tx_up_v
//             -> tx_hb_composite
//   vignette  fused into tx_hb_composite when the glow runs, else tx_vignette(full)
//   grain     tx_grain_gen(coarse grid, spacing Dc >= 1 px) -> tx_blur4 h, v [-> tx_up_v] ; [fine
//             octave on its own grid Df]
//             -> tx_grain_finish(full)
//
// f[] / i[] OF THE COLOUR AND LICENSE PASSES (kernels owned by core / license):
//   decode, display_decode:  f[] = 0, i[] = 0 (everything comes from `state`).
//   color, display_finish:   i[0] = 1 when in1 holds the grain field (then in1 = grain noise xyz,
//                            unit variance per layer), 0 when no grain (in1 unused: inStride[1] = 0,
//                            inW[1] = inH[1] = 0). in2 = SRC (alpha). f[] = 0.
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
bool bloomOn(const tex::TextureParams& tp) { return finitePos(tp.bloomAmount); }
bool vigOn(const tex::TextureParams& tp) { return finitePos(tp.vignette); }

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
        tex::WeaveOffset wo = tex::weaveOffset(tp.time, tp.weaveAmount, tp.weaveSpeed);
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
    const bool hal = halOn(tp), bloom = bloomOn(tp);
    // Texture-Only: acutance and glow work in the film-like pseudo scene (log exposure semantics
    // and display-referred thresholds); the way back goes through the same curve, which for SDR
    // outputs never exceeds 1.0.
    const bool pseudo = dr && (acutOn(tp) || hal || bloom);
    if (pseudo) {
        int o = B.buffer(w, h);
        PassDesc& p = B.pass(KID_tx_tonemap, o, {cur});
        setToneMap(p.args.f, tm);
        p.args.i[0] = 0;
        cur = o;
    }
    // ---- acutance
    if (acutOn(tp)) {
        double r = std::isfinite(tp.acutanceRadius) ? std::max(0.05, double(tp.acutanceRadius)) : 2.0;
        tex::AcutDesign ad = tex::designAcutance(r * ps);
        int luma = B.buffer(w, h);
        B.pass(KID_tx_acut_luma, luma, {cur}).args.f[0] = ad.eps;
        int lb = luma;
        for (int l = 1; l <= ad.level; ++l) {
            int d = B.buffer(tex::levelSize(w, l), tex::levelSize(h, l));
            B.pass(KID_tx_pyr_down, d, {lb});
            lb = d;
        }
        tex::Gauss1D g[4] = {ad.g, tex::Gauss1D{}, tex::Gauss1D{}, tex::Gauss1D{}};
        g[1].w[0] = g[2].w[0] = g[3].w[0] = 0.f;      // unused channels: zero
        int blurred = addBlur(B, lb, g, true);
        if (ad.level > 0) blurred = addUpV(B, blurred, h, std::ldexp(1.0, -ad.level), 0.0);
        int o = B.buffer(w, h);
        PassDesc& p = B.pass(KID_tx_acut_apply, o, {cur, blurred});
        p.args.f[0] = std::min(2.f, std::max(-1.f, tp.acutanceAmount));
        p.args.f[1] = ad.k;
        p.args.f[2] = ad.gmax;
        p.args.f[3] = ad.eps;
        p.args.f[4] = float(std::ldexp(1.0, -ad.level));
        p.args.i[0] = ad.level > 0 ? 1 : 0;
        cur = o;
    }
    // ---- halation + bloom (+ vignette fused)
    bool vigDone = false, backToDisplay = false;
    if (hal || bloom) {
        double hr = (std::isfinite(tp.halationRadius) ? std::max(0.1, double(tp.halationRadius)) : 20.0) * ps;
        double br = (std::isfinite(tp.bloomRadius) ? std::max(0.1, double(tp.bloomRadius)) : 40.0) * ps;
        tex::GlowDesign gd = tex::designGlow(bloom, br, hal, hr, w, h);
        tex::HalationLook hl = tex::halationLook(tp.halationAmount, tp.halationThreshold, tp.halationHue);
        if (gd.nLevels >= 1) {
            std::vector<int> down(size_t(gd.nLevels) + 1, -1);
            {
                int src = B.buffer(w, h);
                PassDesc& p = B.pass(KID_tx_hb_prefilter, src, {cur});
                p.args.f[0] = hl.threshold; p.args.f[1] = hl.knee; p.args.f[2] = hl.emax;
                p.args.f[3] = tex::kBloomBMax;
                p.args.f[4] = hl.srcW[0]; p.args.f[5] = hl.srcW[1]; p.args.f[6] = hl.srcW[2];
                int d1 = B.buffer(tex::levelSize(w, 1), tex::levelSize(h, 1));
                B.pass(KID_tx_pyr_down, d1, {src});
                down[1] = d1;
            }
            for (int l = 2; l <= gd.nLevels; ++l) {
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
            int o = B.buffer(w, h);
            PassDesc& p = B.pass(KID_tx_hb_composite, o, {cur, accUp});
            float* f = p.args.f;
            f[0] = bloom ? std::min(2.f, tp.bloomAmount) * tex::kBloomMixPerAmount : 0.f;
            f[1] = hl.gain;
            f[2] = hl.tint[0]; f[3] = hl.tint[1]; f[4] = hl.tint[2];
            f[5] = hl.threshold; f[6] = hl.knee; f[7] = hl.emax; f[8] = tex::kBloomBMax;
            f[9] = hl.srcW[0]; f[10] = hl.srcW[1]; f[11] = hl.srcW[2];
            f[12] = float(std::ldexp(1.0, -gd.finest));
            if (vigOn(tp)) { setVignette(f + 13, tp.vignette, w, h); vigDone = true; }
            // setVignette writes f[13..17]: amount, k, cx, cy, 1/halfdiag
            setToneMap(f + 18, tm);
            p.args.i[0] = dr ? 1 : 0;
            backToDisplay = dr;
            p.args.i[1] = hal ? 1 : 0;
            p.args.i[2] = bloom ? 1 : 0;
            p.args.i[3] = vigDone ? 1 : 0;
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
    if (vigOn(tp) && !vigDone) {
        int o = B.buffer(w, h);
        setVignette(B.pass(KID_tx_vignette, o, {cur}).args.f, tp.vignette, w, h);
        cur = o;
    }
    return cur;
}

int addGrainOctave(Builder& B, const tex::GrainOctave& o, int h) {
    int n = B.buffer(o.gw, o.gh);
    B.pass(KID_tx_grain_gen, n, {}).args.i[0] = bitsToInt(o.seed);
    int b = addBlur(B, n, o.g);
    if (o.spacing > 1.0) b = addUpV(B, b, h, 1.0 / o.spacing, double(o.margin));
    return b;
}

int addGrain(Builder& B, const tex::TextureParams& tp, int w, int h, float ps) {
    tex::GrainDesign gd = tex::designGrain(tp, w, h, ps);
    int c = addGrainOctave(B, gd.coarse, h);
    int fn = gd.fine.used ? addGrainOctave(B, gd.fine, h) : -1;
    int o = B.buffer(w, h);
    PassDesc& p = B.pass(KID_tx_grain_finish, o, {c, fn});
    float* f = p.args.f;
    f[0] = float(1.0 / gd.coarse.spacing);
    for (int k = 0; k < 3; ++k) f[1 + k] = float(gd.wCoarse * gd.coarse.norm[k]);
    f[4] = gd.coarse.norm[3];
    f[5] = float(gd.shape.a); f[6] = float(gd.shape.b); f[7] = float(gd.shape.m); f[8] = float(gd.shape.invX);
    f[9] = float(gd.shape.gamma); f[10] = float(gd.shape.t); f[11] = float(gd.shape.kappa * gd.shape.kappa);
    f[12] = float(gd.shape.mu); f[13] = float(gd.shape.invSigma);
    if (gd.fine.used) {
        f[14] = float(1.0 / gd.fine.spacing);
        for (int k = 0; k < 3; ++k) f[15 + k] = float(gd.wFine * gd.fine.norm[k]);
    }
    f[18] = float(gd.coarse.margin);
    f[19] = gd.fine.used ? float(gd.fine.margin) : 0.f;
    p.args.i[0] = gd.coarse.spacing > 1.0 ? 1 : 0;
    p.args.i[1] = gd.mono ? 1 : 0;
    p.args.i[2] = gd.fine.used ? 1 : 0;
    p.args.i[3] = (gd.fine.used && gd.fine.spacing > 1.0) ? 1 : 0;
    return o;
}

bool hdrOutput(const FrameSettings& fs) {
    return fs.look.output == core::OutputSpace::Rec2100_PQ || fs.look.output == core::OutputSpace::Rec2100_HLG;
}

bool grainOn(const FrameSettings& fs) {
    return fs.tex.grainEnabled && std::isfinite(fs.look.grainAmount) && fs.look.grainAmount > 0.f;
}

} // namespace

Plan buildPlan(int w, int h, const FrameSettings& fs, const core::Pipeline& pipe) {
    (void)pipe;   // colour state is passed to the backend separately (Pipeline::state())
    w = std::max(1, w); h = std::max(1, h);
    const tex::TextureParams& tp = fs.tex;
    const float ps = tex::effectivePixelScale(tp, w);
    Builder B(w, h);
    int img = -1, noise = -1;
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
            if (grainOn(fs)) noise = addGrain(B, tp, w, h, ps);
            colorK = KID_display_finish;
            break;
        }
        case core::Mode::Full:
        default: {
            int sc = B.buffer(w, h);
            B.pass(KID_decode, sc, {BUF_SRC});
            img = addImageTexture(B, sc, tp, false, hdrOutput(fs), w, h, ps);
            if (grainOn(fs)) noise = addGrain(B, tp, w, h, ps);
            break;
        }
    }
    int colorOut = fs.watermark ? B.buffer(w, h) : int(BUF_DST);
    const bool fusedDecode = fs.mode == core::Mode::ColorOnly;
    PassDesc& cp = fusedDecode ? B.pass(colorK, colorOut, {img, noise, -1}) : B.pass(colorK, colorOut, {img, noise, BUF_SRC});
    cp.args.i[0] = noise >= 0 ? 1 : 0;
    cp.args.i[1] = fusedDecode ? 1 : 0;
    if (fs.watermark) {
        PassDesc& wp = B.pass(KID_watermark, BUF_DST, {colorOut});
        wp.args.f[0] = ps;
        const core::OutputSpace os = fs.look.output;
        lic::fillWatermarkArgs(wp.args, os == core::OutputSpace::Rec2100_PQ ? lic::WatermarkTransfer::PQ
                                        : os == core::OutputSpace::Rec2100_HLG ? lic::WatermarkTransfer::HLG
                                                                                : lic::WatermarkTransfer::SDR);
    }
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

engine::Plan buildGrainPlan(int w, int h, const TextureParams& tp) {
    using namespace engine;
    w = std::max(1, w); h = std::max(1, h);
    Builder B(w, h);
    int r = addGrain(B, tp, w, h, effectivePixelScale(tp, w));
    B.redirectLast(r, BUF_DST);
    return B.finish();
}

}} // namespace cel::tex
