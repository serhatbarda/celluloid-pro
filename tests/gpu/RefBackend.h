// Reafilm Pro - GPU tests: reference executor. A deliberately simple, independent CPU
// implementation of engine::Backend over a table of kernel functions (single thread, one buffer
// per plan buffer, no aliasing, any host stride incl. negative). The GPU backends are checked
// against it pass by pass.
#pragma once
#include "../../src/engine/Engine.h"
#include <cstring>
#include <string>
#include <vector>

namespace celtest {

using KernelFn = void (*)(int, int, const CelPassArgs*, float*, const float*, const float*, const float*,
                          const float*, const float*);

class RefBackend : public cel::engine::Backend {
public:
    explicit RefBackend(std::vector<KernelFn> fns) : m_fns(std::move(fns)) {}
    const char* name() const override { return "reference"; }

    bool execute(const cel::engine::Plan& plan, const std::vector<float>& state, uint64_t,
                 const cel::engine::HostImage& src, const cel::engine::HostImage& dst,
                 const cel::engine::GpuContext&, std::string* err) override {
        using namespace cel::engine;
        const size_t nb = plan.buffers.size();
        if (nb < 2) { if (err) *err = "bad plan"; return false; }
        m_internal.assign(nb, std::vector<float>());
        for (size_t b = 2; b < nb; ++b)
            m_internal[b].assign(size_t(plan.buffers[b].w) * size_t(plan.buffers[b].h) * 4u, 0.f);
        auto ptr = [&](int b) -> float* {
            if (b == BUF_SRC) return static_cast<float*>(src.data);
            if (b == BUF_DST) return static_cast<float*>(dst.data);
            return m_internal[size_t(b)].data();
        };
        auto W = [&](int b) { return b == 0 ? src.w : b == 1 ? dst.w : plan.buffers[size_t(b)].w; };
        auto H = [&](int b) { return b == 0 ? src.h : b == 1 ? dst.h : plan.buffers[size_t(b)].h; };
        auto S = [&](int b) {
            return b == 0 ? src.rowStrideFloats : b == 1 ? dst.rowStrideFloats : plan.buffers[size_t(b)].w * 4;
        };
        for (const PassDesc& p : plan.passes) {
            const int k = int(p.kernel);
            if (k < 0 || k >= int(m_fns.size()) || !m_fns[size_t(k)]) { if (err) *err = "bad kernel"; return false; }
            CelPassArgs a = p.args;
            a.outW = W(p.out); a.outH = H(p.out); a.outStride = S(p.out);
            const float* in[4] = {nullptr, nullptr, nullptr, nullptr};
            for (int j = 0; j < 4; ++j) {
                if (p.in[j] < 0) { a.inW[j] = a.inH[j] = a.inStride[j] = 0; continue; }
                a.inW[j] = W(p.in[j]); a.inH[j] = H(p.in[j]); a.inStride[j] = S(p.in[j]);
                in[j] = ptr(p.in[j]);
            }
            float* o = ptr(p.out);
            for (int y = 0; y < a.outH; ++y)
                for (int x = 0; x < a.outW; ++x)
                    m_fns[size_t(k)](x, y, &a, o, in[0], in[1], in[2], in[3], state.data());
        }
        return true;
    }
    const std::vector<float>& internal(int b) const { return m_internal[size_t(b)]; }

private:
    std::vector<KernelFn> m_fns;
    std::vector<std::vector<float>> m_internal;
};

} // namespace celtest
