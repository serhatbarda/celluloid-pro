// Reafilm Pro - dynamically loaded OpenCL API. See CLApi.h.
#include "CLApi.h"
#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace cel { namespace gpu { namespace cl {

namespace {

// Windows: the ICD loader OpenCL.dll is loaded from System32 only (never from the host
// application's folder or the current directory: DLL planting); an override must be an absolute
// path. Elsewhere: the system library names below, or an absolute override path.
bool isAbsolutePath(const std::string& p) {
#if defined(_WIN32)
    return (p.size() > 2 && p[1] == ':' && (p[2] == '\\' || p[2] == '/')) || (p.size() > 1 && p[0] == '\\' && p[1] == '\\');
#else
    return !p.empty() && p[0] == '/';
#endif
}

void* openLib(const std::string& name, bool system32) {
#if defined(_WIN32)
    int n = ::MultiByteToWideChar(CP_UTF8, 0, name.c_str(), -1, nullptr, 0);
    if (n <= 0) return nullptr;
    std::wstring w(size_t(n), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, name.c_str(), -1, &w[0], n);
    const DWORD flags = system32 ? LOAD_LIBRARY_SEARCH_SYSTEM32 : LOAD_WITH_ALTERED_SEARCH_PATH;
    return reinterpret_cast<void*>(::LoadLibraryExW(w.c_str(), nullptr, flags));
#else
    (void)system32;
    return ::dlopen(name.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
}

void* sym(void* lib, const char* name) {
#if defined(_WIN32)
    return reinterpret_cast<void*>(::GetProcAddress(static_cast<HMODULE>(lib), name));
#else
    return ::dlsym(lib, name);
#endif
}

template <class F>
void assign(F& f, void* p) {
    f = reinterpret_cast<F>(p);
}

Api loadApi() {
    Api a;
    std::vector<std::string> candidates;
    std::string ignored;
    // developer / support override (e.g. a specific ICD loader); absolute paths only
    if (const char* env = std::getenv("CEL_OPENCL_LIBRARY")) {
        if (*env) {
            if (isAbsolutePath(env)) candidates.push_back(env);
            else ignored = env;
        }
    }
    const size_t nOverride = candidates.size();
#if defined(_WIN32)
    candidates.push_back("OpenCL.dll");
#elif defined(__APPLE__)
    candidates.push_back("/System/Library/Frameworks/OpenCL.framework/OpenCL");
#else
    candidates.push_back("libOpenCL.so.1");
    candidates.push_back("libOpenCL.so");
#endif
    void* lib = nullptr;
    for (size_t k = 0; k < candidates.size(); ++k) {
        lib = openLib(candidates[k], k >= nOverride);
        if (lib) { a.library = candidates[k]; break; }
    }
    if (!lib) {
        a.error = "OpenCL runtime not found (tried";
        for (const auto& c : candidates) a.error += " " + c;
        a.error += ")";
        if (!ignored.empty()) a.error += "; CEL_OPENCL_LIBRARY ignored (not an absolute path): " + ignored;
        return a;
    }
    std::string missing;
#define CEL_CL_LOAD(fn)                                   \
    assign(a.fn, sym(lib, #fn));                          \
    if (!a.fn) missing += std::string(missing.empty() ? "" : ", ") + #fn;
    CEL_CL_FUNCS(CEL_CL_LOAD)
#undef CEL_CL_LOAD
#define CEL_CL_LOAD_OPT(fn) assign(a.fn, sym(lib, #fn));
    CEL_CL_OPT_FUNCS(CEL_CL_LOAD_OPT)
#undef CEL_CL_LOAD_OPT
    if (!missing.empty()) {
        a.error = a.library + " lacks required OpenCL 1.1 entry points: " + missing;
        return a;
    }
    a.ok = true;
    return a;
}

} // namespace

const Api& api() {
    // Function-local static: initialised once, thread-safe; intentionally never destroyed
    // (the library stays loaded for the process lifetime).
    static const Api* instance = new Api(loadApi());
    return *instance;
}

const char* errorName(cl_int e) {
    switch (e) {
#define C(x) case x: return #x;
        C(CL_SUCCESS) C(CL_DEVICE_NOT_FOUND) C(CL_DEVICE_NOT_AVAILABLE) C(CL_COMPILER_NOT_AVAILABLE)
        C(CL_MEM_OBJECT_ALLOCATION_FAILURE) C(CL_OUT_OF_RESOURCES) C(CL_OUT_OF_HOST_MEMORY)
        C(CL_PROFILING_INFO_NOT_AVAILABLE) C(CL_MEM_COPY_OVERLAP) C(CL_IMAGE_FORMAT_MISMATCH)
        C(CL_IMAGE_FORMAT_NOT_SUPPORTED) C(CL_BUILD_PROGRAM_FAILURE) C(CL_MAP_FAILURE)
        C(CL_MISALIGNED_SUB_BUFFER_OFFSET) C(CL_EXEC_STATUS_ERROR_FOR_EVENTS_IN_WAIT_LIST)
        C(CL_INVALID_VALUE) C(CL_INVALID_DEVICE_TYPE) C(CL_INVALID_PLATFORM) C(CL_INVALID_DEVICE)
        C(CL_INVALID_CONTEXT) C(CL_INVALID_QUEUE_PROPERTIES) C(CL_INVALID_COMMAND_QUEUE)
        C(CL_INVALID_HOST_PTR) C(CL_INVALID_MEM_OBJECT) C(CL_INVALID_IMAGE_FORMAT_DESCRIPTOR)
        C(CL_INVALID_IMAGE_SIZE) C(CL_INVALID_SAMPLER) C(CL_INVALID_BINARY) C(CL_INVALID_BUILD_OPTIONS)
        C(CL_INVALID_PROGRAM) C(CL_INVALID_PROGRAM_EXECUTABLE) C(CL_INVALID_KERNEL_NAME)
        C(CL_INVALID_KERNEL_DEFINITION) C(CL_INVALID_KERNEL) C(CL_INVALID_ARG_INDEX)
        C(CL_INVALID_ARG_VALUE) C(CL_INVALID_ARG_SIZE) C(CL_INVALID_KERNEL_ARGS)
        C(CL_INVALID_WORK_DIMENSION) C(CL_INVALID_WORK_GROUP_SIZE) C(CL_INVALID_WORK_ITEM_SIZE)
        C(CL_INVALID_GLOBAL_OFFSET) C(CL_INVALID_EVENT_WAIT_LIST) C(CL_INVALID_EVENT)
        C(CL_INVALID_OPERATION) C(CL_INVALID_GL_OBJECT) C(CL_INVALID_BUFFER_SIZE)
        C(CL_INVALID_MIP_LEVEL) C(CL_INVALID_GLOBAL_WORK_SIZE) C(CL_INVALID_PROPERTY)
#undef C
        default: break;
    }
    return "CL_UNKNOWN_ERROR";
}

static std::string devStr(cl_device_id d, cl_device_info what) {
    const Api& a = api();
    size_t n = 0;
    if (a.clGetDeviceInfo(d, what, 0, nullptr, &n) != CL_SUCCESS || n == 0) return std::string();
    std::string s(n, '\0');
    if (a.clGetDeviceInfo(d, what, n, &s[0], nullptr) != CL_SUCCESS) return std::string();
    while (!s.empty() && (s.back() == '\0' || s.back() == ' ')) s.pop_back();
    return s;
}

std::string deviceString(cl_device_id dev) {
    if (!api().ok || !dev) return "?";
    return devStr(dev, CL_DEVICE_NAME) + " (" + devStr(dev, CL_DEVICE_VENDOR) + ", " +
           devStr(dev, CL_DEVICE_VERSION) + ", driver " + devStr(dev, CL_DRIVER_VERSION) + ")";
}

}}} // namespace cel::gpu::cl
