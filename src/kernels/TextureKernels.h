/* Reafilm Pro - texture kernels (gate weave, acutance, halation, bloom, vignette, grain; film
 * damage in DamageKernels.h, included at the end).
 * Owner: texture agent.  Portable dialect (see Portable.h): compiled as C++17, OpenCL C 1.2,
 * CUDA and Metal.  Uniform ABI (KernelABI.h): every kernel writes ONE output pixel (x, y).
 *
 * Buffer conventions
 *   - all buffers are float RGBA, row stride in floats (a->outStride / a->inStride[k]);
 *   - "full res" = the frame size; pyramid level L has size ceil(w / 2^L) x ceil(h / 2^L);
 *     pixel centres are at (i + 0.5); level-L coordinate = full-res coordinate / 2^L;
 *   - the image passes (weave, acutance, composite, vignette) work on scene-linear Rec.2020
 *     (Full mode) or display-linear (Texture-Only; see tx_tonemap) and pass alpha (w) through;
 *   - unused inputs are flagged with i[] values, never detected through null pointers.
 *
 * Every kernel documents its f[] / i[] usage right above its body.  The values are computed on
 * the host by src/engine/Plan.cpp + src/texture/TextureDesign.cpp (all pixel sizes there are
 * already multiplied by pixelScale, so the kernels never see "px @1920").
 *
 * Robustness: the first pass that touches the image (weave, acut_luma/apply, prefilter,
 * composite, vignette, tonemap) replaces NaN/Inf with 0 and clamps magnitudes to 1e7, so one
 * bad pixel can never spread through a blur pyramid.
 */
#ifndef CEL_TEXTURE_KERNELS_H
#define CEL_TEXTURE_KERNELS_H
#include "KernelABI.h"
#include "GrainKernels.h"

/* ------------------------------------------------------------------------------------------ */
/* helpers                                                                                     */
/* ------------------------------------------------------------------------------------------ */

/* NOTE: helpers use the cel_txh_ prefix and must never be named cel_<kernel name>: the GPU
 * backends generate an entry point cel_<name> for every kernel in the list. */
#define CEL_TX_BIG 1.0e7f
/* CPU build only (GCC/Clang): force inlining of the small per-tap helpers (GCC otherwise keeps
 * some as calls returning structs, which made the 4x4 samplers much slower). GPU compilers
 * inline anyway. */
#if defined(CEL_API_CPU) && (defined(__GNUC__) || defined(__clang__))
  #define CEL_TXH_INLINE __attribute__((always_inline))
#else   /* MSVC: "static inline __forceinline" would warn (C4141); its inliner handles these */
  #define CEL_TXH_INLINE
#endif
/* the uniform ABI hands every kernel all buffers; mark the ones a kernel ignores as used */
#define CEL_TX_ABI_USE() \
    (void)x; (void)y; (void)a; (void)out; (void)in0; (void)in1; (void)in2; (void)in3; (void)state

/* Cheap min/max/floor. cel_fmin/cel_fmax/cel_floor map to libm calls on some CPU compilers
 * (IEEE NaN rules), which dominated the CPU backend's inner loops. These are plain compare/select
 * and NaN-safe by operand order: when `a` is NaN the result is `b` (pass the trusted operand
 * second). cel_txh_floor is exact for |v| < 2^31 (all callers clamp their coordinates first). */
CEL_DEVICE CEL_TXH_INLINE float cel_txh_min(float a, float b) { return a < b ? a : b; }
CEL_DEVICE CEL_TXH_INLINE float cel_txh_max(float a, float b) { return a > b ? a : b; }
CEL_DEVICE CEL_TXH_INLINE float cel_txh_clampf(float v, float lo, float hi) { return cel_txh_min(cel_txh_max(v, lo), hi); }
CEL_DEVICE CEL_TXH_INLINE float cel_txh_floor(float v) { float t = (float)(int)v; return t > v ? t - 1.0f : t; }
CEL_DEVICE CEL_TXH_INLINE int cel_txh_cidx(int i, int n) { return i < 0 ? 0 : (i >= n ? n - 1 : i); }
#define CEL_TX_COORD_MAX 1.0e8f

CEL_DEVICE CEL_TXH_INLINE float cel_txh_clean(float v) { return cel_txh_clampf(cel_finite_or(v, 0.0f), -CEL_TX_BIG, CEL_TX_BIG); }
CEL_DEVICE CEL_TXH_INLINE CelF4 cel_txh_clean4(CelF4 v) {
    return cel_f4(cel_txh_clean(v.x), cel_txh_clean(v.y), cel_txh_clean(v.z), cel_txh_clean(v.w));
}
CEL_DEVICE CEL_TXH_INLINE CelF4 cel_txh_add4(CelF4 a, CelF4 b) { return cel_f4(a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w); }
CEL_DEVICE CEL_TXH_INLINE CelF4 cel_txh_scale4(CelF4 a, float s) { return cel_f4(a.x * s, a.y * s, a.z * s, a.w * s); }
CEL_DEVICE CEL_TXH_INLINE CelF4 cel_txh_madd4(CelF4 acc, CelF4 v, float s) {
    return cel_f4(acc.x + v.x * s, acc.y + v.y * s, acc.z + v.z * s, acc.w + v.w * s);
}
/* per channel min/max; NaN in `a` yields `b` */
CEL_DEVICE CEL_TXH_INLINE CelF4 cel_txh_min4(CelF4 a, CelF4 b) {
    return cel_f4(cel_txh_min(a.x, b.x), cel_txh_min(a.y, b.y), cel_txh_min(a.z, b.z), cel_txh_min(a.w, b.w));
}
CEL_DEVICE CEL_TXH_INLINE CelF4 cel_txh_max4(CelF4 a, CelF4 b) {
    return cel_f4(cel_txh_max(a.x, b.x), cel_txh_max(a.y, b.y), cel_txh_max(a.z, b.z), cel_txh_max(a.w, b.w));
}
/* Rec.2020 luminance (working space of the scene-linear passes) */
CEL_DEVICE CEL_TXH_INLINE float cel_txh_luma(CelF4 v) { return 0.2627f * v.x + 0.6780f * v.y + 0.0593f * v.z; }

/* cubic B-spline weights for taps (i-1, i, i+1, i+2), fractional position t in [0,1) */
CEL_DEVICE CEL_TXH_INLINE CelF4 cel_txh_bspline_w(float t) {
    float t2 = t * t, t3 = t2 * t, u = 1.0f - t;
    return cel_f4(u * u * u * (1.0f / 6.0f),
                  (3.0f * t3 - 6.0f * t2 + 4.0f) * (1.0f / 6.0f),
                  (-3.0f * t3 + 3.0f * t2 + 3.0f * t + 1.0f) * (1.0f / 6.0f),
                  t3 * (1.0f / 6.0f));
}
/* Catmull-Rom weights (interpolating bicubic, a = -0.5) */
CEL_DEVICE CEL_TXH_INLINE CelF4 cel_txh_catmull_w(float t) {
    float t2 = t * t, t3 = t2 * t;
    return cel_f4(0.5f * (-t3 + 2.0f * t2 - t),
                  0.5f * (3.0f * t3 - 5.0f * t2 + 2.0f),
                  0.5f * (-3.0f * t3 + 4.0f * t2 + t),
                  0.5f * (t3 - t2));
}
CEL_DEVICE CEL_TXH_INLINE CelF4 cel_txh_px(CEL_GLOBAL const float* row, int xi) {
    CEL_GLOBAL const float* p = row + xi * 4;
    return cel_f4(p[0], p[1], p[2], p[3]);
}
/* weighted sum of 4 texels of one row at (already clamped) columns c0..c3 */
CEL_DEVICE CEL_TXH_INLINE CelF4 cel_txh_row4c(CEL_GLOBAL const float* row, int c0, int c1, int c2, int c3, CelF4 wx) {
    CelF4 r = cel_txh_scale4(cel_txh_px(row, c0), wx.x);
    r = cel_txh_madd4(r, cel_txh_px(row, c1), wx.y);
    r = cel_txh_madd4(r, cel_txh_px(row, c2), wx.z);
    r = cel_txh_madd4(r, cel_txh_px(row, c3), wx.w);
    return r;
}
/* separable 4x4 filter with top-left tap (x0, y0), weights wx / wy, clamp-to-edge */
CEL_DEVICE CEL_TXH_INLINE CelF4 cel_txh_filter4x4(CEL_GLOBAL const float* buf, int st, int w, int h, int x0, int y0, CelF4 wx, CelF4 wy) {
    int c0 = cel_txh_cidx(x0, w), c1 = cel_txh_cidx(x0 + 1, w), c2 = cel_txh_cidx(x0 + 2, w), c3 = cel_txh_cidx(x0 + 3, w);
    CelF4 r = cel_txh_scale4(cel_txh_row4c(buf + cel_txh_cidx(y0, h) * st, c0, c1, c2, c3, wx), wy.x);
    r = cel_txh_madd4(r, cel_txh_row4c(buf + cel_txh_cidx(y0 + 1, h) * st, c0, c1, c2, c3, wx), wy.y);
    r = cel_txh_madd4(r, cel_txh_row4c(buf + cel_txh_cidx(y0 + 2, h) * st, c0, c1, c2, c3, wx), wy.z);
    r = cel_txh_madd4(r, cel_txh_row4c(buf + cel_txh_cidx(y0 + 3, h) * st, c0, c1, c2, c3, wx), wy.w);
    return r;
}
/* smooth (C2) cubic B-spline reconstruction at continuous coordinate (fx, fy) in the buffer's own
 * pixel units (centres at i + 0.5), clamp-to-edge.  Used for every pyramid upsample: no ringing,
 * no overshoot, no visible grid. */
CEL_DEVICE CEL_TXH_INLINE CelF4 cel_txh_sample_bspline(CEL_GLOBAL const float* buf, int st, int w, int h, float fx, float fy) {
    float sx = cel_txh_clampf(fx - 0.5f, -CEL_TX_COORD_MAX, CEL_TX_COORD_MAX);
    float sy = cel_txh_clampf(fy - 0.5f, -CEL_TX_COORD_MAX, CEL_TX_COORD_MAX);
    float ix = cel_txh_floor(sx), iy = cel_txh_floor(sy);
    return cel_txh_filter4x4(buf, st, w, h, (int)ix - 1, (int)iy - 1,
                             cel_txh_bspline_w(sx - ix), cel_txh_bspline_w(sy - iy));
}

/* horizontal half of the separable B-spline: 4 taps on row y at continuous column fx (pixel
 * units of the buffer). Used on buffers that tx_up_v already upsampled vertically. */
CEL_DEVICE CEL_TXH_INLINE CelF4 cel_txh_sample_bspline_h(CEL_GLOBAL const float* buf, int st, int w, int h, float fx, int y) {
    float sx = cel_txh_clampf(fx - 0.5f, -CEL_TX_COORD_MAX, CEL_TX_COORD_MAX);
    float ix = cel_txh_floor(sx);
    int x0 = (int)ix - 1;
    return cel_txh_row4c(buf + cel_txh_cidx(y, h) * st, cel_txh_cidx(x0, w), cel_txh_cidx(x0 + 1, w),
                         cel_txh_cidx(x0 + 2, w), cel_txh_cidx(x0 + 3, w), cel_txh_bspline_w(sx - ix));
}

/* ---- Texture-Only tone mapping between display-linear and a film-like pseudo scene ---------
 * Hill/Naka-Rushton curve with a gamma-2.4 display encode: v = q/(1+q), q = (x/x0)^p, Y = v^2.4.
 * (x0, p chosen so 18% grey -> v = 0.40 and +6 stops -> v ~ 0.95, i.e. like a print curve.)
 * Below 0 it is the identity. Above Yk (slope-matched, C1):
 *   sdr = 0 (HDR outputs): linear continuation -> exact bijection on all reals (values > 1 kept)
 *   sdr = 1 (SDR outputs): exponential approach to 1.0, Y = 1 - (1 - Yk) exp(-(x - xk) / L),
 *            L = (1 - Yk) slope. Monotonic and never above 1, so texture effects added in the
 *            pseudo scene can never push an SDR output out of range. Display values >= 1 map to
 *            the finite x of Y = 1 - 1e-6 (super-whites are compressed into range). */
#define CEL_TX_SDR_TOP 0.999999f
/* exp(x) - 1 without cancellation near 0 (Kahan's trick, see celc_expm1 in ColorTransfer.h) */
CEL_DEVICE CEL_TXH_INLINE float cel_txh_expm1(float x) {
    float u = cel_exp(x);
    if (!(cel_fabs(x) < 0.5f)) return u - 1.0f;
    if (u == 1.0f) return x;
    return (u - 1.0f) * x / cel_log(u);
}
/* Numerics: both branches avoid 1 - (number close to 1). With v = Y^(1/2.4) = e^w,
 * v / (1 - v) = 1 / expm1(-w); and 1 - u = (1 - Y) / (1 - Yk) (1 - Y is exact for Y >= 0.5).
 * The naive forms amplified a 1-ulp error of pow() up to ~30x near Yk and ~1e4x near display
 * white, which the following blurs spread and the GPU/CPU parity test caught. */
CEL_DEVICE CEL_TXH_INLINE float cel_txh_d2s(float Y, float x0, float p, float Yk, float xk, float slope, float sdr) {
    if (Y <= 0.0f) return Y;
    if (Y >= Yk) {
        if (sdr != 0.0f) {
            float L = (1.0f - Yk) * slope;
            return xk - L * cel_log((1.0f - cel_txh_min(Y, CEL_TX_SDR_TOP)) / (1.0f - Yk));
        }
        return xk + (Y - Yk) * slope;
    }
    float r = cel_txh_expm1(cel_log(Y) * (-1.0f / 2.4f));   /* (1 - v) / v > 0 */
    return x0 * cel_pow(r, -1.0f / p);
}
CEL_DEVICE CEL_TXH_INLINE float cel_txh_s2d(float x, float x0, float p, float Yk, float xk, float slope, float sdr) {
    if (x <= 0.0f) return x;
    if (x >= xk) {
        if (sdr != 0.0f) return 1.0f - (1.0f - Yk) * cel_exp(-(x - xk) / ((1.0f - Yk) * slope));
        return Yk + (x - xk) / slope;
    }
    float q = cel_pow(x / x0, p);
    return cel_pow(q / (1.0f + q), 2.4f);
}

/* ---- halation / bloom sources ---------------------------------------------------------------
 * Halation energy: light that reaches the film base and is reflected back into the (mostly red
 * sensitive) bottom layer.  Weighted towards red light.  T <= 0 (v1.2 default, physical): linear
 * with a highlight emphasis k (linear units, see below), e' = yh / (1 + (k / yh)^2),
 * e = e' emax / (emax + e').  Otherwise (optional gate) above the threshold T the energy is the
 * linear excess (halation is a fixed fraction of the light, so a +8-stop practical carries ~40x the
 * energy of a +4-stop sky) eased in by a knee in STOPS over the threshold:
 *     s = log2(yh / T),   e = (yh - T) * s^2 / (s^2 + k^2)        (k = knee in stops, 2; C1 at s = 0)
 * so broad areas just above the threshold (bright overcast skies) add almost nothing while
 * practicals and speculars keep their full glow; extreme energies are compressed rationally
 * towards emax. */
CEL_DEVICE CEL_TXH_INLINE float cel_txh_hal_energy(CelF4 v, float T, float k, float emax, float wr, float wg, float wb) {
    float yh = cel_txh_max(wr * v.x + wg * v.y + wb * v.z, 0.0f);
    /* v1.2 physical halation (T <= 0): a fixed fraction of the light, no threshold; k > 0 = highlight
     * emphasis in LINEAR units, e = yh / (1 + (k / yh)^2) (full share from about a stop over k, small
     * for skin and mid-tones well under k), so broad mid-bright areas do not tint the dark objects next
     * to them; only extreme energies compressed */
    if (!(T > 0.0f)) {
        float e = yh;
        if (k > 0.0f) { float q = k / cel_txh_max(yh, 1e-20f); e = yh / (1.0f + q * q); }
        return e * emax / (emax + e);
    }
    if (!(yh > T)) return 0.0f;
    float s = cel_log2(yh / T);
    float s2 = s * s;
    float e = (yh - T) * s2 / (s2 + k * k);
    return e * emax / (emax + e);
}
/* Bloom source: the image itself (diffusion is linear), with super-bright values compressed
 * (hue preserving) so a specular sun cannot wash out the whole frame. */
CEL_DEVICE CEL_TXH_INLINE CelF4 cel_txh_bloom_src(CelF4 v, float bmax) {
    float m = cel_txh_max(cel_txh_max(v.x, v.y), cel_txh_max(v.z, 0.0f));
    float s = bmax / (bmax + m);
    return cel_f4(v.x * s, v.y * s, v.z * s, 0.0f);
}
/* v1.3 P1 bloom threshold: the source weighted by m^2 / (m^2 + T^2) of its max channel (hue preserving,
 * soft); T <= 0 = no threshold (exactly the plain source) */
CEL_DEVICE CEL_TXH_INLINE CelF4 cel_txh_bloom_src_t(CelF4 v, float bmax, float T) {
    CelF4 b = cel_txh_bloom_src(v, bmax);
    if (T > 0.0f) {
        float m = cel_txh_max(cel_txh_max(v.x, v.y), cel_txh_max(v.z, 0.0f));
        float k = m * m / (m * m + T * T);
        b = cel_f4(b.x * k, b.y * k, b.z * k, 0.0f);
    }
    return b;
}
/* natural vignette: cos^4 of the field angle, field angle from the distance to the optical centre
 * in units of the half diagonal (circular for every aspect ratio). */
CEL_DEVICE CEL_TXH_INLINE float cel_txh_vig_factor(int x, int y, float amount, float k, float cx, float cy, float invHalfDiag) {
    float dx = ((float)x + 0.5f - cx) * invHalfDiag, dy = ((float)y + 0.5f - cy) * invHalfDiag;
    float t2 = (dx * dx + dy * dy) * k * k;          /* tan^2 of the field angle */
    float c2 = 1.0f / (1.0f + t2);                   /* cos^2 */
    return cel_mix(1.0f, c2 * c2, amount);
}

/* approximately Gaussian, zero-mean, unit-variance white noise from a 32-bit key: sum of the
 * eight bytes of two hashes (Irwin-Hall n = 8, kurtosis 2.85; the following blur makes it
 * Gaussian for all practical purposes). */
CEL_DEVICE CEL_TXH_INLINE float cel_txh_gauss_noise(cel_uint key) {
    cel_uint h1 = cel_hash_u32(key);
    cel_uint h2 = cel_hash_u32(key ^ 0x5bd1e995U);
    cel_uint s = (h1 & 255U) + ((h1 >> 8) & 255U) + ((h1 >> 16) & 255U) + (h1 >> 24)
               + (h2 & 255U) + ((h2 >> 8) & 255U) + ((h2 >> 16) & 255U) + (h2 >> 24);
    return ((float)s - 1020.0f) * (1.0f / 209.0215f);
}

/* the 4 white-noise channels of generation-grid texel (x, y) (tx_grain_gen, and fused into tx_blur4) */
CEL_DEVICE CEL_TXH_INLINE CelF4 cel_txh_grain_white(int x, int y, cel_uint seed) {
    cel_uint base = cel_hash_u32(((cel_uint)x * 0x8da6b343U) ^ cel_hash_u32(((cel_uint)y * 0xd8163841U) ^ seed));
    return cel_f4(cel_txh_gauss_noise(base + 0x9e3779b9U), cel_txh_gauss_noise(base + 0x3c6ef372U),
                  cel_txh_gauss_noise(base + 0xdaa66d2bU), cel_txh_gauss_noise(base + 0x78dde6e4U));
}

/* ------------------------------------------------------------------------------------------ */
/* kernels                                                                                     */
/* ------------------------------------------------------------------------------------------ */

/* tx_copy: out = in0 (same size).  f/i unused. */
CEL_DEVICE void cel_k_tx_copy(CEL_KERNEL_PARAMS) {
    CEL_TX_ABI_USE();
    cel_store(out, a->outStride, x, y, cel_load(in0, a->inStride[0], a->inW[0], a->inH[0], x, y));
}

/* tx_weave: gate weave.  out(p) = in0(R(rot) (p - c) + c + d), Catmull-Rom bicubic, de-ringed by
 * clamping to the range of the 4 nearest texels (no overshoot around lamps), clamp-to-edge.
 *   f[0], f[1] = d (px, actual resolution)     f[2], f[3] = cos(rot), sin(rot)
 *   f[4], f[5] = rotation centre c (px)
 * Alpha is NOT resampled (v1.2): out.w = in0.w at (x, y), so the colour pass can take the host's alpha
 * from the image buffer instead of reading the source a second time.
 * Input is sanitised (first image pass): only the 4 nearest texels are cleaned; a NaN/Inf in the
 * 4x4 footprint makes the sum non-finite and the de-ring clamp then falls back to the clean
 * bounds (cel_txh_max/min select the second operand when the first is NaN). */
CEL_DEVICE void cel_k_tx_weave(CEL_KERNEL_PARAMS) {
    CEL_TX_ABI_USE();
    int w = a->inW[0], h = a->inH[0], st = a->inStride[0];
    float px = (float)x + 0.5f - a->f[4], py = (float)y + 0.5f - a->f[5];
    float fx = a->f[2] * px - a->f[3] * py + a->f[4] + a->f[0];
    float fy = a->f[3] * px + a->f[2] * py + a->f[5] + a->f[1];
    float sx = cel_txh_clampf(fx - 0.5f, -CEL_TX_COORD_MAX, CEL_TX_COORD_MAX);
    float sy = cel_txh_clampf(fy - 0.5f, -CEL_TX_COORD_MAX, CEL_TX_COORD_MAX);
    float ix = cel_txh_floor(sx), iy = cel_txh_floor(sy);
    int x0 = (int)ix, y0 = (int)iy;
    CelF4 r = cel_txh_filter4x4(in0, st, w, h, x0 - 1, y0 - 1, cel_txh_catmull_w(sx - ix), cel_txh_catmull_w(sy - iy));
    int c0 = cel_txh_cidx(x0, w), c1 = cel_txh_cidx(x0 + 1, w);
    CEL_GLOBAL const float* r0 = in0 + cel_txh_cidx(y0, h) * st;
    CEL_GLOBAL const float* r1 = in0 + cel_txh_cidx(y0 + 1, h) * st;
    CelF4 p00 = cel_txh_clean4(cel_txh_px(r0, c0)), p10 = cel_txh_clean4(cel_txh_px(r0, c1));
    CelF4 p01 = cel_txh_clean4(cel_txh_px(r1, c0)), p11 = cel_txh_clean4(cel_txh_px(r1, c1));
    CelF4 lo = cel_txh_min4(cel_txh_min4(p00, p10), cel_txh_min4(p01, p11));
    CelF4 hi = cel_txh_max4(cel_txh_max4(p00, p10), cel_txh_max4(p01, p11));
    r = cel_txh_min4(cel_txh_max4(r, lo), hi);
    r.w = cel_txh_clean(cel_load(in0, st, w, h, x, y).w);
    cel_store(out, a->outStride, x, y, r);
}

/* tx_acut_luma: out.x = log2(max(Y, 0) + eps), Y = Rec.2020 luminance of in0 (same size).
 *   f[0] = eps */
CEL_DEVICE void cel_k_tx_acut_luma(CEL_KERNEL_PARAMS) {
    CEL_TX_ABI_USE();
    CelF4 v = cel_txh_clean4(cel_load(in0, a->inStride[0], a->inW[0], a->inH[0], x, y));
    float l = cel_log2(cel_txh_max(cel_txh_luma(v), 0.0f) + a->f[0]);
    cel_store(out, a->outStride, x, y, cel_f4(l, 0.0f, 0.0f, 0.0f));
}

/* tx_acut_apply: adjacency effect / acutance in log exposure.
 *   in0 = image (full res), in1 = blurred log2 luminance (.x), full res or pyramid level L.
 *   d = log2Y - blur;  g = amount * k * u / sqrt(1 + u^2), u = d / k   (soft-limited: fine detail
 *   gets ~amount * d, large edges at most amount * k -> crisp, no halos); |g| <= gmax;
 *   out.rgb = in0.rgb * 2^g (alpha kept).  Negative amount softens (pulls log luminance to blur).
 *   f[0] = amount   f[1] = k (log2 units)   f[2] = gmax (log2)   f[3] = eps (as tx_acut_luma)
 *   f[4] = in1 column scale (1 / 2^L)
 *   f[5] = v1.3 toe weight Yt (linear luminance, 0 = off): g *= Yb / (Yb + Yt), Yb = 2^blur - eps the
 *         local mean luminance. The adjacency effect follows the density difference on the negative,
 *         i.e. the local negative gamma, which falls off in the toe (thin negative): shadow edges get
 *         less snap than mid-tone edges (D = gamma log(E + Et) has dD/dlogE = gamma E / (E + Et))
 *   i[0] = 1: in1 is level L upsampled vertically by tx_up_v (size w_L x h): horizontal B-spline
 *         sampling here; 0: in1 is full res, direct load */
CEL_DEVICE void cel_k_tx_acut_apply(CEL_KERNEL_PARAMS) {
    CEL_TX_ABI_USE();
    CelF4 v = cel_txh_clean4(cel_load(in0, a->inStride[0], a->inW[0], a->inH[0], x, y));
    float l = cel_log2(cel_txh_max(cel_txh_luma(v), 0.0f) + a->f[3]);
    float b;
    if (a->i[0] != 0) {
        float s = a->f[4];
        b = cel_txh_sample_bspline_h(in1, a->inStride[1], a->inW[1], a->inH[1], ((float)x + 0.5f) * s, y).x;
    } else {
        b = cel_load(in1, a->inStride[1], a->inW[1], a->inH[1], x, y).x;
    }
    float k = a->f[1];
    float u = (l - b) / k;
    float g = a->f[0] * k * u / cel_sqrt(1.0f + u * u);
    g = cel_txh_clampf(g, -a->f[2], a->f[2]);
    if (a->f[5] > 0.0f) {
        float yb = cel_exp2(b);          /* local mean luminance (+ eps << Yt: no cancellation) */
        g *= yb / (yb + a->f[5]);
    }
    float m = cel_exp2(g);
    cel_store(out, a->outStride, x, y, cel_f4(v.x * m, v.y * m, v.z * m, v.w));
}

/* log2 luminance of an image texel (acutance; == tx_acut_luma) */
CEL_DEVICE CEL_TXH_INLINE float cel_txh_logluma(CEL_GLOBAL const float* row, int xi, float eps) {
    return cel_log2(cel_txh_max(cel_txh_luma(cel_txh_clean4(cel_txh_px(row, xi))), 0.0f) + eps);
}
/* tx_pyr_down: 2x downsample, separable binomial [1 3 3 1]/8 (variance 0.75 source px^2),
 * all four channels, clamp-to-edge.  out size = ceil(in / 2).
 *   i[0] = 1 (v1.2 fusion): in0 is the IMAGE and the source value is its log2 luminance computed on
 *          the fly (tx_acut_luma fused away); out = (L, 0, 0, 0).  f[0] = eps */
CEL_DEVICE void cel_k_tx_pyr_down(CEL_KERNEL_PARAMS) {
    CEL_TX_ABI_USE();
    CelF4 wb = cel_f4(0.125f, 0.375f, 0.375f, 0.125f);
    if (a->i[0] == 1) {
        int w = a->inW[0], h = a->inH[0], st = a->inStride[0];
        int x0 = 2 * x - 1, y0 = 2 * y - 1;
        int c0 = cel_txh_cidx(x0, w), c1 = cel_txh_cidx(x0 + 1, w), c2 = cel_txh_cidx(x0 + 2, w), c3 = cel_txh_cidx(x0 + 3, w);
        float eps = a->f[0], r = 0.0f;
        for (int j = 0; j < 4; ++j) {
            CEL_GLOBAL const float* row = in0 + cel_txh_cidx(y0 + j, h) * st;
            float q = cel_txh_logluma(row, c0, eps) * wb.x;
            q = q + cel_txh_logluma(row, c1, eps) * wb.y;
            q = q + cel_txh_logluma(row, c2, eps) * wb.z;
            q = q + cel_txh_logluma(row, c3, eps) * wb.w;
            float wy = j == 0 ? wb.x : (j == 1 ? wb.y : (j == 2 ? wb.z : wb.w));
            r = j == 0 ? q * wy : r + q * wy;
        }
        cel_store(out, a->outStride, x, y, cel_f4(r, 0.0f, 0.0f, 0.0f));
        return;
    }
    cel_store(out, a->outStride, x, y,
              cel_txh_filter4x4(in0, a->inStride[0], a->inW[0], a->inH[0], 2 * x - 1, 2 * y - 1, wb, wb));
}

/* tx_blur4: one direction of a separable symmetric blur with an individual kernel per channel.
 *   i[0] = radius R (0..7)   i[1] = direction (0 = horizontal, 1 = vertical)
 *   i[2] = 1: only channel x carries data (y, z, w written as 0), 0: all four channels
 *   f[0..7]   = channel x weights for taps 0..7 (tap k is applied at +k and -k)
 *   f[8..15]  = channel y weights, f[16..23] = channel z, f[24..31] = channel w
 * v1.2 fusions (the source is computed on the fly instead of read from a buffer a pass wrote):
 *   i[3] = 1: the source is the grain white noise of tx_grain_gen with seed i[4] on a grid of the
 *             output size (in0 unused; horizontal only)
 *   i[2] = 1 and i[6] = 1: the source is the log2 luminance of the image in0 (tx_acut_luma fused;
 *             eps = f[8], horizontal only)
 *   i[5] = 1: channel w is passed through unchanged (alpha of the film-resolution blur) */
CEL_DEVICE void cel_k_tx_blur4(CEL_KERNEL_PARAMS) {
    CEL_TX_ABI_USE();
    int w = a->inW[0], h = a->inH[0], st = a->inStride[0];
    int R = cel_imin(cel_imax(a->i[0], 0), 7);
    if (a->i[3] == 1) {   /* virtual grain noise source (generation grid = output grid), horizontal */
        int gw = a->outW;
        cel_uint seed = (cel_uint)a->i[4];
        CelF4 c = cel_txh_grain_white(x, y, seed);
        float sx = c.x * a->f[0], sy = c.y * a->f[8], sz = c.z * a->f[16], sw = c.w * a->f[24];
        for (int k = 1; k <= R; ++k) {
            CelF4 p = cel_txh_grain_white(cel_txh_cidx(x + k, gw), y, seed);
            CelF4 m = cel_txh_grain_white(cel_txh_cidx(x - k, gw), y, seed);
            sx += (p.x + m.x) * a->f[k];
            sy += (p.y + m.y) * a->f[8 + k];
            sz += (p.z + m.z) * a->f[16 + k];
            sw += (p.w + m.w) * a->f[24 + k];
        }
        cel_store(out, a->outStride, x, y, cel_f4(sx, sy, sz, sw));
        return;
    }
    if (a->i[2] == 1 && a->i[6] == 1) {   /* log2 luminance of the image, horizontal */
        CEL_GLOBAL const float* row = in0 + cel_txh_cidx(y, h) * st;
        float eps = a->f[8];
        float s1 = cel_txh_logluma(row, cel_txh_cidx(x, w), eps) * a->f[0];
        for (int k = 1; k <= R; ++k)
            s1 += (cel_txh_logluma(row, cel_txh_cidx(x + k, w), eps) + cel_txh_logluma(row, cel_txh_cidx(x - k, w), eps)) * a->f[k];
        cel_store(out, a->outStride, x, y, cel_f4(s1, 0.0f, 0.0f, 0.0f));
        return;
    }
    if (a->i[2] == 1) {   /* only channel x carries data (acutance luma): 1-channel loop */
        int hx1 = a->i[1] == 0 ? 1 : 0, hy1 = 1 - hx1;
        float s1 = cel_load(in0, st, w, h, x, y).x * a->f[0];
        for (int k = 1; k <= R; ++k)
            s1 += (cel_load(in0, st, w, h, x + k * hx1, y + k * hy1).x + cel_load(in0, st, w, h, x - k * hx1, y - k * hy1).x) * a->f[k];
        cel_store(out, a->outStride, x, y, cel_f4(s1, 0.0f, 0.0f, 0.0f));
        return;
    }
    int hx = a->i[1] == 0 ? 1 : 0, hy = 1 - hx;
    CelF4 c = cel_load(in0, st, w, h, x, y);
    float sx = c.x * a->f[0], sy = c.y * a->f[8], sz = c.z * a->f[16], sw = c.w * a->f[24];
    int interior = hx != 0 ? (x - R >= 0 && x + R < w && y < h) : (y - R >= 0 && y + R < h && x < w);
    CEL_GLOBAL const float* cp = in0 + cel_txh_cidx(y, h) * st + cel_txh_cidx(x, w) * 4;
    int step = hx != 0 ? 4 : st;
    for (int k = 1; k <= R; ++k) {
        CelF4 p, m;
        if (interior != 0) {
            CEL_GLOBAL const float* pp = cp + k * step;
            CEL_GLOBAL const float* mp = cp - k * step;
            p = cel_f4(pp[0], pp[1], pp[2], pp[3]);
            m = cel_f4(mp[0], mp[1], mp[2], mp[3]);
        } else {
            p = cel_load(in0, st, w, h, x + k * hx, y + k * hy);
            m = cel_load(in0, st, w, h, x - k * hx, y - k * hy);
        }
        sx += (p.x + m.x) * a->f[k];
        sy += (p.y + m.y) * a->f[8 + k];
        sz += (p.z + m.z) * a->f[16 + k];
        sw += (p.w + m.w) * a->f[24 + k];
    }
    if (a->i[5] == 1) sw = c.w;
    cel_store(out, a->outStride, x, y, cel_f4(sx, sy, sz, sw));
}

/* tx_pyr_up_add: pyramid accumulation  out = Bspline(in0) + weight (per channel) * in1.
 *   in0 = coarser accumulation (sampled at (p + 0.5) * f[4]), in1 = this level's blurred source
 *   (same size as out).  f[0..3] = per-channel weights of in1   f[4] = in0 coordinate scale
 *   i[0] = 1 if in0 is used   i[1] = 1 if in1 is used */
CEL_DEVICE void cel_k_tx_pyr_up_add(CEL_KERNEL_PARAMS) {
    CEL_TX_ABI_USE();
    CelF4 r = cel_f4(0.0f, 0.0f, 0.0f, 0.0f);
    if (a->i[0] != 0) {
        float s = a->f[4];
        r = cel_txh_sample_bspline(in0, a->inStride[0], a->inW[0], a->inH[0],
                                  ((float)x + 0.5f) * s, ((float)y + 0.5f) * s);
    }
    if (a->i[1] != 0) {
        CelF4 v = cel_load(in1, a->inStride[1], a->inW[1], a->inH[1], x, y);
        r = cel_f4(r.x + v.x * a->f[0], r.y + v.y * a->f[1], r.z + v.z * a->f[2], r.w + v.w * a->f[3]);
    }
    cel_store(out, a->outStride, x, y, r);
}

/* tx_hb_prefilter: per-pixel source of the glow pyramid (same size as in0; tx_pyr_down then
 * builds level 1). Each texel is sanitised and turned into
 *   xyz = bloom source (cel_txh_bloom_src), w = halation energy (cel_txh_hal_energy),
 * BEFORE any filtering, so thin bright lines keep their full halation energy.
 *   f[0] = threshold T   f[1] = knee k (stops over T)   f[2] = emax   f[3] = bloom bmax
 *   f[4..6] = halation source weights (r, g, b)   f[7] = v1.3 P1 bloom threshold (linear, 0 = none) */
CEL_DEVICE void cel_k_tx_hb_prefilter(CEL_KERNEL_PARAMS) {
    CEL_TX_ABI_USE();
    CelF4 v = cel_txh_clean4(cel_load(in0, a->inStride[0], a->inW[0], a->inH[0], x, y));
    CelF4 b = cel_txh_bloom_src_t(v, a->f[3], a->f[7]);
    b.w = cel_txh_hal_energy(v, a->f[0], a->f[1], a->f[2], a->f[4], a->f[5], a->f[6]);
    cel_store(out, a->outStride, x, y, b);
}

/* tx_hb_prefilter_down (v1.2 fusion): tx_hb_prefilter + tx_pyr_down in one pass. out = pyramid level
 * 1 (ceil(w/2) x ceil(h/2)) of the glow source of in0 (full-res image); every one of the 4 x 4 taps is
 * prefiltered (sanitised -> bloom source, halation energy) before the binomial weights, so the result
 * equals the two-pass chain. f[] as tx_hb_prefilter. */
CEL_DEVICE CEL_TXH_INLINE CelF4 cel_txh_prefilter_px(CEL_GLOBAL const float* row, int xi, CEL_CONSTANT const CelPassArgs* a) {
    CelF4 v = cel_txh_clean4(cel_txh_px(row, xi));
    CelF4 b = cel_txh_bloom_src_t(v, a->f[3], a->f[7]);
    b.w = cel_txh_hal_energy(v, a->f[0], a->f[1], a->f[2], a->f[4], a->f[5], a->f[6]);
    return b;
}
CEL_DEVICE void cel_k_tx_hb_prefilter_down(CEL_KERNEL_PARAMS) {
    CEL_TX_ABI_USE();
    int w = a->inW[0], h = a->inH[0], st = a->inStride[0];
    CelF4 wb = cel_f4(0.125f, 0.375f, 0.375f, 0.125f);
    int x0 = 2 * x - 1, y0 = 2 * y - 1;
    int c0 = cel_txh_cidx(x0, w), c1 = cel_txh_cidx(x0 + 1, w), c2 = cel_txh_cidx(x0 + 2, w), c3 = cel_txh_cidx(x0 + 3, w);
    CelF4 r = cel_f4(0.0f, 0.0f, 0.0f, 0.0f);
    for (int j = 0; j < 4; ++j) {
        CEL_GLOBAL const float* row = in0 + cel_txh_cidx(y0 + j, h) * st;
        CelF4 q = cel_txh_scale4(cel_txh_prefilter_px(row, c0, a), wb.x);
        q = cel_txh_madd4(q, cel_txh_prefilter_px(row, c1, a), wb.y);
        q = cel_txh_madd4(q, cel_txh_prefilter_px(row, c2, a), wb.z);
        q = cel_txh_madd4(q, cel_txh_prefilter_px(row, c3, a), wb.w);
        float wy = j == 0 ? wb.x : (j == 1 ? wb.y : (j == 2 ? wb.z : wb.w));
        r = j == 0 ? cel_txh_scale4(q, wy) : cel_txh_madd4(r, q, wy);
    }
    cel_store(out, a->outStride, x, y, r);
}

/* tx_hb_composite: adds bloom + halation from the accumulated glow pyramid and applies the
 * vignette, at full res.
 *   in0 = image (scene-linear, or pseudo-scene in Texture-Only), in1 = glow accumulation at its
 *   finest level L, upsampled vertically by tx_up_v (size w_L x h); B-spline sampled horizontally
 *   at column (x + 0.5) * f[12].
 *   bloom:     out = v + s * (glow.xyz - bloomsrc(v))        (energy conserving diffusion)
 *   halation:  out += gain * max(glow.w - E(v), 0) * tint    (only light spread beyond the source:
 *              flat highlight areas stay untinted, the glow hugs the outside of bright edges)
 *   f[0] = bloom mix s        f[1] = halation gain     f[2..4] = halation tint (r, g, b)
 *   f[5] = T  f[6] = k  f[7] = emax  f[8] = bmax  f[9..11] = halation source weights
 *   f[12] = in1 column scale (1 / 2^L)
 *   f[13] = vignette amount  f[14] = vignette k  f[15], f[16] = optical centre  f[17] = 1/half diag
 *   f[18..23] = tone map (x0, p, Yk, xk, slope, sdr), used when i[0] = 1
 *   f[24] = K, f[25] = h0 (v1.2 physical halation, chroma activity; K = 0: fixed tint): the green
 *           (and blue) share of the tint is scaled by 1 + K (2 h / (h + h0) - 1), h = the added
 *           halation light: faint light (the outer tail, weak sources) stays deep red, strong light
 *           near bright sources turns orange, so the hue varies with source intensity and radius
 *   i[0] = 1: Texture-Only (in0 is pseudo-scene; result is mapped back to display-linear and the
 *             vignette is applied in display-linear, like the stand-alone tx_vignette; for SDR
 *             outputs the result is <= 1 by construction of the curve)
 *   i[1] = halation on   i[2] = bloom on   i[3] = vignette on
 *   v1.3 chroma halation (i[4] = 1): the added tint t is desaturated / saturated around its luminance
 *           Yt: t' = Yt + S (t - Yt), S = max(f[26] (1 + f[27] (1 - 2 Y / (Y + 0.18))), 0), Y = the
 *           luminance of the image under the halo (f[27] > 0: stronger colour over dark backgrounds)
 *   v1.3 P1 bloom: f[31] = bloom threshold (linear, 0 = none, as the prefilter's f[7]); i[6] = Keep
 *           Highlights in 1/1000 (the source is subtracted x (1 - i[6] / 1000); 0 = energy conserving)
 *   v1.3 aura (i[5] = 1): in2 = second, wide halation lobe (w channel, finest level column scale f[30],
 *           upsampled vertically like in1): out += f[28] max(aura.w - E(v), 0) (1, f[29], 0) (same
 *           chroma rule) */
CEL_DEVICE CEL_TXH_INLINE CelF3 cel_txh_hal_tint(float tr, float tg, float tb, float Y, CEL_CONSTANT const CelPassArgs* a) {
    if (a->i[4] == 0) return cel_f3(tr, tg, tb);
    float yt = 0.2126f * tr + 0.7152f * tg + 0.0722f * tb;
    float yb = cel_txh_max(Y, 0.0f);
    float S = a->f[26] * (1.0f + a->f[27] * (1.0f - 2.0f * yb / (yb + 0.18f)));
    S = cel_txh_max(S, 0.0f);
    return cel_f3(yt + S * (tr - yt), yt + S * (tg - yt), yt + S * (tb - yt));
}
CEL_DEVICE void cel_k_tx_hb_composite(CEL_KERNEL_PARAMS) {
    CEL_TX_ABI_USE();
    CelF4 v = cel_txh_clean4(cel_load(in0, a->inStride[0], a->inW[0], a->inH[0], x, y));
    float s = a->f[12];
    CelF4 g = cel_txh_sample_bspline_h(in1, a->inStride[1], a->inW[1], a->inH[1], ((float)x + 0.5f) * s, y);
    CelF4 o = v;
    if (a->i[2] != 0) {
        CelF4 b = cel_txh_bloom_src_t(v, a->f[8], a->f[31]);
        float m = a->f[0];
        if (a->i[6] != 0) {   /* v1.3 P1 Keep Highlights: part of the blooming light is not taken from its source */
            float kk = 1.0f - 0.001f * (float)a->i[6];
            b = cel_f4(b.x * kk, b.y * kk, b.z * kk, 0.0f);
        }
        o.x += m * (g.x - b.x); o.y += m * (g.y - b.y); o.z += m * (g.z - b.z);
    }
    if (a->i[1] != 0) {
        float e = cel_txh_hal_energy(v, a->f[5], a->f[6], a->f[7], a->f[9], a->f[10], a->f[11]);
        float hal = a->f[1] * cel_txh_max(g.w - e, 0.0f);
        float cs = 1.0f;
        if (a->f[24] != 0.0f) cs = 1.0f + a->f[24] * (2.0f * hal / (hal + a->f[25]) - 1.0f);
        float Y = 0.2126f * v.x + 0.7152f * v.y + 0.0722f * v.z;
        if (a->i[4] == 0) {
            o.x += hal * a->f[2]; o.y += hal * a->f[3] * cs; o.z += hal * a->f[4] * cs;
        } else {
            CelF3 t = cel_txh_hal_tint(a->f[2], a->f[3] * cs, a->f[4] * cs, Y, a);
            o.x += hal * t.x; o.y += hal * t.y; o.z += hal * t.z;
        }
        if (a->i[5] != 0) {
            CelF4 ga = cel_txh_sample_bspline_h(in2, a->inStride[2], a->inW[2], a->inH[2], ((float)x + 0.5f) * a->f[30], y);
            float au = a->f[28] * cel_txh_max(ga.w - e, 0.0f);
            CelF3 ta = cel_txh_hal_tint(1.0f, a->f[29], 0.0f, Y, a);
            o.x += au * ta.x; o.y += au * ta.y; o.z += au * ta.z;
        }
    }
    if (a->i[0] != 0) {
        o.x = cel_txh_s2d(o.x, a->f[18], a->f[19], a->f[20], a->f[21], a->f[22], a->f[23]);
        o.y = cel_txh_s2d(o.y, a->f[18], a->f[19], a->f[20], a->f[21], a->f[22], a->f[23]);
        o.z = cel_txh_s2d(o.z, a->f[18], a->f[19], a->f[20], a->f[21], a->f[22], a->f[23]);
    }
    if (a->i[3] != 0) {
        float vg = cel_txh_vig_factor(x, y, a->f[13], a->f[14], a->f[15], a->f[16], a->f[17]);
        o.x *= vg; o.y *= vg; o.z *= vg;
    }
    o.w = v.w;
    cel_store(out, a->outStride, x, y, o);
}

/* tx_vignette: out.rgb = in0.rgb * vignette (alpha kept).
 *   f[0] = amount (0..1)   f[1] = k = tan(field angle at the corner)   f[2], f[3] = optical centre
 *   f[4] = 1 / half diagonal (px)
 *   Beyond Filmbox I mechanical vignette (i[0] = 1): f[5] = amount (0..1), f[6] = start radius (x half diagonal),
 *   f[7] = edge width (x half diagonal): x (1 - f[5] smoothstep(f[6] - f[7], f[6] + f[7], r)), r = distance to
 *   the centre / half diagonal (a lens hood / matte box cutting the light cone) */
CEL_DEVICE void cel_k_tx_vignette(CEL_KERNEL_PARAMS) {
    CEL_TX_ABI_USE();
    CelF4 v = cel_txh_clean4(cel_load(in0, a->inStride[0], a->inW[0], a->inH[0], x, y));
    float vg = cel_txh_vig_factor(x, y, a->f[0], a->f[1], a->f[2], a->f[3], a->f[4]);
    if (a->i[0] == 1) {
        float dx = ((float)x + 0.5f - a->f[2]) * a->f[4], dy = ((float)y + 0.5f - a->f[3]) * a->f[4];
        float r = cel_sqrt(dx * dx + dy * dy);
        float t = cel_txh_clampf((r - (a->f[6] - a->f[7])) / (2.0f * a->f[7]), 0.0f, 1.0f);
        vg *= 1.0f - a->f[5] * t * t * (3.0f - 2.0f * t);
    }
    cel_store(out, a->outStride, x, y, cel_f4(v.x * vg, v.y * vg, v.z * vg, v.w));
}

/* tx_tonemap: Texture-Only conversion display-linear <-> pseudo scene (see cel_txh_d2s/s2d).
 *   i[0] = 0: display -> pseudo scene, 1: pseudo scene -> display
 *   f[0..5] = x0, p, Yk, xk, slope, sdr */
CEL_DEVICE void cel_k_tx_tonemap(CEL_KERNEL_PARAMS) {
    CEL_TX_ABI_USE();
    CelF4 v = cel_txh_clean4(cel_load(in0, a->inStride[0], a->inW[0], a->inH[0], x, y));
    float x0 = a->f[0], p = a->f[1], yk = a->f[2], xk = a->f[3], sl = a->f[4], sd = a->f[5];
    if (a->i[0] == 0) {
        v.x = cel_txh_d2s(v.x, x0, p, yk, xk, sl, sd); v.y = cel_txh_d2s(v.y, x0, p, yk, xk, sl, sd);
        v.z = cel_txh_d2s(v.z, x0, p, yk, xk, sl, sd);
    } else {
        v.x = cel_txh_s2d(v.x, x0, p, yk, xk, sl, sd); v.y = cel_txh_s2d(v.y, x0, p, yk, xk, sl, sd);
        v.z = cel_txh_s2d(v.z, x0, p, yk, xk, sl, sd);
    }
    cel_store(out, a->outStride, x, y, v);
}

/* tx_up_v: vertical half of a separable cubic B-spline upsample. out (in0 width x target height)
 *   out(x, y) = sum_j B_j * in0(x, row j around (y + 0.5) * f[0] + f[1])
 *   f[0] = row scale (1 / 2^L or 1 / D)   f[1] = row offset in in0 px (grain grid margin, else 0) */
CEL_DEVICE void cel_k_tx_up_v(CEL_KERNEL_PARAMS) {
    CEL_TX_ABI_USE();
    int w = a->inW[0], h = a->inH[0], st = a->inStride[0];
    float sy = cel_txh_clampf(((float)y + 0.5f) * a->f[0] + a->f[1] - 0.5f, -CEL_TX_COORD_MAX, CEL_TX_COORD_MAX);
    float iy = cel_txh_floor(sy);
    CelF4 wy = cel_txh_bspline_w(sy - iy);
    int y0 = (int)iy - 1;
    CEL_GLOBAL const float* col = in0 + cel_txh_cidx(x, w) * 4;
    CelF4 r = cel_txh_scale4(cel_txh_px(col + cel_txh_cidx(y0, h) * st, 0), wy.x);
    r = cel_txh_madd4(r, cel_txh_px(col + cel_txh_cidx(y0 + 1, h) * st, 0), wy.y);
    r = cel_txh_madd4(r, cel_txh_px(col + cel_txh_cidx(y0 + 2, h) * st, 0), wy.z);
    r = cel_txh_madd4(r, cel_txh_px(col + cel_txh_cidx(y0 + 3, h) * st, 0), wy.w);
    cel_store(out, a->outStride, x, y, r);
}

/* ---- grain ---------------------------------------------------------------------------------- */

/* tx_grain_gen: 4 independent white-noise channels on a generation grid (any size):
 *   x, y, z = per-layer grain (red-, green-, blue-sensitive layer), w = shared clump field.
 *   i[0] = frame seed (bit pattern of a 32-bit hash of grainSeed and, when animated, the frame) */
CEL_DEVICE void cel_k_tx_grain_gen(CEL_KERNEL_PARAMS) {
    CEL_TX_ABI_USE();
    cel_store(out, a->outStride, x, y, cel_txh_grain_white(x, y, (cel_uint)a->i[0]));
}

/* tx_grain_finish (v1.2): DIAGNOSTIC grain field = what the colour pass adds per layer (in units of the
 * stock's grain sigma at grey) when every layer is exposed at s = f[24] stops relative to grey: the
 * dual-emulsion dye-cloud model of GrainKernels.h (celg_eval) on the coarse (in0) and fine (in1)
 * grids. f[0..23] / i[] = the GrainKernels.h argument block. out = (r, g, b, 0). The production plan
 * does not run this pass: the colour pass evaluates the same function from the true layer exposures. */
CEL_DEVICE void cel_k_tx_grain_finish(CEL_KERNEL_PARAMS) {
    CEL_TX_ABI_USE();
    CelgPx g = celg_sample(in0, a->inStride[0], a->inW[0], a->inH[0], in1, a->inStride[1], a->inW[1], a->inH[1], x, y, a);
    float s = a->f[24];
    CelF3 n = celg_eval(g, cel_f3(s, s, s), a);
    cel_store(out, a->outStride, x, y, cel_f4(n.x, n.y, n.z, 0.0f));
}

/* tx_diag (v1.3 diagnostic views, "Show" controls of the modules; never part of a production plan):
 * in0 = the image WITH the module, in1 = the same image WITHOUT it (both from the real render path, same
 * size), out = an SDR display picture of the module's isolated contribution. i[0] = mode:
 *   0 added light (scene- or display-linear): out = sRGB encode of min(f[0] max(in0 - in1, 0), 1), on black
 *   1 edge / detail map: out = 0.5 + f[0] log2((Y0 + f[1]) / (Y1 + f[1])), grey = no change
 *   2 signed difference of encoded pictures: out = 0.5 + f[0] (in0 - in1) per channel, grey = no change
 *   3 halation mask (in0 only): the share of each pixel's light that feeds halation (cel_txh_hal_energy,
 *     f[2..7] = T, k, emax, source weights) in red over a dim grey picture of the image
 *   4 wipe: in1 (before) left of column f[2] x width, in0 (after) right of it, a 2 px white line between
 * alpha = in0.w. Results are clamped to 0 .. 1. */
CEL_DEVICE void cel_k_tx_diag(CEL_KERNEL_PARAMS) {
    CEL_TX_ABI_USE();
    CelF4 p = cel_txh_clean4(cel_load(in0, a->inStride[0], a->inW[0], a->inH[0], x, y));
    int mode = a->i[0];
    float g = a->f[0];
    CelF4 o = p;
    if (mode == 3) {
        float yh = cel_txh_max(a->f[5] * p.x + a->f[6] * p.y + a->f[7] * p.z, 0.0f);
        float e = cel_txh_hal_energy(p, a->f[2], a->f[3], a->f[4], a->f[5], a->f[6], a->f[7]);
        float sh = yh > 1e-12f ? cel_txh_clampf(e / yh, 0.0f, 1.0f) : 0.0f;
        float yl = cel_txh_max(cel_txh_luma(p), 0.0f);
        float c = 0.3f * yl / (yl + 0.18f);
        o = cel_f4(c + sh * (1.0f - c), c + sh * (0.25f - c), c * (1.0f - sh), p.w);
    } else {
        CelF4 q = cel_txh_clean4(cel_load(in1, a->inStride[1], a->inW[1], a->inH[1], x, y));
        if (mode == 0) {
            float r = cel_txh_clampf(g * (p.x - q.x), 0.0f, 1.0f);
            float gg = cel_txh_clampf(g * (p.y - q.y), 0.0f, 1.0f);
            float b = cel_txh_clampf(g * (p.z - q.z), 0.0f, 1.0f);
            /* sRGB-shaped encode: the linear toe keeps the slope finite, so the tiny rounding differences of
               two renders are not blown up near black (pow(x, 1/2.2) has an infinite slope at 0) */
            o = cel_f4(r > 0.0031308f ? 1.055f * cel_pow(r, 0.41666667f) - 0.055f : 12.92f * r,
                       gg > 0.0031308f ? 1.055f * cel_pow(gg, 0.41666667f) - 0.055f : 12.92f * gg,
                       b > 0.0031308f ? 1.055f * cel_pow(b, 0.41666667f) - 0.055f : 12.92f * b, p.w);
        } else if (mode == 1) {
            float y0 = cel_txh_max(cel_txh_luma(p), 0.0f), y1 = cel_txh_max(cel_txh_luma(q), 0.0f);
            float v = cel_txh_clampf(0.5f + g * cel_log2((y0 + a->f[1]) / (y1 + a->f[1])), 0.0f, 1.0f);
            o = cel_f4(v, v, v, p.w);
        } else if (mode == 2) {
            o = cel_f4(cel_txh_clampf(0.5f + g * (p.x - q.x), 0.0f, 1.0f), cel_txh_clampf(0.5f + g * (p.y - q.y), 0.0f, 1.0f),
                       cel_txh_clampf(0.5f + g * (p.z - q.z), 0.0f, 1.0f), p.w);
        } else {
            int xb = (int)(a->f[2] * (float)a->outW);
            if (x == xb || x == xb - 1) o = cel_f4(1.0f, 1.0f, 1.0f, p.w);
            else if (x < xb) o = cel_f4(q.x, q.y, q.z, p.w);
        }
    }
    cel_store(out, a->outStride, x, y, o);
}

/* Beyond Filmbox I: film gate transmission at pixel (x, y): a rounded rectangle (signed distance d, < 0 inside)
 * with a soft edge (smoothstep over +-f[5] px) times an optional gate hair (a 4-segment polyline of half width
 * f[7] px, 1 px anti-aliasing, opacity 0.95). f[] as documented at tx_gate. */
CEL_DEVICE CEL_TXH_INLINE float cel_txh_gate_mask(int x, int y, CEL_CONSTANT const CelPassArgs* a) {
    float px = (float)x + 0.5f, py = (float)y + 0.5f;
    float qx = cel_fabs(px - a->f[0]) - a->f[2] + a->f[4], qy = cel_fabs(py - a->f[1]) - a->f[3] + a->f[4];
    float ox = cel_txh_max(qx, 0.0f), oy = cel_txh_max(qy, 0.0f);
    float d = cel_sqrt(ox * ox + oy * oy) + cel_txh_min(cel_txh_max(qx, qy), 0.0f) - a->f[4];
    float t = cel_txh_clampf(0.5f - 0.5f * d / a->f[5], 0.0f, 1.0f);
    float m = t * t * (3.0f - 2.0f * t);
    if (a->f[7] > 0.0f && m > 0.0f) {
        float dm = CEL_TX_COORD_MAX;
        for (int k = 0; k < 4; ++k) {
            float ax = a->f[8 + 2 * k], ay = a->f[9 + 2 * k], bx = a->f[10 + 2 * k], by = a->f[11 + 2 * k];
            float ex = bx - ax, ey = by - ay, wx = px - ax, wy = py - ay;
            float h = cel_txh_clampf((wx * ex + wy * ey) / cel_txh_max(ex * ex + ey * ey, 1e-6f), 0.0f, 1.0f);
            float rx = wx - ex * h, ry = wy - ey * h;
            dm = cel_txh_min(dm, cel_sqrt(rx * rx + ry * ry));
        }
        float cov = cel_txh_clampf(a->f[7] + 0.5f - dm, 0.0f, 1.0f) * cel_txh_min(1.0f, 2.0f * a->f[7]);
        m *= 1.0f - 0.95f * cov;
    }
    return m;
}

/* tx_gate (Beyond Filmbox I): the film gate (frame line) as a density event.
 *   f[0], f[1] = gate centre (px)   f[2], f[3] = half width / height (px)   f[4] = corner radius (px, <= half size)
 *   f[5] = edge half width (px, > 0)   f[6] = rebate density (mode 1)   f[7] = hair half width (px; 0 = no hair)
 *   f[8 .. 17] = the hair's 5 polyline points (x, y px)
 *   i[0] = 0: camera gate: out.rgb = in0.rgb * mask (scene light held back by the aperture: the negative stays
 *             clear there, which prints black), alpha kept
 *          1: printer gate: out = in0 (a damage field, when i[1] = 1, else 0) with x += the mean neutral density of
 *             a pixel a share (1 - mask) of which is covered by density f[6] (an unexposed print border = clear)
 *          2: diagnostic: out = (mask, mask, mask, in0.w or 1)
 *   i[1] = 1: in0 present (modes 1 and 2) */
CEL_DEVICE void cel_k_tx_gate(CEL_KERNEL_PARAMS) {
    CEL_TX_ABI_USE();
    float m = cel_txh_gate_mask(x, y, a);
    int mode = a->i[0];
    CelF4 v = cel_f4(0.0f, 0.0f, 0.0f, 1.0f);
    if (mode == 0 || a->i[1] != 0) v = cel_txh_clean4(cel_load(in0, a->inStride[0], a->inW[0], a->inH[0], x, y));
    CelF4 o;
    if (mode == 0) {
        o = cel_f4(v.x * m, v.y * m, v.z * m, v.w);
    } else if (mode == 1) {
        if (a->i[1] == 0) v = cel_f4(0.0f, 0.0f, 0.0f, 0.0f);
        float cov = 1.0f - m, nd = 0.0f;
        if (cov > 0.0f) {
            float tr = 1.0f - cov * (1.0f - cel_exp2(-3.32192809f * a->f[6]));
            nd = -0.30102999f * cel_log2(cel_txh_max(tr, 1e-6f));
        }
        o = cel_f4(v.x + nd, v.y, v.z, v.w);
    } else {
        o = cel_f4(m, m, m, a->i[1] != 0 ? v.w : 1.0f);
    }
    cel_store(out, a->outStride, x, y, o);
}

/* film damage (v1.1): tx_params, tx_damage */
#include "DamageKernels.h"

#endif /* CEL_TEXTURE_KERNELS_H */
