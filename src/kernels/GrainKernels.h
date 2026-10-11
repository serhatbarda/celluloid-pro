/* Celluloid Pro - exposure-coupled dye-cloud grain (v1.2, texture agent).  Portable dialect.
 *
 * Shared by the colour pass (ColorKernels.h: cel_k_color / cel_k_display_finish evaluate the grain
 * per pixel from the TRUE layer exposures of the film model) and by the diagnostic grain field
 * kernel tx_grain_finish (TextureKernels.h: the same evaluation at a fixed exposure).
 *
 * MODEL (fitted to the published KODAK VISION3 diffuse RMS granularity curves,
 * reference/filmdata/fit_grain.py):
 *   every colour layer holds a FAST (coarse-grained) and a SLOW (fine-grained) emulsion. At layer
 *   exposure s (stops relative to 18 % grey) emulsion k has developed the fraction
 *       p_k = Phi(z_k),  z_fast = smax((s - s_fast) / b_fast, z_fog),  z_slow = (s - s_slow) / b_slow
 *   (probit = log-normal grain sensitivities; the fast emulsion carries the fog floor; smax = smooth
 *   maximum with softness kappa). Its developed dye clouds are the EXCURSION SET of a band-limited
 *   Gaussian sensitivity field G_k (unit variance, the emulsion's cloud size): soft indicator
 *       I_k = Phi((G_k + sqrt(1 + w^2) z_k) / w)          E[I_k] = Phi(z_k) = p_k exactly
 *   and the dye-density fluctuation of the layer is
 *       n = A_fast (I_fast - p_fast) + A_slow (I_slow - p_slow)
 *   i.e. sparse isolated clouds in the toe (positively skewed: few, coarse, bright flecks in the print's
 *   shadows), merging clouds in the mid tones, sparse holes toward saturation; the coarse fast emulsion
 *   dominates the shadows and lower mids, the fine slow emulsion the highlights (granularity falls
 *   there, binomially). A_fast / A_slow are normalised on the host so that n has unit standard
 *   deviation at grey (s = 0); the colour pass scales it by the stock's grain sigma.
 *   Layers: G_k(layer) = cs * shared_k + ci * own_k (cs^2 + ci^2 = 1): mostly shared structure, so
 *   film grain reads as luminance grain; layers exposed differently (saturated colours) develop
 *   different clouds and decorrelate physically.
 *
 * GRIDS: the band-limited fields live on the generation grids of the coarse (fast) and fine (slow)
 *   emulsion (tx_grain_gen -> tx_blur4 h, v [-> tx_up_v]), x, y, z = per-layer, w = shared; when the grid
 *   spacing D > 1 the grid was upsampled vertically by tx_up_v and is B-spline sampled horizontally.
 *
 * ARGUMENT BLOCK (f[] / i[] of color, display_finish and tx_grain_finish when the grain is on):
 *   f[0]  coarse column scale (1 / Dc)     f[1]  coarse grid margin (grid px)
 *   f[2]  fine column scale (1 / Df)       f[3]  fine grid margin
 *   f[4..7]   coarse normalisation (x, y, z, w)     f[8..11] fine normalisation (x, y, z, w)
 *   f[12] cs (shared weight)               f[13] ci (own-layer weight)
 *   f[14] 1 / w                            f[15] sqrt(1 + w^2)
 *   f[16] s_fast   f[17] 1 / b_fast   f[18] z_fog   f[19] kappa^2
 *   f[20] s_slow   f[21] 1 / b_slow   f[22] A_fast  f[23] A_slow
 *   f[24..29] Texture-Only tone map (x0, p, Yk, xk, slope, sdr; see TextureKernels.h cel_txh_d2s)
 *   f[30] tx_grain_finish only: exposure s of the diagnostic field
 *   i[0]  2 = grain v2 (this block)        i[3] 1 = B-spline upsample the coarse grid
 *   i[4]  1 = B-spline upsample the fine grid                i[5] 1 = monochrome (one field)
 */
#ifndef CEL_GRAIN_KERNELS_H
#define CEL_GRAIN_KERNELS_H
#include "KernelABI.h"

#define CEL_GRAIN_V2 2
#define CEL_INV_LOG10_2 3.3219280948873623f

/* standard normal CDF: Abramowitz & Stegun 7.1.26 erfc (|abs error| <= 7.5e-8 in Phi) */
CEL_DEVICE float celg_phi(float x) {
    float z = cel_fabs(x) * 0.70710678118654752f;
    float t = 1.0f / (1.0f + 0.3275911f * z);
    float poly = t * (0.254829592f + t * (-0.284496736f + t * (1.421413741f + t * (-1.453152027f + t * 1.061405429f))));
    float q = 0.5f * poly * cel_exp(-z * z);
    return x >= 0.0f ? 1.0f - q : q;
}

/* cubic B-spline weights (taps i-1 .. i+2) */
CEL_DEVICE CelF4 celg_bspline_w(float t) {
    float t2 = t * t, t3 = t2 * t, u = 1.0f - t;
    return cel_f4(u * u * u * (1.0f / 6.0f), (3.0f * t3 - 6.0f * t2 + 4.0f) * (1.0f / 6.0f),
                  (-3.0f * t3 + 3.0f * t2 + 3.0f * t + 1.0f) * (1.0f / 6.0f), t3 * (1.0f / 6.0f));
}
CEL_DEVICE int celg_cidx(int i, int n) { return i < 0 ? 0 : (i >= n ? n - 1 : i); }
CEL_DEVICE CelF4 celg_px(CEL_GLOBAL const float* row, int xi) {
    CEL_GLOBAL const float* p = row + xi * 4;
    return cel_f4(p[0], p[1], p[2], p[3]);
}
/* grid value for full-res pixel (x, y): bspline = 1: buf holds the grid upsampled vertically (rows =
 * full-res rows), 4 taps at column (x + 0.5) * s + margin; 0: direct texel (x + margin, y + margin).
 * Same arithmetic as the v1.1 tx_grain_finish fetch (cel_txh_grain_fetch). */
CEL_DEVICE CelF4 celg_fetch(CEL_GLOBAL const float* buf, int st, int w, int h, int x, int y, int bspline, float s, float margin) {
    if (bspline != 0) {
        float sx = ((float)x + 0.5f) * s + margin - 0.5f;
        float ix = cel_floor(sx);
        int x0 = (int)ix - 1;
        CelF4 wx = celg_bspline_w(sx - ix);
        CEL_GLOBAL const float* row = buf + celg_cidx(y, h) * st;
        CelF4 a = celg_px(row, celg_cidx(x0, w)), b = celg_px(row, celg_cidx(x0 + 1, w));
        CelF4 c = celg_px(row, celg_cidx(x0 + 2, w)), d = celg_px(row, celg_cidx(x0 + 3, w));
        return cel_f4(a.x * wx.x + b.x * wx.y + c.x * wx.z + d.x * wx.w, a.y * wx.x + b.y * wx.y + c.y * wx.z + d.y * wx.w,
                      a.z * wx.x + b.z * wx.y + c.z * wx.z + d.z * wx.w, a.w * wx.x + b.w * wx.y + c.w * wx.z + d.w * wx.w);
    }
    int m = (int)margin;
    return cel_load(buf, st, w, h, x + m, y + m);
}

/* the two (unit variance) sensitivity fields of one layer at this pixel */
typedef struct { CelF4 c, f; } CelgPx;

CEL_DEVICE CelgPx celg_sample(CEL_GLOBAL const float* coarse, int cst, int cw, int ch,
                              CEL_GLOBAL const float* fine, int fst, int fw, int fh,
                              int x, int y, CEL_CONSTANT const CelPassArgs* a) {
    CelgPx r;
    CelF4 c = celg_fetch(coarse, cst, cw, ch, x, y, a->i[3], a->f[0], a->f[1]);
    CelF4 f = celg_fetch(fine, fst, fw, fh, x, y, a->i[4], a->f[2], a->f[3]);
    r.c = cel_f4(c.x * a->f[4], c.y * a->f[5], c.z * a->f[6], c.w * a->f[7]);
    r.f = cel_f4(f.x * a->f[8], f.y * a->f[9], f.z * a->f[10], f.w * a->f[11]);
    return r;
}

/* density fluctuation (units: grain sigma at grey) of one layer with own fields gc / gf at exposure s */
CEL_DEVICE float celg_layer(float gc, float gf, float shc, float shf, float s, CEL_CONSTANT const CelPassArgs* a) {
    float cs = a->f[12], ci = a->f[13], iw = a->f[14], cw = a->f[15];
    float Gc = cs * shc + ci * gc, Gf = cs * shf + ci * gf;
    s = cel_clamp(s, -40.0f, 40.0f);
    float zf = (s - a->f[16]) * a->f[17], zg = a->f[18];
    float dz = zf - zg;
    zf = 0.5f * (zf + zg + cel_sqrt(dz * dz + a->f[19]));      /* smooth max with the fog floor */
    float zs = (s - a->f[20]) * a->f[21];
    float nf = celg_phi((Gc + cw * zf) * iw) - celg_phi(zf);
    float ns = celg_phi((Gf + cw * zs) * iw) - celg_phi(zs);
    return a->f[22] * nf + a->f[23] * ns;
}

/* all three layers (r, g, b = red-, green-, blue-sensitive) at per-layer exposures s (stops re grey) */
CEL_DEVICE CelF3 celg_eval(CelgPx g, CelF3 s, CEL_CONSTANT const CelPassArgs* a) {
    if (a->i[5] != 0) {
        float m = celg_layer(g.c.y, g.f.y, g.c.w, g.f.w, s.y, a);
        return cel_f3(m, m, m);
    }
    return cel_f3(celg_layer(g.c.x, g.f.x, g.c.w, g.f.w, s.x, a), celg_layer(g.c.y, g.f.y, g.c.w, g.f.w, s.y, a),
                  celg_layer(g.c.z, g.f.z, g.c.w, g.f.w, s.z, a));
}

/* Texture-Only: exposure (stops re 18 % grey) of a display-linear value through the film-like pseudo
 * scene (== cel_txh_d2s of TextureKernels.h with the tone map f[24..29]; duplicated here because the
 * colour kernels are compiled before the texture kernels) */
CEL_DEVICE float celg_expm1(float x) {
    float u = cel_exp(x);
    if (!(cel_fabs(x) < 0.5f)) return u - 1.0f;
    if (u == 1.0f) return x;
    return (u - 1.0f) * x / cel_log(u);
}
CEL_DEVICE float celg_display_stops(float Y, CEL_CONSTANT const CelPassArgs* a) {
    float x0 = a->f[24], p = a->f[25], Yk = a->f[26], xk = a->f[27], slope = a->f[28], sdr = a->f[29];
    Y = cel_finite_or(Y, 0.0f);
    if (!(Y > 1e-7f)) return -40.0f;
    float sc;
    if (Y >= Yk) {
        if (sdr != 0.0f) {
            float L = (1.0f - Yk) * slope;
            sc = xk - L * cel_log((1.0f - cel_fmin(Y, 0.999999f)) / (1.0f - Yk));
        } else {
            sc = xk + (Y - Yk) * slope;
        }
    } else {
        float r = celg_expm1(cel_log(Y) * (-1.0f / 2.4f));
        sc = x0 * cel_pow(r, -1.0f / p);
    }
    return cel_log2(cel_fmax(sc, 1e-12f) * (1.0f / 0.18f));
}

#endif /* CEL_GRAIN_KERNELS_H */
