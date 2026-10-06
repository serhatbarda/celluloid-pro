// ofxhost - OpenCL host side.
#include "OpenCLHost.h"

#include <cstring>
#include <limits>

#ifdef OFXHOST_HAVE_OPENCL
#ifndef CL_TARGET_OPENCL_VERSION
#define CL_TARGET_OPENCL_VERSION 120
#endif
#define CL_USE_DEPRECATED_OPENCL_1_1_APIS
#define CL_USE_DEPRECATED_OPENCL_1_2_APIS
#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif
#endif

namespace ofxhost {

#ifdef OFXHOST_HAVE_OPENCL

static std::string clErr(cl_int e) {
    switch (e) {
    case CL_SUCCESS: return "CL_SUCCESS";
    case CL_DEVICE_NOT_FOUND: return "CL_DEVICE_NOT_FOUND";
    case CL_OUT_OF_RESOURCES: return "CL_OUT_OF_RESOURCES";
    case CL_OUT_OF_HOST_MEMORY: return "CL_OUT_OF_HOST_MEMORY";
    case CL_MEM_OBJECT_ALLOCATION_FAILURE: return "CL_MEM_OBJECT_ALLOCATION_FAILURE";
    case CL_IMAGE_FORMAT_NOT_SUPPORTED: return "CL_IMAGE_FORMAT_NOT_SUPPORTED";
    case CL_BUILD_PROGRAM_FAILURE: return "CL_BUILD_PROGRAM_FAILURE";
    case CL_INVALID_VALUE: return "CL_INVALID_VALUE";
    case CL_INVALID_PLATFORM: return "CL_INVALID_PLATFORM";
    case CL_INVALID_DEVICE: return "CL_INVALID_DEVICE";
    case CL_INVALID_CONTEXT: return "CL_INVALID_CONTEXT";
    case CL_INVALID_COMMAND_QUEUE: return "CL_INVALID_COMMAND_QUEUE";
    case CL_INVALID_MEM_OBJECT: return "CL_INVALID_MEM_OBJECT";
    case CL_INVALID_KERNEL_ARGS: return "CL_INVALID_KERNEL_ARGS";
    case CL_INVALID_WORK_GROUP_SIZE: return "CL_INVALID_WORK_GROUP_SIZE";
    case -1001: return "CL_PLATFORM_NOT_FOUND_KHR (no ICD)";
    default: return "CL error " + std::to_string(e);
    }
}

static std::string platformStr(cl_platform_id p, cl_platform_info what) {
    char buf[1024] = {0};
    clGetPlatformInfo(p, what, sizeof(buf) - 1, buf, nullptr);
    return buf;
}
static std::string deviceStr(cl_device_id d, cl_device_info what) {
    char buf[1024] = {0};
    clGetDeviceInfo(d, what, sizeof(buf) - 1, buf, nullptr);
    return buf;
}

bool OpenCLHost::compiledIn() { return true; }

std::vector<ClDeviceInfo> OpenCLHost::enumerate(std::string& err) {
    std::vector<ClDeviceInfo> r;
    cl_uint np = 0;
    cl_int e = clGetPlatformIDs(0, nullptr, &np);
    if (e != CL_SUCCESS || np == 0) { err = "no OpenCL platform (" + clErr(e) + ")"; return r; }
    std::vector<cl_platform_id> plats(np);
    clGetPlatformIDs(np, plats.data(), nullptr);
    for (cl_uint p = 0; p < np; ++p) {
        cl_uint nd = 0;
        if (clGetDeviceIDs(plats[p], CL_DEVICE_TYPE_ALL, 0, nullptr, &nd) != CL_SUCCESS || nd == 0) continue;
        std::vector<cl_device_id> devs(nd);
        clGetDeviceIDs(plats[p], CL_DEVICE_TYPE_ALL, nd, devs.data(), nullptr);
        for (cl_uint d = 0; d < nd; ++d) {
            ClDeviceInfo inf;
            inf.platform = int(p); inf.device = int(d);
            inf.platformName = platformStr(plats[p], CL_PLATFORM_NAME);
            inf.deviceName = deviceStr(devs[d], CL_DEVICE_NAME);
            inf.deviceVersion = deviceStr(devs[d], CL_DEVICE_VERSION);
            inf.driverVersion = deviceStr(devs[d], CL_DRIVER_VERSION);
            cl_device_type t = 0;
            clGetDeviceInfo(devs[d], CL_DEVICE_TYPE, sizeof(t), &t, nullptr);
            inf.isGpu = (t & CL_DEVICE_TYPE_GPU) != 0;
            r.push_back(inf);
        }
    }
    return r;
}

OpenCLHost::~OpenCLHost() {
    if (queue_) { clFinish(cl_command_queue(queue_)); clReleaseCommandQueue(cl_command_queue(queue_)); }
    if (context_) clReleaseContext(cl_context(context_));
}

bool OpenCLHost::init(int platform, int device, std::string& err) {
    auto devs = enumerate(err);
    if (devs.empty()) return false;
    const ClDeviceInfo* pick = nullptr;
    if (platform >= 0 || device >= 0) {
        for (auto& d : devs)
            if ((platform < 0 || d.platform == platform) && (device < 0 || d.device == device)) { pick = &d; break; }
        if (!pick) { err = "requested OpenCL platform/device not found"; return false; }
    } else {
        for (auto& d : devs) if (d.isGpu) { pick = &d; break; }
        if (!pick) pick = &devs[0];
    }
    cl_uint np = 0;
    clGetPlatformIDs(0, nullptr, &np);
    std::vector<cl_platform_id> plats(np);
    clGetPlatformIDs(np, plats.data(), nullptr);
    cl_platform_id plat = plats[size_t(pick->platform)];
    cl_uint nd = 0;
    clGetDeviceIDs(plat, CL_DEVICE_TYPE_ALL, 0, nullptr, &nd);
    std::vector<cl_device_id> dl(nd);
    clGetDeviceIDs(plat, CL_DEVICE_TYPE_ALL, nd, dl.data(), nullptr);
    cl_device_id dev = dl[size_t(pick->device)];
    cl_context_properties cp[] = {CL_CONTEXT_PLATFORM, cl_context_properties(plat), 0};
    cl_int e = CL_SUCCESS;
    cl_context ctx = clCreateContext(cp, 1, &dev, nullptr, nullptr, &e);
    if (!ctx || e != CL_SUCCESS) { err = "clCreateContext: " + clErr(e); return false; }
    // in-order, non-profiling queue (what the OFX GPU extension tells plug-ins to assume)
    cl_command_queue q = clCreateCommandQueue(ctx, dev, 0, &e);
    if (!q || e != CL_SUCCESS) { clReleaseContext(ctx); err = "clCreateCommandQueue: " + clErr(e); return false; }
    platform_ = plat; device_ = dev; context_ = ctx; queue_ = q;
    info_ = *pick;
    return true;
}

void* OpenCLHost::createBuffer(size_t bytes, std::string& err) {
    cl_int e = CL_SUCCESS;
    cl_mem m = clCreateBuffer(cl_context(context_), CL_MEM_READ_WRITE, bytes ? bytes : 16, nullptr, &e);
    if (!m || e != CL_SUCCESS) { err = "clCreateBuffer(" + std::to_string(bytes) + "): " + clErr(e); return nullptr; }
    return m;
}

void* OpenCLHost::createImage2D(int w, int h, int channels, bool isFloat, int bytesPerComp, std::string& err) {
    cl_image_format fmt;
    fmt.image_channel_order = channels == 4 ? CL_RGBA : (channels == 1 ? CL_R : CL_RGB);
    if (isFloat) fmt.image_channel_data_type = bytesPerComp == 2 ? CL_HALF_FLOAT : CL_FLOAT;
    else fmt.image_channel_data_type = bytesPerComp == 2 ? CL_UNORM_INT16 : CL_UNORM_INT8;
    cl_int e = CL_SUCCESS;
    cl_mem m = clCreateImage2D(cl_context(context_), CL_MEM_READ_WRITE, &fmt, size_t(w), size_t(h), 0, nullptr, &e);
    if (!m || e != CL_SUCCESS) { err = "clCreateImage2D: " + clErr(e); return nullptr; }
    return m;
}

bool OpenCLHost::upload(void* mem, const void* src, size_t bytes, std::string& err) {
    cl_int e = clEnqueueWriteBuffer(cl_command_queue(queue_), cl_mem(mem), CL_TRUE, 0, bytes, src, 0, nullptr, nullptr);
    if (e != CL_SUCCESS) { err = "clEnqueueWriteBuffer: " + clErr(e); return false; }
    return true;
}
bool OpenCLHost::download(void* mem, void* dst, size_t bytes, std::string& err) {
    cl_int e = clEnqueueReadBuffer(cl_command_queue(queue_), cl_mem(mem), CL_TRUE, 0, bytes, dst, 0, nullptr, nullptr);
    if (e != CL_SUCCESS) { err = "clEnqueueReadBuffer: " + clErr(e); return false; }
    return true;
}
bool OpenCLHost::uploadImage(void* mem, const void* src, int w, int h, size_t rowBytes, std::string& err) {
    size_t origin[3] = {0, 0, 0}, region[3] = {size_t(w), size_t(h), 1};
    cl_int e = clEnqueueWriteImage(cl_command_queue(queue_), cl_mem(mem), CL_TRUE, origin, region, rowBytes, 0, src, 0, nullptr, nullptr);
    if (e != CL_SUCCESS) { err = "clEnqueueWriteImage: " + clErr(e); return false; }
    return true;
}
bool OpenCLHost::downloadImage(void* mem, void* dst, int w, int h, size_t rowBytes, std::string& err) {
    size_t origin[3] = {0, 0, 0}, region[3] = {size_t(w), size_t(h), 1};
    cl_int e = clEnqueueReadImage(cl_command_queue(queue_), cl_mem(mem), CL_TRUE, origin, region, rowBytes, 0, dst, 0, nullptr, nullptr);
    if (e != CL_SUCCESS) { err = "clEnqueueReadImage: " + clErr(e); return false; }
    return true;
}
bool OpenCLHost::fillPattern(void* mem, size_t bytes, std::string& err) {
    const float q = std::numeric_limits<float>::quiet_NaN();
    std::vector<float> tmp(bytes / 4 + 1, q);
    return upload(mem, tmp.data(), bytes, err);
}
void OpenCLHost::release(void* mem) {
    if (mem) clReleaseMemObject(cl_mem(mem));
}
bool OpenCLHost::finish(std::string& err) {
    cl_int e = clFinish(cl_command_queue(queue_));
    if (e != CL_SUCCESS) { err = "clFinish: " + clErr(e); return false; }
    return true;
}

int OpenCLHost::compileProgram(const char* source, void* resultProgram, std::string& log) {
    if (!context_ || !source || !resultProgram) return -1;
    cl_int e = CL_SUCCESS;
    cl_program prog = clCreateProgramWithSource(cl_context(context_), 1, &source, nullptr, &e);
    if (!prog || e != CL_SUCCESS) { log = "clCreateProgramWithSource: " + clErr(e); return -1; }
    cl_device_id dev = cl_device_id(device_);
    e = clBuildProgram(prog, 1, &dev, "", nullptr, nullptr);
    size_t n = 0;
    clGetProgramBuildInfo(prog, dev, CL_PROGRAM_BUILD_LOG, 0, nullptr, &n);
    if (n > 1) {
        std::string b(n, '\0');
        clGetProgramBuildInfo(prog, dev, CL_PROGRAM_BUILD_LOG, n, &b[0], nullptr);
        log = b;
    }
    if (e != CL_SUCCESS) { clReleaseProgram(prog); return -1; }
    *static_cast<cl_program*>(resultProgram) = prog;
    return 0;
}

#else  // !OFXHOST_HAVE_OPENCL

bool OpenCLHost::compiledIn() { return false; }
std::vector<ClDeviceInfo> OpenCLHost::enumerate(std::string& err) { err = "ofxhost was built without OpenCL"; return {}; }
OpenCLHost::~OpenCLHost() {}
bool OpenCLHost::init(int, int, std::string& err) { err = "ofxhost was built without OpenCL"; return false; }
void* OpenCLHost::createBuffer(size_t, std::string& err) { err = "no OpenCL"; return nullptr; }
void* OpenCLHost::createImage2D(int, int, int, bool, int, std::string& err) { err = "no OpenCL"; return nullptr; }
bool OpenCLHost::upload(void*, const void*, size_t, std::string& err) { err = "no OpenCL"; return false; }
bool OpenCLHost::download(void*, void*, size_t, std::string& err) { err = "no OpenCL"; return false; }
bool OpenCLHost::uploadImage(void*, const void*, int, int, size_t, std::string& err) { err = "no OpenCL"; return false; }
bool OpenCLHost::downloadImage(void*, void*, int, int, size_t, std::string& err) { err = "no OpenCL"; return false; }
bool OpenCLHost::fillPattern(void*, size_t, std::string& err) { err = "no OpenCL"; return false; }
void OpenCLHost::release(void*) {}
bool OpenCLHost::finish(std::string& err) { err = "no OpenCL"; return false; }
int OpenCLHost::compileProgram(const char*, void*, std::string& log) { log = "no OpenCL"; return -1; }

#endif

} // namespace ofxhost
