// Celluloid Pro - OpenCL backend (owned by the gpu role). See src/gpu/README.md.
//
// Objects and their lifetime
//   Program  per (cl_context, cl_device_id): built once from the embedded flattened source; a
//            failed build is remembered (no rebuild every frame) and reported with the build log.
//   Lane     per host cl_command_queue: its own cl_kernel objects (clSetKernelArg is not
//            thread-safe per kernel object, so lanes never share them), the internal buffer pool,
//            the state buffer + its content key, the per-frame pass-args buffer and pending
//            staging copies of non-blocking uploads. A lane is used under its mutex; distinct
//            queues render concurrently without blocking each other.
// Ordering
//   Everything is enqueued on the host's queue, nothing waits on the host thread (no clFinish).
//   In-order queues (the OFX convention) give the ordering for free: the state/args uploads
//   precede the kernels, and buffer reuse across frames is ordered by the queue. For an
//   out-of-order queue we enqueue a barrier first (host work producing src), chain every command
//   with events, and enqueue a final barrier (host work consuming dst).
// Uploads
//   Non-blocking writes read host memory asynchronously, so the data is first copied into a
//   staging block that stays alive until the write's event reports completion (polled on later
//   frames; never a callback on a driver thread).
#include "../GpuCommon.h"
#include "CLApi.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace cel { namespace gpu { namespace detail {

namespace {

std::string clErr(const char* what, cl_int e) {
    return std::string(what) + " failed: " + cl::errorName(e) + " (" + std::to_string(e) + ")";
}

template <class T>
bool devInfo(cl_device_id d, cl_device_info what, T* out) {
    return cl::api().clGetDeviceInfo(d, what, sizeof(T), out, nullptr) == CL_SUCCESS;
}

std::string devInfoStr(cl_device_id d, cl_device_info what) {
    const auto& a = cl::api();
    size_t n = 0;
    if (a.clGetDeviceInfo(d, what, 0, nullptr, &n) != CL_SUCCESS || n == 0) return std::string();
    std::string s(n, '\0');
    if (a.clGetDeviceInfo(d, what, n, &s[0], nullptr) != CL_SUCCESS) return std::string();
    while (!s.empty() && s.back() == '\0') s.pop_back();
    return s;
}

// "OpenCL C 1.2 ..." -> 12
int openclCVersion(cl_device_id d) {
    const std::string s = devInfoStr(d, CL_DEVICE_OPENCL_C_VERSION);
    const size_t p = s.find_first_of("0123456789");
    if (p == std::string::npos) return 10;
    const int major = std::atoi(s.c_str() + p);
    const size_t dot = s.find('.', p);
    const int minor = dot == std::string::npos ? 0 : std::atoi(s.c_str() + dot + 1);
    return major * 10 + minor;
}

size_t roundUp(size_t v, size_t m) { return m == 0 ? v : ((v + m - 1) / m) * m; }

} // namespace

class OpenCLBackend final : public GpuBackendBase {
public:
    explicit OpenCLBackend(const KernelSet& ks) : GpuBackendBase(ks) {}
    ~OpenCLBackend() override { releaseResources(); }
    const char* name() const override { return "OpenCL"; }

    bool execute(const engine::Plan& plan, const std::vector<float>& stateBuf, uint64_t stateVersion,
                 const engine::HostImage& src, const engine::HostImage& dst, const engine::GpuContext& gpu,
                 std::string* err) override {
        return executeImpl(plan, stateBuf, stateVersion, src, dst, gpu, false, err);
    }
    bool executeImpl(const engine::Plan& plan, const std::vector<float>& stateBuf, uint64_t stateVersion,
                     const engine::HostImage& src, const engine::HostImage& dst,
                     const engine::GpuContext& gpu, bool allowProbe, std::string* err);
    void releaseResources() override;

private:
    // One per (context, device). Released when the last owner lets go (the map entry and every lane
    // using it hold a shared_ptr): trimIdleLanes drops map entries no live lane uses, so a host
    // context the plug-in no longer renders on is not kept alive by its program (clCreateProgram*
    // retains the context).
    struct Program {
        ~Program() {
            if (prog) {
                const auto& a = cl::api();
                if (a.ok) a.clReleaseProgram(prog);
            }
        }
        cl_context ctx = nullptr;
        cl_device_id dev = nullptr;
        std::mutex m;
        bool built = false, ok = false;
        cl_program prog = nullptr;
        std::string error, log, device, options;
        bool isCpu = false;
        cl_ulong maxConst = 65536, maxAlloc = 0;
        size_t maxItems[3] = {1, 1, 1};
    };
    struct Pending {
        cl_event ev = nullptr;
        std::vector<unsigned char> data;
    };
    struct Lane {
        cl_command_queue q = nullptr;
        cl_context ctx = nullptr;
        cl_device_id dev = nullptr;
        std::mutex m;
        std::shared_ptr<Program> prog;
        bool outOfOrder = false;
        std::vector<cl_kernel> kernels;
        std::vector<std::array<size_t, 2>> local;
        SizedPool<cl_mem> pool;
        cl_mem state = nullptr;
        size_t stateBytes = 0;
        StateKey stateKey;
        cl_mem args = nullptr;
        size_t argsBytes = 0;
        std::vector<std::unique_ptr<Pending>> pending;
        uint64_t frame = 0;
        std::atomic<double> lastUseMs{0.0};   // set when the lane is taken from the map
        bool retired = false;                 // released by trimIdleLanes (under m); take a new lane
    };

    std::shared_ptr<Program> program(cl_context ctx, cl_device_id dev);
    bool build(Program& p, std::string* err);
    bool ensureKernel(Lane& l, int kid, std::string* err);
    static void reap(Lane& l, bool wait);
    static void releaseLane(Lane& l);
    bool upload(Lane& l, cl_mem dst, const void* data, size_t bytes, cl_event* chain, std::string* err);
    void trimIdleLanes(Lane* current);

    std::mutex m_mapMutex;
    std::map<std::pair<cl_context, cl_device_id>, std::shared_ptr<Program>> m_programs;
    std::map<cl_command_queue, std::shared_ptr<Lane>> m_lanes;
};

// ---------------------------------------------------------------------------------------------
std::shared_ptr<OpenCLBackend::Program> OpenCLBackend::program(cl_context ctx, cl_device_id dev) {
    std::lock_guard<std::mutex> lk(m_mapMutex);
    auto& p = m_programs[std::make_pair(ctx, dev)];
    if (!p) {
        p = std::make_shared<Program>();
        p->ctx = ctx;
        p->dev = dev;
    }
    return p;
}

bool OpenCLBackend::build(Program& p, std::string* err) {
    std::lock_guard<std::mutex> lk(p.m);
    if (p.built) {
        if (!p.ok && err) *err = p.error;
        return p.ok;
    }
    const auto& a = cl::api();
    const double t0 = nowMs();
    p.built = true;
    p.device = cl::deviceString(p.dev);
    cl_device_type type = 0;
    devInfo(p.dev, CL_DEVICE_TYPE, &type);
    p.isCpu = (type & CL_DEVICE_TYPE_CPU) != 0;
    devInfo(p.dev, CL_DEVICE_MAX_CONSTANT_BUFFER_SIZE, &p.maxConst);
    devInfo(p.dev, CL_DEVICE_MAX_MEM_ALLOC_SIZE, &p.maxAlloc);
    a.clGetDeviceInfo(p.dev, CL_DEVICE_MAX_WORK_ITEM_SIZES, sizeof(p.maxItems), p.maxItems, nullptr);

    // Build options: correctly rounded divide/sqrt where the device offers it (CPU parity), no
    // fast/relaxed math, OpenCL C 1.2 when available. CEL_OPENCL_BUILD_OPTIONS appends options.
    std::string opts;
    if (openclCVersion(p.dev) >= 12) opts += "-cl-std=CL1.2 ";
    cl_device_fp_config fp = 0;
    if (devInfo(p.dev, CL_DEVICE_SINGLE_FP_CONFIG, &fp) && (fp & CL_FP_CORRECTLY_ROUNDED_DIVIDE_SQRT))
        opts += "-cl-fp32-correctly-rounded-divide-sqrt ";
    if (const char* extra = std::getenv("CEL_OPENCL_BUILD_OPTIONS")) opts += extra;
    p.options = opts;

    const char* srcs[1] = {m_ks.openclSource};
    const size_t lens[1] = {m_ks.openclSourceLen};
    cl_int e = CL_SUCCESS;
    if (!m_ks.openclSource || m_ks.openclSourceLen == 0) {
        p.error = "no OpenCL kernel source embedded in this build";
    } else {
        p.prog = a.clCreateProgramWithSource(p.ctx, 1, srcs, lens, &e);
        if (e != CL_SUCCESS || !p.prog) {
            p.error = clErr("clCreateProgramWithSource", e);
            p.prog = nullptr;
        } else {
            e = a.clBuildProgram(p.prog, 1, &p.dev, opts.c_str(), nullptr, nullptr);
            size_t n = 0;
            if (a.clGetProgramBuildInfo(p.prog, p.dev, CL_PROGRAM_BUILD_LOG, 0, nullptr, &n) == CL_SUCCESS && n > 1) {
                p.log.assign(n, '\0');
                a.clGetProgramBuildInfo(p.prog, p.dev, CL_PROGRAM_BUILD_LOG, n, &p.log[0], nullptr);
                while (!p.log.empty() && (p.log.back() == '\0' || p.log.back() == '\n')) p.log.pop_back();
            }
            if (e != CL_SUCCESS) {
                std::string log = p.log;
                if (log.size() > 8000) log = log.substr(0, 8000) + "\n...(truncated)";
                p.error = clErr("clBuildProgram", e) + " on " + p.device + " with options '" + opts +
                          "'. Build log:\n" + log;
                a.clReleaseProgram(p.prog);
                p.prog = nullptr;
            } else {
                p.ok = true;
            }
        }
    }
    noteBuild(nowMs() - t0);
    noteDevice(p.device);
    if (!p.ok && err) *err = p.error;
    return p.ok;
}

bool OpenCLBackend::ensureKernel(Lane& l, int kid, std::string* err) {
    const auto& a = cl::api();
    if (l.kernels.empty()) {
        l.kernels.assign(size_t(m_ks.count) + 1, nullptr);
        l.local.assign(size_t(m_ks.count) + 1, std::array<size_t, 2>{{0, 0}});
    }
    if (l.kernels[size_t(kid)]) return true;
    const char* nm = m_ks.names[kid];
    cl_int e = CL_SUCCESS;
    cl_kernel k = a.clCreateKernel(l.prog->prog, nm, &e);
    if (e != CL_SUCCESS || !k) {
        if (err) *err = clErr("clCreateKernel", e) + " for kernel '" + nm + "'";
        return false;
    }
    size_t wgs = 0, pm = 0;
    if (a.clGetKernelWorkGroupInfo(k, l.dev, CL_KERNEL_WORK_GROUP_SIZE, sizeof wgs, &wgs, nullptr) != CL_SUCCESS || wgs == 0)
        wgs = 1;
    if (a.clGetKernelWorkGroupInfo(k, l.dev, CL_KERNEL_PREFERRED_WORK_GROUP_SIZE_MULTIPLE, sizeof pm, &pm, nullptr) != CL_SUCCESS || pm == 0)
        pm = 1;
    size_t lx, ly;
    const size_t maxX = std::max<size_t>(1, l.prog->maxItems[0]), maxY = std::max<size_t>(1, l.prog->maxItems[1]);
    if (l.prog->isCpu) {
        // CPU devices (POCL, Intel CPU runtime): one row segment per work group vectorises best
        lx = std::min<size_t>({wgs, maxX, 64});
        ly = 1;
    } else {
        lx = std::min<size_t>({std::max<size_t>(pm, 16), 64, wgs, maxX});
        const size_t target = std::min<size_t>(wgs, 256);
        ly = std::max<size_t>(1, std::min<size_t>(target / lx, maxY));
        while (lx * ly > wgs && ly > 1) --ly;
    }
    l.kernels[size_t(kid)] = k;
    l.local[size_t(kid)] = {{std::max<size_t>(1, lx), std::max<size_t>(1, ly)}};
    return true;
}

void OpenCLBackend::reap(Lane& l, bool wait) {
    const auto& a = cl::api();
    for (size_t i = 0; i < l.pending.size();) {
        Pending& p = *l.pending[i];
        cl_int status = CL_COMPLETE;
        if (wait) {
            a.clWaitForEvents(1, &p.ev);
        } else if (a.clGetEventInfo(p.ev, CL_EVENT_COMMAND_EXECUTION_STATUS, sizeof status, &status, nullptr) != CL_SUCCESS) {
            status = CL_COMPLETE;   // event unusable: treat as finished below after waiting
            a.clWaitForEvents(1, &p.ev);
        }
        if (wait || status <= CL_COMPLETE) {   // CL_COMPLETE (0) or an error (< 0)
            a.clReleaseEvent(p.ev);
            l.pending.erase(l.pending.begin() + std::ptrdiff_t(i));
        } else {
            ++i;
        }
    }
    // bound the staging memory held by a queue the host never flushes
    while (l.pending.size() > 64) {
        a.clWaitForEvents(1, &l.pending.front()->ev);
        a.clReleaseEvent(l.pending.front()->ev);
        l.pending.erase(l.pending.begin());
    }
}

bool OpenCLBackend::upload(Lane& l, cl_mem dst, const void* data, size_t bytes, cl_event* chain, std::string* err) {
    const auto& a = cl::api();
    auto p = std::unique_ptr<Pending>(new Pending());
    p->data.resize(bytes);
    std::memcpy(p->data.data(), data, bytes);
    const cl_uint nw = (l.outOfOrder && *chain) ? 1u : 0u;
    cl_int e = a.clEnqueueWriteBuffer(l.q, dst, CL_FALSE, 0, bytes, p->data.data(), nw, nw ? chain : nullptr, &p->ev);
    if (e != CL_SUCCESS) {
        if (err) *err = clErr("clEnqueueWriteBuffer", e);
        return false;
    }
    if (l.outOfOrder) {   // the chain holds its own reference; the pending entry keeps one too
        if (*chain) a.clReleaseEvent(*chain);
        a.clRetainEvent(p->ev);
        *chain = p->ev;
    }
    l.pending.push_back(std::move(p));
    return true;
}

void OpenCLBackend::releaseLane(Lane& l) {
    const auto& a = cl::api();
    if (!a.ok) return;
    reap(l, true);
    for (cl_kernel k : l.kernels)
        if (k) a.clReleaseKernel(k);
    l.kernels.clear();
    l.local.clear();
    l.pool.clear([&](cl_mem m) { a.clReleaseMemObject(m); });
    if (l.state) a.clReleaseMemObject(l.state);
    if (l.args) a.clReleaseMemObject(l.args);
    l.state = l.args = nullptr;
    l.stateBytes = l.argsBytes = 0;
    l.stateKey = StateKey();
    l.prog.reset();
}

void OpenCLBackend::releaseResources() {
    const auto& a = cl::api();
    std::lock_guard<std::mutex> lk(m_mapMutex);
    for (auto& kv : m_lanes) {
        std::lock_guard<std::mutex> ll(kv.second->m);
        releaseLane(*kv.second);
        kv.second->retired = true;
    }
    m_lanes.clear();
    (void)a;
    m_programs.clear();   // ~Program releases the cl_program once no lane holds it
}

void OpenCLBackend::trimIdleLanes(Lane* current) {
    // Lanes of queues that have not rendered for laneIdleReleaseMs() are released completely (the
    // host may have destroyed the queue or switched devices); called with no lane lock held.
    // Releasing is safe even while the queue still executes: OpenCL objects are reference counted
    // and destroyed by the runtime once the commands using them have completed.
    const double now = nowMs();
    std::vector<std::shared_ptr<Lane>> lanes;
    std::vector<std::shared_ptr<Program>> progs;   // dropped outside the map lock
    {
        std::lock_guard<std::mutex> lk(m_mapMutex);
        for (auto it = m_lanes.begin(); it != m_lanes.end();) {
            if (it->second.get() != current && now - it->second->lastUseMs > laneIdleReleaseMs()) {
                lanes.push_back(it->second);
                it = m_lanes.erase(it);   // nobody can take it from the map any more
            } else {
                ++it;
            }
        }
        if (!lanes.empty()) {
            // programs of (context, device) pairs no live lane renders on: drop the map's reference
            for (auto it = m_programs.begin(); it != m_programs.end();) {
                bool used = false;
                for (const auto& kv : m_lanes)
                    if (kv.second->ctx == it->first.first && kv.second->dev == it->first.second) { used = true; break; }
                if (!used) {
                    progs.push_back(std::move(it->second));
                    it = m_programs.erase(it);
                } else {
                    ++it;
                }
            }
        }
        noteLanes(m_lanes.size());
    }
    for (auto& l : lanes) {   // a thread that took it just before the erase sees `retired` and retries
        std::lock_guard<std::mutex> ll(l->m);
        releaseLane(*l);
        l->retired = true;
    }
    progs.clear();   // ~Program -> clReleaseProgram (unless an executing thread still holds it)
}

bool OpenCLBackend::executeImpl(const engine::Plan& plan, const std::vector<float>& stateBuf,
                                uint64_t stateVersion, const engine::HostImage& src,
                                const engine::HostImage& dst, const engine::GpuContext& gpu,
                                bool allowProbe, std::string* err) {
    const auto& a = cl::api();
    if (!a.ok) return fail(err, a.error);
    cl_command_queue q = static_cast<cl_command_queue>(gpu.openclCommandQueue);
    if (!q) return fail(err, "no OpenCL command queue supplied by the host");

    cl_context ctx = nullptr;
    cl_device_id dev = nullptr;
    cl_command_queue_properties props = 0;
    cl_int e = a.clGetCommandQueueInfo(q, CL_QUEUE_CONTEXT, sizeof ctx, &ctx, nullptr);
    if (e == CL_SUCCESS) e = a.clGetCommandQueueInfo(q, CL_QUEUE_DEVICE, sizeof dev, &dev, nullptr);
    if (e == CL_SUCCESS) e = a.clGetCommandQueueInfo(q, CL_QUEUE_PROPERTIES, sizeof props, &props, nullptr);
    if (e != CL_SUCCESS) return fail(err, clErr("clGetCommandQueueInfo (invalid host queue?)", e));

    FrameSchedule fs;
    ScheduleOptions so;
    so.allowNegativeStride = false;
    so.allowProbe = allowProbe;
    so.aliasInternal = !debugCapture();
    std::string serr;
    if (!buildSchedule(plan, m_ks.count, src, dst, so, &fs, &serr)) return fail(err, "invalid plan: " + serr);

    cl_mem srcMem = static_cast<cl_mem>(src.data), dstMem = static_cast<cl_mem>(dst.data);
    for (int i = 0; i < 2; ++i) {
        cl_mem m = i == 0 ? srcMem : dstMem;
        cl_context mc = nullptr;
        size_t msz = 0;
        e = a.clGetMemObjectInfo(m, CL_MEM_CONTEXT, sizeof mc, &mc, nullptr);
        if (e == CL_SUCCESS) e = a.clGetMemObjectInfo(m, CL_MEM_SIZE, sizeof msz, &msz, nullptr);
        if (e != CL_SUCCESS)
            return fail(err, clErr(i == 0 ? "clGetMemObjectInfo(source)" : "clGetMemObjectInfo(destination)", e));
        if (mc != ctx) return fail(err, std::string(i == 0 ? "source" : "destination") +
                                            " buffer belongs to a different OpenCL context than the queue");
        const size_t need = i == 0 ? fs.srcBytesNeeded : fs.dstBytesNeeded;
        if (msz < need)
            return fail(err, std::string(i == 0 ? "source" : "destination") + " buffer holds " +
                                 std::to_string(msz) + " bytes, image needs " + std::to_string(need));
    }

    std::shared_ptr<Program> prog = program(ctx, dev);
    if (!build(*prog, &serr)) return fail(err, serr);

    // lane for this queue (recreated if the handle now names a different context/device, or if
    // trimIdleLanes retired it between the lookup and the lock)
    std::shared_ptr<Lane> lane;
    std::unique_lock<std::mutex> laneLock;
    for (;;) {
        {
            std::lock_guard<std::mutex> lk(m_mapMutex);
            auto& slot = m_lanes[q];
            if (slot && (slot->ctx != ctx || slot->dev != dev)) {
                std::lock_guard<std::mutex> ll(slot->m);
                releaseLane(*slot);
                slot->retired = true;
                slot.reset();
            }
            if (!slot) {
                slot = std::make_shared<Lane>();
                slot->q = q;
                slot->ctx = ctx;
                slot->dev = dev;
            }
            slot->lastUseMs = nowMs();
            lane = slot;
        }
        laneLock = std::unique_lock<std::mutex>(lane->m);
        if (!lane->retired) break;
        laneLock.unlock();
    }

    bool ok = true;
    {
        Lane& l = *lane;
        l.prog = prog;
        l.outOfOrder = (props & CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE) != 0;
        l.frame++;
        reap(l, false);

        for (int kid : fs.usedKernels)
            if (!ensureKernel(l, kid, &serr)) return fail(err, serr);

        const size_t argsBytes = fs.passes.size() * sizeof(CelPassArgs);
        if (argsBytes > prog->maxConst)
            return fail(err, "plan has " + std::to_string(fs.passes.size()) +
                                 " passes; their arguments exceed the device constant buffer limit");
        for (size_t s : fs.slotBytes)
            if (prog->maxAlloc && s > prog->maxAlloc)
                return fail(err, "internal buffer of " + std::to_string(s) +
                                     " bytes exceeds CL_DEVICE_MAX_MEM_ALLOC_SIZE (" +
                                     std::to_string(prog->maxAlloc) + ")");

        // internal buffers
        std::vector<cl_mem> slots;
        BackendStats delta;
        auto allocFn = [&](size_t bytes, cl_mem* out, std::string* aerr) {
            cl_int ae = CL_SUCCESS;
            *out = a.clCreateBuffer(ctx, CL_MEM_READ_WRITE, bytes, nullptr, &ae);
            if (ae != CL_SUCCESS || !*out) { if (aerr) *aerr = clErr("clCreateBuffer", ae); *out = nullptr; return false; }
            return true;
        };
        auto freeFn = [&](cl_mem m) { a.clReleaseMemObject(m); };
        if (!l.pool.acquire(fs.slotBytes, l.frame, &slots, allocFn, freeFn, &delta, &serr))
            return fail(err, serr);
        notePool(delta);

        cl_event chain = nullptr;
        auto releaseChain = [&]() { if (chain) { a.clReleaseEvent(chain); chain = nullptr; } };
        if (l.outOfOrder) {
            if (a.clEnqueueBarrierWithWaitList)
                e = a.clEnqueueBarrierWithWaitList(q, 0, nullptr, &chain);
            else if (a.clEnqueueBarrier)
                e = a.clEnqueueBarrier(q);
            if (e != CL_SUCCESS) return fail(err, clErr("clEnqueueBarrier", e));
        }

        // state buffer: (re)upload only when the content key changes
        const StateKey key = makeStateKey(stateBuf, stateVersion);
        const size_t stateBytes = std::max<size_t>(16, stateBuf.size() * sizeof(float));
        if (!l.state || l.stateBytes < stateBytes) {
            if (l.state) a.clReleaseMemObject(l.state);   // deferred deletion by the runtime
            l.state = a.clCreateBuffer(ctx, CL_MEM_READ_ONLY, stateBytes, nullptr, &e);
            if (e != CL_SUCCESS || !l.state) {
                l.state = nullptr; l.stateBytes = 0; l.stateKey = StateKey();
                releaseChain();
                return fail(err, clErr("clCreateBuffer(state)", e));
            }
            l.stateBytes = stateBytes;
            l.stateKey = StateKey();
        }
        if (l.stateKey != key) {
            if (!stateBuf.empty() && !upload(l, l.state, stateBuf.data(), stateBuf.size() * sizeof(float), &chain, &serr)) {
                l.stateKey = StateKey();
                releaseChain();
                return fail(err, "state upload: " + serr);
            }
            l.stateKey = key;
            noteStateUpload();
        }

        // pass arguments of the whole frame in one constant buffer
        if (!l.args || l.argsBytes < argsBytes) {
            if (l.args) a.clReleaseMemObject(l.args);
            const size_t cap = std::max<size_t>(argsBytes, 32 * sizeof(CelPassArgs));
            l.args = a.clCreateBuffer(ctx, CL_MEM_READ_ONLY, cap, nullptr, &e);
            if (e != CL_SUCCESS || !l.args) {
                l.args = nullptr; l.argsBytes = 0;
                releaseChain();
                return fail(err, clErr("clCreateBuffer(args)", e));
            }
            l.argsBytes = cap;
        }
        {
            std::vector<CelPassArgs> all(fs.passes.size());
            for (size_t p = 0; p < fs.passes.size(); ++p) all[p] = fs.passes[p].args;
            if (!upload(l, l.args, all.data(), argsBytes, &chain, &serr)) {
                releaseChain();
                return fail(err, "args upload: " + serr);
            }
        }

        if (debugPoison()) {   // test hook: NaN in every internal buffer before the first pass
            for (size_t s = 0; s < slots.size(); ++s) {
                const std::vector<unsigned char> nanBytes(fs.slotBytes[s], 0xFF);
                if (!upload(l, slots[s], nanBytes.data(), nanBytes.size(), &chain, &serr)) {
                    releaseChain();
                    return fail(err, "poison: " + serr);
                }
            }
        }

        auto memOf = [&](int b) -> cl_mem {
            if (b == engine::BUF_SRC) return srcMem;
            if (b == engine::BUF_DST) return dstMem;
            return slots[size_t(fs.bufferSlot[size_t(b)])];
        };
        for (size_t p = 0; p < fs.passes.size() && ok; ++p) {
            const ScheduledPass& sp = fs.passes[p];
            cl_kernel k = l.kernels[size_t(sp.kernel)];
            const cl_int passIdx = cl_int(p);
            cl_mem outMem = memOf(sp.out);
            e = a.clSetKernelArg(k, 0, sizeof(cl_mem), &l.args);
            if (e == CL_SUCCESS) e = a.clSetKernelArg(k, 1, sizeof(cl_int), &passIdx);
            if (e == CL_SUCCESS) e = a.clSetKernelArg(k, 2, sizeof(cl_mem), &outMem);
            for (int j = 0; j < CEL_PASS_MAX_INPUTS && e == CL_SUCCESS; ++j) {
                cl_mem m = sp.in[j] >= 0 ? memOf(sp.in[j]) : nullptr;
                e = a.clSetKernelArg(k, cl_uint(3 + j), sizeof(cl_mem), &m);
            }
            if (e == CL_SUCCESS) e = a.clSetKernelArg(k, 7, sizeof(cl_mem), &l.state);
            if (e != CL_SUCCESS) {
                serr = clErr("clSetKernelArg", e) + " for " + m_ks.names[sp.kernel];
                ok = false;
                break;
            }
            const auto& loc = l.local[size_t(sp.kernel)];
            const size_t global[2] = {roundUp(size_t(sp.args.outW), loc[0]), roundUp(size_t(sp.args.outH), loc[1])};
            const size_t local[2] = {loc[0], loc[1]};
            cl_event ev = nullptr;
            const cl_uint nw = (l.outOfOrder && chain) ? 1u : 0u;
            e = a.clEnqueueNDRangeKernel(q, k, 2, nullptr, global, local, nw, nw ? &chain : nullptr,
                                         l.outOfOrder ? &ev : nullptr);
            if (e != CL_SUCCESS) {
                serr = clErr("clEnqueueNDRangeKernel", e) + " for " + m_ks.names[sp.kernel] + " (" +
                       std::to_string(sp.args.outW) + "x" + std::to_string(sp.args.outH) + ", local " +
                       std::to_string(local[0]) + "x" + std::to_string(local[1]) + ")";
                ok = false;
                break;
            }
            if (l.outOfOrder) { releaseChain(); chain = ev; }
        }
        if (l.outOfOrder) {
            cl_event endEv = nullptr;
            if (a.clEnqueueBarrierWithWaitList)
                a.clEnqueueBarrierWithWaitList(q, chain ? 1u : 0u, chain ? &chain : nullptr, &endEv);
            else if (a.clEnqueueBarrier)
                a.clEnqueueBarrier(q);
            if (endEv) a.clReleaseEvent(endEv);
            releaseChain();
        }
        a.clFlush(q);   // start the work; never wait (OFX: the host synchronises)
        if (!ok) return fail(err, serr);

        if (debugCapture()) {
            clearCapture();
            a.clFinish(q);
            for (size_t b = 1; b < fs.bufferSlot.size(); ++b) {
                const int w = b == 1 ? dst.w : plan.buffers[b].w, h = b == 1 ? dst.h : plan.buffers[b].h;
                if (b >= 2 && fs.bufferSlot[b] < 0) continue;
                std::vector<float> data(size_t(w) * size_t(h) * 4u);
                const int stride = b == 1 ? dst.rowStrideFloats : w * 4;
                const size_t bo[3] = {0, 0, 0}, ho[3] = {0, 0, 0};
                const size_t region[3] = {size_t(w) * 16u, size_t(h), 1};
                e = a.clEnqueueReadBufferRect(q, memOf(int(b)), CL_TRUE, bo, ho, region, size_t(stride) * 4u, 0,
                                              size_t(w) * 16u, 0, data.data(), 0, nullptr, nullptr);
                if (e == CL_SUCCESS) storeCapture(int(b), std::move(data), w, h);
            }
        }
        l.pool.trim(l.frame, kPoolMaxIdleFrames, freeFn);
        noteFrame(l.pool.bytes());
        l.lastUseMs = nowMs();
    }
    laneLock.unlock();
    trimIdleLanes(lane.get());
    return true;
}

// ---------------------------------------------------------------------------------------------
namespace {
std::atomic<OpenCLBackend*> g_production{nullptr};
}

GpuBackendBase* productionOpenCLIfCreated() { return g_production.load(); }

// Enumerate devices of every platform.
static std::vector<cl_device_id> allDevices(std::string* listing, int* gpus) {
    const auto& a = cl::api();
    std::vector<cl_device_id> out;
    cl_uint np = 0;
    if (a.clGetPlatformIDs(0, nullptr, &np) != CL_SUCCESS || np == 0) return out;
    std::vector<cl_platform_id> plats(np);
    a.clGetPlatformIDs(np, plats.data(), nullptr);
    for (cl_platform_id p : plats) {
        cl_uint nd = 0;
        if (a.clGetDeviceIDs(p, CL_DEVICE_TYPE_ALL, 0, nullptr, &nd) != CL_SUCCESS || nd == 0) continue;
        std::vector<cl_device_id> devs(nd);
        a.clGetDeviceIDs(p, CL_DEVICE_TYPE_ALL, nd, devs.data(), nullptr);
        for (cl_device_id d : devs) {
            cl_device_type t = 0;
            devInfo(d, CL_DEVICE_TYPE, &t);
            if (t & CL_DEVICE_TYPE_GPU) ++*gpus;
            if (listing) *listing += (listing->empty() ? "" : "; ") + cl::deviceString(d) +
                                     ((t & CL_DEVICE_TYPE_GPU) ? " [GPU]" : (t & CL_DEVICE_TYPE_CPU) ? " [CPU]" : " [other]");
            out.push_back(d);
        }
    }
    // GPUs first
    std::stable_sort(out.begin(), out.end(), [](cl_device_id x, cl_device_id y) {
        cl_device_type tx = 0, ty = 0;
        devInfo(x, CL_DEVICE_TYPE, &tx);
        devInfo(y, CL_DEVICE_TYPE, &ty);
        return (tx & CL_DEVICE_TYPE_GPU) && !(ty & CL_DEVICE_TYPE_GPU);
    });
    return out;
}

SelfTestResult selfTestOpenCL(bool full) {
    SelfTestResult r;
    r.compiledIn = true;
    const double t0 = nowMs();
    const auto& a = cl::api();
    if (!a.ok) { r.message = a.error; r.ms = nowMs() - t0; return r; }
    std::string listing;
    int gpus = 0;
    std::vector<cl_device_id> devs = allDevices(&listing, &gpus);
    if (devs.empty()) {
        r.message = "OpenCL runtime " + a.library + " loaded but no OpenCL device found";
        r.ms = nowMs() - t0;
        return r;
    }
    r.message = std::to_string(devs.size()) + " device(s), " + std::to_string(gpus) + " GPU: " + listing;
    if (!full) { r.ok = true; r.ms = nowMs() - t0; return r; }

    cl_device_id dev = devs.front();
    cl_int e = CL_SUCCESS;
    cl_context ctx = a.clCreateContext(nullptr, 1, &dev, nullptr, nullptr, &e);
    if (e != CL_SUCCESS || !ctx) { r.message += " | " + clErr("clCreateContext", e); r.ms = nowMs() - t0; return r; }
    cl_command_queue q = a.clCreateCommandQueue(ctx, dev, 0, &e);
    std::string report;
    bool ok = false;
    if (e == CL_SUCCESS && q) {
        std::vector<float> state, srcPx;
        engine::Plan plan = makeProbePlan(productionKernelSet().count, &state, &srcPx);
        cl_mem sm = a.clCreateBuffer(ctx, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR, srcPx.size() * 4, srcPx.data(), &e);
        cl_mem dm = a.clCreateBuffer(ctx, CL_MEM_READ_WRITE, size_t(kProbeWidth) * 16u, nullptr, &e);
        if (sm && dm) {
            OpenCLBackend be(productionKernelSet());
            engine::HostImage hs, hd;
            hs.data = sm; hs.w = 4; hs.h = 2; hs.rowStrideFloats = 16;
            hd.data = dm; hd.w = kProbeWidth; hd.h = 1; hd.rowStrideFloats = kProbeWidth * 4;
            engine::GpuContext g;
            g.openclCommandQueue = q;
            std::string eerr;
            if (be.executeImpl(plan, state, 1, hs, hd, g, true, &eerr)) {
                std::vector<float> outPx(size_t(kProbeWidth) * 4u);
                e = a.clEnqueueReadBuffer(q, dm, CL_TRUE, 0, outPx.size() * 4, outPx.data(), 0, nullptr, nullptr);
                if (e == CL_SUCCESS) ok = checkProbe(outPx, state, srcPx, &report);
                else report = clErr("clEnqueueReadBuffer", e);
                BackendStats st = be.stats();
                char b[64];
                std::snprintf(b, sizeof b, " (kernel build %.0f ms)", st.lastBuildMs);
                report += b;
            } else {
                report = eerr;
            }
            a.clFinish(q);
        } else {
            report = clErr("clCreateBuffer", e);
        }
        if (sm) a.clReleaseMemObject(sm);
        if (dm) a.clReleaseMemObject(dm);
        a.clReleaseCommandQueue(q);
    } else {
        report = clErr("clCreateCommandQueue", e);
    }
    a.clReleaseContext(ctx);
    r.ok = ok;
    r.message += " | full test on " + cl::deviceString(dev) + ": " + report;
    r.ms = nowMs() - t0;
    return r;
}

bool cpuFallbackOpenCL(engine::Backend& cpu, const engine::Plan& plan, const std::vector<float>& st, uint64_t ver,
                       const engine::HostImage& src, const engine::HostImage& dst, const engine::GpuContext& gpu,
                       std::string* err) {
    const auto& a = cl::api();
    auto bad = [&](const std::string& m) { if (err) *err = "OpenCL CPU fallback: " + m; return false; };
    if (!a.ok) return bad(a.error);
    cl_command_queue q = static_cast<cl_command_queue>(gpu.openclCommandQueue);
    if (!q || !src.data || !dst.data) return bad("missing queue or images");
    if (src.rowStrideFloats < src.w * 4 || dst.rowStrideFloats < dst.w * 4) return bad("unsupported row stride");
    cl_command_queue_properties props = 0;
    a.clGetCommandQueueInfo(q, CL_QUEUE_PROPERTIES, sizeof props, &props, nullptr);
    if ((props & CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE)) {
        if (a.clEnqueueBarrierWithWaitList) a.clEnqueueBarrierWithWaitList(q, 0, nullptr, nullptr);
        else if (a.clEnqueueBarrier) a.clEnqueueBarrier(q);
    }
    std::vector<float> hs(size_t(src.w) * size_t(src.h) * 4u), hd(size_t(dst.w) * size_t(dst.h) * 4u, 0.f);
    const size_t o[3] = {0, 0, 0};
    const size_t rs[3] = {size_t(src.w) * 16u, size_t(src.h), 1};
    cl_int e = a.clEnqueueReadBufferRect(q, static_cast<cl_mem>(src.data), CL_TRUE, o, o, rs,
                                         size_t(src.rowStrideFloats) * 4u, 0, size_t(src.w) * 16u, 0, hs.data(), 0,
                                         nullptr, nullptr);
    if (e != CL_SUCCESS) return bad(clErr("clEnqueueReadBufferRect(src)", e));
    engine::HostImage cs, cd;
    cs.data = hs.data(); cs.w = src.w; cs.h = src.h; cs.rowStrideFloats = src.w * 4;
    cd.data = hd.data(); cd.w = dst.w; cd.h = dst.h; cd.rowStrideFloats = dst.w * 4;
    std::string cerr;
    if (!cpu.execute(plan, st, ver, cs, cd, engine::GpuContext(), &cerr)) return bad("CPU backend: " + cerr);
    const size_t rd[3] = {size_t(dst.w) * 16u, size_t(dst.h), 1};
    e = a.clEnqueueWriteBufferRect(q, static_cast<cl_mem>(dst.data), CL_TRUE, o, o, rd,
                                   size_t(dst.rowStrideFloats) * 4u, 0, size_t(dst.w) * 16u, 0, hd.data(), 0, nullptr,
                                   nullptr);
    if (e != CL_SUCCESS) return bad(clErr("clEnqueueWriteBufferRect(dst)", e));
    return true;
}

std::unique_ptr<engine::Backend> createOpenCLBackendImpl(const KernelSet& ks) {
    return std::unique_ptr<engine::Backend>(new OpenCLBackend(ks));
}

engine::Backend* productionOpenCL() {
    // Intentionally leaked: destroying driver objects during static destruction (process exit /
    // DLL unload) is unsafe. releaseAllResources() frees the GPU objects at plugin unload.
    static OpenCLBackend* b = [] {
        auto* p = new OpenCLBackend(productionKernelSet());
        g_production.store(p);
        return p;
    }();
    return b;
}

}}} // namespace cel::gpu::detail
