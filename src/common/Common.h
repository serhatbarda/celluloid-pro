// Celluloid Pro - shared basic types and a portable parallel_for.
// Interface owned by the architect. Do not change signatures without updating SPEC.md.
#pragma once
#include <cstddef>
#include <vector>
#include <thread>
#include <atomic>
#include <algorithm>
#include <functional>

namespace cel {

struct Float3 {
    float r = 0.f, g = 0.f, b = 0.f;
};

// Interleaved RGB float buffer (3 floats per pixel, row-major, no padding).
struct RGBBuffer {
    int w = 0, h = 0;
    std::vector<float> px;
    void resize(int width, int height) { w = width; h = height; px.assign(size_t(w) * size_t(h) * 3u, 0.f); }
    float* row(int y) { return px.data() + size_t(y) * size_t(w) * 3u; }
    const float* row(int y) const { return px.data() + size_t(y) * size_t(w) * 3u; }
    Float3 get(int x, int y) const { const float* p = row(y) + size_t(x) * 3u; return {p[0], p[1], p[2]}; }
    void set(int x, int y, Float3 v) { float* p = row(y) + size_t(x) * 3u; p[0] = v.r; p[1] = v.g; p[2] = v.b; }
};

// Run fn(y0, y1) over [0, rows) in contiguous chunks on all hardware threads.
// fn must be thread-safe for disjoint row ranges.
inline void parallelRows(int rows, const std::function<void(int, int)>& fn) {
    unsigned hw = std::thread::hardware_concurrency();
    int nThreads = int(hw == 0 ? 4u : hw);
    nThreads = std::max(1, std::min(nThreads, rows));
    if (nThreads == 1) { fn(0, rows); return; }
    // fine-grained chunks for load balancing
    const int chunk = std::max(1, rows / (nThreads * 8));
    std::atomic<int> next{0};
    std::vector<std::thread> pool;
    pool.reserve(size_t(nThreads));
    for (int t = 0; t < nThreads; ++t) {
        pool.emplace_back([&]() {
            for (;;) {
                int y0 = next.fetch_add(chunk);
                if (y0 >= rows) break;
                fn(y0, std::min(rows, y0 + chunk));
            }
        });
    }
    for (auto& th : pool) th.join();
}

} // namespace cel
