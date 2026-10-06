/* Celluloid Pro - GPU ABI test kernels (portable dialect, see src/kernels/Portable.h). */
#ifndef CEL_TEST_ABI_KERNELS_H
#define CEL_TEST_ABI_KERNELS_H
#include "../../../src/kernels/KernelABI.h"

CEL_DEVICE void cel_k_t_gradient(CEL_KERNEL_PARAMS) {
    float fx = (float)x, fy = (float)y;
    cel_store(out, a->outStride, x, y,
              cel_f4(fx * a->f[0] + a->f[1], fy * a->f[2] + a->f[3],
                     (float)(x * a->i[0] + y * a->i[1]), (float)((x ^ y) & 255)));
}

/* sample input j at the output pixel mapped proportionally onto its size */
CEL_DEVICE CelF4 cel_t_at(CEL_GLOBAL const float* buf, int st, int w, int h, int x, int y, int ow, int oh) {
    int xx = (x * w) / ow, yy = (y * h) / oh;
    return cel_load(buf, st, w, h, xx, yy);
}

CEL_DEVICE void cel_k_t_add4(CEL_KERNEL_PARAMS) {
    CelF4 p0 = cel_t_at(in0, a->inStride[0], a->inW[0], a->inH[0], x, y, a->outW, a->outH);
    CelF4 p1 = cel_t_at(in1, a->inStride[1], a->inW[1], a->inH[1], x, y, a->outW, a->outH);
    CelF4 p2 = cel_t_at(in2, a->inStride[2], a->inW[2], a->inH[2], x, y, a->outW, a->outH);
    CelF4 p3 = cel_t_at(in3, a->inStride[3], a->inW[3], a->inH[3], x, y, a->outW, a->outH);
    cel_store(out, a->outStride, x, y,
              cel_f4(p0.x + 2.0f * p1.x + 3.0f * p2.x + 4.0f * p3.x, p0.y + 2.0f * p1.y + 3.0f * p2.y + 4.0f * p3.y,
                     p0.z + 2.0f * p1.z + 3.0f * p2.z + 4.0f * p3.z, p0.w + 2.0f * p1.w + 3.0f * p2.w + 4.0f * p3.w));
}

CEL_DEVICE void cel_k_t_state(CEL_KERNEL_PARAMS) {
    CelF4 p = cel_load(in0, a->inStride[0], a->inW[0], a->inH[0], x, y);
    cel_uint hsh = cel_hash_u32((cel_uint)x * 73856093U ^ (cel_uint)y * 19349663U);   /* unsigned: no signed overflow */
    int idx = a->i[0] + (int)(hsh % (cel_uint)a->i[1]);
    float s = state[idx], t = state[a->i[2]];
    cel_store(out, a->outStride, x, y, cel_f4(p.x * s + t, p.y * s, p.z + s, (float)idx));
}

CEL_DEVICE void cel_k_t_math(CEL_KERNEL_PARAMS) {
    CelF4 p = cel_load(in0, a->inStride[0], a->inW[0], a->inH[0], x, y);
    float v = cel_fabs(p.x) + 0.01f;
    float r = cel_pow(v, 0.4545f) + cel_log(v) * 0.1f + cel_exp(-v) + cel_log2(v + 1.0f) * 0.25f;
    float g = cel_sin(p.y * 3.0f) * cel_cos(p.y * 1.7f) + cel_atan2(p.y, v) + cel_sqrt(v) + cel_log10(v + 2.0f);
    float b = cel_smoothstep(-0.5f, 1.5f, p.z) + cel_mix(p.z, p.x, 0.3f) + cel_floor(p.z * 4.0f) * 0.125f +
              cel_exp2(cel_clamp(p.z, -3.0f, 3.0f));
    float w = cel_finite_or(cel_log(p.w), -9.0f) + cel_fmin(p.w, 0.5f) + cel_fmax(p.w, 0.25f);
    cel_store(out, a->outStride, x, y, cel_f4(r, g, b, w));
}

CEL_DEVICE void cel_k_t_down2(CEL_KERNEL_PARAMS) {
    int sx = 2 * x, sy = 2 * y;
    CelF4 p0 = cel_load(in0, a->inStride[0], a->inW[0], a->inH[0], sx, sy);
    CelF4 p1 = cel_load(in0, a->inStride[0], a->inW[0], a->inH[0], sx + 1, sy);
    CelF4 p2 = cel_load(in0, a->inStride[0], a->inW[0], a->inH[0], sx, sy + 1);
    CelF4 p3 = cel_load(in0, a->inStride[0], a->inW[0], a->inH[0], sx + 1, sy + 1);
    cel_store(out, a->outStride, x, y,
              cel_f4(0.25f * (p0.x + p1.x + p2.x + p3.x), 0.25f * (p0.y + p1.y + p2.y + p3.y),
                     0.25f * (p0.z + p1.z + p2.z + p3.z), 0.25f * (p0.w + p1.w + p2.w + p3.w)));
}

CEL_DEVICE void cel_k_t_bilinear(CEL_KERNEL_PARAMS) {
    float fx = ((float)x + 0.5f) * a->f[0] + a->f[2], fy = ((float)y + 0.5f) * a->f[1] + a->f[3];
    cel_store(out, a->outStride, x, y, cel_sample_bilinear(in0, a->inStride[0], a->inW[0], a->inH[0], fx, fy));
}

CEL_DEVICE void cel_k_t_args(CEL_KERNEL_PARAMS) {
    int k = x % 4;
    cel_store(out, a->outStride, x, y,
              cel_f4(a->f[(x + y) % CEL_PASS_NF], (float)a->i[(x + 3 * y) % CEL_PASS_NI],
                     (float)(a->inW[k] * 100000 + a->inH[k]),
                     (float)(a->inStride[k] * 10 + (k == 0 ? a->outW * 1000 + a->outH : a->outStride))));
}

CEL_DEVICE void cel_k_t_hash(CEL_KERNEL_PARAMS) {
    cel_uint s = (cel_uint)a->i[0];
    cel_uint h = cel_hash_u32((cel_uint)x + 7919U * (cel_uint)y + s * 104729U);
    cel_store(out, a->outStride, x, y,
              cel_f4(cel_hash01(h), cel_hash01(h ^ 0x9e3779b9U), (float)(h >> 16), (float)(h & 65535U)));
}

CEL_DEVICE void cel_k_t_unused(CEL_KERNEL_PARAMS) {
    float v = 0.0f;
    if (a->inStride[0] != 0) v += cel_load(in0, a->inStride[0], a->inW[0], a->inH[0], x, y).x;
    if (a->inStride[1] != 0) v += cel_load(in1, a->inStride[1], a->inW[1], a->inH[1], x, y).x;
    if (a->inStride[2] != 0) v += cel_load(in2, a->inStride[2], a->inW[2], a->inH[2], x, y).x;
    if (a->inStride[3] != 0) v += cel_load(in3, a->inStride[3], a->inW[3], a->inH[3], x, y).x;
    cel_store(out, a->outStride, x, y,
              cel_f4(v, (float)((a->inStride[0] == 0 ? 1 : 0) + (a->inStride[1] == 0 ? 2 : 0) +
                                (a->inStride[2] == 0 ? 4 : 0) + (a->inStride[3] == 0 ? 8 : 0)),
                     (float)(a->inW[1] + a->inH[2] + a->inW[3]), 1.0f));
}

#endif /* CEL_TEST_ABI_KERNELS_H */
