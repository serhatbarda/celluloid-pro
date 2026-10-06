// Celluloid Pro - GPU backends: public API beyond engine::Backend (owned by the gpu role).
//
// engine::openclBackend() / cudaBackend() / metalBackend() (declared in engine/Engine.h) return the
// process-wide production backends (nullptr when the API was not compiled in). This header adds:
//   - load-time self tests (used by the OFX shell to decide which GPU render flags to declare),
//   - a CPU fallback that runs a frame on the CPU backend while the images stay in GPU memory
//     (download src -> CPU render -> upload dst on the host's queue/stream),
//   - resource release (plugin unload) and statistics,
//   - factories for backends over a custom KernelSet (unit tests of the generic machinery).
// See src/gpu/README.md for the threading, ordering and memory strategy.
#pragma once
#include "../engine/Engine.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace cel { namespace gpu {

enum class Api : int { OpenCL = 0, CUDA = 1, Metal = 2 };
const char* apiName(Api api);

// ---- kernel sets ------------------------------------------------------------------------------
// A kernel set is a list of kernels with the uniform ABI (KernelABI.h) compiled for every API.
// The production set is generated from src/kernels/KernelList.h; PassDesc::kernel indexes it.
// cudaEntries[i] is the host-side handle of the __global__ entry for kernel i (from the .cu TU).
struct KernelSet {
    const char*        name = "";
    int                count = 0;
    const char* const* names = nullptr;        // entry point names "cel_<name>", count entries
    const char*        openclSource = nullptr; // flattened OpenCL C source incl. entry points
    std::size_t        openclSourceLen = 0;
    const char*        openclSha256 = "";
    const char*        metalSource = nullptr;  // flattened MSL source incl. entry points
    std::size_t        metalSourceLen = 0;
    const char*        metalSha256 = "";
    const void* const* cudaEntries = nullptr;  // count entries (nullptr if CUDA not compiled in)
    const void*        cudaProbe = nullptr;    // CUDA probe entry (self test)
};
const KernelSet& productionKernelSet();

// Factories (each call creates an independent backend with its own caches).
// Return nullptr if the API is not compiled in.
std::unique_ptr<engine::Backend> createOpenCLBackend(const KernelSet& ks);
std::unique_ptr<engine::Backend> createCudaBackend(const KernelSet& ks);
std::unique_ptr<engine::Backend> createMetalBackend(const KernelSet& ks);

// ---- self tests ---------------------------------------------------------------------------------
struct SelfTestResult {
    bool        compiledIn = false;  // backend compiled into this binary
    bool        ok = false;          // usable
    std::string message;             // human readable: devices found / why not usable
    double      ms = 0.0;            // time taken
};
// quick (full=false): runtime library loads, >= 1 suitable device. Cheap: safe at plugin describe.
// full  (full=true):  additionally creates a private context/queue on the first suitable device,
//                     builds the production kernels and runs a probe kernel that checks the ABI
//                     layout and float math against the CPU. Can take seconds (kernel compile).
SelfTestResult selfTest(Api api, bool full);

// ---- CPU fallback on GPU images -------------------------------------------------------------------
// Runs the plan on `cpu` while src/dst are device images of `api` (cl_mem / device pointer /
// id<MTLBuffer>) on the host's queue/stream given in `gpu`: downloads src, renders on the CPU,
// uploads dst (ordered on the host queue/stream; blocks the calling thread for the transfers).
// Use when GPU execute() failed (e.g. device out of memory, kernel build failure).
bool cpuFallback(Api api, engine::Backend& cpu, const engine::Plan& plan,
                 const std::vector<float>& stateBuf, uint64_t stateVersion,
                 const engine::HostImage& src, const engine::HostImage& dst,
                 const engine::GpuContext& gpu, std::string* err);

// ---- resources / stats ------------------------------------------------------------------------------
// Release every cached GPU object (programs, pipelines, pooled buffers, state buffers) of the
// production backends. Call at plugin unload (kOfxActionUnload). Thread-safe; must not race with
// execute() calls (the OFX host guarantees that at unload).
void releaseAllResources();

struct BackendStats {
    uint64_t frames = 0;             // successful execute() calls
    uint64_t failures = 0;
    uint64_t programBuilds = 0;      // program / library / module builds
    uint64_t stateUploads = 0;       // state buffer uploads (stateVersion changes)
    uint64_t bufferAllocs = 0;       // pooled buffer allocations
    uint64_t bufferReuses = 0;       // pooled buffer reuses
    uint64_t bytesPooled = 0;        // bytes held by the pool of the lane that rendered last
    uint64_t lanes = 0;              // live per-queue/stream lanes (after idle trimming)
    double   lastBuildMs = 0.0;
    std::string lastError;
    std::string lastDevice;
};
// Stats of a backend created by this module (production singletons or factories); false if the
// backend is not one of ours.
bool backendStats(const engine::Backend* backend, BackendStats* out);

// Test hook: when enabled, execute() additionally reads back every internal buffer after the
// frame (blocking) and keeps them for debugCapturedBuffer(). Per backend instance. Only for tests.
void setDebugCapture(engine::Backend* backend, bool on);
// Test hook: when enabled, execute() fills every internal buffer it hands to the passes (pooled,
// possibly holding a previous frame) with NaN (all bits set) before the first pass, so a kernel that
// reads memory no pass wrote shows up as NaN in the output. Per backend instance. Only for tests.
void setDebugPoison(engine::Backend* backend, bool on);
bool debugCapturedBuffer(const engine::Backend* backend, int bufferIndex, std::vector<float>* out,
                         int* w, int* h);

}} // namespace cel::gpu
