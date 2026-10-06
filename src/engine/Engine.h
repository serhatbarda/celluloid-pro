// Celluloid Pro - frame engine: a frame is a Plan (list of passes over buffers) executed by a
// Backend (CPU, OpenCL, CUDA, Metal).  FIXED (architect).
#pragma once
#include "../kernels/KernelABI.h"
#include "../core/Core.h"
#include "../texture/Texture.h"
#include <cstdint>
#include <string>
#include <vector>

namespace cel { namespace engine {

enum KernelId : int {
#define CEL_KERNEL(name) KID_##name,
#include "../kernels/KernelList.h"
#undef CEL_KERNEL
    KID_COUNT
};
const char* kernelName(KernelId id);      // "decode", ... (for logging / GPU entry names "cel_<name>")

// Buffer 0 = host source image, buffer 1 = host destination image. Others are internal,
// packed float RGBA, allocated by the backend (and cached across frames).
enum : int { BUF_SRC = 0, BUF_DST = 1 };

struct BufferDesc { int w = 0, h = 0; };  // for BUF_SRC/BUF_DST: the host image size

struct PassDesc {
    KernelId kernel;
    int out = -1;                          // buffer index written
    int in[CEL_PASS_MAX_INPUTS] = {-1, -1, -1, -1};
    CelPassArgs args{};                    // sizes/strides are filled by the backend at execute time;
                                           // f[] / i[] are filled by the plan builder
};

struct Plan {
    std::vector<BufferDesc> buffers;       // [0]=src, [1]=dst, rest internal
    std::vector<PassDesc>   passes;        // executed in order
    std::string debugString() const;
};

struct FrameSettings {
    core::LookParams    look;
    core::Mode          mode = core::Mode::Full;
    tex::TextureParams  tex;
    bool                watermark = false; // trial mode (license)
};

// Build the plan for a frame of size w x h. Texture agent implements this (it owns pass design);
// colour passes are inserted at the documented points (see SPEC §3).
Plan buildPlan(int w, int h, const FrameSettings& fs, const core::Pipeline& pipe);

// Host image description handed to a backend. For CPU backend: host pointers.
// For GPU backends: device handles in the host's API (cl_mem / CUdeviceptr / id<MTLBuffer>) as void*.
struct HostImage {
    void* data = nullptr;
    int   w = 0, h = 0;
    int   rowStrideFloats = 0;             // may be negative on some hosts for CPU images (bottom-up)
};

struct GpuContext {                        // filled from OFX render arguments for GPU renders
    void* openclCommandQueue = nullptr;    // cl_command_queue
    void* cudaStream = nullptr;            // cudaStream_t (may be null = default stream)
    void* metalCommandQueue = nullptr;     // id<MTLCommandQueue>
};

class Backend {
public:
    virtual ~Backend() = default;
    virtual const char* name() const = 0;
    // stateBuf = core::Pipeline::state() (uploaded/cached by the backend; stateVersion changes
    // whenever the content changes). Returns false + err on failure (caller may fall back to CPU).
    virtual bool execute(const Plan& plan, const std::vector<float>& stateBuf, uint64_t stateVersion,
                         const HostImage& src, const HostImage& dst, const GpuContext& gpu,
                         std::string* err) = 0;
};

Backend& cpuBackend();                     // implemented in src/engine/CpuBackend.cpp (texture agent)
Backend* openclBackend();                  // src/gpu/ (gpu agent); nullptr if not compiled in
Backend* cudaBackend();
Backend* metalBackend();

}} // namespace cel::engine
