/* Celluloid Pro - colour kernels (portable dialect, owned by core).
 *
 * Per-pixel evaluation of the spectral film model baked by core::Pipeline::prepare() into the
 * state buffer (layout: ColorState.h). Mirrors reference/model.py function by function.
 *
 * Kernels (KernelList.h), all with the uniform ABI; f[] / i[] are NOT used by these kernels:
 *   decode          in0 = host source RGBA (encoded in LookParams.input)
 *                   out = scene-linear Rec.2020 x 2^exposure, alpha copied from in0
 *   color           in0 = scene-linear RGBA (after texture passes)
 *                   in1 = grain noise field (xyz = R-,G-,B-sensitive layer; zero mean, unit variance)
 *                         -- used only when a->inStride[1] != 0 (ABI: stride 0 = input unused)
 *                   in2 = host source (alpha source) -- used when a->inStride[2] != 0, else alpha of in0
 *                   out = encoded output (LookParams.output)
 *   display_decode  Texture-Only: in0 = host source, display-encoded in LookParams.output
 *                   out = display-linear in the output primaries (SDR: 1.0 = display white;
 *                         PQ / HLG: cd/m^2 / 100), alpha copied
 *   display_finish  Texture-Only: in0 = display-linear (after texture passes), in1 = grain noise
 *                   (as above), in2 = host source (alpha, as above) -> out = display-encoded
 * Helper functions are prefixed celc_ (never cel_<kernel name>, which the GPU entry points use).
 */
#ifndef CEL_COLOR_KERNELS_H
#define CEL_COLOR_KERNELS_H
#include "KernelABI.h"
#include "ColorState.h"
#include "ColorTransfer.h"

/* ------------------------------------------------------------------------- small helpers */
CEL_DEVICE int celc_i(CEL_GLOBAL const float* s, int idx) { return (int)(s[idx] + 0.5f); }

CEL_DEVICE CelF3 celc_mat3(CEL_GLOBAL const float* s, int o, CelF3 v) {
    return cel_f3(s[o + 0] * v.x + s[o + 1] * v.y + s[o + 2] * v.z,
                  s[o + 3] * v.x + s[o + 4] * v.y + s[o + 5] * v.z,
                  s[o + 6] * v.x + s[o + 7] * v.y + s[o + 8] * v.z);
}
CEL_DEVICE float celc_dot3(CEL_GLOBAL const float* s, int o, CelF3 v) { return s[o] * v.x + s[o + 1] * v.y + s[o + 2] * v.z; }
CEL_DEVICE float celc_comp(CelF3 v, int c) { return c == 0 ? v.x : (c == 1 ? v.y : v.z); }
CEL_DEVICE float celc_max3(CelF3 v) { return cel_fmax(v.x, cel_fmax(v.y, v.z)); }
CEL_DEVICE float celc_min3(CelF3 v) { return cel_fmin(v.x, cel_fmin(v.y, v.z)); }
CEL_DEVICE CelF3 celc_scale(CelF3 v, float k) { return cel_f3(v.x * k, v.y * k, v.z * k); }
CEL_DEVICE float celc_sanitize(float v) { return cel_clamp(cel_finite_or(v, 0.0f), -65504.0f, 65504.0f); }

/* 1D LUT row `row` of a 3 x N block over [x0,x1], corner aligned, clamped */
CEL_DEVICE float celc_lut1(CEL_GLOBAL const float* s, int off, int n, int row, float x0, float x1, float x) {
    float t = cel_clamp((x - x0) / (x1 - x0) * (float)(n - 1), 0.0f, (float)(n - 1));
    int i = cel_imin((int)cel_floor(t), n - 2);
    float f = t - (float)i;
    CEL_GLOBAL const float* p = s + off + row * n + i;
    return p[0] + (p[1] - p[0]) * f;
}
CEL_DEVICE CelF3 celc_lut1_3(CEL_GLOBAL const float* s, int off, int n, float x0, float x1, CelF3 x) {
    return cel_f3(celc_lut1(s, off, n, 0, x0, x1, x.x), celc_lut1(s, off, n, 1, x0, x1, x.y),
                  celc_lut1(s, off, n, 2, x0, x1, x.z));
}

/* trilinear 3D LUT (N^3 x 3), every axis over [0, mx] */
CEL_DEVICE CelF3 celc_lut3(CEL_GLOBAL const float* s, int off, int n, float mx, CelF3 x) {
    float sc = (float)(n - 1) / mx;
    float tx = cel_clamp(x.x * sc, 0.0f, (float)(n - 1));
    float ty = cel_clamp(x.y * sc, 0.0f, (float)(n - 1));
    float tz = cel_clamp(x.z * sc, 0.0f, (float)(n - 1));
    int ix = cel_imin((int)cel_floor(tx), n - 2), iy = cel_imin((int)cel_floor(ty), n - 2), iz = cel_imin((int)cel_floor(tz), n - 2);
    float fx = tx - (float)ix, fy = ty - (float)iy, fz = tz - (float)iz;
    int sz = 3, sy = 3 * n, sx = 3 * n * n;
    CEL_GLOBAL const float* p = s + off + ix * sx + iy * sy + iz * sz;
    CelF3 r;
    float c00, c01, c10, c11, c0, c1;
    /* channel 0 */
    c00 = p[0] + (p[sz] - p[0]) * fz;           c01 = p[sy] + (p[sy + sz] - p[sy]) * fz;
    c10 = p[sx] + (p[sx + sz] - p[sx]) * fz;    c11 = p[sx + sy] + (p[sx + sy + sz] - p[sx + sy]) * fz;
    c0 = c00 + (c01 - c00) * fy; c1 = c10 + (c11 - c10) * fy; r.x = c0 + (c1 - c0) * fx;
    p += 1;
    c00 = p[0] + (p[sz] - p[0]) * fz;           c01 = p[sy] + (p[sy + sz] - p[sy]) * fz;
    c10 = p[sx] + (p[sx + sz] - p[sx]) * fz;    c11 = p[sx + sy] + (p[sx + sy + sz] - p[sx + sy]) * fz;
    c0 = c00 + (c01 - c00) * fy; c1 = c10 + (c11 - c10) * fy; r.y = c0 + (c1 - c0) * fx;
    p += 1;
    c00 = p[0] + (p[sz] - p[0]) * fz;           c01 = p[sy] + (p[sy + sz] - p[sy]) * fz;
    c10 = p[sx] + (p[sx + sz] - p[sx]) * fz;    c11 = p[sx + sy] + (p[sx + sy + sz] - p[sx + sy]) * fz;
    c0 = c00 + (c01 - c00) * fy; c1 = c10 + (c11 - c10) * fy; r.z = c0 + (c1 - c0) * fx;
    return r;
}

/* ------------------------------------------------------------------------- input */
CEL_DEVICE CelF3 celc_decode_px(CEL_GLOBAL const float* s, CelF3 e) {
    int sp = celc_i(s, CEL_CS_INPUT);
    CelF3 l = cel_f3(celc_decode_transfer(sp, celc_sanitize(e.x)), celc_decode_transfer(sp, celc_sanitize(e.y)),
                     celc_decode_transfer(sp, celc_sanitize(e.z)));
    l = celc_mat3(s, CEL_CS_MIN, l);
    float g = s[CEL_CS_GAIN];
    return cel_f3(celc_sanitize(l.x * g), celc_sanitize(l.y * g), celc_sanitize(l.z * g));
}

/* ------------------------------------------------------------------------- spectral exposure */
/* octahedral direction of scene RGB (opponent basis) -> layer log10 exposures */
CEL_DEVICE CelF3 celc_exposures(CEL_GLOBAL const float* s, CelF3 rgb) {
    float a = (rgb.x - rgb.y) * 0.70710678118654752f;
    float b = (rgb.x + rgb.y - 2.0f * rgb.z) * 0.40824829046386302f;
    float nn = (rgb.x + rgb.y + rgb.z) * 0.57735026918962576f;
    float L1 = cel_fabs(a) + cel_fabs(b) + cel_fabs(nn);
    float u = 0.0f, v = 0.0f;
    if (L1 > 0.0f) { u = a / L1; v = b / L1; }
    if (nn < 0.0f) {
        float fu = (1.0f - cel_fabs(v)) * (u >= 0.0f ? 1.0f : -1.0f);
        float fv = (1.0f - cel_fabs(u)) * (v >= 0.0f ? 1.0f : -1.0f);
        u = fu; v = fv;
    }
    int n = celc_i(s, CEL_CS_OCT_N), off = celc_i(s, CEL_CS_OCT_OFF);
    float tu = cel_clamp((u + 1.0f) * 0.5f * (float)(n - 1), 0.0f, (float)(n - 1));
    float tv = cel_clamp((v + 1.0f) * 0.5f * (float)(n - 1), 0.0f, (float)(n - 1));
    int i = cel_imin((int)cel_floor(tu), n - 2), j = cel_imin((int)cel_floor(tv), n - 2);
    float fu = tu - (float)i, fv = tv - (float)j;
    CEL_GLOBAL const float* p00 = s + off + (j * n + i) * 3;
    CEL_GLOBAL const float* p10 = p00 + 3;
    CEL_GLOBAL const float* p01 = p00 + n * 3;
    CEL_GLOBAL const float* p11 = p01 + 3;
    float w00 = (1.0f - fu) * (1.0f - fv), w10 = fu * (1.0f - fv), w01 = (1.0f - fu) * fv, w11 = fu * fv;
    CelF3 K = cel_f3(p00[0] * w00 + p10[0] * w10 + p01[0] * w01 + p11[0] * w11,
                     p00[1] * w00 + p10[1] * w10 + p01[1] * w01 + p11[1] * w11,
                     p00[2] * w00 + p10[2] * w10 + p01[2] * w01 + p11[2] * w11);
    return cel_f3(cel_log10(cel_fmax(L1 * K.x, 1e-10f)), cel_log10(cel_fmax(L1 * K.y, 1e-10f)),
                  cel_log10(cel_fmax(L1 * K.z, 1e-10f)));
}

typedef struct { CelF3 d; float silver; float xref; } CelcNeg;

/* layer exposures -> interlayer (DIR) -> curves -> grain -> retained silver */
CEL_DEVICE CelcNeg celc_negative(CEL_GLOBAL const float* s, CelF3 rgb, CelF3 noise, int hasNoise) {
    CelF3 x = celc_exposures(s, rgb);
    int nco = celc_i(s, CEL_CS_NC_OFF), ncn = celc_i(s, CEL_CS_NC_N);
    float x0 = s[CEL_CS_NC_X0], x1 = s[CEL_CS_NC_X1];
    float xref = celc_dot3(s, CEL_CS_LW, x);
    float sg = s[CEL_CS_REVSIGN];
    CelF3 dx = celc_lut1_3(s, nco, ncn, x0, x1, x);
    CelF3 xr = cel_f3(xref, xref, xref);
    CelF3 dr = celc_lut1_3(s, nco, ncn, x0, x1, xr);
    CelF3 c = cel_f3(sg * (dx.x - dr.x) * s[CEL_CS_NIGAM + 0], sg * (dx.y - dr.y) * s[CEL_CS_NIGAM + 1],
                     sg * (dx.z - dr.z) * s[CEL_CS_NIGAM + 2]);
    CelF3 del = celc_mat3(s, CEL_CS_INTER, c);
    del = cel_f3(-del.x, -del.y, -del.z);
    float dm = celc_dot3(s, CEL_CS_LW, del);
    x = cel_f3(x.x + del.x - dm, x.y + del.y - dm, x.z + del.z - dm);
    if (celc_i(s, CEL_CS_KIND) == 2)
        x = cel_f3(x.x + s[CEL_CS_OFF], x.y + s[CEL_CS_OFF + 1], x.z + s[CEL_CS_OFF + 2]);
    CelF3 d = celc_lut1_3(s, nco, ncn, x0, x1, x);
    CelF3 dmin = cel_f3(s[CEL_CS_DMIN], s[CEL_CS_DMIN + 1], s[CEL_CS_DMIN + 2]);
    if (hasNoise && s[CEL_CS_GRAIN_ON] > 0.5f) {
        CelF3 nz = noise;
        if (celc_i(s, CEL_CS_MONO)) nz = cel_f3(noise.y, noise.y, noise.y);
        d.x += s[CEL_CS_GSIG + 0] * cel_sqrt(cel_fmax(d.x - dmin.x, 0.0f) + CEL_GRAIN_EPS) * nz.x;
        d.y += s[CEL_CS_GSIG + 1] * cel_sqrt(cel_fmax(d.y - dmin.y, 0.0f) + CEL_GRAIN_EPS) * nz.y;
        d.z += s[CEL_CS_GSIG + 2] * cel_sqrt(cel_fmax(d.z - dmin.z, 0.0f) + CEL_GRAIN_EPS) * nz.z;
    }
    CelcNeg r;
    r.d = d;
    r.silver = s[CEL_CS_NB] * (s[CEL_CS_LW] * cel_fmax(d.x - dmin.x, 0.0f) + s[CEL_CS_LW + 1] * cel_fmax(d.y - dmin.y, 0.0f) +
                               s[CEL_CS_LW + 2] * cel_fmax(d.z - dmin.z, 0.0f));
    r.xref = xref;
    return r;
}

CEL_DEVICE CelF3 celc_pow10_3(CelF3 v) { return cel_f3(celc_pow10(v.x), celc_pow10(v.y), celc_pow10(v.z)); }

/* projection: XYZ of a processed film (print, or reversal original) with dye amounts D under the
 * projector, integrated per pixel over the K spectral samples of the PJ block (no 3D LUT, so
 * smooth gradients stay smooth after any later grade):
 *   XYZ_c = sum_k w_c(k) 10^-(base(k) + sum_i D_i eps_i(k) + nd)       (nd = retained silver)
 * D is clamped to the designed density domain [0, PJ_MAX]. */
CEL_DEVICE CelF3 celc_project(CEL_GLOBAL const float* s, CelF3 D, float nd) {
    int off = celc_i(s, CEL_CS_PJ_OFF), K = celc_i(s, CEL_CS_PJ_N);
    float mx = s[CEL_CS_PJ_MAX];
    float d0 = cel_clamp(D.x, 0.0f, mx), d1 = cel_clamp(D.y, 0.0f, mx), d2 = cel_clamp(D.z, 0.0f, mx);
    float X = 0.0f, Y = 0.0f, Z = 0.0f;
    CEL_GLOBAL const float* q = s + off;
    for (int k = 0; k < K; ++k) {
        float t = celc_pow10(-(q[0] + nd + d0 * q[1] + d1 * q[2] + d2 * q[3]));
        X += q[4] * t; Y += q[5] * t; Z += q[6] * t;
        q += CEL_CS_PJ_STRIDE;
    }
    return cel_f3(X, Y, Z);
}

/* scene-linear Rec.2020 -> D65-adapted display XYZ (1 = display white); xref out for HDR */
typedef struct { CelF3 X; float xref; } CelcFilm;

CEL_DEVICE CelcFilm celc_film_xyz(CEL_GLOBAL const float* s, CelF3 rgb, CelF3 noise, int hasNoise) {
    CelcNeg ng = celc_negative(s, rgb, noise, hasNoise);
    int kind = celc_i(s, CEL_CS_KIND);
    CelF3 X;
    if (kind == 0) {
        CelF3 h = celc_lut3(s, celc_i(s, CEL_CS_PL_OFF), celc_i(s, CEL_CS_PL_N), s[CEL_CS_PL_MAX], ng.d);
        h = cel_f3(h.x + s[CEL_CS_OFF] - ng.silver, h.y + s[CEL_CS_OFF + 1] - ng.silver, h.z + s[CEL_CS_OFF + 2] - ng.silver);
        CelF3 P = celc_lut1_3(s, celc_i(s, CEL_CS_PC_OFF), celc_i(s, CEL_CS_PC_N), s[CEL_CS_PC_U0], s[CEL_CS_PC_U1], h);
        float ps = s[CEL_CS_PB] * (s[CEL_CS_LW] * cel_fmax(P.x - s[CEL_CS_PMIN], 0.0f) +
                                   s[CEL_CS_LW + 1] * cel_fmax(P.y - s[CEL_CS_PMIN + 1], 0.0f) +
                                   s[CEL_CS_LW + 2] * cel_fmax(P.z - s[CEL_CS_PMIN + 2], 0.0f));
        X = celc_mat3(s, CEL_CS_MXYZ, celc_project(s, P, ps));
    } else if (kind == 2) {
        X = celc_mat3(s, CEL_CS_MXYZ, celc_project(s, ng.d, ng.silver));
    } else {
        CelF3 ls = celc_lut3(s, celc_i(s, CEL_CS_PL_OFF), celc_i(s, CEL_CS_PL_N), s[CEL_CS_PL_MAX], ng.d);
        int sco = celc_i(s, CEL_CS_SC_OFF), scn = celc_i(s, CEL_CS_SC_N);
        CelF3 v = cel_f3(celc_lut1(s, sco, scn, 0, s[CEL_CS_SD0], s[CEL_CS_SD1], -ls.x + ng.silver) + s[CEL_CS_OFF],
                         celc_lut1(s, sco, scn, 1, s[CEL_CS_SD0 + 1], s[CEL_CS_SD1 + 1], -ls.y + ng.silver) + s[CEL_CS_OFF + 1],
                         celc_lut1(s, sco, scn, 2, s[CEL_CS_SD0 + 2], s[CEL_CS_SD1 + 2], -ls.z + ng.silver) + s[CEL_CS_OFF + 2]);
        CelF3 e = celc_mat3(s, CEL_CS_MSCAN, celc_pow10_3(v));
        float fl = CEL_SCAN_FLOOR * CEL_SCAN_FLOOR;
        e = cel_f3(0.5f * (e.x + cel_sqrt(e.x * e.x + fl)), 0.5f * (e.y + cel_sqrt(e.y * e.y + fl)), 0.5f * (e.z + cel_sqrt(e.z * e.z + fl)));
        CelF3 w = cel_f3(cel_log10(e.x), cel_log10(e.y), cel_log10(e.z));
        CelF3 Dd = celc_lut1_3(s, celc_i(s, CEL_CS_PC_OFF), celc_i(s, CEL_CS_PC_N), s[CEL_CS_PC_U0], s[CEL_CS_PC_U1], w);
        X = celc_mat3(s, CEL_CS_MXYZ, celc_pow10_3(cel_f3(-Dd.x, -Dd.y, -Dd.z)));
    }
    CelcFilm r;
    r.X = X;
    r.xref = ng.xref;
    return r;
}

/* ------------------------------------------------------------------------- output stage */
/* hue-preserving smooth gamut compression toward the luminance axis (C1, asymptote at the boundary) */
CEL_DEVICE CelF3 celc_gamut_compress(CEL_GLOBAL const float* s, CelF3 rgb) {
    float Y = celc_dot3(s, CEL_CS_LUM, rgb);
    if (!(Y > 1e-10f)) return cel_f3(0.0f, 0.0f, 0.0f);
    float sat = (Y - celc_min3(rgb)) / Y;
    if (sat <= CEL_GC_THR) return rgb;
    float x = (sat - CEL_GC_THR) / (1.0f - CEL_GC_THR);
    float sc = CEL_GC_THR + (1.0f - CEL_GC_THR) * x / cel_sqrt(1.0f + x * x);
    float k = sc / sat;
    return cel_f3(Y + (rgb.x - Y) * k, Y + (rgb.y - Y) * k, Y + (rgb.z - Y) * k);
}
/* smooth per-channel highlight limiter: identity below knee, C1 tanh roll-off to `top`
 * (per channel = monotonic in every channel; bright saturated colours ease toward white) */
CEL_DEVICE float celc_limit1(float v, float knee, float top) {
    if (!(v > knee)) return v;
    float x = (v - knee) / (top - knee);
    return knee + (top - knee) * (1.0f - 2.0f / (cel_exp(2.0f * x) + 1.0f));
}
CEL_DEVICE CelF3 celc_limit(CelF3 rgb, float knee, float top) {
    return cel_f3(celc_limit1(rgb.x, knee, top), celc_limit1(rgb.y, knee, top), celc_limit1(rgb.z, knee, top));
}

CEL_DEVICE CelF3 celc_hlg_from_nits(CEL_GLOBAL const float* s, CelF3 nits) {
    /* BT.2100 HLG inverse EOTF, Lw = 1000 cd/m^2, system gamma 1.2, Lb = 0 */
    CelF3 Fd = cel_f3(cel_fmax(nits.x, 0.0f) / 1000.0f, cel_fmax(nits.y, 0.0f) / 1000.0f, cel_fmax(nits.z, 0.0f) / 1000.0f);
    float Yd = celc_dot3(s, CEL_CS_LUM, Fd);
    if (!(Yd > 1e-12f)) return cel_f3(0.0f, 0.0f, 0.0f);
    float Ys = cel_pow(Yd, 1.0f / 1.2f);
    float k = 1.0f / cel_pow(Ys, 0.2f);
    return cel_f3(celc_hlg_oetf(Fd.x * k), celc_hlg_oetf(Fd.y * k), celc_hlg_oetf(Fd.z * k));
}

CEL_DEVICE CelF3 celc_encode_sdr(int out, CelF3 rgb) {
    if (out == CEL_OUT_709_SRGB) return cel_f3(celc_srgb_enc(rgb.x), celc_srgb_enc(rgb.y), celc_srgb_enc(rgb.z));
    float e = (out == CEL_OUT_709_G24) ? (1.0f / 2.4f) : (1.0f / 2.6f);
    return cel_f3(celc_spow(rgb.x, e), celc_spow(rgb.y, e), celc_spow(rgb.z, e));
}

CEL_DEVICE CelF3 celc_output(CEL_GLOBAL const float* s, CelF3 XYZ, float xref) {
    int out = celc_i(s, CEL_CS_OUTPUT);
    int hdr = celc_i(s, CEL_CS_HDR);
    if (hdr == 0) {
        CelF3 rgb = celc_mat3(s, CEL_CS_MOUT, XYZ);
        rgb = celc_gamut_compress(s, rgb);
        rgb = celc_limit(rgb, CEL_SDR_KNEE, 1.0f);
        return celc_encode_sdr(out, rgb);
    }
    float e = xref - CEL_LOG10_018;
    float t = cel_fmax(e - CEL_HDR_E0, 0.0f) / CEL_HDR_ER;
    float G = s[CEL_CS_HDR_GMAX] * t * t / (1.0f + t * t);
    float gain = s[CEL_CS_HDR_LREF] * celc_pow10(s[CEL_CS_HDR_UNROLL] * G);
    CelF3 rgb = celc_mat3(s, CEL_CS_MOUT, celc_scale(XYZ, gain));
    rgb = celc_gamut_compress(s, rgb);
    float peak = s[CEL_CS_HDR_PEAK];
    rgb = celc_limit(rgb, CEL_HDR_KNEE * peak, peak);
    if (hdr == 1) return cel_f3(celc_pq_enc(rgb.x), celc_pq_enc(rgb.y), celc_pq_enc(rgb.z));
    return celc_hlg_from_nits(s, rgb);
}

CEL_DEVICE CelF3 celc_scene_to_output(CEL_GLOBAL const float* s, CelF3 rgb, CelF3 noise, int hasNoise) {
    rgb = cel_f3(celc_sanitize(rgb.x), celc_sanitize(rgb.y), celc_sanitize(rgb.z));
    noise = cel_f3(cel_clamp(cel_finite_or(noise.x, 0.0f), -8.0f, 8.0f), cel_clamp(cel_finite_or(noise.y, 0.0f), -8.0f, 8.0f),
                   cel_clamp(cel_finite_or(noise.z, 0.0f), -8.0f, 8.0f));
    CelcFilm f = celc_film_xyz(s, rgb, noise, hasNoise);
    CelF3 o = celc_output(s, f.X, f.xref);
    return cel_f3(cel_finite_or(o.x, 0.0f), cel_finite_or(o.y, 0.0f), cel_finite_or(o.z, 0.0f));
}

/* ------------------------------------------------------------------------- Texture-Only */
CEL_DEVICE CelF3 celc_display_decode_px(CEL_GLOBAL const float* s, CelF3 v) {
    int out = celc_i(s, CEL_CS_OUTPUT);
    v = cel_f3(celc_sanitize(v.x), celc_sanitize(v.y), celc_sanitize(v.z));
    if (out == CEL_OUT_709_SRGB) return cel_f3(celc_srgb_dec(v.x), celc_srgb_dec(v.y), celc_srgb_dec(v.z));
    if (out == CEL_OUT_709_G24) return cel_f3(celc_spow(v.x, 2.4f), celc_spow(v.y, 2.4f), celc_spow(v.z, 2.4f));
    if (out == CEL_OUT_PQ) return cel_f3(celc_pq_dec(v.x) * 0.01f, celc_pq_dec(v.y) * 0.01f, celc_pq_dec(v.z) * 0.01f);
    if (out == CEL_OUT_HLG) {
        v = cel_f3(cel_clamp(v.x, 0.0f, 1.5f), cel_clamp(v.y, 0.0f, 1.5f), cel_clamp(v.z, 0.0f, 1.5f));
        CelF3 E = cel_f3(celc_hlg_oetf_inv(v.x), celc_hlg_oetf_inv(v.y), celc_hlg_oetf_inv(v.z));
        float Ys = celc_dot3(s, CEL_CS_LUM, E);
        float k = Ys > 0.0f ? 1000.0f * cel_pow(Ys, 0.2f) : 0.0f;
        return celc_scale(E, k * 0.01f);
    }
    return cel_f3(celc_spow(v.x, 2.6f), celc_spow(v.y, 2.6f), celc_spow(v.z, 2.6f));
}

CEL_DEVICE CelF3 celc_display_finish_px(CEL_GLOBAL const float* s, CelF3 lin, CelF3 noise, int hasNoise) {
    int out = celc_i(s, CEL_CS_OUTPUT);
    lin = cel_f3(celc_sanitize(lin.x), celc_sanitize(lin.y), celc_sanitize(lin.z));
    if (hasNoise && s[CEL_CS_TX_GSIG] > 0.0f) {
        CelF3 nz = cel_f3(cel_clamp(cel_finite_or(noise.x, 0.0f), -8.0f, 8.0f), cel_clamp(cel_finite_or(noise.y, 0.0f), -8.0f, 8.0f),
                          cel_clamp(cel_finite_or(noise.z, 0.0f), -8.0f, 8.0f));
        if (celc_i(s, CEL_CS_MONO)) nz = cel_f3(nz.y, nz.y, nz.y);
        float Y = celc_dot3(s, CEL_CS_LUM, lin);
        float l = cel_log10(cel_fmax(Y, 1e-6f)) + 0.95f;
        float sig = s[CEL_CS_TX_GSIG] * cel_exp(-0.5f * l * l / (0.7f * 0.7f));
        if (out <= CEL_OUT_P3DCI_G26) {
            /* SDR: grain fades out toward display white and never pushes a channel above
             * max(its value, 1.0), so a legal input stays legal */
            sig *= 1.0f - cel_smoothstep(0.6f, 1.0f, Y);
            CelF3 g = cel_f3(lin.x * celc_pow10(-sig * nz.x), lin.y * celc_pow10(-sig * nz.y), lin.z * celc_pow10(-sig * nz.z));
            lin = cel_f3(cel_fmin(g.x, cel_fmax(lin.x, 1.0f)), cel_fmin(g.y, cel_fmax(lin.y, 1.0f)), cel_fmin(g.z, cel_fmax(lin.z, 1.0f)));
        } else {
            lin = cel_f3(lin.x * celc_pow10(-sig * nz.x), lin.y * celc_pow10(-sig * nz.y), lin.z * celc_pow10(-sig * nz.z));
        }
    }
    CelF3 o;
    if (out == CEL_OUT_PQ) o = cel_f3(celc_pq_enc(lin.x * 100.0f), celc_pq_enc(lin.y * 100.0f), celc_pq_enc(lin.z * 100.0f));
    else if (out == CEL_OUT_HLG) o = celc_hlg_from_nits(s, celc_scale(lin, 100.0f));
    else o = celc_encode_sdr(out, lin);
    return cel_f3(cel_finite_or(o.x, 0.0f), cel_finite_or(o.y, 0.0f), cel_finite_or(o.z, 0.0f));
}

/* ------------------------------------------------------------------------- kernels */
CEL_DEVICE void cel_k_decode(CEL_KERNEL_PARAMS) {
    CelF4 p = cel_load(in0, a->inStride[0], a->inW[0], a->inH[0], x, y);
    CelF3 r = celc_decode_px(state, cel_f3(p.x, p.y, p.z));
    cel_store(out, a->outStride, x, y, cel_f4(r.x, r.y, r.z, p.w));
    (void)in1; (void)in2; (void)in3;
}

CEL_DEVICE void cel_k_color(CEL_KERNEL_PARAMS) {
    CelF4 p = cel_load(in0, a->inStride[0], a->inW[0], a->inH[0], x, y);
    int hasNoise = a->inStride[1] != 0;
    CelF3 nz = cel_f3(0.0f, 0.0f, 0.0f);
    if (hasNoise) { CelF4 n = cel_load(in1, a->inStride[1], a->inW[1], a->inH[1], x, y); nz = cel_f3(n.x, n.y, n.z); }
    float alpha = p.w;
    if (a->inStride[2] != 0) alpha = cel_load(in2, a->inStride[2], a->inW[2], a->inH[2], x, y).w;
    CelF3 r = celc_scene_to_output(state, cel_f3(p.x, p.y, p.z), nz, hasNoise);
    cel_store(out, a->outStride, x, y, cel_f4(r.x, r.y, r.z, alpha));
    (void)in3;
}

CEL_DEVICE void cel_k_display_decode(CEL_KERNEL_PARAMS) {
    CelF4 p = cel_load(in0, a->inStride[0], a->inW[0], a->inH[0], x, y);
    CelF3 r = celc_display_decode_px(state, cel_f3(p.x, p.y, p.z));
    cel_store(out, a->outStride, x, y, cel_f4(r.x, r.y, r.z, p.w));
    (void)in1; (void)in2; (void)in3;
}

CEL_DEVICE void cel_k_display_finish(CEL_KERNEL_PARAMS) {
    CelF4 p = cel_load(in0, a->inStride[0], a->inW[0], a->inH[0], x, y);
    int hasNoise = a->inStride[1] != 0;
    CelF3 nz = cel_f3(0.0f, 0.0f, 0.0f);
    if (hasNoise) { CelF4 n = cel_load(in1, a->inStride[1], a->inW[1], a->inH[1], x, y); nz = cel_f3(n.x, n.y, n.z); }
    float alpha = p.w;
    if (a->inStride[2] != 0) alpha = cel_load(in2, a->inStride[2], a->inW[2], a->inH[2], x, y).w;
    CelF3 r = celc_display_finish_px(state, cel_f3(p.x, p.y, p.z), nz, hasNoise);
    cel_store(out, a->outStride, x, y, cel_f4(r.x, r.y, r.z, alpha));
    (void)in3;
}

#endif /* CEL_COLOR_KERNELS_H */
