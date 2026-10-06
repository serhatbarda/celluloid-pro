/* Celluloid Pro - camera / display transfer functions (portable dialect, owned by core).
 *
 * Input decodes (code value -> scene-linear, 18% grey -> 0.18) with the constants from the
 * manufacturers' published documents (also mirrored in reference/model.py):
 *   F-Log2 / F-Log2 C : Fujifilm "F-Log2 Data Sheet" Ver.1.0 (and F-Log2 C addendum: same curve)
 *   F-Log             : Fujifilm "F-Log Data Sheet" Ver.1.1
 *   DaVinci Interm.   : Blackmagic Design "DaVinci Resolve 17 Wide Gamut Intermediate" (2020)
 *   ARRI LogC3 EI800  : ARRI "ALEXA Log C Curve - Usage in VFX" (2017), EI 800 parameters
 *   ARRI LogC4        : ARRI "LogC4 Logarithmic Color Space SPECIFICATION" (2022)
 *   Sony S-Log3       : Sony "Technical Summary for S-Gamut3.Cine/S-Log3 and S-Gamut3/S-Log3"
 *   Panasonic V-Log   : Panasonic "V-Log/V-Gamut Reference manual" (2014)
 *   Canon Log 3       : Canon "Canon Log Gamma Curves" white paper v1.2 (full-range code values)
 *   BMD Film Gen 5    : Blackmagic Design "Blackmagic Generation 5 Color Science" (2021)
 *   ACEScct           : AMPAS S-2016-001 (ACEScct)
 * Output encodes: ITU-R BT.1886 / pure 2.4 and 2.6 power laws, IEC 61966-2-1 sRGB,
 * SMPTE ST 2084 (PQ), ITU-R BT.2100 HLG.
 */
#ifndef CEL_COLOR_TRANSFER_H
#define CEL_COLOR_TRANSFER_H
#include "Portable.h"

#define CEL_IN_FLOG2C 0
#define CEL_IN_FLOG2 1
#define CEL_IN_FLOG 2
#define CEL_IN_DAVINCI 3
#define CEL_IN_LOGC3 4
#define CEL_IN_LOGC4 5
#define CEL_IN_SLOG3 6
#define CEL_IN_VLOG 7
#define CEL_IN_CLOG3 8
#define CEL_IN_BMDGEN5 9
#define CEL_IN_ACESCCT 10
#define CEL_IN_LINEAR 11

#define CEL_OUT_709_G24 0
#define CEL_OUT_709_SRGB 1
#define CEL_OUT_P3D65_G26 2
#define CEL_OUT_P3DCI_G26 3
#define CEL_OUT_PQ 4
#define CEL_OUT_HLG 5

/* ARRI LogC4 derived constants: a = (2^18-16)/117.45, b = (1023-95)/1023, c = 95/1023,
 * s = 7 ln2 2^(7-14c/b) / (a b), t = (2^(14(-c/b)+6) - 64) / a */
#define CEL_LOGC4_A 2231.8263090676883f
#define CEL_LOGC4_B 0.9071358748778103f
#define CEL_LOGC4_C 0.09286412512218964f
#define CEL_LOGC4_S 0.1135972086105891f
#define CEL_LOGC4_T (-0.01805699611991131f)

CEL_DEVICE float celc_pow10(float x) { return cel_pow(10.0f, x); }

CEL_DEVICE float celc_decode_transfer(int space, float v) {
    if (space == CEL_IN_FLOG2C || space == CEL_IN_FLOG2) {
        /* a=5.555556 b=0.064829 c=0.245281 d=0.384316 e=8.799461 f=0.092864 cut2=0.100686685370811 */
        return v >= 0.100686685370811f ? (celc_pow10((v - 0.384316f) / 0.245281f) - 0.064829f) / 5.555556f
                                       : (v - 0.092864f) / 8.799461f;
    }
    if (space == CEL_IN_FLOG) {
        /* a=0.555556 b=0.009468 c=0.344676 d=0.790453 e=8.735631 f=0.092864 cut2=0.100537775223865 */
        return v >= 0.100537775223865f ? (celc_pow10((v - 0.790453f) / 0.344676f) - 0.009468f) / 0.555556f
                                       : (v - 0.092864f) / 8.735631f;
    }
    if (space == CEL_IN_DAVINCI) {
        /* A=0.0075 B=7 C=0.07329248 M=10.44426855 LOG_CUT=0.02740668 */
        return v <= 0.02740668f ? v / 10.44426855f : cel_exp2(v / 0.07329248f - 7.0f) - 0.0075f;
    }
    if (space == CEL_IN_LOGC3) {
        /* EI800: cut=0.010591 a=5.555556 b=0.052272 c=0.247190 d=0.385537 e=5.367655 f=0.092809 */
        return v > 0.149658f ? (celc_pow10((v - 0.385537f) / 0.247190f) - 0.052272f) / 5.555556f
                             : (v - 0.092809f) / 5.367655f;
    }
    if (space == CEL_IN_LOGC4) {
        return v >= 0.0f ? (cel_exp2(14.0f * (v - CEL_LOGC4_C) / CEL_LOGC4_B + 6.0f) - 64.0f) / CEL_LOGC4_A
                         : v * CEL_LOGC4_S + CEL_LOGC4_T;
    }
    if (space == CEL_IN_SLOG3) {
        return v >= 171.2102946929f / 1023.0f ? celc_pow10((v * 1023.0f - 420.0f) / 261.5f) * 0.19f - 0.01f
                                              : (v * 1023.0f - 95.0f) * 0.01125f / (171.2102946929f - 95.0f);
    }
    if (space == CEL_IN_VLOG) {
        /* cut2=0.181 b=0.00873 c=0.241514 d=0.598206 */
        return v < 0.181f ? (v - 0.125f) / 5.6f : celc_pow10((v - 0.598206f) / 0.241514f) - 0.00873f;
    }
    if (space == CEL_IN_CLOG3) {
        float x;
        if (v < 0.097465473f) x = -(celc_pow10((0.12783901f - v) / 0.36726845f) - 1.0f) / 14.98325f;
        else if (v <= 0.15277891f) x = (v - 0.12512219f) / 1.9754798f;
        else x = (celc_pow10((v - 0.12240537f) / 0.36726845f) - 1.0f) / 14.98325f;
        return x * 0.9f;
    }
    if (space == CEL_IN_BMDGEN5) {
        /* A=0.08692876065491224 B=0.005494072432257808 C=0.5300133392291939 D=8.283605932402494
         * E=0.09246575342465753 LIN_CUT=0.005 -> LOG_CUT = D*LIN_CUT+E */
        return v < 0.13388378308667f ? (v - 0.09246575342465753f) / 8.283605932402494f
                                     : cel_exp((v - 0.5300133392291939f) / 0.08692876065491224f) - 0.005494072432257808f;
    }
    if (space == CEL_IN_ACESCCT) {
        if (v <= 0.155251141552511f) return (v - 0.0729055341958355f) / 10.5402377416545f;
        if (v < 1.4679962638f) return cel_exp2(v * 17.52f - 9.72f);
        return 65504.0f;
    }
    return v; /* linear */
}

/* Inverse of celc_decode_transfer (used by tests / tools; not needed by the kernels). */
CEL_DEVICE float celc_encode_transfer(int space, float x) {
    if (space == CEL_IN_FLOG2C || space == CEL_IN_FLOG2)
        return x >= 0.000889f ? 0.245281f * cel_log10(5.555556f * x + 0.064829f) + 0.384316f : 8.799461f * x + 0.092864f;
    if (space == CEL_IN_FLOG)
        return x >= 0.00089f ? 0.344676f * cel_log10(0.555556f * x + 0.009468f) + 0.790453f : 8.735631f * x + 0.092864f;
    if (space == CEL_IN_DAVINCI)
        return x <= 0.00262409f ? x * 10.44426855f : (cel_log2(x + 0.0075f) + 7.0f) * 0.07329248f;
    if (space == CEL_IN_LOGC3)
        return x > 0.010591f ? 0.247190f * cel_log10(5.555556f * x + 0.052272f) + 0.385537f : 5.367655f * x + 0.092809f;
    if (space == CEL_IN_LOGC4)
        return x >= CEL_LOGC4_T ? (cel_log2(CEL_LOGC4_A * x + 64.0f) - 6.0f) / 14.0f * CEL_LOGC4_B + CEL_LOGC4_C
                                : (x - CEL_LOGC4_T) / CEL_LOGC4_S;
    if (space == CEL_IN_SLOG3)
        return x >= 0.01125f ? (420.0f + cel_log10((x + 0.01f) / 0.19f) * 261.5f) / 1023.0f
                             : (x * (171.2102946929f - 95.0f) / 0.01125f + 95.0f) / 1023.0f;
    if (space == CEL_IN_VLOG)
        return x < 0.01f ? 5.6f * x + 0.125f : 0.241514f * cel_log10(x + 0.00873f) + 0.598206f;
    if (space == CEL_IN_CLOG3) {
        float y = x / 0.9f;
        if (y < -0.014f) return -0.36726845f * cel_log10(1.0f - 14.98325f * y) + 0.12783901f;
        if (y <= 0.014f) return 1.9754798f * y + 0.12512219f;
        return 0.36726845f * cel_log10(14.98325f * y + 1.0f) + 0.12240537f;
    }
    if (space == CEL_IN_BMDGEN5)
        return x < 0.005f ? 8.283605932402494f * x + 0.09246575342465753f
                          : 0.08692876065491224f * cel_log(x + 0.005494072432257808f) + 0.5300133392291939f;
    if (space == CEL_IN_ACESCCT)
        return x <= 0.0078125f ? 10.5402377416545f * x + 0.0729055341958355f : (cel_log2(x) + 9.72f) / 17.52f;
    return x;
}

/* ---- display encodes (sign preserving for the power laws) ---- */
CEL_DEVICE float celc_spow(float x, float e) { return x >= 0.0f ? cel_pow(x, e) : -cel_pow(-x, e); }
CEL_DEVICE float celc_srgb_enc(float x) {
    float a = cel_fabs(x);
    float r = a <= 0.0031308f ? 12.92f * a : 1.055f * cel_pow(a, 1.0f / 2.4f) - 0.055f;
    return x < 0.0f ? -r : r;
}
CEL_DEVICE float celc_srgb_dec(float v) {
    float a = cel_fabs(v);
    float r = a <= 0.04045f ? a / 12.92f : cel_pow((a + 0.055f) / 1.055f, 2.4f);
    return v < 0.0f ? -r : r;
}
/* SMPTE ST 2084 */
CEL_DEVICE float celc_pq_enc(float nits) {
    float y = cel_clamp(nits / 10000.0f, 0.0f, 1.0f);
    float p = cel_pow(y, 0.1593017578125f);
    return cel_pow((0.8359375f + 18.8515625f * p) / (1.0f + 18.6875f * p), 78.84375f);
}
CEL_DEVICE float celc_pq_dec(float v) {
    float p = cel_pow(cel_clamp(v, 0.0f, 1.0f), 1.0f / 78.84375f);
    return 10000.0f * cel_pow(cel_fmax(p - 0.8359375f, 0.0f) / (18.8515625f - 18.6875f * p), 1.0f / 0.1593017578125f);
}
/* ITU-R BT.2100 HLG OETF */
CEL_DEVICE float celc_hlg_oetf(float e) {
    e = cel_fmax(e, 0.0f);
    return e <= 1.0f / 12.0f ? cel_sqrt(3.0f * e) : 0.17883277f * cel_log(cel_fmax(12.0f * e - 0.28466892f, 1e-30f)) + 0.55991073f;
}
CEL_DEVICE float celc_hlg_oetf_inv(float v) {
    v = cel_fmax(v, 0.0f);
    return v <= 0.5f ? v * v / 3.0f : (cel_exp((v - 0.55991073f) / 0.17883277f) + 0.28466892f) / 12.0f;
}

#endif /* CEL_COLOR_TRANSFER_H */
