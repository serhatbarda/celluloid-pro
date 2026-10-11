// Reafilm Pro - GPU tests: the production kernels compiled for the CPU with "worst-case GPU"
// math. Every transcendental cel_* wrapper returns its correctly rounded result moved by a
// deterministic pseudo-random amount of up to the ulp error the OpenCL 1.2 full profile allows
// (pow 16 ulp, exp/exp2/log/log2/log10 3 ulp, sin/cos 4 ulp, atan2 6 ulp, sqrt 1 ulp; CUDA and
// Metal's precise math are tighter). Running a plan with these kernels and comparing it with the
// exact CPU build shows whether the encoded output stays within the parity tolerance on ANY
// conformant device, not only on the one GPU the test happens to have (POCL is ~1 ulp). Formulas
// that amplify rounding errors (cancellation) show up here immediately. Needs no GPU, so it also
// runs in CI (ctest gpu_math_tolerance).
#include "../../src/kernels/Portable.h"
#include "PerturbedKernels.h"

#include <cmath>
#include <cstdint>
#include <cstring>

namespace {

float g_scale = 1.0f;   // multiplies every allowance (0 = exact)

inline uint32_t bitsOf(float v) { uint32_t b; std::memcpy(&b, &v, 4); return b; }

// result r of function fn at (a, b), allowance `ulps`: r + u * ulps * ulp(r), u in [-1, 1]
float perturb(float r, int fn, float a, float b, float ulps) {
    if (!std::isfinite(r) || r == 0.0f || r == 1.0f || r == -1.0f || g_scale == 0.0f) return r;
    uint32_t h = bitsOf(a) * 0x9e3779b1u ^ (bitsOf(b) + 0x7f4a7c15u) * 0x85ebca77u ^ uint32_t(fn) * 0xc2b2ae3du;
    h ^= h >> 16; h *= 0x7feb352du; h ^= h >> 15; h *= 0x846ca68bu; h ^= h >> 16;
    const float u = float(h >> 8) * (2.0f / 16777216.0f) - 1.0f;
    int e = 0;
    std::frexp(r, &e);
    const float ulp = std::ldexp(1.0f, e - 24);
    return r + u * ulps * g_scale * ulp;
}

float p_pow(float a, float b) {
    if (b == 0.0f || a == 1.0f) return std::pow(a, b);          // exact special cases (C99 Annex F)
    return perturb(std::pow(a, b), 0, a, b, 16.0f);
}
float p_exp(float a) { return perturb(std::exp(a), 1, a, 0.f, 3.0f); }
float p_exp2(float a) { return perturb(std::exp2(a), 2, a, 0.f, 3.0f); }
float p_log(float a) { return perturb(std::log(a), 3, a, 0.f, 3.0f); }
float p_log2(float a) { return perturb(std::log2(a), 4, a, 0.f, 3.0f); }
float p_log10(float a) { return perturb(std::log10(a), 5, a, 0.f, 3.0f); }
float p_sqrt(float a) { return perturb(std::sqrt(a), 6, a, 0.f, 1.0f); }
[[maybe_unused]] float p_sin(float a) { return perturb(std::sin(a), 7, a, 0.f, 4.0f); }
[[maybe_unused]] float p_cos(float a) { return perturb(std::cos(a), 8, a, 0.f, 4.0f); }
[[maybe_unused]] float p_atan2(float a, float b) { return perturb(std::atan2(a, b), 9, a, b, 6.0f); }

} // namespace

#undef cel_pow
#undef cel_exp
#undef cel_exp2
#undef cel_log
#undef cel_log2
#undef cel_log10
#undef cel_sqrt
#undef cel_sin
#undef cel_cos
#undef cel_atan2
#define cel_pow(a, b) p_pow((float)(a), (float)(b))
#define cel_exp(a) p_exp((float)(a))
#define cel_exp2(a) p_exp2((float)(a))
#define cel_log(a) p_log((float)(a))
#define cel_log2(a) p_log2((float)(a))
#define cel_log10(a) p_log10((float)(a))
#define cel_sqrt(a) p_sqrt((float)(a))
#define cel_sin(a) p_sin((float)(a))
#define cel_cos(a) p_cos((float)(a))
#define cel_atan2(a, b) p_atan2((float)(a), (float)(b))

#include "../../src/kernels/AllKernels.h"

namespace celtest {

void setPerturbedMathScale(float s) { g_scale = s; }

std::vector<KernelFn> perturbedProductionKernels() {
    return {
#define CEL_KERNEL(name) &cel_k_##name,
#include "../../src/kernels/KernelList.h"
#undef CEL_KERNEL
    };
}

} // namespace celtest
