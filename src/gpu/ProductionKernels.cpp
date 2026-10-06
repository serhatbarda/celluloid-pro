// Celluloid Pro - the production kernel set (KernelList.h) for the GPU backends.
// Entry names are generated from the X-macro list; the sources are the embedded flattened
// programs (generated .cpp files, see CMakeLists.txt) and the CUDA entry table (CudaKernels.cu).
#include "GpuCommon.h"

namespace cel { namespace gpu {

namespace embedded {
#if CEL_GPU_HAVE_OPENCL
extern const char cel_opencl_production[];
extern const std::size_t cel_opencl_production_len;
extern const char cel_opencl_production_sha256[];
#endif
#if CEL_GPU_HAVE_METAL
extern const char cel_metal_production[];
extern const std::size_t cel_metal_production_len;
extern const char cel_metal_production_sha256[];
#endif
} // namespace embedded

#if CEL_GPU_HAVE_CUDA
namespace detail { extern const void* const cel_cuda_production_entries[]; }
#endif

namespace {
const char* const kProductionNames[] = {
#define CEL_KERNEL(name) "cel_" #name,
#include "../kernels/KernelList.h"
#undef CEL_KERNEL
    "cel_gpu_probe"};
static_assert(sizeof(kProductionNames) / sizeof(kProductionNames[0]) == size_t(engine::KID_COUNT) + 1,
              "kernel name table out of sync with KernelList.h");
} // namespace

const KernelSet& productionKernelSet() {
    static const KernelSet ks = [] {
        KernelSet k;
        k.name = "production";
        k.count = int(engine::KID_COUNT);
        k.names = kProductionNames;
#if CEL_GPU_HAVE_OPENCL
        k.openclSource = embedded::cel_opencl_production;
        k.openclSourceLen = embedded::cel_opencl_production_len;
        k.openclSha256 = embedded::cel_opencl_production_sha256;
#endif
#if CEL_GPU_HAVE_METAL
        k.metalSource = embedded::cel_metal_production;
        k.metalSourceLen = embedded::cel_metal_production_len;
        k.metalSha256 = embedded::cel_metal_production_sha256;
#endif
#if CEL_GPU_HAVE_CUDA
        k.cudaEntries = detail::cel_cuda_production_entries;
#endif
        return k;
    }();
    return ks;
}

}} // namespace cel::gpu
