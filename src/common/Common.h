// Celluloid Pro - shared basic types and a portable parallel_for.
// Interface owned by the architect. Do not change signatures without updating SPEC.md.
#pragma once
#include <cstddef>
#include <vector>
#include <thread>
#include <atomic>
#include <algorithm>
#include <condition_variable>
#include <exception>
#include <functional>
#include <mutex>

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

namespace detail {

// Persistent worker pool behind parallelRows(). A frame runs ~40 passes; starting and joining
// hardware_concurrency() threads per pass costs milliseconds per frame on many-core Windows/macOS
// machines, so the workers are created once and parked on a condition variable between jobs.
//  * One job at a time: a second thread calling parallelRows() while the pool is busy (e.g. a
//    colour bake in another instance) falls back to short-lived threads, as before.
//  * Nested calls (fn itself calling parallelRows) run serially on the calling thread.
//  * Exceptions thrown by fn are caught on the worker and rethrown on the caller.
//  * The pool object is intentionally never destroyed by a static destructor (joining threads
//    from DllMain / atexit can deadlock); shutdownParallelPool() joins the workers explicitly
//    (the OFX unload action calls it) and the next parallelRows() call starts them again.
class RowPool {
public:
    static RowPool& instance() {
        static RowPool* pool = new RowPool();   // leaked on purpose, see above
        return *pool;
    }
    static bool& insideJob() {
        static thread_local bool inside = false;
        return inside;
    }

    // false = pool busy (caller must use another strategy)
    bool run(int rows, int chunk, int nThreads, const std::function<void(int, int)>& fn) {
        std::unique_lock<std::mutex> busy(m_dispatch, std::try_to_lock);
        if (!busy.owns_lock()) return false;
        std::unique_lock<std::mutex> lk(m_mutex);
        const int nWorkers = nThreads - 1;
        while (int(m_workers.size()) < nWorkers) {
            m_workers.emplace_back([this, id = int(m_workers.size())]() { workerLoop(id); });
        }
        m_fn = &fn;
        m_rows = rows;
        m_chunk = chunk;
        m_next.store(0);
        m_error = nullptr;
        m_participants = nWorkers;
        m_active = nWorkers;
        ++m_generation;
        lk.unlock();
        m_wake.notify_all();

        insideJob() = true;
        try {
            work(fn, rows, chunk);
        } catch (...) {
            std::lock_guard<std::mutex> g(m_mutex);
            if (!m_error) m_error = std::current_exception();
            m_next.store(rows);                        // stop handing out rows
        }
        insideJob() = false;

        lk.lock();
        m_done.wait(lk, [this] { return m_active == 0; });
        m_fn = nullptr;
        std::exception_ptr err = m_error;
        m_error = nullptr;
        lk.unlock();
        if (err) std::rethrow_exception(err);
        return true;
    }

    void shutdown() {
        std::lock_guard<std::mutex> busy(m_dispatch);  // waits for a running job
        std::vector<std::thread> workers;
        {
            std::lock_guard<std::mutex> g(m_mutex);
            m_stop = true;
            workers.swap(m_workers);
        }
        m_wake.notify_all();
        for (auto& t : workers) t.join();
        std::lock_guard<std::mutex> g(m_mutex);
        m_stop = false;
    }

private:
    RowPool() = default;

    void work(const std::function<void(int, int)>& fn, int rows, int chunk) {
        for (;;) {
            const int y0 = m_next.fetch_add(chunk);
            if (y0 >= rows) break;
            fn(y0, std::min(rows, y0 + chunk));
        }
    }

    void workerLoop(int id) {
        insideJob() = true;
        unsigned long long seen = 0;
        std::unique_lock<std::mutex> lk(m_mutex);
        seen = m_generation;   // a worker started for the current job joins it below
        if (m_fn && id < m_participants) seen = m_generation - 1;
        for (;;) {
            m_wake.wait(lk, [&] { return m_stop || m_generation != seen; });
            if (m_stop) return;
            seen = m_generation;
            if (id >= m_participants || !m_fn) continue;   // job uses fewer threads than the pool has
            const std::function<void(int, int)>* fn = m_fn;
            const int rows = m_rows, chunk = m_chunk;
            lk.unlock();
            try {
                work(*fn, rows, chunk);
            } catch (...) {
                std::lock_guard<std::mutex> g(m_mutex);
                if (!m_error) m_error = std::current_exception();
                m_next.store(rows);
            }
            lk.lock();
            if (--m_active == 0) m_done.notify_all();
        }
    }

    std::mutex m_dispatch;                 // held for the duration of one job
    std::mutex m_mutex;                    // protects everything below
    std::condition_variable m_wake, m_done;
    std::vector<std::thread> m_workers;
    const std::function<void(int, int)>* m_fn = nullptr;
    int m_rows = 0, m_chunk = 1, m_participants = 0, m_active = 0;
    unsigned long long m_generation = 0;
    bool m_stop = false;
    std::exception_ptr m_error;
    std::atomic<int> m_next{0};
};

} // namespace detail

// Run fn(y0, y1) over [0, rows) in contiguous chunks on all hardware threads.
// fn must be thread-safe for disjoint row ranges.
inline void parallelRows(int rows, const std::function<void(int, int)>& fn) {
    if (rows <= 0) return;
    unsigned hw = std::thread::hardware_concurrency();
    int nThreads = int(hw == 0 ? 4u : hw);
    nThreads = std::max(1, std::min(nThreads, rows));
    if (nThreads == 1 || detail::RowPool::insideJob()) { fn(0, rows); return; }
    // fine-grained chunks for load balancing
    const int chunk = std::max(1, rows / (nThreads * 8));
    if (detail::RowPool::instance().run(rows, chunk, nThreads, fn)) return;

    // pool busy with another caller's job: short-lived threads
    std::atomic<int> next{0};
    std::vector<std::thread> pool;
    pool.reserve(size_t(nThreads));
    for (int t = 0; t < nThreads; ++t) {
        pool.emplace_back([&]() {
            detail::RowPool::insideJob() = true;
            for (;;) {
                int y0 = next.fetch_add(chunk);
                if (y0 >= rows) break;
                fn(y0, std::min(rows, y0 + chunk));
            }
        });
    }
    for (auto& th : pool) th.join();
}

// Joins the persistent parallelRows() workers (call before the module is unloaded, e.g. from the
// OFX unload action). Safe to call at any time; later parallelRows() calls restart the workers.
inline void shutdownParallelPool() { detail::RowPool::instance().shutdown(); }

} // namespace cel
