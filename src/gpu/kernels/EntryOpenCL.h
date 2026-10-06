/* Celluloid Pro - OpenCL C entry points for the uniform kernel ABI (owned by the gpu role).
 * CEL_GPU_ENTRY(name) defines  __kernel void cel_<name>(...)  wrapping cel_k_<name>.
 * Kernel arguments (set generically by src/gpu/opencl/OpenCLBackend.cpp):
 *   0 __constant CelPassArgs* args   all passes of the frame (one buffer)
 *   1 int pass                       index of this pass in args
 *   2 out, 3..6 in0..in3, 7 state    (__global; unused inputs are NULL)
 * The ND-range is rounded up to the work-group size: out-of-range work items return early.
 * Outputs never alias inputs within a pass (validated on the host), hence `restrict`. */
#ifndef CEL_GPU_ENTRY_OPENCL_H
#define CEL_GPU_ENTRY_OPENCL_H

#define CEL_GPU_ENTRY(name)                                                                     \
__kernel void cel_##name(__constant CelPassArgs* cel_args, int cel_pass,                        \
                         __global float* restrict cel_out,                                      \
                         __global const float* restrict cel_in0,                                \
                         __global const float* restrict cel_in1,                                \
                         __global const float* restrict cel_in2,                                \
                         __global const float* restrict cel_in3,                                \
                         __global const float* restrict cel_state) {                            \
    const int cel_x = (int)get_global_id(0);                                                    \
    const int cel_y = (int)get_global_id(1);                                                    \
    __constant const CelPassArgs* cel_a = cel_args + cel_pass;                                  \
    if (cel_x >= cel_a->outW || cel_y >= cel_a->outH) return;                                   \
    cel_k_##name(cel_x, cel_y, cel_a, cel_out, cel_in0, cel_in1, cel_in2, cel_in3, cel_state);  \
}

#endif /* CEL_GPU_ENTRY_OPENCL_H */
