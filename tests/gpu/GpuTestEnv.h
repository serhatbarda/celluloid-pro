// Reafilm Pro - GPU tests: device environments (own context/queue/stream like a host would
// create), host images with arbitrary strides and padding sentinels, comparison helpers.
#pragma once
#include "../../src/gpu/GpuBackends.h"
#include "RefBackend.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace celtest {

// ---- tiny check framework ----------------------------------------------------------------------
struct Counters { int checks = 0, failures = 0; };
inline Counters& counters() { static Counters c; return c; }
#define CHECK(cond, ...)                                                                    \
    do {                                                                                    \
        celtest::counters().checks++;                                                       \
        if (!(cond)) {                                                                      \
            celtest::counters().failures++;                                                 \
            std::printf("  FAIL %s:%d: %s -- ", __FILE__, __LINE__, #cond);                 \
            std::printf(__VA_ARGS__);                                                       \
            std::printf("\n");                                                              \
        }                                                                                   \
    } while (0)

constexpr float kSentinel = 12345.678f;

// Host image with stride (floats, may be negative) and padding filled with a sentinel.
struct HostImg {
    int w = 0, h = 0, stride = 0;
    std::vector<float> store;      // the whole span
    float* row0 = nullptr;         // pointer to row 0
    HostImg() = default;
    HostImg(int w_, int h_, int stride_) { init(w_, h_, stride_); }
    void init(int w_, int h_, int stride_) {
        w = w_; h = h_; stride = stride_;
        const size_t as = size_t(stride < 0 ? -stride : stride);
        store.assign((size_t(h) - 1) * as + size_t(w) * 4u, kSentinel);
        row0 = store.data() + (stride < 0 ? (size_t(h) - 1) * as : 0);
    }
    HostImg(const HostImg& o) { *this = o; }
    HostImg& operator=(const HostImg& o) {
        w = o.w; h = o.h; stride = o.stride; store = o.store;
        const size_t as = size_t(stride < 0 ? -stride : stride);
        row0 = store.data() + (stride < 0 ? (size_t(h) - 1) * as : 0);
        return *this;
    }
    float* px(int x, int y) { return row0 + std::ptrdiff_t(y) * stride + x * 4; }
    const float* px(int x, int y) const { return row0 + std::ptrdiff_t(y) * stride + x * 4; }
    cel::engine::HostImage host() { cel::engine::HostImage hi; hi.data = row0; hi.w = w; hi.h = h; hi.rowStrideFloats = stride; return hi; }
    size_t bytes() const { return store.size() * sizeof(float); }
    void fillPattern(uint32_t seed, float lo, float hi) {
        uint32_t s = seed * 2654435761u + 1u;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w * 4; ++x) {
                s ^= s << 13; s ^= s >> 17; s ^= s << 5;
                px(0, y)[x] = lo + (hi - lo) * float(s >> 8) * (1.0f / 16777216.0f);
            }
    }
    // padding (beyond w*4 in each row) still holds the sentinel
    bool paddingIntact() const {
        const size_t as = size_t(stride < 0 ? -stride : stride);
        for (int r = 0; r + 1 < h; ++r)
            for (size_t i = size_t(w) * 4u; i < as; ++i)
                if (store[size_t(r) * as + i] != kSentinel) return false;
        return true;
    }
};

struct DiffStats {
    double maxAbs = 0, maxRel = 0;
    long nanMismatch = 0;
    long count = 0;
    int worstX = -1, worstY = -1, worstC = -1;
    float worstA = 0, worstB = 0;
};
inline DiffStats compare(const HostImg& a, const HostImg& b) {
    DiffStats d;
    for (int y = 0; y < a.h; ++y)
        for (int x = 0; x < a.w; ++x)
            for (int c = 0; c < 4; ++c) {
                const float va = a.px(x, y)[c], vb = b.px(x, y)[c];
                d.count++;
                if (std::isnan(va) || std::isnan(vb)) { if (std::isnan(va) != std::isnan(vb)) d.nanMismatch++; continue; }
                if (std::isinf(va) || std::isinf(vb)) { if (va != vb) d.nanMismatch++; continue; }
                const double e = std::fabs(double(va) - double(vb));
                const double r = e / std::max(1.0, std::fabs(double(va)));
                if (r > d.maxRel) { d.maxRel = r; d.worstX = x; d.worstY = y; d.worstC = c; d.worstA = va; d.worstB = vb; }
                if (e > d.maxAbs) d.maxAbs = e;
            }
    return d;
}
inline std::string describe(const DiffStats& d) {
    char b[256];
    std::snprintf(b, sizeof b, "maxAbs %.3g maxRel %.3g nanMismatch %ld (worst at %d,%d c%d: ref %.9g gpu %.9g)",
                  d.maxAbs, d.maxRel, d.nanMismatch, d.worstX, d.worstY, d.worstC, double(d.worstA), double(d.worstB));
    return b;
}

// ---- device environment -------------------------------------------------------------------------
class Env {
public:
    virtual ~Env() = default;
    virtual const char* name() const = 0;
    virtual cel::gpu::Api api() const = 0;
    virtual std::string device() const = 0;
    // device image holding the host image's whole span; returns the handle the backend expects
    virtual void* upload(const HostImg& img) = 0;
    virtual void* allocLike(const HostImg& img) = 0;                // filled with the host content
    virtual bool download(void* handle, HostImg& img) = 0;          // whole span back into img.store
    virtual void release(void* handle) = 0;
    virtual cel::engine::GpuContext context() = 0;
    virtual void finish() = 0;
    // optional extra queue / out-of-order queue
    virtual bool makeSecondQueue() { return false; }
    virtual cel::engine::GpuContext secondContext() { return context(); }
    virtual bool supportsNegativeStride() const { return false; }
};

std::unique_ptr<Env> makeOpenCLEnv(std::string* why, bool outOfOrder = false);   // test_env_opencl.cpp
std::unique_ptr<Env> makeCudaEnv(std::string* why);                               // test_env_cuda.cpp
std::unique_ptr<Env> makeMetalEnv(std::string* why);                              // test_env_metal.mm (Apple)

} // namespace celtest
