// Reafilm Pro - GPU tests: OpenCL environment (creates its own context + queue like a host).
#include "GpuTestEnv.h"
#include "../../src/gpu/opencl/CLApi.h"

#include <cstdlib>

namespace celtest {

namespace {
namespace cl = cel::gpu::cl;

class OpenCLEnv : public Env {
public:
    ~OpenCLEnv() override {
        const auto& a = cl::api();
        if (m_q2) a.clReleaseCommandQueue(m_q2);
        if (m_q) a.clReleaseCommandQueue(m_q);
        if (m_ctx) a.clReleaseContext(m_ctx);
    }
    bool init(std::string* why, bool ooo) {
        const auto& a = cl::api();
        if (!a.ok) { *why = a.error; return false; }
        cl_uint np = 0;
        if (a.clGetPlatformIDs(0, nullptr, &np) != CL_SUCCESS || np == 0) { *why = "no OpenCL platform"; return false; }
        std::vector<cl_platform_id> plats(np);
        a.clGetPlatformIDs(np, plats.data(), nullptr);
        std::vector<cl_device_id> devs;
        for (auto p : plats) {
            cl_uint nd = 0;
            if (a.clGetDeviceIDs(p, CL_DEVICE_TYPE_ALL, 0, nullptr, &nd) != CL_SUCCESS || !nd) continue;
            std::vector<cl_device_id> d(nd);
            a.clGetDeviceIDs(p, CL_DEVICE_TYPE_ALL, nd, d.data(), nullptr);
            devs.insert(devs.end(), d.begin(), d.end());
        }
        if (devs.empty()) { *why = "no OpenCL device"; return false; }
        int idx = 0;
        if (const char* e = std::getenv("CEL_TEST_OPENCL_DEVICE")) idx = std::atoi(e);
        m_dev = devs[size_t(idx) % devs.size()];
        cl_int err = CL_SUCCESS;
        m_ctx = a.clCreateContext(nullptr, 1, &m_dev, nullptr, nullptr, &err);
        if (err != CL_SUCCESS) { *why = std::string("clCreateContext: ") + cl::errorName(err); return false; }
        m_q = a.clCreateCommandQueue(m_ctx, m_dev, ooo ? CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE : 0, &err);
        if (err != CL_SUCCESS) { *why = std::string("clCreateCommandQueue: ") + cl::errorName(err); return false; }
        return true;
    }
    const char* name() const override { return "OpenCL"; }
    cel::gpu::Api api() const override { return cel::gpu::Api::OpenCL; }
    std::string device() const override { return cl::deviceString(m_dev); }
    void* upload(const HostImg& img) override {
        const auto& a = cl::api();
        cl_int e = CL_SUCCESS;
        cl_mem m = a.clCreateBuffer(m_ctx, CL_MEM_READ_WRITE, img.bytes(), nullptr, &e);
        if (e != CL_SUCCESS) return nullptr;
        a.clEnqueueWriteBuffer(m_q, m, CL_TRUE, 0, img.bytes(), img.store.data(), 0, nullptr, nullptr);
        return m;
    }
    void* allocLike(const HostImg& img) override { return upload(img); }
    bool download(void* h, HostImg& img) override {
        const auto& a = cl::api();
        return a.clEnqueueReadBuffer(m_q, static_cast<cl_mem>(h), CL_TRUE, 0, img.bytes(), img.store.data(), 0,
                                     nullptr, nullptr) == CL_SUCCESS;
    }
    void release(void* h) override { if (h) cl::api().clReleaseMemObject(static_cast<cl_mem>(h)); }
    cel::engine::GpuContext context() override { cel::engine::GpuContext g; g.openclCommandQueue = m_q; return g; }
    void finish() override { cl::api().clFinish(m_q); if (m_q2) cl::api().clFinish(m_q2); }
    bool makeSecondQueue() override {
        cl_int e = CL_SUCCESS;
        if (!m_q2) m_q2 = cl::api().clCreateCommandQueue(m_ctx, m_dev, 0, &e);
        return m_q2 != nullptr;
    }
    cel::engine::GpuContext secondContext() override { cel::engine::GpuContext g; g.openclCommandQueue = m_q2; return g; }

private:
    cl_device_id m_dev = nullptr;
    cl_context m_ctx = nullptr;
    cl_command_queue m_q = nullptr, m_q2 = nullptr;
};
} // namespace

std::unique_ptr<Env> makeOpenCLEnv(std::string* why, bool outOfOrder) {
    std::unique_ptr<OpenCLEnv> e(new OpenCLEnv());
    if (!e->init(why, outOfOrder)) return nullptr;
    return std::unique_ptr<Env>(e.release());
}

} // namespace celtest
