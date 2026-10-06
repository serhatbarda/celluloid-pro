// Compile-only check of the watermark kernel as CUDA device code without the CUDA toolkit:
//   clang++ -x cuda --cuda-device-only --cuda-gpu-arch=sm_52 -nocudainc -nocudalib -S cuda_syntax_check.cu
// The prelude declares the few CUDA built-ins Portable.h uses (normally from the CUDA headers).
// clang rejects device code that reads host-only globals, so this proves the font tables are
// correctly placed in __constant__ memory.
#ifndef __CUDACC__
#define __CUDACC__ 1
#endif
#define __device__ __attribute__((device))
#define __constant__ __attribute__((constant))
#define __global__ __attribute__((global))
#define __forceinline__ __inline__ __attribute__((always_inline))
__device__ float powf(float, float);
__device__ float expf(float);
__device__ float exp2f(float);
__device__ float logf(float);
__device__ float log2f(float);
__device__ float log10f(float);
__device__ float sqrtf(float);
__device__ float floorf(float);
__device__ float fabsf(float);
__device__ float fminf(float, float);
__device__ float fmaxf(float, float);
__device__ float sinf(float);
__device__ float cosf(float);
__device__ float atan2f(float, float);
__device__ bool isfinite(float);
__device__ int min(int, int);
__device__ int max(int, int);

#include "../../src/kernels/LicenseKernels.h"

extern "C" __global__ void cel_watermark(const CelPassArgs* a, float* out, const float* in0, int x, int y) {
    cel_k_watermark(x, y, a, out, in0, 0, 0, 0, 0);
}
