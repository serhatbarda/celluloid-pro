// Celluloid Pro - tests for cel::parallelRows (persistent pool) and the inline CPU math of
// Portable.h (cel_floor / cel_fmin / cel_fmax must equal the C library functions).
#include "common/Common.h"
#include "kernels/Portable.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <random>
#include <stdexcept>
#include <thread>
#include <vector>

static int g_fail = 0, g_checks = 0;
static void check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) { ++g_fail; std::printf("FAIL: %s\n", what); }
}
static bool sameFloat(float a, float b) {
    if (a != a && b != b) return true;
    uint32_t x, y;
    std::memcpy(&x, &a, 4);
    std::memcpy(&y, &b, 4);
    return x == y;
}
static bool sameValue(float a, float b) { return (a != a && b != b) || a == b; }   // +0 == -0 allowed

static void testMath() {
    long bad = 0;
    for (uint64_t u = 0; u <= 0xFFFFFFFFull; u += 4093) {
        uint32_t bits = uint32_t(u);
        float x;
        std::memcpy(&x, &bits, 4);
        if (!sameFloat(cel_floor(x), std::floor(x))) ++bad;
    }
    const float sp[] = {0.f, -0.f, 0.5f, -0.5f, 1.f, -1.f, 8388607.5f, -8388607.5f, 8388608.f, 1e30f, -1e30f,
                        std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(),
                        std::numeric_limits<float>::quiet_NaN(), 1e-40f, -1e-40f, 2147483648.f, -2147483904.f};
    for (float a : sp) {
        if (!sameFloat(cel_floor(a), std::floor(a))) ++bad;
        for (float b : sp)
            if (!sameValue(cel_fmin(a, b), std::fmin(a, b)) || !sameValue(cel_fmax(a, b), std::fmax(a, b))) ++bad;
    }
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> d(-1e4f, 1e4f);
    for (int i = 0; i < 1000000; ++i) {
        float a = d(rng), b = d(rng);
        if (!sameValue(cel_fmin(a, b), std::fmin(a, b)) || !sameValue(cel_fmax(a, b), std::fmax(a, b))) ++bad;
        if (!sameFloat(cel_floor(a), std::floor(a))) ++bad;
    }
    check(bad == 0, "cel_floor / cel_fmin / cel_fmax equal floor / fmin / fmax");
}

static void testPool() {
    // every row exactly once, for many sizes, with nested calls inside the job
    long bad = 0;
    for (int rows = 1; rows < 400; rows += (rows < 40 ? 1 : 7)) {
        std::vector<std::atomic<int>> c(static_cast<size_t>(rows));
        for (auto& v : c) v = 0;
        cel::parallelRows(rows, [&](int y0, int y1) {
            for (int y = y0; y < y1; ++y) {
                c[size_t(y)]++;
                int inner = 0;
                cel::parallelRows(5, [&](int a, int b) { inner += b - a; });   // nested: serial
                if (inner != 5) ++bad;
            }
        });
        for (auto& v : c) if (v != 1) ++bad;
    }
    check(bad == 0, "parallelRows covers every row exactly once (incl. nested calls)");
    cel::parallelRows(0, [&](int, int) { ++bad; });
    check(bad == 0, "parallelRows(0) does nothing");

    // exceptions propagate to the caller and the pool stays usable
    bool caught = false;
    try {
        cel::parallelRows(2000, [&](int y0, int) { if (y0 > 1000) throw std::runtime_error("x"); });
    } catch (const std::runtime_error&) {
        caught = true;
    }
    check(caught, "exception thrown in a worker reaches the caller");
    std::atomic<long> sum{0};
    cel::parallelRows(1000, [&](int a, int b) { sum += b - a; });
    check(sum == 1000, "pool usable after an exception");

    // concurrent callers (pool busy -> fallback threads)
    sum = 0;
    std::vector<std::thread> ts;
    for (int t = 0; t < 4; ++t)
        ts.emplace_back([&] { for (int i = 0; i < 100; ++i) cel::parallelRows(300, [&](int a, int b) { sum += b - a; }); });
    for (auto& t : ts) t.join();
    check(sum == 4L * 100 * 300, "concurrent parallelRows callers");

    // shutdown and restart
    cel::shutdownParallelPool();
    sum = 0;
    cel::parallelRows(777, [&](int a, int b) { sum += b - a; });
    check(sum == 777, "parallelRows after shutdownParallelPool()");
    cel::shutdownParallelPool();
    cel::shutdownParallelPool();   // idempotent

    // overhead per call (informational)
    std::vector<float> buf(1080);
    auto t0 = std::chrono::steady_clock::now();
    const int n = 500;
    for (int i = 0; i < n; ++i)
        cel::parallelRows(1080, [&](int a, int b) { for (int y = a; y < b; ++y) buf[size_t(y)] += 1.f; });
    double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / n;
    std::printf("  parallelRows overhead: %.1f us per call (%u hardware threads)\n", us, std::thread::hardware_concurrency());
    cel::shutdownParallelPool();
}

int main() {
    testMath();
    testPool();
    std::printf("%d checks, %d failures\n", g_checks, g_fail);
    return g_fail == 0 ? 0 : 1;
}
