/* Reafilm Pro - CUDA entry points for the uniform kernel ABI (owned by the gpu role).
 * CEL_GPU_ENTRY(name) defines  __global__ void cel_<name>(...)  wrapping cel_k_<name>.
 * Parameters (launched generically with cudaLaunchKernel by src/gpu/cuda/CudaBackend.cpp):
 *   CelPassArgs by value (kernel parameter space, 220 bytes), out, in0..in3 (nullptr if unused),
 *   state. The grid is rounded up to whole blocks: out-of-range threads return early.
 * Outputs never alias inputs within a pass (validated on the host), hence __restrict__. */
#ifndef CEL_GPU_ENTRY_CUDA_H
#define CEL_GPU_ENTRY_CUDA_H

#define CEL_GPU_ENTRY(name)                                                                     \
__global__ void cel_##name(const CelPassArgs cel_args, float* __restrict__ cel_out,             \
                           const float* __restrict__ cel_in0, const float* __restrict__ cel_in1, \
                           const float* __restrict__ cel_in2, const float* __restrict__ cel_in3, \
                           const float* __restrict__ cel_state) {                               \
    const int cel_x = (int)(blockIdx.x * blockDim.x + threadIdx.x);                             \
    const int cel_y = (int)(blockIdx.y * blockDim.y + threadIdx.y);                             \
    if (cel_x >= cel_args.outW || cel_y >= cel_args.outH) return;                               \
    cel_k_##name(cel_x, cel_y, &cel_args, cel_out, cel_in0, cel_in1, cel_in2, cel_in3, cel_state); \
}

#endif /* CEL_GPU_ENTRY_CUDA_H */
