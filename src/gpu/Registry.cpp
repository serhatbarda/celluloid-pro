// Celluloid Pro - GPU backend registry: engine::openclBackend()/cudaBackend()/metalBackend(),
// factories, and the "not compiled in" implementations for disabled APIs.
#include "GpuCommon.h"

namespace cel { namespace gpu {

namespace detail {
#if CEL_GPU_HAVE_OPENCL
std::unique_ptr<engine::Backend> createOpenCLBackendImpl(const KernelSet& ks);
engine::Backend* productionOpenCL();
#else
GpuBackendBase* productionOpenCLIfCreated() { return nullptr; }
SelfTestResult selfTestOpenCL(bool) {
    SelfTestResult r;
    r.message = "OpenCL backend not compiled in (CEL_ENABLE_OPENCL=OFF)";
    return r;
}
bool cpuFallbackOpenCL(engine::Backend&, const engine::Plan&, const std::vector<float>&, uint64_t,
                       const engine::HostImage&, const engine::HostImage&, const engine::GpuContext&,
                       std::string* err) {
    if (err) *err = "OpenCL backend not compiled in";
    return false;
}
#endif
#if CEL_GPU_HAVE_CUDA
std::unique_ptr<engine::Backend> createCudaBackendImpl(const KernelSet& ks);
engine::Backend* productionCuda();
#else
GpuBackendBase* productionCudaIfCreated() { return nullptr; }
SelfTestResult selfTestCuda(bool) {
    SelfTestResult r;
    r.message = "CUDA backend not compiled in (CEL_ENABLE_CUDA=OFF)";
    return r;
}
bool cpuFallbackCuda(engine::Backend&, const engine::Plan&, const std::vector<float>&, uint64_t,
                     const engine::HostImage&, const engine::HostImage&, const engine::GpuContext&,
                     std::string* err) {
    if (err) *err = "CUDA backend not compiled in";
    return false;
}
#endif
#if CEL_GPU_HAVE_METAL
std::unique_ptr<engine::Backend> createMetalBackendImpl(const KernelSet& ks);
engine::Backend* productionMetal();
#else
GpuBackendBase* productionMetalIfCreated() { return nullptr; }
SelfTestResult selfTestMetal(bool) {
    SelfTestResult r;
    r.message = "Metal backend not compiled in (CEL_ENABLE_METAL=OFF)";
    return r;
}
bool cpuFallbackMetal(engine::Backend&, const engine::Plan&, const std::vector<float>&, uint64_t,
                      const engine::HostImage&, const engine::HostImage&, const engine::GpuContext&,
                      std::string* err) {
    if (err) *err = "Metal backend not compiled in";
    return false;
}
#endif
} // namespace detail

std::unique_ptr<engine::Backend> createOpenCLBackend(const KernelSet& ks) {
#if CEL_GPU_HAVE_OPENCL
    return detail::createOpenCLBackendImpl(ks);
#else
    (void)ks;
    return nullptr;
#endif
}
std::unique_ptr<engine::Backend> createCudaBackend(const KernelSet& ks) {
#if CEL_GPU_HAVE_CUDA
    return detail::createCudaBackendImpl(ks);
#else
    (void)ks;
    return nullptr;
#endif
}
std::unique_ptr<engine::Backend> createMetalBackend(const KernelSet& ks) {
#if CEL_GPU_HAVE_METAL
    return detail::createMetalBackendImpl(ks);
#else
    (void)ks;
    return nullptr;
#endif
}

}} // namespace cel::gpu

namespace cel { namespace engine {

Backend* openclBackend() {
#if CEL_GPU_HAVE_OPENCL
    return gpu::detail::productionOpenCL();
#else
    return nullptr;
#endif
}
Backend* cudaBackend() {
#if CEL_GPU_HAVE_CUDA
    return gpu::detail::productionCuda();
#else
    return nullptr;
#endif
}
Backend* metalBackend() {
#if CEL_GPU_HAVE_METAL
    return gpu::detail::productionMetal();
#else
    return nullptr;
#endif
}

}} // namespace cel::engine
