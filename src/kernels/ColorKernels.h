/* Reafilm Pro - colour kernels (portable dialect, owned by core).
 *
 * Per-pixel evaluation of the spectral film model baked by core::Pipeline::prepare() into the
 * state buffer (layout: ColorState.h). Mirrors reference/model.py function by function.
 *
 * Kernels (KernelList.h), all with the uniform ABI; f[] is not used by these kernels, i[] only by color (below):
 *   decode          in0 = host source RGBA (encoded in LookParams.input)
 *                   out = scene-linear Rec.2020 x 2^exposure, then the v1.3 scene tone (Zones + Filmic
 *                         Contrast, celc_tone; off by default), alpha copied from in0
 *   color           in0 = scene-linear RGBA (after texture passes); i[1] = 1: in0 is the encoded
 *                         host source and is decoded here (Color Only plan)
 *                   in1 = grain noise field (xyz = R-,G-,B-sensitive layer; zero mean, unit variance)
 *                         -- used only when a->inStride[1] != 0 (ABI: stride 0 = input unused)
 *                   in2 = host source (alpha source) -- used when a->inStride[2] != 0, else alpha of in0
 *                   GRAIN v2 (v1.2, i[0] = 2, texture-owned GrainKernels.h): in1 = coarse, in2 = fine
 *                         grain grid, f[] / i[3..5] = the GrainKernels.h argument block; the grain is
 *                         evaluated here from the layer exposures (exposure-coupled dye clouds) and
 *                         added in density without the v1 sqrt(D) law (the model carries its own
 *                         exposure dependence); alpha = in0.w (the texture passes keep it)
 *                   in3 = film damage field (v1.1, tx_damage in DamageKernels.h) -- used when
 *                         a->inStride[3] != 0: x = negative neutral density, y = negative emulsion
 *                         removal depth (layers, 0..3), z = print neutral density, w = print emulsion
 *                         removal depth. Applied physically: see celc_negative_dmg / celc_film_xyz_dmg.
 *                   out = encoded output (LookParams.output)
 *   display_decode  Texture-Only: in0 = host source, display-encoded in LookParams.output
 *                   out = display-linear in the output primaries (SDR: 1.0 = display white;
 *                         PQ / HLG: cd/m^2 / 100), alpha copied
 *   display_finish  Texture-Only: in0 = display-linear (after texture passes), in1 = grain noise
 *                   (as above), in2 = host source (alpha, as above), in3 = damage field (as above,
 *                   display-referred approximation) -> out = display-encoded. Also re-encodes the
 *                   film-blur chain's result (no grain, no damage).
 * v1.3 stage exits (state CEL_CS_STAGE, SPEC 4.7; same kernels, branches on the state):
 *   1 Negative -> Log Scan  color writes printing density as Cineon-style code values
 *   2 Negative -> Scene     color writes the developed negative as scene light (CEL_CS_SCENE_TF / MSCENE)
 *   3 Print Only            decode reads a Log Scan (-> scene estimate for the texture passes), color
 *                           takes it back to printing density, adds grain / negative damage there, prints
 * Helper functions are prefixed celc_ (never cel_<kernel name>, which the GPU entry points use).
 */
#ifndef CEL_COLOR_KERNELS_H
#define CEL_COLOR_KERNELS_H
#include "KernelABI.h"
#include "ColorState.h"
#include "ColorTransfer.h"
#include "GrainKernels.h"   /* v1.2 grain v2 (texture agent): celg_* */

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
/* NaN -> 0; +-Inf and huge values clamp to the half-float range (an overflowing log decode of an
 * absurd code value renders as the brightest white, not as black) */
/* Magnitudes below 1e-20 (subnormals from CG / EXR blacks) are flushed to 0: they render as black
 * anyway, and on x86 CPUs every operation on a denormal takes a microcode assist (a near-black
 * frame rendered up to 6x slower through the blur pyramids). */
CEL_DEVICE float celc_sanitize(float v) { return (v != v || cel_fabs(v) < 1e-20f) ? 0.0f : cel_clamp(v, -65504.0f, 65504.0f); }
/* display-referred inputs (Texture-Only): any non-finite value -> 0 (tiny values flushed as above) */
CEL_DEVICE float celc_sanitize_d(float v) { return cel_fabs(v) < 1e-20f ? 0.0f : cel_clamp(cel_finite_or(v, 0.0f), -65504.0f, 65504.0f); }

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
/* Camera Emulation (v1.2): scene-linear Rec.2020 of one camera -> as if shot on another (fitted from measured
 * spectral responsivities, reference/camera_emulation.py): rgb' = A rgb + B (sqrt(rg), sqrt(gb), sqrt(rb)),
 * A, B already blended with the strength in prepare(). Degree-2 root polynomial: exposure-invariant (k rgb ->
 * k rgb'), neutrals unchanged (rows of [A B] sum to 1); the root terms only see the positive part of each
 * product, so out-of-gamut (negative) inputs stay finite and continuous. */
CEL_DEVICE CelF3 celc_emulate(CEL_GLOBAL const float* s, CelF3 l) {
    CelF3 o = celc_mat3(s, CEL_CS_EMU_A, l);
    CelF3 rp = cel_f3(cel_sqrt(cel_fmax(l.x * l.y, 0.0f)), cel_sqrt(cel_fmax(l.y * l.z, 0.0f)), cel_sqrt(cel_fmax(l.x * l.z, 0.0f)));
    CelF3 q = celc_mat3(s, CEL_CS_EMU_B, rp);
    return cel_f3(o.x + q.x, o.y + q.y, o.z + q.z);
}
/* ---- v1.3 Highlight Latitude (header CEL_CS_HL_*): scene-linear, in the camera's own gamut right after the
 * log decode, before Sensor Match, Exposure and everything else. With m = the largest channel and
 * x = log2(m / clip) (the sensor clip, decoded from its F-Log2 code):
 *   reconstruction  near-white pixels whose largest channel reaches the clip (x > -RECW) while the median
 *                   channel is close to it (median / m above Q0 .. Q1) have their lagging channels raised
 *                   toward m: a clip goes white instead of cyan / yellow / magenta; a saturated light where
 *                   only its own channel clips (median / m < Q0) keeps its colour
 *   desaturation    within the extension the colour is pulled toward m by DES h(t)
 *   extension       every channel x 2^(L h(t)), t = (x + W) / W in [0, 1], h = smoothstep: the last W stops
 *                   below the clip are stretched to W + L stops, C1 at both ends (slope 1 below and at the
 *                   clip), strictly increasing; values above the clip (saturated colours the camera's matrix
 *                   took past it) get the full 2^L
 * Below the extension (x <= -W and x <= -RECW) the pixel is returned unchanged (bit-exact). Mixing with the
 * neutral at m keeps the dominant hue; the common gain keeps the channel ratios; per pixel, so no halos. */
CEL_DEVICE float celc_hl_h(float t) { t = cel_clamp(t, 0.0f, 1.0f); return t * t * (3.0f - 2.0f * t); }
CEL_DEVICE CelF3 celc_latitude(CEL_GLOBAL const float* s, CelF3 l) {
    float m = celc_max3(l);
    if (!(m > 0.0f)) return l;
    float x = cel_log2(m / s[CEL_CS_HL_LC]);
    float W = s[CEL_CS_HL_W];
    if (x <= -W && x <= -CEL_HL_RECW) return l;
    float md = l.x + l.y + l.z - m - celc_min3(l);   /* median channel */
    float q = celc_hl_h((md / m - CEL_HL_Q0) * (1.0f / (CEL_HL_Q1 - CEL_HL_Q0)));
    float w = s[CEL_CS_HL_REC] * q * celc_hl_h((x + CEL_HL_RECW) * (1.0f / CEL_HL_RECW));
    float h = celc_hl_h((x + W) / W);
    float d = s[CEL_CS_HL_DES] * h;
    float k = 1.0f - (1.0f - w) * (1.0f - d);
    float g = cel_exp2(s[CEL_CS_HL_L] * h);
    return cel_f3((l.x + k * (m - l.x)) * g, (l.y + k * (m - l.y)) * g, (l.z + k * (m - l.z)) * g);
}
/* ---- v1.3 ALEXA Rendering (AL block, ColorState.h; fitted by reference/alexa_rendering.py) ----------
 * Our own parametric model of an ARRI-style LogC3 -> Rec.709 rendering (fitted against a reference
 * rendering, nothing of it shipped): a render matrix (rows sum to 1), a tone curve applied per channel on
 * the LogC3 code (contrast around the grey code; toe and shoulder that are C1 at their knees, strictly
 * increasing, bounded in [0, 1)), then smooth colour bands in Oklab. Per-channel tone gives the highlight /
 * shadow desaturation and the hue twist of bright saturated colours; the bands add what that leaves out. */
CEL_DEVICE float celc_al_knee(float r, float p) { return r / cel_pow(1.0f + cel_pow(r, p), 1.0f / p); }
/* 1 - knee(r, p) without cancellation (the deep toe): for r >= 1, with q = r^-p <= 1,
 * 1 - (1 + q)^(-1/p) = -expm1(-log1p(q) / p) (Kahan's log1p / expm1, accurate with any ulp-bounded log / exp) */
CEL_DEVICE float celc_al_knee_c(float r, float p) {
    if (!(r >= 1.0f)) return 1.0f - celc_al_knee(r, p);
    float q = cel_pow(r, -p), u = 1.0f + q;
    float l1p = u == 1.0f ? q : cel_log(u) * (q / (u - 1.0f));
    float y = -l1p / p, e = cel_exp(y);
    return e == 1.0f ? -y : -(e - 1.0f) * (y / cel_log(e));
}
/* scene-linear (render RGB) -> display code (Gamma 2.4) */
CEL_DEVICE float celc_al_tone(CEL_GLOBAL const float* t, float x) {
    float l = t[0] + t[1] * (celc_encode_transfer(CEL_IN_LOGC3, x) - CEL_AL_U_GREY);
    if (l > t[2]) return t[2] + (1.0f - t[2]) * celc_al_knee((l - t[2]) / (1.0f - t[2]), t[3]);
    if (l < t[4]) return t[4] * celc_al_knee_c((t[4] - l) / t[4], t[5]);
    return l;
}
/* cube root, one Newton step after pow: ~1 ulp on any device (pow may be 16 ulp off; the Oklab inverse cubes
 * it and its matrix cancels, so the error would reach the dark end of saturated colours) */
CEL_DEVICE float celc_cbrt(float v) {
    float y = celc_spow(v, 0.333333333333333f);
    return y == 0.0f ? y : y - (y * y * y - v) / (3.0f * y * y);
}
CEL_DEVICE CelF3 celc_al_oklab(CEL_GLOBAL const float* a, CelF3 rgb709) {
    CelF3 m = celc_mat3(a, CEL_AL_OK1, rgb709);
    return celc_mat3(a, CEL_AL_OK2, cel_f3(celc_cbrt(m.x), celc_cbrt(m.y), celc_cbrt(m.z)));
}
CEL_DEVICE CelF3 celc_al_oklab_inv(CEL_GLOBAL const float* a, CelF3 lab) {
    CelF3 m = celc_mat3(a, CEL_AL_OK2I, lab);
    return celc_mat3(a, CEL_AL_OK1I, cel_f3(m.x * m.x * m.x, m.y * m.y * m.y, m.z * m.z * m.z));
}
/* colour bands on Oklab (L, a, b); b[] = dh0[7] dh1[5] ds0[7] ds1[5] dl0[5] s2 skin(ks, hs, sw):
 *   hue shift   dh = w (F3(h).dh0 + t F2(h).dh1 - ks sin(h - hs) exp((cos(h - hs) - 1) / sw^2))
 *   chroma      C' = C exp(w (F3(h).ds0 + t F2(h).ds1 + s2 t^2))
 *   lightness   L' = L + C F2(h).dl0
 * Fn = Fourier series to order n of the hue, t = L - lgrey, w = C / (C + C0): neutrals are unchanged and
 * the map is smooth through them. */
CEL_DEVICE CelF3 celc_al_bands(CEL_GLOBAL const float* b, CelF3 lab, float lgrey) {
    float C = cel_sqrt(lab.y * lab.y + lab.z * lab.z);
    if (!(C > 1e-12f)) return lab;
    float c1 = lab.y / C, s1 = lab.z / C;
    float c2 = c1 * c1 - s1 * s1, s2 = s1 * c1 + c1 * s1;
    float c3 = c2 * c1 - s2 * s1, s3 = s2 * c1 + c2 * s1;
    float t = lab.x - lgrey;
    float w = C / (C + CEL_AL_C0);
    float dh = b[0] + b[1] * c1 + b[2] * s1 + b[3] * c2 + b[4] * s2 + b[5] * c3 + b[6] * s3
             + t * (b[7] + b[8] * c1 + b[9] * s1 + b[10] * c2 + b[11] * s2);
    float ch = cel_cos(b[31]), sh = cel_sin(b[31]);
    float cd = c1 * ch + s1 * sh, sd = s1 * ch - c1 * sh;
    dh = (dh - b[30] * sd * cel_exp((cd - 1.0f) / (b[32] * b[32]))) * w;
    float ds = (b[12] + b[13] * c1 + b[14] * s1 + b[15] * c2 + b[16] * s2 + b[17] * c3 + b[18] * s3
              + t * (b[19] + b[20] * c1 + b[21] * s1 + b[22] * c2 + b[23] * s2) + b[29] * t * t) * w;
    float dl = C * (b[24] + b[25] * c1 + b[26] * s1 + b[27] * c2 + b[28] * s2);
    float g = cel_exp(ds), cr = cel_cos(dh), sr = cel_sin(dh);
    return cel_f3(lab.x + dl, g * (lab.y * cr - lab.z * sr), g * (lab.y * sr + lab.z * cr));
}
/* display-linear channel -> (0, 1]: C1 soft floor k exp(v / k - 1) below k = CEL_AL_FLOOR (under the neutral
 * black of the rendering, so greys are unchanged): a channel the colour bands take to 0 or below stays a
 * small positive value instead of a clipped one, and the display encoding's infinite slope at 0 cannot
 * magnify the last-ulp differences between devices */
CEL_DEVICE float celc_al_floor(float v) {
    v = cel_fmin(cel_finite_or(v, 0.0f), 1.0f);
    return v < CEL_AL_FLOOR ? CEL_AL_FLOOR * cel_exp(cel_fmax(v / CEL_AL_FLOOR, -80.0f) - 1.0f) : v;
}
/* scene-linear Rec.2020 -> display-linear Rec.709 in (0, 1] (ALEXA Rec.709 stage) */
CEL_DEVICE CelF3 celc_alexa_render(CEL_GLOBAL const float* s, CelF3 x) {
    CEL_GLOBAL const float* a = s + celc_i(s, CEL_CS_AL_OFF);
    CelF3 w = celc_mat3(a, CEL_AL_MR, x);
    CEL_GLOBAL const float* t = a + CEL_AL_TONE;
    CelF3 e = cel_f3(cel_fmax(celc_al_tone(t, w.x), 0.0f), cel_fmax(celc_al_tone(t, w.y), 0.0f), cel_fmax(celc_al_tone(t, w.z), 0.0f));
    CelF3 d = cel_f3(cel_pow(e.x, 2.4f), cel_pow(e.y, 2.4f), cel_pow(e.z, 2.4f));
    CelF3 o = celc_al_oklab_inv(a, celc_al_bands(a + CEL_AL_RB, celc_al_oklab(a, d), a[CEL_AL_LG_R]));
    return cel_f3(celc_al_floor(o.x), celc_al_floor(o.y), celc_al_floor(o.z));
}
/* ALEXA Colour tone term (v1.3 ARRI look II, fitted through the default film by reference/alexa_film_fit.py):
 * per scene stop of the luminance Y, gains on R and B (renormalised to Y: greys stay at their level) that keep
 * the film's whites neutral and its shadows a touch cool -- faded out with saturation (1 - min/max) so that only
 * near-neutrals are balanced and coloured skies / skin keep their hue -- and a chroma factor about the neutral of Y */
CEL_DEVICE CelF3 celc_al_tone_term(CEL_GLOBAL const float* a, float Y, CelF3 o) {
    float st = cel_log2(Y * (1.0f / 0.18f));
    float mx = celc_max3(o);
    float wn = mx > 0.0f ? celc_hl_h(1.0f - (mx - celc_min3(o)) / (mx * CEL_AL_TN_SAT)) : 0.0f;   /* near-neutrals only */
    float gr = cel_exp2(wn * celc_lut1(a, CEL_AL_TN, CEL_AL_TN_N, 0, CEL_AL_TN_X0, CEL_AL_TN_X1, st));
    float gb = cel_exp2(wn * celc_lut1(a, CEL_AL_TN, CEL_AL_TN_N, 1, CEL_AL_TN_X0, CEL_AL_TN_X1, st));
    float kc = celc_lut1(a, CEL_AL_TN, CEL_AL_TN_N, 2, CEL_AL_TN_X0, CEL_AL_TN_X1, st);
    o = cel_f3(o.x * gr, o.y, o.z * gb);
    float Yo = celc_dot3(a, CEL_AL_Y2020, o);
    if (Yo > 1e-12f) o = celc_scale(o, Y / Yo);
    return cel_f3(Y + kc * (o.x - Y), Y + kc * (o.y - Y), Y + kc * (o.z - Y));
}
/* ALEXA Colour: the rendering's colour character without its tone curve, scene-linear Rec.2020 -> Rec.2020.
 * Evaluated at grey luminance (n = x 0.18 / Y) and scaled back, so f(k x) = k f(x) (exposure-stable);
 * neutrals and non-positive luminance pass unchanged. Blended with the strength CEL_CS_AL_COLOUR. */
CEL_DEVICE CelF3 celc_alexa_colour(CEL_GLOBAL const float* s, CelF3 x) {
    CEL_GLOBAL const float* a = s + celc_i(s, CEL_CS_AL_OFF);
    float Y = celc_dot3(a, CEL_AL_Y2020, x);
    if (!(Y > 1e-10f)) return x;
    /* the colour model is fitted inside the Rec.709 gamut: it acts fully there and fades out smoothly for
     * colours beyond it (saturated lights, noise), down to unchanged at a Rec.709 channel of -0.25 x the
     * largest, where the grey normalisation and the bands would extrapolate without bound */
    CelF3 v = celc_mat3(a, CEL_AL_M2020_709, x);
    float mx = cel_fmax(cel_fabs(v.x), cel_fmax(cel_fabs(v.y), cel_fabs(v.z)));
    float r = cel_fmin(cel_clamp(1.0f + cel_fmin(v.x, cel_fmin(v.y, v.z)) / (0.25f * mx), 0.0f, 1.0f),
                       cel_clamp((Y - 0.03f * mx) / (0.03f * mx), 0.0f, 1.0f));   /* (Rec.709: Y >= 0.072 max) */
    /* and above the camera's range (LogC3 clips near 55 x linear): fade out between Y 64 and 128, so
     * super-whites and hostile values pass unchanged instead of carrying rounding at their magnitude */
    r = cel_fmin(r, cel_clamp((128.0f - Y) / 64.0f, 0.0f, 1.0f));
    if (!(r > 0.0f)) return x;
    CelF3 l0 = celc_al_oklab(a, celc_scale(v, 0.18f / Y));
    /* and beyond the fitted chroma (Oklab C <= ~0.45 at grey inside Rec.709): fade between C 0.5 and 0.8 */
    float cf = cel_clamp((0.8f - cel_sqrt(l0.y * l0.y + l0.z * l0.z)) / 0.3f, 0.0f, 1.0f);
    r = cel_fmin(r, cf);
    if (!(r > 0.0f)) return x;
    CelF3 lab = celc_al_bands(a + CEL_AL_CB, l0, a[CEL_AL_LG_C]);
    CelF3 o = celc_al_tone_term(a, Y, celc_scale(celc_mat3(a, CEL_AL_M709_2020, celc_al_oklab_inv(a, lab)), Y / 0.18f));
    float k = s[CEL_CS_AL_COLOUR] * r * r * (3.0f - 2.0f * r);
    return cel_f3(x.x + k * (o.x - x.x), x.y + k * (o.y - x.y), x.z + k * (o.z - x.z));
}
/* ---- v1.3 stage exits: the neutral stage axis (NS block, ColorState.h) ---------------------------
 * Rows 0..2 = printing density of each print layer for a neutral scene at the knots, row 3 = the
 * scene-referred log10 level the knot maps back to; all rows strictly increasing, so every lookup is
 * an exact piecewise-linear inverse of the other (Log Scan -> Print Only round trips exactly).
 * celc_ns_find: fractional knot index of v in row r (binary search; linear extrapolation outside,
 * bounded); celc_ns_at: value of row r at fractional knot index t (same extrapolation). */
CEL_DEVICE float celc_ns_find(CEL_GLOBAL const float* s, int r, float v) {
    int n = celc_i(s, CEL_CS_NS_N);
    CEL_GLOBAL const float* p = s + celc_i(s, CEL_CS_NS_OFF) + r * n;
    float t;
    if (!(v > p[0])) t = (v - p[0]) / (p[1] - p[0]);
    else if (!(v < p[n - 1])) t = (float)(n - 1) + (v - p[n - 1]) / (p[n - 1] - p[n - 2]);
    else {
        int lo = 0, hi = n - 1;
        while (hi - lo > 1) {
            int m = (lo + hi) / 2;
            if (p[m] <= v) lo = m; else hi = m;
        }
        t = (float)lo + (v - p[lo]) / (p[hi] - p[lo]);
    }
    return cel_clamp(cel_finite_or(t, 0.0f), -4.0f * (float)n, 5.0f * (float)n);
}
CEL_DEVICE float celc_ns_at(CEL_GLOBAL const float* s, int r, float t) {
    int n = celc_i(s, CEL_CS_NS_N);
    CEL_GLOBAL const float* p = s + celc_i(s, CEL_CS_NS_OFF) + r * n;
    int i = cel_imin(cel_imax((int)cel_floor(t), 0), n - 2);
    return p[i] + (p[i + 1] - p[i]) * (t - (float)i);
}
/* printing density (above the clear negative) -> scene-referred linear level of each channel. With
 * colour masking on, the first-order masking matrix MMASK acts on the log10 levels (it is the inverse of
 * the chain's log-log Jacobian at grey, rows summing to 1): exposure-invariant, neutrals unchanged, never
 * negative, and well conditioned for saturated colours. */
CEL_DEVICE CelF3 celc_ns_scene(CEL_GLOBAL const float* s, CelF3 pd) {
    CelF3 y = cel_f3(celc_ns_at(s, 3, celc_ns_find(s, 0, pd.x)), celc_ns_at(s, 3, celc_ns_find(s, 1, pd.y)),
                     celc_ns_at(s, 3, celc_ns_find(s, 2, pd.z)));
    if (s[CEL_CS_MASK_ON] > 0.5f) y = celc_mat3(s, CEL_CS_MMASK, y);
    return cel_f3(celc_pow10(y.x), celc_pow10(y.y), celc_pow10(y.z));
}
/* scene-referred linear level -> printing density (exact inverse of celc_ns_scene) */
CEL_DEVICE CelF3 celc_ns_density(CEL_GLOBAL const float* s, CelF3 l) {
    CelF3 y = cel_f3(cel_log10(cel_fmax(l.x, 1e-10f)), cel_log10(cel_fmax(l.y, 1e-10f)), cel_log10(cel_fmax(l.z, 1e-10f)));
    if (s[CEL_CS_MASK_ON] > 0.5f) y = celc_mat3(s, CEL_CS_MMASKI, y);
    return cel_f3(celc_ns_at(s, 0, celc_ns_find(s, 3, y.x)), celc_ns_at(s, 1, celc_ns_find(s, 3, y.y)),
                  celc_ns_at(s, 2, celc_ns_find(s, 3, y.z)));
}
/* Log Scan code value <-> printing density (ColorState.h CEL_LOGSCAN_*) */
CEL_DEVICE float celc_logscan_pd(float v) { return (v * 1023.0f - CEL_LOGSCAN_CV0) * CEL_LOGSCAN_STEP; }
CEL_DEVICE float celc_logscan_code(float pd) { return (CEL_LOGSCAN_CV0 + pd * (1.0f / CEL_LOGSCAN_STEP)) * (1.0f / 1023.0f); }

/* ---- v1.3 Tone: Zones (HDR-palette style) + Filmic Contrast, in scene-linear light after Exposure ----------
 * x = log2(Y / 0.18) of the scene luminance (Rec.2020), E(x) = the TN table (stops; built in prepare(): the
 * zone offsets with smooth log2 weights and the contrast about its pivot, integrated with a slope floor so
 * x + E(x) is strictly increasing). The pixel is scaled by 2^E (ratio-preserving: neutrals stay neutral, hue
 * and the scene's channel ratios are kept); with Filmic Contrast the channel ratios to the luminance are
 * additionally raised to kappa = 1 + 0.5 (contrast - 1) and renormalised to the same luminance (half the
 * per-channel log contrast: the film keeps its own colour density while the tone scale changes). */
CEL_DEVICE float celc_tone_e(CEL_GLOBAL const float* s, float x) {
    int n = celc_i(s, CEL_CS_TONE_N), off = celc_i(s, CEL_CS_TONE_OFF);
    float x0 = s[CEL_CS_TONE_X0], x1 = s[CEL_CS_TONE_X1];
    if (x < x0) return s[off] + s[CEL_CS_TONE_K0] * (x - x0);
    if (x > x1) return s[off + n - 1] + s[CEL_CS_TONE_K1] * (x - x1);
    return celc_lut1(s, off, n, 0, x0, x1, x);
}
CEL_DEVICE float celc_tone_q(float q, float k) { return q > 0.0f ? cel_exp2(k * cel_log2(q)) : q; }
CEL_DEVICE CelF3 celc_tone(CEL_GLOBAL const float* s, CelF3 l) {
    float Y = CEL_TONE_LW_R * l.x + CEL_TONE_LW_G * l.y + CEL_TONE_LW_B * l.z;
    float x = cel_clamp(cel_log2(cel_fmax(Y, 1.6763806e-10f) * (1.0f / 0.18f)), -30.0f, 30.0f);
    float g = cel_exp2(celc_tone_e(s, x));
    float k = s[CEL_CS_TONE_KAPPA];
    if (k != 1.0f && Y > 1e-10f) {
        float iy = 1.0f / Y;
        CelF3 q = cel_f3(celc_tone_q(l.x * iy, k), celc_tone_q(l.y * iy, k), celc_tone_q(l.z * iy, k));
        float m = CEL_TONE_LW_R * q.x + CEL_TONE_LW_G * q.y + CEL_TONE_LW_B * q.z;
        if (m > 0.05f) l = celc_scale(q, Y / m);
    }
    return celc_scale(l, g);
}

CEL_DEVICE CelF3 celc_decode_px(CEL_GLOBAL const float* s, CelF3 e) {
    if (celc_i(s, CEL_CS_STAGE) == 3) {
        /* Print Only: the input is a Log Scan; textures then work on the scene-referred level it maps back
         * to (the same neutral axis the colour pass re-encodes exactly) */
        CelF3 pd = cel_f3(celc_logscan_pd(celc_sanitize(e.x)), celc_logscan_pd(celc_sanitize(e.y)), celc_logscan_pd(celc_sanitize(e.z)));
        CelF3 l = celc_ns_scene(s, pd);   /* the scene estimate of Negative -> Scene (masked) */
        float g0 = s[CEL_CS_GAIN];
        return cel_f3(celc_sanitize(l.x * g0), celc_sanitize(l.y * g0), celc_sanitize(l.z * g0));
    }
    int sp = celc_i(s, CEL_CS_INPUT);
    CelF3 l = cel_f3(celc_sanitize(celc_decode_transfer(sp, celc_sanitize(e.x))), celc_sanitize(celc_decode_transfer(sp, celc_sanitize(e.y))),
                     celc_sanitize(celc_decode_transfer(sp, celc_sanitize(e.z))));
    if (s[CEL_CS_HL_ON] > 0.5f) l = celc_latitude(s, l);   /* v1.3 Highlight Latitude (camera gamut, off = 0) */
    l = celc_mat3(s, CEL_CS_MIN, l);
    if (s[CEL_CS_EMU_ON] > 0.5f) l = celc_emulate(s, l);
    if (s[CEL_CS_AL_COLOUR] > 0.0f) l = celc_alexa_colour(s, l);   /* v1.3 ALEXA Colour (off = 0) */
    float g = s[CEL_CS_GAIN];
    CelF3 r = cel_f3(celc_sanitize(l.x * g), celc_sanitize(l.y * g), celc_sanitize(l.z * g));
    if (s[CEL_CS_TONE_ON] > 0.5f) {   /* v1.3 Tone: Zones + Filmic Contrast, scene-referred, before the negative */
        r = celc_tone(s, r);
        r = cel_f3(celc_sanitize(r.x), celc_sanitize(r.y), celc_sanitize(r.z));
    }
    return r;
}

/* ------------------------------------------------------------------------- spectral exposure */
/* octahedral parameterisation of a scene RGB direction (opponent basis): (u, v, L1 magnitude) */
CEL_DEVICE CelF3 celc_oct_uv(CelF3 rgb) {
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
    return cel_f3(u, v, L1);
}

/* octahedral direction of scene RGB (opponent basis) -> layer log10 exposures (log10 L1 + log10 K) */
CEL_DEVICE CelF3 celc_exposures(CEL_GLOBAL const float* s, CelF3 rgb) {
    CelF3 o = celc_oct_uv(rgb);
    float u = o.x, v = o.y, L1 = o.z;
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
    /* the table holds log10 K (bilinear in the log domain: the small minority-layer K of saturated
     * directions curves strongly across texels) */
    float l1 = cel_log10(cel_fmax(L1, 1e-30f));
    return cel_f3(cel_fmax(l1 + p00[0] * w00 + p10[0] * w10 + p01[0] * w01 + p11[0] * w11, CEL_LOGK_FLOOR),
                  cel_fmax(l1 + p00[1] * w00 + p10[1] * w10 + p01[1] * w01 + p11[1] * w11, CEL_LOGK_FLOOR),
                  cel_fmax(l1 + p00[2] * w00 + p10[2] * w10 + p01[2] * w01 + p11[2] * w11, CEL_LOGK_FLOOR));
}

typedef struct { CelF3 d; float silver; float xref; } CelcNeg;

/* ---- film damage (v1.1): emulsion removal by a scratch, per dye layer ------------------------
 * A scratch `depth` layers deep (0..3, fractional = partly through a layer) removes the dye of the
 * layers it cuts through, top layer first. Camera negative (and reversal) emulsions carry the
 * blue-sensitive (yellow dye) layer on top, then green (magenta), then red (cyan) next to the base;
 * colour print film carries the green-sensitive (magenta) layer on top, then red (cyan), then blue
 * (yellow). Black & white films have one layer. Returns the removed fraction per layer (r, g, b =
 * red-, green-, blue-sensitive layer). */
CEL_DEVICE CelF3 celc_dmg_removal(float depth, int printFilm, int mono) {
    if (mono) { float r = cel_saturate(depth); return cel_f3(r, r, r); }
    if (printFilm) return cel_f3(cel_saturate(depth - 1.0f), cel_saturate(depth), cel_saturate(depth - 2.0f));
    return cel_f3(cel_saturate(depth - 2.0f), cel_saturate(depth - 1.0f), cel_saturate(depth));
}
CEL_DEVICE CelF3 celc_dmg_remove(CelF3 d, CelF3 r) {
    return cel_f3(d.x * (1.0f - r.x), d.y * (1.0f - r.y), d.z * (1.0f - r.z));
}

/* layer exposures -> interlayer (DIR) -> curves -> grain -> retained silver (celc_negative), with
 * film damage on the negative: dmgNd = neutral density in the light path (dust, hair, base
 * scratch: acts like retained silver, i.e. it holds back printer / scanner / projector light),
 * dmgDepth = emulsion removal depth (scratch through the dye layers, after grain: the dye clouds are
 * gone where the emulsion is). Zero damage is bit-identical to celc_negative. */
CEL_DEVICE CelcNeg celc_negative_dmg(CEL_GLOBAL const float* s, CelF3 rgb, CelF3 noise, int hasNoise, float dmgNd, float dmgDepth) {
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
    if (hasNoise == CEL_GRAIN_V2 && s[CEL_CS_GRAIN_ON] > 0.5f) {
        /* grain v2: noise is the dye-cloud density fluctuation in units of the grain sigma at grey */
        d.x += s[CEL_CS_GSIG + 0] * noise.x;
        d.y += s[CEL_CS_GSIG + 1] * noise.y;
        d.z += s[CEL_CS_GSIG + 2] * noise.z;
    } else if (hasNoise && s[CEL_CS_GRAIN_ON] > 0.5f) {
        CelF3 nz = noise;
        if (celc_i(s, CEL_CS_MONO)) nz = cel_f3(noise.y, noise.y, noise.y);
        d.x += s[CEL_CS_GSIG + 0] * cel_sqrt(cel_fmax(d.x - dmin.x, 0.0f) + CEL_GRAIN_EPS) * nz.x;
        d.y += s[CEL_CS_GSIG + 1] * cel_sqrt(cel_fmax(d.y - dmin.y, 0.0f) + CEL_GRAIN_EPS) * nz.y;
        d.z += s[CEL_CS_GSIG + 2] * cel_sqrt(cel_fmax(d.z - dmin.z, 0.0f) + CEL_GRAIN_EPS) * nz.z;
    }
    if (dmgDepth > 0.0f) d = celc_dmg_remove(d, celc_dmg_removal(dmgDepth, 0, celc_i(s, CEL_CS_MONO)));
    CelcNeg r;
    r.d = d;
    r.silver = s[CEL_CS_NB] * (s[CEL_CS_LW] * cel_fmax(d.x - dmin.x, 0.0f) + s[CEL_CS_LW + 1] * cel_fmax(d.y - dmin.y, 0.0f) +
                               s[CEL_CS_LW + 2] * cel_fmax(d.z - dmin.z, 0.0f));
    if (dmgNd > 0.0f) r.silver += dmgNd;
    r.xref = xref;
    return r;
}
CEL_DEVICE CelcNeg celc_negative(CEL_GLOBAL const float* s, CelF3 rgb, CelF3 noise, int hasNoise) {
    return celc_negative_dmg(s, rgb, noise, hasNoise, 0.0f, 0.0f);
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

/* scene -> film XYZ (celc_film_xyz), with film damage dmg = (negative neutral density, negative emulsion depth, print neutral density,
 * print emulsion depth), see the colour kernel's in3:
 *   Print     negative-side damage holds back printer light (dust / hair / base scratch print WHITE,
 *             an emulsion scratch prints in the colours of the missing layers: yellow -> red -> dark);
 *             print-side damage sits in the projected print (dust / hair / base scratch DARK, an
 *             emulsion scratch shows the layers below: green -> yellow -> clear white).
 *   Reversal  the camera film is the projected film: both sides act on it (all dust is dark).
 *   Lab Scan  negative-side damage is scanned and inverted (white); print-side damage is applied to
 *             the scanned positive's densities (dark), like dirt on a print of the scan. */
/* the print: log10 print-layer exposure h through the negative (PL block, before the printer light
 * offsets), retained silver / neutral density `silver` of the negative -> projected D65 display XYZ */
/* ---- v1.3 Dye Saturation (subtractive): the dye amounts D of the film that is looked at (print, reversal
 * original or the scan's display densities) are split into the dyes of the NEUTRAL with the same weighted
 * density t (DN block) and the deviation d = D - N (the colour), which is scaled by S. A neutral density
 * then restores the colour's luminance (the exponential transmittance would otherwise brighten a scaled
 * spread), and Richness adds the dye a denser colour carries: rho (S - 1) m on every layer, m = sum of the
 * smooth positive parts of d (the colour's own excess dye), so a more saturated colour gets deeper and
 * darker, a less saturated one lighter. Neutrals (d = 0, m = 0), print white (D-min) and black are
 * unchanged; S = 0 is the neutral of the same luminance. Balance: S x 2^(bal u), u = +1 in the
 * highlights .. -1 in the shadows. */
CEL_DEVICE float celc_dsat_pos(float d) { return 0.5f * (d + cel_sqrt(d * d + CEL_DSAT_EPS * CEL_DSAT_EPS)) - 0.5f * CEL_DSAT_EPS; }
/* display luminance of dye amounts D: proj = 1 spectral projection (print, reversal), 0 = scan (RGB transmittance) */
CEL_DEVICE float celc_dsat_y(CEL_GLOBAL const float* s, CelF3 D, int proj) {
    CelF3 v = proj ? celc_project(s, D, 0.0f) : celc_pow10_3(cel_f3(-D.x, -D.y, -D.z));
    return s[CEL_CS_MXYZ + 3] * v.x + s[CEL_CS_MXYZ + 4] * v.y + s[CEL_CS_MXYZ + 5] * v.z;
}
CEL_DEVICE CelF3 celc_dsat(CEL_GLOBAL const float* s, CelF3 D, int proj) {
    if (!(s[CEL_CS_DSAT_ON] > 0.5f)) return D;
    float t = celc_dot3(s, CEL_CS_LW, D);
    float tt = cel_clamp(t, s[CEL_CS_DSAT_T0], s[CEL_CS_DSAT_T1]);
    CelF3 N = celc_lut1_3(s, celc_i(s, CEL_CS_DSAT_OFF), celc_i(s, CEL_CS_DSAT_N), s[CEL_CS_DSAT_T0], s[CEL_CS_DSAT_T1],
                          cel_f3(tt, tt, tt));
    float z = (s[CEL_CS_DSAT_TG] - t) / s[CEL_CS_DSAT_W];
    float S = s[CEL_CS_DSAT_S] * cel_exp2(s[CEL_CS_DSAT_BAL] * z / cel_sqrt(1.0f + z * z));
    CelF3 d = cel_f3(D.x - N.x, D.y - N.y, D.z - N.z);
    float m = celc_dsat_pos(d.x) + celc_dsat_pos(d.y) + celc_dsat_pos(d.z);
    CelF3 R = cel_f3(N.x + S * d.x, N.y + S * d.y, N.z + S * d.z);
    /* the scaled spread keeps the colour's projected luminance: a neutral density scales every band's
     * transmittance alike, so log10(Y'/Y) restores Y exactly (the exponential transmittance would
     * otherwise brighten a saturated colour) */
    float y0 = celc_dsat_y(s, D, proj), y1 = celc_dsat_y(s, R, proj);
    float add = cel_log10(cel_fmax(y1, 1e-30f) / cel_fmax(y0, 1e-30f)) + s[CEL_CS_DSAT_RICH] * (S - 1.0f) * m;
    return cel_f3(R.x + add, R.y + add, R.z + add);
}

/* the print's dye amounts (before dye saturation and damage) */
CEL_DEVICE CelF3 celc_print_dyes(CEL_GLOBAL const float* s, CelF3 h, float silver) {
    h = cel_f3(h.x + s[CEL_CS_OFF] - silver, h.y + s[CEL_CS_OFF + 1] - silver, h.z + s[CEL_CS_OFF + 2] - silver);
    return celc_lut1_3(s, celc_i(s, CEL_CS_PC_OFF), celc_i(s, CEL_CS_PC_N), s[CEL_CS_PC_U0], s[CEL_CS_PC_U1], h);
}
CEL_DEVICE CelF3 celc_print_xyz(CEL_GLOBAL const float* s, CelF3 h, float silver, CelF4 dmg, int mono) {
    CelF3 P = celc_dsat(s, celc_print_dyes(s, h, silver), 1);
    if (dmg.w > 0.0f) P = celc_dmg_remove(P, celc_dmg_removal(dmg.w, 1, mono));
    float ps = s[CEL_CS_PB] * (s[CEL_CS_LW] * cel_fmax(P.x - s[CEL_CS_PMIN], 0.0f) +
                               s[CEL_CS_LW + 1] * cel_fmax(P.y - s[CEL_CS_PMIN + 1], 0.0f) +
                               s[CEL_CS_LW + 2] * cel_fmax(P.z - s[CEL_CS_PMIN + 2], 0.0f));
    if (dmg.z > 0.0f) ps += dmg.z;
    return celc_mat3(s, CEL_CS_MXYZ, celc_project(s, P, ps));
}

/* Lab Scan: the developed negative -> display densities of the scanned positive (before dye saturation and damage) */
CEL_DEVICE CelF3 celc_scan_dd(CEL_GLOBAL const float* s, CelcNeg ng) {
    CelF3 ls = celc_lut3(s, celc_i(s, CEL_CS_PL_OFF), celc_i(s, CEL_CS_PL_N), s[CEL_CS_PL_MAX], ng.d);
    int sco = celc_i(s, CEL_CS_SC_OFF), scn = celc_i(s, CEL_CS_SC_N);
    CelF3 v = cel_f3(celc_lut1(s, sco, scn, 0, s[CEL_CS_SD0], s[CEL_CS_SD1], -ls.x + ng.silver) + s[CEL_CS_OFF],
                     celc_lut1(s, sco, scn, 1, s[CEL_CS_SD0 + 1], s[CEL_CS_SD1 + 1], -ls.y + ng.silver) + s[CEL_CS_OFF + 1],
                     celc_lut1(s, sco, scn, 2, s[CEL_CS_SD0 + 2], s[CEL_CS_SD1 + 2], -ls.z + ng.silver) + s[CEL_CS_OFF + 2]);
    /* lab colour correction: 3x3 + root-polynomial terms (exposure homogeneous, rows sum to 1 so
     * neutrals stay neutral) */
    CelF3 lin = celc_pow10_3(v);
    CelF3 e = celc_mat3(s, CEL_CS_MSCAN, lin);
    CelF3 rp = cel_f3(cel_sqrt(lin.x * lin.y), cel_sqrt(lin.y * lin.z), cel_sqrt(lin.x * lin.z));
    CelF3 e2 = celc_mat3(s, CEL_CS_MSCAN2, rp);
    e = cel_f3(e.x + e2.x, e.y + e2.y, e.z + e2.z);
    float fl = CEL_SCAN_FLOOR * CEL_SCAN_FLOOR;
    e = cel_f3(0.5f * (e.x + cel_sqrt(e.x * e.x + fl)), 0.5f * (e.y + cel_sqrt(e.y * e.y + fl)), 0.5f * (e.z + cel_sqrt(e.z * e.z + fl)));
    CelF3 w = cel_f3(cel_log10(e.x), cel_log10(e.y), cel_log10(e.z));
    return celc_lut1_3(s, celc_i(s, CEL_CS_PC_OFF), celc_i(s, CEL_CS_PC_N), s[CEL_CS_PC_U0], s[CEL_CS_PC_U1], w);
}

CEL_DEVICE CelcFilm celc_film_xyz_dmg(CEL_GLOBAL const float* s, CelF3 rgb, CelF3 noise, int hasNoise, CelF4 dmg) {
    int kind = celc_i(s, CEL_CS_KIND);
    int mono = celc_i(s, CEL_CS_MONO);
    /* reversal: the camera original is projected, so print-side damage lands on the same film */
    float negNd = kind == 2 ? dmg.x + dmg.z : dmg.x;
    float negDepth = kind == 2 ? cel_fmax(dmg.y, dmg.w) : dmg.y;
    CelcNeg ng = celc_negative_dmg(s, rgb, noise, hasNoise, negNd, negDepth);
    CelF3 X;
    if (kind == 0) {
        CelF3 h = celc_lut3(s, celc_i(s, CEL_CS_PL_OFF), celc_i(s, CEL_CS_PL_N), s[CEL_CS_PL_MAX], ng.d);
        X = celc_print_xyz(s, h, ng.silver, dmg, mono);
    } else if (kind == 2) {
        X = celc_mat3(s, CEL_CS_MXYZ, celc_project(s, celc_dsat(s, ng.d, 1), ng.silver));
    } else {
        CelF3 Dd = celc_dsat(s, celc_scan_dd(s, ng), 0);
        if (dmg.w > 0.0f) Dd = celc_dmg_remove(Dd, celc_dmg_removal(dmg.w, 1, mono));
        if (dmg.z > 0.0f) Dd = cel_f3(Dd.x + dmg.z, Dd.y + dmg.z, Dd.z + dmg.z);
        X = celc_mat3(s, CEL_CS_MXYZ, celc_pow10_3(cel_f3(-Dd.x, -Dd.y, -Dd.z)));
    }
    CelcFilm r;
    r.X = X;
    r.xref = ng.xref;
    return r;
}
CEL_DEVICE CelcFilm celc_film_xyz(CEL_GLOBAL const float* s, CelF3 rgb, CelF3 noise, int hasNoise) {
    return celc_film_xyz_dmg(s, rgb, noise, hasNoise, cel_f4(0.0f, 0.0f, 0.0f, 0.0f));
}

/* ------------------------------------------------------------------------- hue hold */
/* Every per-layer log-domain tone scale bends a colour's path toward white (highlights) or black
 * (shadows): orange slides to yellow, red to pink, sky blue to cyan. The hold keeps a colour on the
 * hue the stock gives it at 18 % grey luminance (anchor, per scene chromaticity, points 0): in
 * CIELAB coordinates (f = CIELAB f(XYZ / W_D65), a = 2.5 (f_x - f_y), b = f_y - f_z) the colour's (a, b)
 * relative to the film's own neutral at the same exposure and luminance (crossover tints and
 * printer lights stay) is turned toward the anchor's (relative to the grey neutral) by
 * t = hold * w * (1 - exp(-K stops^2)), its length raised by a monotonic chroma hold; Y unchanged.
 * == reference/model.py Pipeline.apply_hold */
/* CIELAB f(t) and its inverse (cube root above (6/29)^3, linear segment below) */
CEL_DEVICE float celc_labf(float x) { return x > 0.008856451679f ? cel_pow(x, 0.333333333333333f) : 7.787037037f * x + 0.137931034f; }
CEL_DEVICE float celc_labfinv(float f) { return f > 0.206896552f ? f * f * f : (f - 0.137931034f) * 0.128418549f; }
/* celc_hold_k: holdScale k in [0, 1]: 1 = the stock's hold; film damage lowers it (a scratch or a speck must keep
 * the colour the damage gives it, not be turned back toward the scene's hue) */
CEL_DEVICE CelF3 celc_hold_k(CEL_GLOBAL const float* s, CelF3 X, float xref, CelF3 rgb, float holdScale) {
    float hold = s[CEL_CS_HOLD] * holdScale;
    if (!(hold > 0.0f)) return X;
    float Ys = 0.26270021f * rgb.x + 0.67799807f * rgb.y + 0.05930172f * rgb.z;
    if (!(Ys > 1e-10f) || !(X.y > 1e-10f)) return X;
    CelF3 o = celc_oct_uv(rgb);
    int n = celc_i(s, CEL_CS_HOLD_A_N), off = celc_i(s, CEL_CS_HOLD_A_OFF);
    float tu = cel_clamp((o.x + 1.0f) * 0.5f * (float)(n - 1), 0.0f, (float)(n - 1));
    float tv = cel_clamp((o.y + 1.0f) * 0.5f * (float)(n - 1), 0.0f, (float)(n - 1));
    int i = cel_imin((int)cel_floor(tu), n - 2), j = cel_imin((int)cel_floor(tv), n - 2);
    float fu = tu - (float)i, fv = tv - (float)j;
    CEL_GLOBAL const float* p00 = s + off + (j * n + i) * 4;
    CEL_GLOBAL const float* p10 = p00 + 4;
    CEL_GLOBAL const float* p01 = p00 + n * 4;
    CEL_GLOBAL const float* p11 = p01 + 4;
    float w00 = (1.0f - fu) * (1.0f - fv), w10 = fu * (1.0f - fv), w01 = (1.0f - fu) * fv, w11 = fu * fv;
    float d0 = p00[0] * w00 + p10[0] * w10 + p01[0] * w01 + p11[0] * w11;
    float d1 = p00[1] * w00 + p10[1] * w10 + p01[1] * w01 + p11[1] * w11;
    float aw = p00[3] * w00 + p10[3] * w10 + p01[3] * w01 + p11[3] * w11;
    float st = cel_log2(Ys / 0.18f);
    /* g: hold strength without the anchor validity weight aw (the chroma hold uses g: aw falls along
     * a saturation ramp toward the gamut edge and would fold the chroma); hue turn t = g * aw */
    float so = cel_fmax(cel_fabs(st) - s[CEL_CS_HOLD_ONSET], 0.0f);   /* v1.2: no hold within +-onset stops of grey */
    float g = hold * (1.0f - cel_exp(-CEL_HOLD_K * so * so));
    int nno = celc_i(s, CEL_CS_HOLD_N_OFF), nnn = celc_i(s, CEL_CS_HOLD_N_N);
    CelF3 nt = celc_lut1_3(s, nno, nnn, CEL_HOLD_X0, CEL_HOLD_X1, cel_f3(xref, xref, xref));
    float cy = celc_labf(X.y);
    float fx = celc_labf(X.x / CEL_D65_X), fz = celc_labf(X.z / CEL_D65_Z);
    /* (a, b) scaled like CIELAB (a* = 500 (f_x - f_y), b* = 200 (f_y - f_z)): angles are CIELAB hues */
    float Yn = X.y;
    float nx = celc_labf(nt.x * Yn / CEL_D65_X), nz = celc_labf(nt.z * Yn / CEL_D65_Z);
    float an0 = 2.5f * (nx - cy), an1 = cy - nz;
    float c0 = 2.5f * (fx - cy) - an0, c1 = (cy - fz) - an1;
    float cn = cel_sqrt(c0 * c0 + c1 * c1), dn = cel_sqrt(d0 * d0 + d1 * d1);
    if (!(cn > 1e-9f) || !(dn > 1e-9f)) return X;
    /* near-neutral colours (chroma below ~1..4 CIELAB units relative to the film's neutral) are left
     * alone: their hue is meaningless and the neutral table's interpolation residual must not be
     * amplified */
    g *= cel_smoothstep(0.002f, 0.008f, dn);
    /* turn by t times the (wrapped) hue angle difference: well conditioned for any pair of hues */
    float hc = cel_atan2(c1, c0), dh = cel_atan2(d1, d0) - hc;
    if (dh > CEL_PI) dh -= 2.0f * CEL_PI;
    if (dh < -CEL_PI) dh += 2.0f * CEL_PI;
    /* opposite hues (|dh| -> 180 deg, near-neutral noise): fade the turn out so the wrap is continuous */
    g *= 1.0f - cel_smoothstep(0.5f * CEL_PI, 0.9f * CEL_PI, cel_fabs(dh));
    float h = hc + g * aw * dh;
    /* chroma hold, full in the highlights (skies), 30 % of it in the shadows:
     *   k = cn + b cn / (1 + q),  q = (cn / C0)^4,  b = chromaHold g (0.3 + 0.7 ss(st)) <= 1.5
     * dk/dcn = 1 + b (1 - 3q) / (1 + q)^2 >= 1 - 0.5625 b > 0: monotonic by construction (a more
     * saturated film colour always renders more saturated). The boost is ~ b cn for weak colours,
     * peaks at ~0.11 b near cn = 0.76 C0 and fades for strongly saturated colours, which the display
     * gamut mapping would only cut back. */
    float b = cel_fmin(s[CEL_CS_HOLD_CHROMA] * g * (0.3f + 0.7f * cel_smoothstep(-0.5f, 0.5f, st)), 1.5f);
    float qc = cn * (1.0f / CEL_HOLD_C0);
    qc = qc * qc;
    float k = cn + b * cn / (1.0f + qc * qc);
    float e0 = an0 + cel_cos(h) * k, e1 = an1 + cel_sin(h) * k;
    return cel_f3(CEL_D65_X * celc_labfinv(e0 * 0.4f + cy), X.y, CEL_D65_Z * celc_labfinv(cy - e1));
}
CEL_DEVICE CelF3 celc_hold(CEL_GLOBAL const float* s, CelF3 X, float xref, CelF3 rgb) { return celc_hold_k(s, X, xref, rgb, 1.0f); }

/* ------------------------------------------------------------------------- output stage */
/* hue-preserving smooth gamut compression toward the luminance axis (C1, asymptote at the boundary) */
CEL_DEVICE CelF3 celc_gamut_compress(CEL_GLOBAL const float* s, CelF3 rgb) {
    float Y = celc_dot3(s, CEL_CS_LUM, rgb);
    if (!(Y > 1e-10f)) return cel_f3(0.0f, 0.0f, 0.0f);
    /* smooth minimum of the channels (no slope kink where the smallest channel changes) */
    float e = 0.02f * Y;
    float m1 = 0.5f * (rgb.x + rgb.y - cel_sqrt((rgb.x - rgb.y) * (rgb.x - rgb.y) + e * e));
    float mn = 0.5f * (m1 + rgb.z - cel_sqrt((m1 - rgb.z) * (m1 - rgb.z) + e * e));
    float sat = (Y - mn) / Y;
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
    /* E is clipped to the HLG signal range (a saturated colour at the peak has one channel above it) */
    return cel_f3(celc_hlg_oetf(cel_fmin(Fd.x * k, 1.0f)), celc_hlg_oetf(cel_fmin(Fd.y * k, 1.0f)), celc_hlg_oetf(cel_fmin(Fd.z * k, 1.0f)));
}

CEL_DEVICE CelF3 celc_encode_sdr(int out, CelF3 rgb) {
    if (out == CEL_OUT_709_SRGB) return cel_f3(celc_srgb_enc(rgb.x), celc_srgb_enc(rgb.y), celc_srgb_enc(rgb.z));
    float e = (out == CEL_OUT_709_G24) ? (1.0f / 2.4f) : (1.0f / 2.6f);
    return cel_f3(celc_spow(rgb.x, e), celc_spow(rgb.y, e), celc_spow(rgb.z, e));
}

CEL_DEVICE CelF3 celc_output_k(CEL_GLOBAL const float* s, CelF3 XYZ, float xref, CelF3 scene, float holdScale) {
    int out = celc_i(s, CEL_CS_OUTPUT);
    int hdr = celc_i(s, CEL_CS_HDR);
    XYZ = celc_hold_k(s, XYZ, xref, scene, holdScale);
    /* v1.3 monotone black: display-linear correction x luminance (NT block; off = 0, nothing added) */
    CelF3 bfix = cel_f3(0.0f, 0.0f, 0.0f);
    int bn = celc_i(s, CEL_CS_BLACK_N);
    if (bn > 0) bfix = celc_scale(celc_lut1_3(s, celc_i(s, CEL_CS_BLACK_OFF), bn, CEL_HOLD_X0, CEL_HOLD_X1, cel_f3(xref, xref, xref)), XYZ.y);
    if (hdr == 0) {
        CelF3 rgb = celc_mat3(s, CEL_CS_MOUT, XYZ);
        if (bn > 0) rgb = cel_f3(rgb.x + bfix.x, rgb.y + bfix.y, rgb.z + bfix.z);
        rgb = celc_gamut_compress(s, rgb);
        rgb = celc_limit(rgb, CEL_SDR_KNEE, 1.0f);
        return celc_encode_sdr(out, rgb);
    }
    float e = xref - CEL_LOG10_018;
    float t = cel_fmax(e - CEL_HDR_E0, 0.0f) / CEL_HDR_ER;
    float G = s[CEL_CS_HDR_GMAX] * t * t / (1.0f + t * t);
    float gain = s[CEL_CS_HDR_LREF] * celc_pow10(s[CEL_CS_HDR_UNROLL] * G);
    CelF3 rgb = celc_mat3(s, CEL_CS_MOUT, celc_scale(XYZ, gain));
    if (bn > 0) rgb = cel_f3(rgb.x + bfix.x * gain, rgb.y + bfix.y * gain, rgb.z + bfix.z * gain);
    rgb = celc_gamut_compress(s, rgb);
    float peak = s[CEL_CS_HDR_PEAK];
    rgb = celc_limit(rgb, CEL_HDR_KNEE * peak, peak);
    if (hdr == 1) return cel_f3(celc_pq_enc(rgb.x), celc_pq_enc(rgb.y), celc_pq_enc(rgb.z));
    return celc_hlg_from_nits(s, rgb);
}
CEL_DEVICE CelF3 celc_output(CEL_GLOBAL const float* s, CelF3 XYZ, float xref, CelF3 scene) {
    return celc_output_k(s, XYZ, xref, scene, 1.0f);
}

/* damage field sanitised: finite, non-negative, densities <= 6, depths <= 3 */
CEL_DEVICE CelF4 celc_dmg_clean(CelF4 g) {
    return cel_f4(cel_clamp(cel_finite_or(g.x, 0.0f), 0.0f, 6.0f), cel_clamp(cel_finite_or(g.y, 0.0f), 0.0f, 3.0f),
                  cel_clamp(cel_finite_or(g.z, 0.0f), 0.0f, 6.0f), cel_clamp(cel_finite_or(g.w, 0.0f), 0.0f, 3.0f));
}
/* ------------------------------------------------------------------------- v1.3 stage exits */
/* printing density of the developed negative above the clear negative, as the print stock sees it
 * (Cineon convention): PD = h(D-min) - h(D) + retained silver / negative-side neutral density */
CEL_DEVICE CelF3 celc_printing_density(CEL_GLOBAL const float* s, CelcNeg ng) {
    CelF3 h = celc_lut3(s, celc_i(s, CEL_CS_PL_OFF), celc_i(s, CEL_CS_PL_N), s[CEL_CS_PL_MAX], ng.d);
    return cel_f3(s[CEL_CS_HCLEAR] - h.x + ng.silver, s[CEL_CS_HCLEAR + 1] - h.y + ng.silver, s[CEL_CS_HCLEAR + 2] - h.z + ng.silver);
}
/* scene-referred linear Rec.2020 -> Negative -> Scene output encoding */
CEL_DEVICE CelF3 celc_scene_encode(CEL_GLOBAL const float* s, CelF3 l) {
    l = celc_mat3(s, CEL_CS_MSCENE, l);
    int tf = celc_i(s, CEL_CS_SCENE_TF);
    return cel_f3(celc_encode_transfer(tf, l.x), celc_encode_transfer(tf, l.y), celc_encode_transfer(tf, l.z));
}
/* Stage 1 Log Scan, 2 Negative -> Scene: the negative with its grain and negative-side damage (print-side
 * damage has no film to land on). Stage 3 Print Only: `rgb` is the scene-referred level of a Log Scan
 * (celc_decode_px), taken back to printing density; grain and negative-side damage act on that density,
 * then the print, hue hold and display encode run as in the full chain. */
CEL_DEVICE CelF3 celc_stage_exit(CEL_GLOBAL const float* s, int stage, CelF3 rgb, CelF3 noise, int hasNoise, CelF4 dmg) {
    if (stage != 3) {
        CelcNeg ng = celc_negative_dmg(s, rgb, noise, hasNoise, dmg.x, dmg.y);
        CelF3 pd = celc_printing_density(s, ng);
        if (stage == 1) return cel_f3(celc_logscan_code(pd.x), celc_logscan_code(pd.y), celc_logscan_code(pd.z));
        return celc_scene_encode(s, celc_ns_scene(s, pd));
    }
    int mono = celc_i(s, CEL_CS_MONO);
    CelF3 pd = celc_ns_density(s, rgb);
    if (hasNoise == CEL_GRAIN_V2 && s[CEL_CS_GRAIN_ON] > 0.5f) {
        /* grain v2: the dye-cloud fluctuation at the scan's layer exposures, added in density like the
         * negative path (celc_negative_dmg; the model carries its own exposure dependence, mono is
         * already folded in by celg_eval) */
        pd.x += s[CEL_CS_GSIG + 0] * noise.x;
        pd.y += s[CEL_CS_GSIG + 1] * noise.y;
        pd.z += s[CEL_CS_GSIG + 2] * noise.z;
    } else if (hasNoise && s[CEL_CS_GRAIN_ON] > 0.5f) {
        CelF3 nz = noise;
        if (mono) nz = cel_f3(noise.y, noise.y, noise.y);
        pd.x += s[CEL_CS_GSIG + 0] * cel_sqrt(cel_fmax(pd.x, 0.0f) + CEL_GRAIN_EPS) * nz.x;
        pd.y += s[CEL_CS_GSIG + 1] * cel_sqrt(cel_fmax(pd.y, 0.0f) + CEL_GRAIN_EPS) * nz.y;
        pd.z += s[CEL_CS_GSIG + 2] * cel_sqrt(cel_fmax(pd.z, 0.0f) + CEL_GRAIN_EPS) * nz.z;
    }
    if (dmg.y > 0.0f) pd = celc_dmg_remove(pd, celc_dmg_removal(dmg.y, 0, mono));
    if (dmg.x > 0.0f) pd = cel_f3(pd.x + dmg.x, pd.y + dmg.x, pd.z + dmg.x);
    CelF3 h = cel_f3(s[CEL_CS_HCLEAR] - pd.x, s[CEL_CS_HCLEAR + 1] - pd.y, s[CEL_CS_HCLEAR + 2] - pd.z);
    CelF3 X = celc_print_xyz(s, h, 0.0f, dmg, mono);
    float xref = celc_dot3(s, CEL_CS_LW, celc_exposures(s, rgb));
    float hk = 1.0f - cel_saturate(3.0f * (dmg.x + dmg.y + dmg.z + dmg.w));
    return celc_output_k(s, X, xref, rgb, hk);
}

/* Texture-Only film damage on a display-linear image (no film model available: a display-referred
 * stand-in with the same signs and colours as celc_film_xyz_dmg). Values above display white (HDR)
 * are left alone by the density re-mappings, so an SDR-legal input stays legal.
 *   negative neutral density nd: the print received less light -> display density shrinks,
 *       L' = L^(e^(-3 nd)) (white specks; ~ print gamma x nd at mid grey)
 *   negative emulsion removal r: the print received more light in that layer -> L' = L 10^(-2 r)
 *   print emulsion removal r:    the print's dye is gone -> T' = T^(1 - r)
 *   print neutral density pd:    L' = L 10^(-pd) */
CEL_DEVICE float celc_dmg_pow1(float L, float e) { return (L > 0.0f && L < 1.0f) ? cel_exp2(e * cel_log2(L)) : L; }
CEL_DEVICE CelF3 celc_display_damage(CEL_GLOBAL const float* s, CelF3 lin, CelF4 dmg) {
    int mono = celc_i(s, CEL_CS_MONO);
    if (dmg.x > 0.0f) {
        float e = cel_exp(-3.0f * dmg.x);
        lin = cel_f3(celc_dmg_pow1(lin.x, e), celc_dmg_pow1(lin.y, e), celc_dmg_pow1(lin.z, e));
    }
    if (dmg.y > 0.0f) {
        CelF3 r = celc_dmg_removal(dmg.y, 0, mono);
        lin = cel_f3(lin.x * celc_pow10(-2.0f * r.x), lin.y * celc_pow10(-2.0f * r.y), lin.z * celc_pow10(-2.0f * r.z));
    }
    if (dmg.w > 0.0f) {
        CelF3 r = celc_dmg_removal(dmg.w, 1, mono);
        lin = cel_f3(celc_dmg_pow1(lin.x, 1.0f - r.x), celc_dmg_pow1(lin.y, 1.0f - r.y), celc_dmg_pow1(lin.z, 1.0f - r.z));
    }
    if (dmg.z > 0.0f) lin = celc_scale(lin, celc_pow10(-dmg.z));
    return lin;
}

/* Texture-Only (and ALEXA Rec.709) display-referred grain on a display-linear image */
CEL_DEVICE CelF3 celc_display_grain(CEL_GLOBAL const float* s, CelF3 lin, CelF3 noise, int hasNoise) {
    int out = celc_i(s, CEL_CS_OUTPUT);
    if (hasNoise && s[CEL_CS_TX_GSIG] > 0.0f) {
        CelF3 nz = cel_f3(cel_clamp(cel_finite_or(noise.x, 0.0f), -8.0f, 8.0f), cel_clamp(cel_finite_or(noise.y, 0.0f), -8.0f, 8.0f),
                          cel_clamp(cel_finite_or(noise.z, 0.0f), -8.0f, 8.0f));
        if (celc_i(s, CEL_CS_MONO)) nz = cel_f3(nz.y, nz.y, nz.y);
        float Y = celc_dot3(s, CEL_CS_LUM, lin);
        float l = cel_log10(cel_fmax(Y, 1e-6f)) + 0.95f;
        /* grain v2 carries its own exposure dependence (and a positive fluctuation = more negative
         * density = a brighter print, as in Full mode) */
        float sig = hasNoise == CEL_GRAIN_V2 ? -s[CEL_CS_TX_GSIG] : s[CEL_CS_TX_GSIG] * cel_exp(-0.5f * l * l / (0.7f * 0.7f));
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
    return lin;
}
/* display-linear (SDR: 1 = white; PQ / HLG: cd/m^2 / 100) -> display-encoded output */
CEL_DEVICE CelF3 celc_display_encode(CEL_GLOBAL const float* s, CelF3 lin) {
    int out = celc_i(s, CEL_CS_OUTPUT);
    CelF3 o;
    if (out == CEL_OUT_PQ) o = cel_f3(celc_pq_enc(lin.x * 100.0f), celc_pq_enc(lin.y * 100.0f), celc_pq_enc(lin.z * 100.0f));
    else if (out == CEL_OUT_HLG) o = celc_hlg_from_nits(s, celc_scale(lin, 100.0f));
    else o = celc_encode_sdr(out, lin);
    return cel_f3(cel_finite_or(o.x, 0.0f), cel_finite_or(o.y, 0.0f), cel_finite_or(o.z, 0.0f));
}
/* v1.3 ALEXA Rec.709 stage (4): scene -> ALEXA rendering -> output display; no film, so grain and damage are
 * the display-referred ones of Texture Only (on the rendered image, before the encode). SDR outputs show the
 * Rec.709 rendering in their primaries; PQ / HLG place it with grey at the HDR grey (no highlight unroll). */
CEL_DEVICE CelF3 celc_alexa_stage(CEL_GLOBAL const float* s, CelF3 rgb, CelF3 noise, int hasNoise, CelF4 dmg) {
    CEL_GLOBAL const float* a = s + celc_i(s, CEL_CS_AL_OFF);
    CelF3 lin = celc_scale(celc_mat3(a, CEL_AL_MOUT, celc_alexa_render(s, rgb)), a[CEL_AL_DISP]);
    if (celc_i(s, CEL_CS_HDR) == 0) lin = cel_f3(cel_clamp(lin.x, 0.0f, 1.0f), cel_clamp(lin.y, 0.0f, 1.0f), cel_clamp(lin.z, 0.0f, 1.0f));
    else lin = cel_f3(cel_fmax(lin.x, 0.0f), cel_fmax(lin.y, 0.0f), cel_fmax(lin.z, 0.0f));
    lin = celc_display_grain(s, lin, noise, hasNoise);
    lin = celc_display_damage(s, lin, dmg);
    return celc_display_encode(s, lin);
}

CEL_DEVICE CelF3 celc_scene_to_output_dmg(CEL_GLOBAL const float* s, CelF3 rgb, CelF3 noise, int hasNoise, CelF4 dmg) {
    rgb = cel_f3(celc_sanitize(rgb.x), celc_sanitize(rgb.y), celc_sanitize(rgb.z));
    noise = cel_f3(cel_clamp(cel_finite_or(noise.x, 0.0f), -8.0f, 8.0f), cel_clamp(cel_finite_or(noise.y, 0.0f), -8.0f, 8.0f),
                   cel_clamp(cel_finite_or(noise.z, 0.0f), -8.0f, 8.0f));
    dmg = celc_dmg_clean(dmg);
    int stage = celc_i(s, CEL_CS_STAGE);
    if (stage == 4) {   /* v1.3 ALEXA Rec.709 */
        CelF3 ao = celc_alexa_stage(s, rgb, noise, hasNoise, dmg);
        return cel_f3(cel_finite_or(ao.x, 0.0f), cel_finite_or(ao.y, 0.0f), cel_finite_or(ao.z, 0.0f));
    }
    if (stage != 0) {
        CelF3 so = celc_stage_exit(s, stage, rgb, noise, hasNoise, dmg);
        return cel_f3(cel_finite_or(so.x, 0.0f), cel_finite_or(so.y, 0.0f), cel_finite_or(so.z, 0.0f));
    }
    CelcFilm f = celc_film_xyz_dmg(s, rgb, noise, hasNoise, dmg);
    float hk = 1.0f - cel_saturate(3.0f * (dmg.x + dmg.y + dmg.z + dmg.w));
    CelF3 o = celc_output_k(s, f.X, f.xref, rgb, hk);
    return cel_f3(cel_finite_or(o.x, 0.0f), cel_finite_or(o.y, 0.0f), cel_finite_or(o.z, 0.0f));
}
CEL_DEVICE CelF3 celc_scene_to_output(CEL_GLOBAL const float* s, CelF3 rgb, CelF3 noise, int hasNoise) {
    return celc_scene_to_output_dmg(s, rgb, noise, hasNoise, cel_f4(0.0f, 0.0f, 0.0f, 0.0f));
}

/* ------------------------------------------------------------------------- Texture-Only */
CEL_DEVICE CelF3 celc_display_decode_px(CEL_GLOBAL const float* s, CelF3 v) {
    int out = celc_i(s, CEL_CS_OUTPUT);
    v = cel_f3(celc_sanitize_d(v.x), celc_sanitize_d(v.y), celc_sanitize_d(v.z));
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

CEL_DEVICE CelF3 celc_display_finish_dmg(CEL_GLOBAL const float* s, CelF3 lin, CelF3 noise, int hasNoise, CelF4 dmg, int hasDmg) {
    lin = cel_f3(celc_sanitize_d(lin.x), celc_sanitize_d(lin.y), celc_sanitize_d(lin.z));
    lin = celc_display_grain(s, lin, noise, hasNoise);
    if (hasDmg) lin = celc_display_damage(s, lin, celc_dmg_clean(dmg));
    return celc_display_encode(s, lin);
}
CEL_DEVICE CelF3 celc_display_finish_px(CEL_GLOBAL const float* s, CelF3 lin, CelF3 noise, int hasNoise) {
    return celc_display_finish_dmg(s, lin, noise, hasNoise, cel_f4(0.0f, 0.0f, 0.0f, 0.0f), 0);
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
    int v2 = a->i[0] == CEL_GRAIN_V2;
    int hasNoise = a->inStride[1] != 0;
    CelF3 nz = cel_f3(0.0f, 0.0f, 0.0f);
    if (hasNoise && !v2) { CelF4 n = cel_load(in1, a->inStride[1], a->inW[1], a->inH[1], x, y); nz = cel_f3(n.x, n.y, n.z); }
    float alpha = p.w;
    if (a->inStride[2] != 0 && !v2) alpha = cel_load(in2, a->inStride[2], a->inW[2], a->inH[2], x, y).w;
    CelF3 sc = cel_f3(p.x, p.y, p.z);
    /* i[1] = 1: in0 is the encoded host source (Color Only plan: decode fused into this pass,
     * bit-identical to a separate decode pass) */
    if (a->i[1] != 0) sc = celc_decode_px(state, sc);
    if (v2) {   /* grain v2: dye clouds at the layer exposures of this pixel (stops re 18 % grey) */
        hasNoise = 0;
        if ((a->inStride[1] != 0 && a->inStride[2] != 0) && state[CEL_CS_GRAIN_ON] > 0.5f) {
            CelF3 sn = cel_f3(celc_sanitize(sc.x), celc_sanitize(sc.y), celc_sanitize(sc.z));
            CelF3 e = celc_exposures(state, sn);
            CelF3 st = cel_f3((e.x - CEL_LOG10_018) * CEL_INV_LOG10_2, (e.y - CEL_LOG10_018) * CEL_INV_LOG10_2,
                              (e.z - CEL_LOG10_018) * CEL_INV_LOG10_2);
            CelgPx g = celg_sample(in1, a->inStride[1], a->inW[1], a->inH[1], in2, a->inStride[2], a->inW[2], a->inH[2], x, y, a);
            nz = celg_eval(g, st, a);
            hasNoise = CEL_GRAIN_V2;
        }
    }
    CelF4 dmg = cel_f4(0.0f, 0.0f, 0.0f, 0.0f);
    if (a->inStride[3] != 0) dmg = cel_load(in3, a->inStride[3], a->inW[3], a->inH[3], x, y);
    CelF3 r = celc_scene_to_output_dmg(state, sc, nz, hasNoise, dmg);
    cel_store(out, a->outStride, x, y, cel_f4(r.x, r.y, r.z, alpha));
}

CEL_DEVICE void cel_k_display_decode(CEL_KERNEL_PARAMS) {
    CelF4 p = cel_load(in0, a->inStride[0], a->inW[0], a->inH[0], x, y);
    CelF3 r = celc_display_decode_px(state, cel_f3(p.x, p.y, p.z));
    cel_store(out, a->outStride, x, y, cel_f4(r.x, r.y, r.z, p.w));
    (void)in1; (void)in2; (void)in3;
}

CEL_DEVICE void cel_k_display_finish(CEL_KERNEL_PARAMS) {
    CelF4 p = cel_load(in0, a->inStride[0], a->inW[0], a->inH[0], x, y);
    int v2 = a->i[0] == CEL_GRAIN_V2;
    int hasNoise = a->inStride[1] != 0;
    CelF3 nz = cel_f3(0.0f, 0.0f, 0.0f);
    if (hasNoise && !v2) { CelF4 n = cel_load(in1, a->inStride[1], a->inW[1], a->inH[1], x, y); nz = cel_f3(n.x, n.y, n.z); }
    float alpha = p.w;
    if (a->inStride[2] != 0 && !v2) alpha = cel_load(in2, a->inStride[2], a->inW[2], a->inH[2], x, y).w;
    if (v2) {   /* grain v2 on the display-referred image: exposure from the Texture-Only pseudo scene */
        hasNoise = 0;
        if ((a->inStride[1] != 0 && a->inStride[2] != 0) && state[CEL_CS_TX_GSIG] > 0.0f) {
            CelF3 st = cel_f3(celg_display_stops(p.x, a), celg_display_stops(p.y, a), celg_display_stops(p.z, a));
            CelgPx g = celg_sample(in1, a->inStride[1], a->inW[1], a->inH[1], in2, a->inStride[2], a->inW[2], a->inH[2], x, y, a);
            nz = celg_eval(g, st, a);
            hasNoise = CEL_GRAIN_V2;
        }
    }
    int hasDmg = a->inStride[3] != 0;
    CelF4 dmg = cel_f4(0.0f, 0.0f, 0.0f, 0.0f);
    if (hasDmg) dmg = cel_load(in3, a->inStride[3], a->inW[3], a->inH[3], x, y);
    CelF3 r = celc_display_finish_dmg(state, cel_f3(p.x, p.y, p.z), nz, hasNoise, dmg, hasDmg);
    cel_store(out, a->outStride, x, y, cel_f4(r.x, r.y, r.z, alpha));
}

#endif /* CEL_COLOR_KERNELS_H */
