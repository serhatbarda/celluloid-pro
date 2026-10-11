/* Reafilm Pro - film damage kernels (v1.1): dust, scratches and hair as a procedural damage field.
 * Portable dialect (Portable.h), uniform ABI (KernelABI.h); included at the end of TextureKernels.h
 * (uses its cel_txh_ helpers). Owner: texture.
 *
 * The damage field is NOT painted onto the picture. It describes what is physically in the light
 * path, per pixel, and the colour pass applies it at the right place of the film model
 * (ColorKernels.h, celc_film_xyz_dmg; display-referred stand-in for Texture Only):
 *     x = negative neutral density   dust, hair and base scratches on the camera negative: they hold
 *                                    back printer light, so they PRINT WHITE (a scan inverts them
 *                                    the same way); on reversal film they are dark
 *     y = negative emulsion depth    scratch through the negative's dye layers, 0..3 layers deep
 *                                    (yellow top layer first: prints yellow, then red, then dark)
 *     z = print neutral density      dust, hair and base scratches on the projected print: DARK
 *     w = print emulsion depth       scratch through the print's layers (magenta top layer first:
 *                                    green, then yellow, then clear white)
 * Densities are mean densities of the pixel (area-correct anti-aliasing: the mean transmission of a
 * partly covered pixel, converted back to density), depths are pixel-mean removal depths.
 *
 * Geometry is in reference pixels ("ref px" = pixels of a 1920 px wide frame): u = (x + 0.5) * f[0],
 * v = (y + 0.5) * f[0] with f[0] = 1 / pixelScale, so the damage is resolution independent. Every
 * decision is either an integer hash comparison (identical on every API) or happens where the
 * contribution is zero, so CPU and GPU results differ only by float rounding of smooth functions.
 *
 * Determinism / time: the host (src/texture/Damage.cpp) derives every per-frame value from the
 * damage seed and the frame number: dust lives one frame (new seed per frame); scratches persist for
 * many frames at a slowly wandering position (their tables are computed per frame on the host);
 * hairs on the film last one frame, a hair caught in the gate stays for seconds and wiggles.
 */
#ifndef CEL_DAMAGE_KERNELS_H
#define CEL_DAMAGE_KERNELS_H
#include "KernelABI.h"

#define CEL_DMG_LOG2_10 3.32192809f
#define CEL_DMG_LOG10_2 0.30102999f

/* NOTE: helpers use the celd_ prefix (never cel_<kernel name>). */

/* uniform [0, 1) number k of a particle / scratch key h */
CEL_DEVICE CEL_TXH_INLINE float celd_u01(cel_uint h, cel_uint k) { return cel_hash01(h ^ (k * 0x9e3779b9U)); }

/* mean density of a pixel a fraction `cov` of which is covered by material of density D:
 * mean transmission 1 - cov (1 - 10^-D), back to density (area-correct anti-aliasing) */
CEL_DEVICE CEL_TXH_INLINE float celd_cov_density(float cov, float D) {
    if (!(cov > 0.0f)) return 0.0f;
    float t = 1.0f - cov * (1.0f - cel_exp2(-CEL_DMG_LOG2_10 * D));
    return -CEL_DMG_LOG10_2 * cel_log2(cel_txh_max(t, 1e-6f));
}

/* cross-section of a thin cylinder (hair, fibre) or groove (scratch) of half width hw at distance d
 * from its axis: optical path ~ sqrt(1 - (d/hw)^2), widened by half the anti-aliasing width aa and
 * scaled by hw / (hw + aa / 2), so the line's integrated density (pi / 2 hw) does not depend on the
 * resolution: a line thinner than a pixel covers it partly instead of growing to the pixel width */
CEL_DEVICE CEL_TXH_INLINE float celd_line(float d, float hw, float aa) {
    float we = hw + 0.5f * aa;
    float q = d / we;
    if (!(q < 1.0f)) return 0.0f;
    return (hw / we) * cel_sqrt(1.0f - q * q);
}


/* fraction of a pixel (box of width ae, centred at distance d from the axis) covered by an opaque
 * strand of half width w (hair, fibre): exact box-box overlap, integrates to the strand width 2 w at
 * any resolution; converted to density with celd_cov_density (mean transmission) */
CEL_DEVICE CEL_TXH_INLINE float celd_strand_cov(float d, float w, float ae) {
    float o = cel_txh_min(d + 0.5f * ae, w) - cel_txh_max(d - 0.5f * ae, -w);
    return cel_txh_clampf(o / ae, 0.0f, 1.0f);
}

/* smooth 1D value noise in [0, 1]: hashed lattice values, C1 (smoothstep) interpolation */
CEL_DEVICE CEL_TXH_INLINE float celd_vnoise(float t, cel_uint seed) {
    float ti = cel_txh_floor(t);
    float fr = t - ti;
    cel_uint i0 = (cel_uint)(int)ti;
    float n0 = cel_hash01(cel_hash_u32(i0 ^ seed)), n1 = cel_hash01(cel_hash_u32((i0 + 1U) ^ seed));
    return n0 + (n1 - n0) * (fr * fr * (3.0f - 2.0f * fr));
}

/* nearest point of a quadratic Bezier segment A B C, approximated by n chords, merged into `best`
 * (x = squared distance, y = curve parameter t0 + tspan * s of the nearest point, s in [0, 1]) */
CEL_DEVICE CelF3 celd_bez_d2(float u, float v, float ax, float ay, float bx, float by, float cx, float cy, int n,
                             float t0, float tspan, CelF3 best) {
    float px = ax, py = ay, inv = 1.0f / (float)n;
    for (int s = 1; s <= n; ++s) {
        float tq = (float)s * inv, uq = 1.0f - tq;
        float qx = uq * uq * ax + 2.0f * uq * tq * bx + tq * tq * cx;
        float qy = uq * uq * ay + 2.0f * uq * tq * by + tq * tq * cy;
        float ex = qx - px, ey = qy - py, wx = u - px, wy = v - py;
        float ee = ex * ex + ey * ey;
        float k = ee > 1e-12f ? cel_txh_clampf((wx * ex + wy * ey) / ee, 0.0f, 1.0f) : 0.0f;
        float dx = wx - k * ex, dy = wy - k * ey;
        float d2 = dx * dx + dy * dy;
        if (d2 < best.x) { best.x = d2; best.y = t0 + tspan * ((float)(s - 1) + k) * inv; }
        px = qx; py = qy;
    }
    return best;
}

/* a random unit vector from a key (normalised uniform square sample; no trigonometry) */
CEL_DEVICE CEL_TXH_INLINE CelF3 celd_dir(cel_uint h, cel_uint k) {
    float a = 2.0f * celd_u01(h, k) - 1.0f, b = 2.0f * celd_u01(h, k + 1U) - 1.0f;
    float n = cel_sqrt(a * a + b * b);
    if (!(n > 1e-3f)) return cel_f3(1.0f, 0.0f, 0.0f);
    return cel_f3(a / n, b / n, 0.0f);
}

/* anti-aliased coverage of a disc-like shape of radius R at distance d (edge width ae); shapes
 * smaller than the edge width keep their area instead of growing to the edge width */
CEL_DEVICE CEL_TXH_INLINE float celd_disc_cov(float d, float R, float ae) {
    float c = cel_txh_clampf((R - d) / ae + 0.5f, 0.0f, 1.0f);
    float k = 2.0f * R / ae;
    return k < 1.0f ? c * cel_txh_max(k, 0.0f) : c;
}

/* ---- dust -----------------------------------------------------------------------------------
 * One layer of particles on a jittered grid of cell size C (ref px): each cell holds a particle with
 * probability p, its centre anywhere in the cell. Every particle's footprint (shape + soft edge) is
 * smaller than C / 2, so the 2 x 2 cells whose centres surround the pixel hold every particle that
 * can reach it (the selection changes only where all candidates contribute zero).
 * Specks: irregular clumps of grit = a main body of radius r (1 + a1 cos(t - p1) + a2 cos(2t - p2) +
 * a3 cos(3t - p3)) (no trigonometry: multiple-angle formulas on the unit direction) plus 0..2 smaller
 * grains stuck to it; size distribution r ~ rmin + (rmax - rmin) t^3 (many tiny specks, few large),
 * density 0.35 .. 3 (from translucent film to opaque grit), edge aa (print dust, in focus) or
 * 1.6 .. 3 aa (negative dust: slightly out of the printer's focal plane).
 * Fibres (fibreShare of the particles): lint = a thin, tapering S/C-curved strand (two quadratic
 * Bezier segments) of length L, translucent and uneven along its length.
 * Returns (negative density, 0, print density, 0). */
CEL_DEVICE CelF4 celd_dust_layer(float u, float v, float invC, float C, float p, float rmin, float rmax, float fibreShare,
                                 float negShare, float aa, float fhw0, float fhw1, float fl0, float fl1, cel_uint seed) {
    float nd = 0.0f, pd = 0.0f;
    int gx = (int)cel_txh_floor(u * invC - 0.5f), gy = (int)cel_txh_floor(v * invC - 0.5f);
    for (int j = 0; j < 2; ++j) {
        for (int i = 0; i < 2; ++i) {
            int cx = gx + i, cy = gy + j;
            cel_uint h = cel_hash_u32(((cel_uint)cx * 0x8da6b343U) ^ cel_hash_u32(((cel_uint)cy * 0xd8163841U) ^ seed));
            if (!(cel_hash01(h) < p)) continue;
            float dx = u - ((float)cx + celd_u01(h, 1U)) * C;
            float dy = v - ((float)cy + celd_u01(h, 2U)) * C;
            float d2 = dx * dx + dy * dy;
            int neg = celd_u01(h, 3U) < negShare;
            float halfC = 0.5f * C;
            float dens;
            if (celd_u01(h, 4U) < fibreShare) {
                /* fibre: centre line P0..P4 around the particle centre (local coordinates) */
                float L = fl0 + (fl1 - fl0) * celd_u01(h, 5U);
                float hw = fhw0 + (fhw1 - fhw0) * celd_u01(h, 6U);
                float ae = cel_txh_min(neg ? aa * 1.8f : aa, 2.0f * cel_txh_max(halfC - 0.5f * L - hw, 0.05f));
                float reach = 0.5f * L + hw + 0.5f * ae;   /* control points (|b| <= 0.15 L) within 0.5 L of the centre */
                if (d2 > reach * reach) continue;
                CelF3 m = celd_dir(h, 7U);
                float nx = -m.y, ny = m.x;
                float b1 = 0.3f * (celd_u01(h, 9U) - 0.5f) * L, b2 = 0.3f * (celd_u01(h, 10U) - 0.5f) * L;
                float x0 = -0.5f * L * m.x, y0 = -0.5f * L * m.y, x4 = -x0, y4 = -y0;
                float x1 = 0.5f * x0 + b1 * nx, y1 = 0.5f * y0 + b1 * ny;   /* quarter points, bent */
                float x3 = 0.5f * x4 + b2 * nx, y3 = 0.5f * y4 + b2 * ny;
                float x2 = 0.5f * (x1 + x3), y2 = 0.5f * (y1 + y3);          /* smooth (C1) joint */
                CelF3 best = cel_f3(1e30f, 0.0f, 0.0f);
                best = celd_bez_d2(dx, dy, x0, y0, x1, y1, x2, y2, 6, 0.0f, 0.5f, best);
                best = celd_bez_d2(dx, dy, x2, y2, x3, y3, x4, y4, 6, 0.5f, 0.5f, best);
                float tc = 2.0f * best.y - 1.0f;
                float w = hw * (0.4f + 0.6f * (1.0f - tc * tc));                 /* tapering ends */
                float uneven = 0.55f + 0.45f * celd_vnoise(best.y * 5.0f, h ^ 0x2c1b3c6dU);
                dens = celd_cov_density(celd_strand_cov(cel_sqrt(best.x), w, ae), (0.5f + 1.6f * celd_u01(h, 11U)) * uneven);
            } else {
                /* speck: main body + up to two grains stuck to it */
                float t = celd_u01(h, 5U);
                float r = rmin + (rmax - rmin) * t * t * t;
                float l1 = 0.34f * celd_u01(h, 6U), l2 = 0.22f * celd_u01(h, 7U), l3 = 0.12f * celd_u01(h, 8U);
                float ae = neg ? aa * (1.6f + 1.4f * celd_u01(h, 9U)) : aa;
                ae = cel_txh_min(ae, 2.0f * cel_txh_max(halfC - 1.7f * r, 0.05f));
                float reach = 1.7f * r + 0.5f * ae;   /* body <= 1.68 r, grains <= 1.05 r + 0.65 r */
                if (d2 > reach * reach) continue;
                float d = cel_sqrt(d2);
                float c = 1.0f, sn = 0.0f;
                if (d > 1e-4f) { c = dx / d; sn = dy / d; }
                float c2 = c * c - sn * sn, s2 = 2.0f * c * sn;
                float c3 = c * (4.0f * c * c - 3.0f), s3 = sn * (3.0f - 4.0f * sn * sn);
                CelF3 e1 = celd_dir(h, 11U), e2 = celd_dir(h, 13U), e3 = celd_dir(h, 15U);
                float R = r * (1.0f + l1 * (c * e1.x + sn * e1.y) + l2 * (c2 * e2.x + s2 * e2.y) + l3 * (c3 * e3.x + s3 * e3.y));
                float cov = celd_disc_cov(d, R, ae);
                cel_uint ng = cel_hash_u32(h ^ 0x165667b1U) % 3U;   /* 0..2 grains */
                for (cel_uint g = 0U; g < ng; ++g) {
                    CelF3 eg = celd_dir(h, 21U + 3U * g);
                    float rg = r * (0.25f + 0.40f * celd_u01(h, 23U + 3U * g));
                    float og = r * (0.55f + 0.50f * celd_u01(h, 24U + 3U * g));
                    float gx2 = dx - og * eg.x, gy2 = dy - og * eg.y;
                    cov = cel_txh_max(cov, celd_disc_cov(cel_sqrt(gx2 * gx2 + gy2 * gy2), rg, ae));
                }
                dens = celd_cov_density(cov, 0.35f + 2.65f * cel_sqrt(celd_u01(h, 10U)));
            }
            if (neg) nd += dens; else pd += dens;
        }
    }
    return cel_f4(nd, 0.0f, pd, 0.0f);
}

/* ---- kernels --------------------------------------------------------------------------------- */

/* tx_params: a parameter table for a following pass (the plan's way to hand a kernel more than
 * 32 floats): out is 8 x 1, pixel x = (f[4x], f[4x + 1], f[4x + 2], f[4x + 3]). */
CEL_DEVICE void cel_k_tx_params(CEL_KERNEL_PARAMS) {
    CEL_TX_ABI_USE();
    int k = cel_imin(cel_imax(x, 0), 7) * 4;
    cel_store(out, a->outStride, x, y, cel_f4(a->f[k], a->f[k + 1], a->f[k + 2], a->f[k + 3]));
}

/* tx_damage: the damage field (see the top of this file), full res.
 *   f[0]  = 1 / pixelScale (ref px per pixel)      f[1] = anti-aliasing / edge width aa (ref px)
 *   f[2..6]   fine dust layer:   1/C, C, p, rmin, rmax          (ref px)
 *   f[7..12]  coarse dust layer: 1/C, C, p, rmin, rmax, fibre share
 *   f[13] = share of the dust on the negative       f[14], f[15] = fibre half width min / max
 *   f[16], f[17] = fibre length min / max
 *   i[0] = per-frame dust seed (bits)   i[1] = 1: dust on   i[2] = scratches (0..6)   i[3] = hairs (0..2)
 *   in0, in1, in2 = scratch tables (tx_params, 2 scratches each, 12 floats per scratch):
 *       x0, slope, hw, strength | meander amplitude, meander frequency, meander phase, code |
 *       1 / modulation length, break threshold, modulation offset, modulation seed (integer)
 *       code = side (1 = negative) + 2 * kind (1 = emulsion: strength is a depth in layers)
 *       axis: u = x0 + slope v + A (sin(f v + phase) + 0.45 sin(2.31 f v + 1.7 phase)); along the
 *       axis (film coordinate v + offset) the strength is modulated by value noise: breaks and
 *       intensity over the modulation length, ragged chipped edges over ~3 ref px, width over ~8
 *       ref px; cross-section celd_line
 *   in3 = hair table (2 hairs, 16 floats each): two quadratic Bezier segments P0 P1 P2, P2 P3 P4
 *       (ref px), half width, density, extra softness (ref px), side (1 = negative), taper (0..1:
 *       width at P4 = 1 - taper)
 *   Inputs are read only when their stride is non-zero. */
CEL_DEVICE void cel_k_tx_damage(CEL_KERNEL_PARAMS) {
    CEL_TX_ABI_USE();
    float ip = a->f[0], aa = a->f[1];
    float u = ((float)x + 0.5f) * ip, v = ((float)y + 0.5f) * ip;
    float nd = 0.0f, ndep = 0.0f, pd = 0.0f, pdep = 0.0f;
    if (a->i[1] != 0) {
        cel_uint seed = (cel_uint)a->i[0];
        CelF4 r1 = celd_dust_layer(u, v, a->f[2], a->f[3], a->f[4], a->f[5], a->f[6], 0.0f, a->f[13], aa,
                                   0.0f, 0.0f, 0.0f, 0.0f, seed);
        CelF4 r2 = celd_dust_layer(u, v, a->f[7], a->f[8], a->f[9], a->f[10], a->f[11], a->f[12], a->f[13], aa,
                                   a->f[14], a->f[15], a->f[16], a->f[17], cel_hash_u32(seed ^ 0x68e31da4U));
        nd += r1.x + r2.x;
        pd += r1.z + r2.z;
    }
    int ns = cel_imin(a->i[2], 6);
    for (int k = 0; k < ns; ++k) {
        int t = k / 2;
        int st = t == 0 ? a->inStride[0] : (t == 1 ? a->inStride[1] : a->inStride[2]);
        if (st == 0) break;
        CEL_GLOBAL const float* tb = t == 0 ? in0 : (t == 1 ? in1 : in2);
        int o = (k - 2 * t) * 12;
        float hw = tb[o + 2];
        float ph = tb[o + 5] * v + tb[o + 6];
        float xs = tb[o] + tb[o + 1] * v + tb[o + 4] * (cel_sin(ph) + 0.45f * cel_sin(2.31f * ph + 1.7f));
        float d = cel_fabs(u - xs);
        if (!(d < 1.3f * hw + aa)) continue;
        cel_uint ms = (cel_uint)(int)tb[o + 11];
        float fv = v + tb[o + 10];                                   /* film coordinate along the scratch */
        float n = celd_vnoise(fv * tb[o + 8], ms);
        float thr = tb[o + 9];
        float m = cel_smoothstep(thr - 0.12f, thr + 0.12f, n) * (0.45f + 0.55f * n);
        if (!(m > 0.0f)) continue;
        m *= 0.55f + 0.45f * celd_vnoise(fv * 0.33f, ms ^ 0x5bd1e995U);      /* chipped, uneven groove */
        float w = hw * (0.7f + 0.6f * celd_vnoise(fv * 0.12f, ms ^ 0x27d4eb2fU));
        float val = tb[o + 3] * m * celd_line(d, w, aa);
        int code = (int)tb[o + 7];
        if ((code & 2) != 0) {
            /* emulsion: the dye removal, plus the light the rough groove walls scatter out of the beam
             * (a neutral density of ~0.2 per layer: an emulsion scratch is never a clean colour filter) */
            if ((code & 1) != 0) { ndep = cel_txh_max(ndep, val); nd += 0.2f * val; }
            else { pdep = cel_txh_max(pdep, val); pd += 0.2f * val; }
        } else {
            if ((code & 1) != 0) nd += val; else pd += val;
        }
    }
    int nh = a->inStride[3] != 0 ? cel_imin(a->i[3], 2) : 0;
    for (int k = 0; k < nh; ++k) {
        CEL_GLOBAL const float* hb = in3 + k * 16;
        float hw = hb[10], soft = hb[12];
        float ae = aa + soft;
        float pad = hw + ae;
        float x0 = cel_txh_min(cel_txh_min(cel_txh_min(hb[0], hb[2]), cel_txh_min(hb[4], hb[6])), hb[8]) - pad;
        float x1 = cel_txh_max(cel_txh_max(cel_txh_max(hb[0], hb[2]), cel_txh_max(hb[4], hb[6])), hb[8]) + pad;
        float y0 = cel_txh_min(cel_txh_min(cel_txh_min(hb[1], hb[3]), cel_txh_min(hb[5], hb[7])), hb[9]) - pad;
        float y1 = cel_txh_max(cel_txh_max(cel_txh_max(hb[1], hb[3]), cel_txh_max(hb[5], hb[7])), hb[9]) + pad;
        if (u < x0 || u > x1 || v < y0 || v > y1) continue;
        CelF3 best = cel_f3(1e30f, 0.0f, 0.0f);
        best = celd_bez_d2(u, v, hb[0], hb[1], hb[2], hb[3], hb[4], hb[5], 10, 0.0f, 0.5f, best);
        best = celd_bez_d2(u, v, hb[4], hb[5], hb[6], hb[7], hb[8], hb[9], 10, 0.5f, 0.5f, best);
        float w = hw * (1.0f - hb[14] * best.y * best.y);
        float val = celd_cov_density(celd_strand_cov(cel_sqrt(best.x), w, ae), hb[11]);
        if (hb[13] > 0.5f) nd += val; else pd += val;
    }
    cel_store(out, a->outStride, x, y, cel_f4(nd, ndep, pd, pdep));
}

#endif /* CEL_DAMAGE_KERNELS_H */
