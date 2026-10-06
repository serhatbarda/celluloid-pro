/* Celluloid Pro - uniform kernel ABI.  FIXED (architect).
 *
 * Every pass of a frame is one kernel invocation over an output image. ALL kernels share the
 * signature below, so each GPU backend can generate its entry points from KernelList.h with one
 * macro and execute any Plan generically.
 *
 * Buffers: every image buffer is interleaved float RGBA (4 floats / pixel). Row stride is given in
 * FLOATS (not bytes) and may be larger than width*4. Host source/destination images use the
 * host's stride; internal buffers are packed (stride = width*4).
 *
 * `state` is ONE global float buffer prepared by core::Pipeline::prepare(): a header + colour
 * state + all LUTs; texture kernels may also read data the plan stored there (see Engine.h).
 */
#ifndef CEL_KERNEL_ABI_H
#define CEL_KERNEL_ABI_H
#include "Portable.h"

#define CEL_PASS_MAX_INPUTS 4
#define CEL_PASS_NF 32
#define CEL_PASS_NI 8

typedef struct {
    int outW, outH, outStride;                 /* output image size, stride in floats */
    int inW[CEL_PASS_MAX_INPUTS];
    int inH[CEL_PASS_MAX_INPUTS];
    int inStride[CEL_PASS_MAX_INPUTS];         /* stride in floats; 0 if input unused */
    float f[CEL_PASS_NF];                      /* kernel-specific float params (documented per kernel) */
    int   i[CEL_PASS_NI];                      /* kernel-specific int params */
} CelPassArgs;                                 /* size = 4*(3+12+32+8) = 220 bytes, POD */

/* Uniform signature. x,y = output pixel. Inputs may alias nothing written in the same pass. */
#define CEL_KERNEL_PARAMS \
    int x, int y, \
    CEL_CONSTANT const CelPassArgs* a, \
    CEL_GLOBAL float* out, \
    CEL_GLOBAL const float* in0, CEL_GLOBAL const float* in1, \
    CEL_GLOBAL const float* in2, CEL_GLOBAL const float* in3, \
    CEL_GLOBAL const float* state

#define CEL_KERNEL_ARGS x, y, a, out, in0, in1, in2, in3, state

/* pixel access helpers (clamp-to-edge) */
CEL_DEVICE CelF4 cel_load(CEL_GLOBAL const float* buf, int stride, int w, int h, int x, int y) {
    x = cel_imin(cel_imax(x, 0), w - 1); y = cel_imin(cel_imax(y, 0), h - 1);
    CEL_GLOBAL const float* p = buf + y * stride + x * 4;
    return cel_f4(p[0], p[1], p[2], p[3]);
}
CEL_DEVICE void cel_store(CEL_GLOBAL float* buf, int stride, int x, int y, CelF4 v) {
    CEL_GLOBAL float* p = buf + y * stride + x * 4;
    p[0] = v.x; p[1] = v.y; p[2] = v.z; p[3] = v.w;
}
/* bilinear sample in input pixel coordinates (pixel centres at i+0.5), clamp-to-edge */
CEL_DEVICE CelF4 cel_sample_bilinear(CEL_GLOBAL const float* buf, int stride, int w, int h, float fx, float fy) {
    float sx = fx - 0.5f, sy = fy - 0.5f;
    float x0f = cel_floor(sx), y0f = cel_floor(sy);
    int x0 = (int)x0f, y0 = (int)y0f;
    float tx = sx - x0f, ty = sy - y0f;
    CelF4 a = cel_load(buf, stride, w, h, x0, y0),     b = cel_load(buf, stride, w, h, x0 + 1, y0);
    CelF4 c = cel_load(buf, stride, w, h, x0, y0 + 1), d = cel_load(buf, stride, w, h, x0 + 1, y0 + 1);
    CelF4 r;
    r.x = cel_mix(cel_mix(a.x, b.x, tx), cel_mix(c.x, d.x, tx), ty);
    r.y = cel_mix(cel_mix(a.y, b.y, tx), cel_mix(c.y, d.y, tx), ty);
    r.z = cel_mix(cel_mix(a.z, b.z, tx), cel_mix(c.z, d.z, tx), ty);
    r.w = cel_mix(cel_mix(a.w, b.w, tx), cel_mix(c.w, d.w, tx), ty);
    return r;
}

#endif /* CEL_KERNEL_ABI_H */
