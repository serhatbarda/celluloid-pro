/* Reafilm Pro - trial watermark kernel (license agent).  Portable dialect (see Portable.h).
 *
 * cel_k_watermark stamps a small, semi-transparent "REAFILM PRO - TRIAL" line (with a real em
 * dash) in the bottom-right corner plus a faint diagonal veil band through the centre of the
 * frame. The text sits on a soft translucent dark pill so it stays legible on any background
 * (white walls, skies) without a heavy outline. It runs as the very last pass on the ENCODED output image (display code values), so all
 * blending is perceptual. Everything is relative to the frame size -> identical look at any
 * resolution / render scale; it is fully deterministic and needs no neighbour reads.
 *
 * Typography: an original proportional 5x7 bitmap face (table below, designed for this product).
 * Each lit cell is a node; neighbouring nodes are joined by round-capped strokes (orthogonal
 * neighbours always, diagonal neighbours when no orthogonal path exists), giving a thin monoline
 * capital with rounded corners instead of blocky pixels. Coverage is analytically antialiased
 * from the distance to the strokes.
 *
 * Inputs:  in0 = image to stamp (same size as out). in1..in3 unused. out must NOT alias in0.
 * Alpha:   copied from in0 unchanged.
 * Params (CelPassArgs), all optional - a zero value selects the default, values are clamped so
 * a bad plan can never make the watermark disappear:
 *   f[0]  pixelScale (informational, ignored: the layout is derived from the frame size itself,
 *         so proxies / render scales look identical)
 *   f[1]  opacity of the text (plate: 0.42 x this) default 0.70, clamped [0.35, 1.00]
 *   f[2]  size multiplier                         default 1.00, clamped [0.50, 3.00]
 *         (1.0 -> cap height 1.9% of min(height, width*9/16))
 *   f[3]  "white" level in OUTPUT code values     default 0.92, clamped [0.20, 1.00]
 *         SDR: 0.92. HDR: use ~reference white - PQ 0.58 (203 nit), HLG 0.75.
 *         See cel::lic::fillWatermarkArgs() / watermarkWhiteLevel() in src/license/Watermark.h.
 *   f[4]  diagonal band strength multiplier       default 1.00, clamped [0.50, 2.00]
 *   i[0]  row order of the buffers: 0 = row 0 is the BOTTOM of the picture (OFX convention, the
 *         default for host images), 1 = row 0 is the TOP (top-down buffers, e.g. PNG writers).
 *   i[1..7], f[5..31] reserved (0).
 */
#ifndef CEL_LICENSE_KERNELS_H
#define CEL_LICENSE_KERNELS_H
#include "KernelABI.h"

/* File-scope constant tables need a different qualifier per API (OpenCL/Metal require program-
 * scope constant address space; CUDA needs __constant__ so device code may read them). */
#if defined(CEL_API_OPENCL)
  #define CEL_WM_TABLE __constant const
#elif defined(CEL_API_METAL)
  #define CEL_WM_TABLE constant const
#elif defined(CEL_API_CUDA)
  #define CEL_WM_TABLE static __constant__ const
#else
  #define CEL_WM_TABLE static const
#endif

#define CEL_WM_NGLYPHS   15
#define CEL_WM_GLYPH_U32 8          /* per glyph: advance width in cells, then 7 row masks */
#define CEL_WM_MSG_LEN   19
#define CEL_WM_TRACKING  1.6f       /* extra space between glyphs, in cells */
/* Total text width in cells = sum(width) + (MSG_LEN-1)*TRACKING (checked by tests/license). */
#define CEL_WM_TEXT_W    98.8f
#define CEL_WM_ROWS      7
#define CEL_WM_STROKE_R  0.34f      /* stroke radius in cells */

/* Row masks: bit c = column c (bit 0 = leftmost). Art: '@' = lit, rows separated by '|'. */
CEL_WM_TABLE cel_uint cel_wm_font[CEL_WM_NGLYPHS * CEL_WM_GLYPH_U32] = {
    1u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,   /*  0 space  | | | | | |  */
    4u, 0x0eu, 0x01u, 0x01u, 0x01u, 0x01u, 0x01u, 0x0eu,   /*  1 C      @@@|@   |@   |@   |@   |@   | @@@ */
    4u, 0x0fu, 0x01u, 0x01u, 0x07u, 0x01u, 0x01u, 0x0fu,   /*  2 E     @@@@|@   |@   |@@@ |@   |@   |@@@@ */
    4u, 0x01u, 0x01u, 0x01u, 0x01u, 0x01u, 0x01u, 0x0fu,   /*  3 L     @   |@   |@   |@   |@   |@   |@@@@ */
    5u, 0x11u, 0x11u, 0x11u, 0x11u, 0x11u, 0x11u, 0x0eu,   /*  4 U     @   @|@   @|@   @|@   @|@   @|@   @| @@@  */
    5u, 0x0eu, 0x11u, 0x11u, 0x11u, 0x11u, 0x11u, 0x0eu,   /*  5 O      @@@ |@   @|@   @|@   @|@   @|@   @| @@@  */
    1u, 0x01u, 0x01u, 0x01u, 0x01u, 0x01u, 0x01u, 0x01u,   /*  6 I     @|@|@|@|@|@|@ */
    5u, 0x0fu, 0x11u, 0x11u, 0x11u, 0x11u, 0x11u, 0x0fu,   /*  7 D     @@@@ |@   @|@   @|@   @|@   @|@   @|@@@@  */
    5u, 0x0fu, 0x11u, 0x11u, 0x0fu, 0x01u, 0x01u, 0x01u,   /*  8 P     @@@@ |@   @|@   @|@@@@ |@    |@    |@     */
    5u, 0x0fu, 0x11u, 0x11u, 0x0fu, 0x05u, 0x09u, 0x11u,   /*  9 R     @@@@ |@   @|@   @|@@@@ |@ @  |@  @ |@   @ */
    5u, 0x1fu, 0x04u, 0x04u, 0x04u, 0x04u, 0x04u, 0x04u,   /* 10 T     @@@@@|  @  |  @  |  @  |  @  |  @  |  @   */
    5u, 0x04u, 0x0au, 0x11u, 0x11u, 0x1fu, 0x11u, 0x11u,   /* 11 A       @  | @ @ |@   @|@   @|@@@@@|@   @|@   @ */
    4u, 0x00u, 0x00u, 0x00u, 0x0fu, 0x00u, 0x00u, 0x00u,   /* 12 dash      |    |    |@@@@|    |    |     */
    4u, 0x0fu, 0x01u, 0x01u, 0x07u, 0x01u, 0x01u, 0x01u,   /* 13 F     @@@@|@   |@   |@@@ |@   |@   |@    */
    5u, 0x11u, 0x1bu, 0x15u, 0x15u, 0x11u, 0x11u, 0x11u    /* 14 M     @   @|@@ @@|@ @ @|@ @ @|@   @|@   @|@   @ */
};

/* "REAFILM PRO <em dash> TRIAL" as glyph indices (C, U and D are unused since the rename) */
CEL_WM_TABLE cel_uint cel_wm_msg[CEL_WM_MSG_LEN] = {
    9u, 2u, 11u, 13u, 6u, 3u, 14u, 0u, 8u, 9u, 5u, 0u, 12u, 0u, 10u, 9u, 6u, 11u, 3u
};

/* 1 if cell (cx, cy) of glyph g is lit (cells outside the glyph are dark). */
CEL_DEVICE int cel_wm_lit(int g, int cx, int cy) {
    if (cy < 0 || cy >= CEL_WM_ROWS || cx < 0) return 0;
    int w = (int)cel_wm_font[g * CEL_WM_GLYPH_U32];
    if (cx >= w) return 0;
    cel_uint row = cel_wm_font[g * CEL_WM_GLYPH_U32 + 1 + cy];
    return (int)((row >> (cel_uint)cx) & 1u);
}

/* distance from (px,py) to the segment (ax,ay)-(bx,by) */
CEL_DEVICE float cel_wm_seg(float px, float py, float ax, float ay, float bx, float by) {
    float dx = bx - ax, dy = by - ay;
    float l2 = dx * dx + dy * dy;
    float t = 0.0f;
    if (l2 > 0.0f) t = cel_saturate(((px - ax) * dx + (py - ay) * dy) / l2);
    float ex = px - (ax + t * dx), ey = py - (ay + t * dy);
    return cel_sqrt(ex * ex + ey * ey);
}

/* Distance (in cells) from glyph-local point (u, v) to the strokes of glyph g.
 * Only nodes within +-2 cells can reach the antialiased stroke edge (r + 1 px, cell >= 0.9 px),
 * so a 5x5 window of nodes suffices. */
CEL_DEVICE float cel_wm_glyph_dist(int g, float u, float v) {
    int ci = (int)cel_floor(u), cj = (int)cel_floor(v);
    float d = 1.0e6f;
    for (int j = cj - 2; j <= cj + 2; ++j) {
        for (int i = ci - 2; i <= ci + 2; ++i) {
            if (!cel_wm_lit(g, i, j)) continue;
            float ax = (float)i + 0.5f, ay = (float)j + 0.5f;
            d = cel_fmin(d, cel_wm_seg(u, v, ax, ay, ax, ay));
            int e = cel_wm_lit(g, i + 1, j), s = cel_wm_lit(g, i, j + 1);
            if (e) d = cel_fmin(d, cel_wm_seg(u, v, ax, ay, ax + 1.0f, ay));
            if (s) d = cel_fmin(d, cel_wm_seg(u, v, ax, ay, ax, ay + 1.0f));
            /* diagonals only where no orthogonal path exists (keeps corners round, not filled) */
            if (cel_wm_lit(g, i + 1, j + 1) && !e && !s)
                d = cel_fmin(d, cel_wm_seg(u, v, ax, ay, ax + 1.0f, ay + 1.0f));
            if (cel_wm_lit(g, i - 1, j + 1) && !cel_wm_lit(g, i - 1, j) && !s)
                d = cel_fmin(d, cel_wm_seg(u, v, ax, ay, ax - 1.0f, ay + 1.0f));
        }
    }
    return d;
}

/* Distance (in cells) from text-local point (u, v) to the whole message. */
CEL_DEVICE float cel_wm_text_dist(float u, float v) {
    float pen = 0.0f, d = 1.0e6f;
    for (int k = 0; k < CEL_WM_MSG_LEN; ++k) {
        int g = (int)cel_wm_msg[k];
        float w = (float)cel_wm_font[g * CEL_WM_GLYPH_U32];
        if (u >= pen - 2.0f && u <= pen + w + 2.0f) d = cel_fmin(d, cel_wm_glyph_dist(g, u - pen, v));
        pen += w + CEL_WM_TRACKING;
    }
    return d;
}

CEL_DEVICE float cel_wm_param(float v, float def, float lo, float hi) {
    return (v == 0.0f || !cel_isfinite(v)) ? def : cel_clamp(v, lo, hi);
}

CEL_DEVICE void cel_k_watermark(CEL_KERNEL_PARAMS) {
    (void)in1; (void)in2; (void)in3; (void)state;
    if (x >= a->outW || y >= a->outH) return;
    CelF4 c = cel_load(in0, a->inStride[0], a->inW[0], a->inH[0], x, y);

    float opacity = cel_wm_param(a->f[1], 0.70f, 0.35f, 1.00f);
    float scale   = cel_wm_param(a->f[2], 1.00f, 0.50f, 3.00f);
    float white   = cel_wm_param(a->f[3], 0.92f, 0.20f, 1.00f);
    float bandMul = cel_wm_param(a->f[4], 1.00f, 0.50f, 2.00f);

    float W = (float)a->outW, H = (float)a->outH;
    float sx = (float)x + 0.5f;
    float sy = (a->i[0] == 1) ? (float)y + 0.5f : H - ((float)y + 0.5f);   /* top-down screen y */
    float ref = cel_fmin(H, W * 0.5625f);                                  /* "16:9 height" */

    /* ---- faint diagonal veil band: bottom-left -> top-right through the centre ---- */
    {
        float len = cel_sqrt(W * W + H * H);
        float dist = cel_fabs((sx - 0.5f * W) * H + (sy - 0.5f * H) * W) / len;   /* to the diagonal */
        float halfW = 0.06f * ref, soft = 0.06f * ref;
        float band = 1.0f - cel_smoothstep(halfW, halfW + soft, dist);
        float k = 0.07f * band * bandMul;
        float veil = 0.65f * white;
        c.x = cel_mix(c.x, veil, k); c.y = cel_mix(c.y, veil, k); c.z = cel_mix(c.z, veil, k);
    }

    /* ---- text on a soft translucent pill, bottom-right ---- */
    {
        float capPx = 0.019f * ref * scale;                         /* cap height incl. stroke */
        float cell = cel_fmax(capPx / (6.0f + 2.0f * CEL_WM_STROKE_R), 0.9f);
        float margin = 0.042f * ref;
        float ox = W - margin - CEL_WM_TEXT_W * cell;
        float oy = H - margin - (float)CEL_WM_ROWS * cell;
        float u = (sx - ox) / cell, v = (sy - oy) / cell;
        const float padX = 4.0f, padY = 2.6f;                      /* plate padding in cells */
        if (u > -padX - 2.0f && u < CEL_WM_TEXT_W + padX + 2.0f && v > -padY - 2.0f && v < (float)CEL_WM_ROWS + padY + 2.0f) {
            /* pill = rounded box with radius = half its height; signed distance in cells */
            float hy = 0.5f * (float)CEL_WM_ROWS + padY, hx = 0.5f * CEL_WM_TEXT_W + padX;
            float qx = cel_fabs(u - 0.5f * CEL_WM_TEXT_W) - (hx - hy);
            float qy = cel_fabs(v - 0.5f * (float)CEL_WM_ROWS);
            float pd = cel_sqrt(cel_fmax(qx, 0.0f) * cel_fmax(qx, 0.0f) + qy * qy) - hy;
            float feather = cel_fmax(1.2f, 1.0f / cell);             /* soft edge, >= 1 px */
            float plate = 1.0f - cel_smoothstep(-feather, feather, pd);
            float kPlate = 0.42f * opacity * plate;
            c.x = cel_mix(c.x, 0.0f, kPlate); c.y = cel_mix(c.y, 0.0f, kPlate); c.z = cel_mix(c.z, 0.0f, kPlate);

            float d = cel_wm_text_dist(u, v);
            float cover = cel_saturate(0.5f + (CEL_WM_STROKE_R - d) * cell);   /* analytic AA */
            float kText = opacity * cover;
            c.x = cel_mix(c.x, white, kText); c.y = cel_mix(c.y, white, kText); c.z = cel_mix(c.z, white, kText);
        }
    }
    cel_store(out, a->outStride, x, y, c);
}

#endif /* CEL_LICENSE_KERNELS_H */
