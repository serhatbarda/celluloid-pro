// Reafilm Pro - dynamically loaded OpenCL API (owned by the gpu role).
//
// The plugin never links an OpenCL import library: the ICD loader (OpenCL.dll on Windows,
// libOpenCL.so.1 on Linux, OpenCL.framework on macOS) is loaded on first use. A missing OpenCL
// runtime therefore never prevents the plugin from loading (the backend just reports
// "unavailable"). Inside a host that already loaded the ICD loader (DaVinci Resolve), the same
// module is returned by the OS, so cl_* handles passed by the host are directly usable.
// Override the library with the environment variable CEL_OPENCL_LIBRARY.
#pragma once

#ifndef CL_TARGET_OPENCL_VERSION
#define CL_TARGET_OPENCL_VERSION 120
#endif
#ifndef CL_USE_DEPRECATED_OPENCL_1_1_APIS
#define CL_USE_DEPRECATED_OPENCL_1_1_APIS
#endif
#ifndef CL_USE_DEPRECATED_OPENCL_1_2_APIS
#define CL_USE_DEPRECATED_OPENCL_1_2_APIS
#endif
#if defined(__APPLE__) && !defined(CEL_HAVE_KHRONOS_CL_HEADERS)
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif
#include <string>

namespace cel { namespace gpu { namespace cl {

#define CEL_CL_FUNCS(X)                                                                         \
    X(clGetPlatformIDs) X(clGetPlatformInfo) X(clGetDeviceIDs) X(clGetDeviceInfo)              \
    X(clCreateContext) X(clReleaseContext) X(clGetContextInfo)                                  \
    X(clCreateCommandQueue) X(clReleaseCommandQueue) X(clGetCommandQueueInfo)                   \
    X(clCreateBuffer) X(clReleaseMemObject) X(clGetMemObjectInfo)                               \
    X(clCreateProgramWithSource) X(clBuildProgram) X(clGetProgramBuildInfo) X(clReleaseProgram) \
    X(clCreateKernel) X(clReleaseKernel) X(clSetKernelArg) X(clGetKernelWorkGroupInfo)          \
    X(clEnqueueNDRangeKernel) X(clEnqueueWriteBuffer) X(clEnqueueReadBuffer)                    \
    X(clEnqueueWriteBufferRect) X(clEnqueueReadBufferRect)                                      \
    X(clFinish) X(clFlush) X(clWaitForEvents) X(clGetEventInfo) X(clRetainEvent) X(clReleaseEvent)

// optional entry points (may be missing on old runtimes)
#define CEL_CL_OPT_FUNCS(X) X(clEnqueueBarrierWithWaitList) X(clEnqueueBarrier)

struct Api {
#define CEL_CL_DECL(fn) decltype(&::fn) fn = nullptr;
    CEL_CL_FUNCS(CEL_CL_DECL)
    CEL_CL_OPT_FUNCS(CEL_CL_DECL)
#undef CEL_CL_DECL
    bool ok = false;
    std::string error;     // why loading failed
    std::string library;   // what was loaded
};

// Loads once (thread-safe); never unloads (unloading driver modules at plugin unload is unsafe).
const Api& api();

const char* errorName(cl_int e);
std::string deviceString(cl_device_id dev);       // "NVIDIA GeForce ... (NVIDIA CUDA, OpenCL 3.0 ...)"

}}} // namespace cel::gpu::cl
