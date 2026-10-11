/* Reafilm Pro - GPU self-test probe kernel (portable dialect, owned by the gpu role).
 * Not part of KernelList.h: every GPU kernel set appends it as entry `cel_gpu_probe` at index
 * KernelSet::count. It writes a 16 x 1 output whose pixels echo the CelPassArgs layout, the state
 * buffer and an input, and evaluate every cel_* math wrapper, so the self test can compare the
 * device against the C++ build of the same function (src/gpu/GpuCommon.cpp). */
#ifndef CEL_GPU_PROBE_H
#define CEL_GPU_PROBE_H
#include "../../kernels/KernelABI.h"

CEL_DEVICE void cel_k_gpu_probe(CEL_KERNEL_PARAMS) {
    CelF4 r = cel_f4(0.0f, 0.0f, 0.0f, 0.0f);
    (void)in1; (void)in2; (void)in3;   /* bound (layout check via a->in*), never read */
    float t = a->f[0] + 0.37f * (float)x;
    if (x == 0) {
        r = cel_f4((float)a->outW, (float)a->outH, (float)a->outStride, (float)a->inStride[0]);
    } else if (x == 1) {
        r = cel_f4(a->f[1], a->f[CEL_PASS_NF - 1], (float)a->i[0], (float)a->i[CEL_PASS_NI - 1]);
    } else if (x == 2) {
        r = cel_load(in0, a->inStride[0], a->inW[0], a->inH[0], 1, 0);
    } else if (x == 3) {
        r = cel_f4(state[0], state[1], state[2], state[3]);
    } else if (x == 4) {
        r = cel_f4(cel_pow(t, 2.2f), cel_pow(t, 0.4545f), cel_exp(-t), cel_exp2(t * 0.5f));
    } else if (x == 5) {
        r = cel_f4(cel_log(t), cel_log2(t), cel_log10(t), cel_sqrt(t));
    } else if (x == 6) {
        r = cel_f4(cel_sin(t * 3.0f), cel_cos(t * 3.0f), cel_atan2(t, 1.0f - t), cel_floor(t * 7.3f));
    } else if (x == 7) {
        r = cel_f4(cel_hash01((cel_uint)(x * 7919 + 13)), (float)(cel_hash_u32(123456789U) >> 8),
                   cel_smoothstep(0.0f, 1.0f, t * 0.5f), cel_fabs(-t));
    } else if (x == 8) {
        r = cel_sample_bilinear(in0, a->inStride[0], a->inW[0], a->inH[0], 1.25f, 0.75f);
    } else if (x == 9) {
        r = cel_f4((float)a->inW[1], (float)a->inH[3], (float)a->inStride[3], (float)a->i[3]);
    } else if (x == 10) {
        r = cel_f4(cel_finite_or(cel_log(-1.0f), 7.0f), cel_finite_or(cel_sqrt(-1.0f), 8.0f),
                   cel_mix(1.0f, 3.0f, 0.25f), (float)cel_imin(cel_imax(x, 3), 5));
    } else {
        float s = 0.0f;
        int k;
        for (k = 0; k < 64; ++k) s += cel_sin(t + (float)k * 0.1f) * cel_exp(-0.01f * (float)k);
        r = cel_f4(s, cel_fmin(t, 0.5f), cel_fmax(t, 0.5f), cel_clamp(t, 0.2f, 0.3f));
    }
    cel_store(out, a->outStride, x, y, r);
}

#endif /* CEL_GPU_PROBE_H */
