// tests/license - compiles the watermark kernel as OpenCL C (e.g. on POCL) and compares the GPU
// result with the CPU build of the same portable source.
//   opencl_watermark_check <src/kernels dir>      exit 0 = OK, 1 = mismatch/compile error, 77 = no OpenCL
#define CL_TARGET_OPENCL_VERSION 120
#include <CL/cl.h>
#include "../../src/kernels/LicenseKernels.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static const char* kEntry =
    "#include \"LicenseKernels.h\"\n"
    "__kernel void cel_watermark(__constant CelPassArgs* a, __global float* out,\n"
    "                            __global const float* in0, __global const float* dummy) {\n"
    "    int x = (int)get_global_id(0), y = (int)get_global_id(1);\n"
    "    if (x >= a->outW || y >= a->outH) return;\n"
    "    cel_k_watermark(x, y, a, out, in0, dummy, dummy, dummy, dummy);\n"
    "}\n";

#define CK(e) do { cl_int _e = (e); if (_e != CL_SUCCESS) { std::fprintf(stderr, "%s failed: %d\n", #e, _e); return 1; } } while (0)

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: %s <src/kernels dir>\n", argv[0]); return 2; }
    cl_uint np = 0;
    if (clGetPlatformIDs(0, nullptr, &np) != CL_SUCCESS || np == 0) { std::puts("no OpenCL platform - skipped"); return 77; }
    std::vector<cl_platform_id> plats(np);
    CK(clGetPlatformIDs(np, plats.data(), nullptr));
    cl_device_id dev = nullptr;
    for (auto p : plats)
        if (clGetDeviceIDs(p, CL_DEVICE_TYPE_ALL, 1, &dev, nullptr) == CL_SUCCESS) break;
    if (!dev) { std::puts("no OpenCL device - skipped"); return 77; }
    char name[256] = {0};
    clGetDeviceInfo(dev, CL_DEVICE_NAME, sizeof(name), name, nullptr);
    cl_int err;
    cl_context ctx = clCreateContext(nullptr, 1, &dev, nullptr, nullptr, &err); CK(err);
    cl_command_queue q = clCreateCommandQueue(ctx, dev, 0, &err); CK(err);
    cl_program prog = clCreateProgramWithSource(ctx, 1, &kEntry, nullptr, &err); CK(err);
    const std::string opts = std::string("-cl-std=CL1.2 -cl-fp32-correctly-rounded-divide-sqrt -I ") + argv[1];
    err = clBuildProgram(prog, 1, &dev, opts.c_str(), nullptr, nullptr);
    {
        size_t n = 0;
        clGetProgramBuildInfo(prog, dev, CL_PROGRAM_BUILD_LOG, 0, nullptr, &n);
        std::string log(n, '\0');
        clGetProgramBuildInfo(prog, dev, CL_PROGRAM_BUILD_LOG, n, &log[0], nullptr);
        if (err != CL_SUCCESS || log.find("warning") != std::string::npos) std::fprintf(stderr, "build log:\n%s\n", log.c_str());
    }
    CK(err);
    cl_kernel k = clCreateKernel(prog, "cel_watermark", &err); CK(err);

    double worst = 0;
    const int sizes[][2] = {{1920, 1080}, {1280, 720}, {1080, 1920}, {97, 31}};
    for (auto& s : sizes) {
        const int W = s[0], H = s[1];
        std::vector<float> in(size_t(W) * H * 4), cpu(in.size()), gpu(in.size());
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                float* p = &in[(size_t(y) * W + x) * 4];
                p[0] = float(x) / W; p[1] = float(y) / H; p[2] = 0.5f + 0.5f * std::sin(x * 0.05f + y * 0.03f); p[3] = 0.75f;
            }
        CelPassArgs a;
        std::memset(&a, 0, sizeof(a));
        a.outW = W; a.outH = H; a.outStride = W * 4;
        a.inW[0] = W; a.inH[0] = H; a.inStride[0] = W * 4;
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x)
                cel_k_watermark(x, y, &a, cpu.data(), in.data(), nullptr, nullptr, nullptr, nullptr);

        cl_mem bA = clCreateBuffer(ctx, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, sizeof(a), &a, &err); CK(err);
        cl_mem bIn = clCreateBuffer(ctx, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, in.size() * 4, in.data(), &err); CK(err);
        cl_mem bOut = clCreateBuffer(ctx, CL_MEM_WRITE_ONLY, gpu.size() * 4, nullptr, &err); CK(err);
        CK(clSetKernelArg(k, 0, sizeof(cl_mem), &bA));
        CK(clSetKernelArg(k, 1, sizeof(cl_mem), &bOut));
        CK(clSetKernelArg(k, 2, sizeof(cl_mem), &bIn));
        CK(clSetKernelArg(k, 3, sizeof(cl_mem), &bIn));
        const size_t lw[2] = {16, 16};
        const size_t gw[2] = {(size_t(W) + 15) / 16 * 16, (size_t(H) + 15) / 16 * 16};
        CK(clEnqueueNDRangeKernel(q, k, 2, nullptr, gw, lw, 0, nullptr, nullptr));
        CK(clEnqueueReadBuffer(q, bOut, CL_TRUE, 0, gpu.size() * 4, gpu.data(), 0, nullptr, nullptr));
        clReleaseMemObject(bA); clReleaseMemObject(bIn); clReleaseMemObject(bOut);
        double m = 0;
        size_t changed = 0;
        for (size_t i = 0; i < cpu.size(); ++i) {
            m = std::fmax(m, std::fabs(double(cpu[i]) - gpu[i]));
            if (cpu[i] != in[i]) ++changed;
        }
        std::printf("  %4dx%-4d  max |cpu - opencl| = %.3g  (%zu values stamped)\n", W, H, m, changed);
        worst = std::fmax(worst, m);
    }
    std::printf("OpenCL device: %s  worst diff %.3g\n", name, worst);
    clReleaseKernel(k); clReleaseProgram(prog); clReleaseCommandQueue(q); clReleaseContext(ctx);
    return worst <= 2e-4 ? 0 : 1;
}
