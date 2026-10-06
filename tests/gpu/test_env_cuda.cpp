// Celluloid Pro - GPU tests: CUDA environment (own stream on device 0, like a host would pass).
#include "GpuTestEnv.h"

#if defined(CEL_TEST_HAVE_CUDA)
#include <cuda_runtime_api.h>
#include <map>

namespace celtest {
namespace {
class CudaEnv : public Env {
public:
    ~CudaEnv() override {
        for (auto& kv : m_bases) cudaFree(kv.second);
        if (m_s2) cudaStreamDestroy(m_s2);
        if (m_s) cudaStreamDestroy(m_s);
    }
    bool init(std::string* why) {
        int n = 0;
        cudaError_t e = cudaGetDeviceCount(&n);
        if (e != cudaSuccess || n == 0) {
            *why = std::string("no CUDA device: ") + cudaGetErrorString(e);
            cudaGetLastError();
            return false;
        }
        cudaSetDevice(0);
        cudaDeviceProp p;
        cudaGetDeviceProperties(&p, 0);
        m_name = p.name;
        return cudaStreamCreateWithFlags(&m_s, cudaStreamNonBlocking) == cudaSuccess;
    }
    const char* name() const override { return "CUDA"; }
    cel::gpu::Api api() const override { return cel::gpu::Api::CUDA; }
    std::string device() const override { return m_name; }
    bool supportsNegativeStride() const override { return true; }
    void* upload(const HostImg& img) override {
        void* base = nullptr;
        if (cudaMalloc(&base, img.bytes()) != cudaSuccess) return nullptr;
        cudaMemcpy(base, img.store.data(), img.bytes(), cudaMemcpyHostToDevice);
        const size_t as = size_t(img.stride < 0 ? -img.stride : img.stride);
        float* row0 = static_cast<float*>(base) + (img.stride < 0 ? (size_t(img.h) - 1) * as : 0);
        m_bases[row0] = base;
        return row0;
    }
    void* allocLike(const HostImg& img) override { return upload(img); }
    bool download(void* h, HostImg& img) override {
        cudaDeviceSynchronize();
        return cudaMemcpy(img.store.data(), m_bases[h], img.bytes(), cudaMemcpyDeviceToHost) == cudaSuccess;
    }
    void release(void* h) override {
        auto it = m_bases.find(h);
        if (it != m_bases.end()) { cudaFree(it->second); m_bases.erase(it); }
    }
    cel::engine::GpuContext context() override { cel::engine::GpuContext g; g.cudaStream = m_s; return g; }
    void finish() override { cudaDeviceSynchronize(); }
    bool makeSecondQueue() override {
        if (!m_s2) cudaStreamCreateWithFlags(&m_s2, cudaStreamNonBlocking);
        return m_s2 != nullptr;
    }
    cel::engine::GpuContext secondContext() override { cel::engine::GpuContext g; g.cudaStream = m_s2; return g; }

private:
    cudaStream_t m_s = nullptr, m_s2 = nullptr;
    std::string m_name;
    std::map<void*, void*> m_bases;
};
} // namespace

std::unique_ptr<Env> makeCudaEnv(std::string* why) {
    std::unique_ptr<CudaEnv> e(new CudaEnv());
    if (!e->init(why)) return nullptr;
    return std::unique_ptr<Env>(e.release());
}
} // namespace celtest

#else
namespace celtest {
std::unique_ptr<Env> makeCudaEnv(std::string* why) {
    *why = "CUDA not compiled in";
    return nullptr;
}
} // namespace celtest
#endif
