// CUDA translation unit of the ABI test kernel set (tests/gpu only).
#include "AbiKernels.h"
#include "../../../src/gpu/kernels/Probe.h"
#include "../../../src/gpu/kernels/EntryCuda.h"
namespace cel_cuda_abi {
#define CEL_KERNEL(name) CEL_GPU_ENTRY(name)
#include "AbiKernelList.h"
#undef CEL_KERNEL
CEL_GPU_ENTRY(gpu_probe)
} // namespace cel_cuda_abi
extern const void* const cel_cuda_abi_entries[];
const void* const cel_cuda_abi_entries[] = {
#define CEL_KERNEL(name) reinterpret_cast<const void*>(&cel_cuda_abi::cel_##name),
#include "AbiKernelList.h"
#undef CEL_KERNEL
    reinterpret_cast<const void*>(&cel_cuda_abi::cel_gpu_probe)};
