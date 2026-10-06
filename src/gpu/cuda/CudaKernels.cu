// Celluloid Pro - CUDA translation unit of the production kernel set (owned by the gpu role).
// Compiled by nvcc for every architecture in CEL_CUDA_ARCHITECTURES (SASS) plus PTX of the newest
// one (JIT for future GPUs). The host side looks the entries up in cel_cuda_production_entries
// (index = KernelId; the probe kernel is appended at KID_COUNT).
#include "../../kernels/AllKernels.h"
#include "../kernels/Probe.h"
#include "../kernels/EntryCuda.h"

namespace cel_cuda_production {
#define CEL_KERNEL(name) CEL_GPU_ENTRY(name)
#include "../../kernels/KernelList.h"
#undef CEL_KERNEL
CEL_GPU_ENTRY(gpu_probe)
} // namespace cel_cuda_production

namespace cel { namespace gpu { namespace detail {
extern const void* const cel_cuda_production_entries[];
const void* const cel_cuda_production_entries[] = {
#define CEL_KERNEL(name) reinterpret_cast<const void*>(&cel_cuda_production::cel_##name),
#include "../../kernels/KernelList.h"
#undef CEL_KERNEL
    reinterpret_cast<const void*>(&cel_cuda_production::cel_gpu_probe)};
}}} // namespace cel::gpu::detail
