/* Reafilm Pro - portable kernel dialect.  FIXED (architect).
 *
 * Every per-pixel function (colour science AND texture passes) is written ONCE in this dialect and
 * compiled as: C++17 (CPU backend + tests), OpenCL C 1.2 (runtime-compiled source), CUDA C++,
 * and Metal Shading Language 2.x (runtime-compiled source).  Rules for code in src/kernels/:
 *   - C-style only: functions, structs of float/int, no classes/templates/references/overloads,
 *     no std::, no recursion, no dynamic allocation, no function pointers, no printf.
 *   - use only the cel_* math wrappers below and float literals with the f suffix (1.0f).
 *   - every function is declared CEL_DEVICE; pointers into big buffers are CEL_GLOBAL.
 *   - no double; no int64; no unsigned overflow assumptions beyond 32-bit uint.
 *   - vectors: use CelF3/CelF4 below, never float3/float4 (layout differs between APIs).
 *   - Metal: compiled with fast math OFF (MTLCompileOptions.mathMode = safe) for CPU parity.
 */
#ifndef CEL_PORTABLE_H
#define CEL_PORTABLE_H

#if defined(__OPENCL_VERSION__)
  #define CEL_API_OPENCL 1
  #define CEL_DEVICE static inline
  #define CEL_GLOBAL __global
  #define CEL_CONSTANT __constant
  typedef uint cel_uint;
  #define cel_pow(a,b)   pow((a),(b))
  #define cel_exp(a)     exp(a)
  #define cel_exp2(a)    exp2(a)
  #define cel_log(a)     log(a)
  #define cel_log2(a)    log2(a)
  #define cel_log10(a)   log10(a)
  #define cel_sqrt(a)    sqrt(a)
  #define cel_floor(a)   floor(a)
  #define cel_fabs(a)    fabs(a)
  #define cel_fmin(a,b)  fmin((a),(b))
  #define cel_fmax(a,b)  fmax((a),(b))
  #define cel_sin(a)     sin(a)
  #define cel_cos(a)     cos(a)
  #define cel_atan2(a,b) atan2((a),(b))
  #define cel_isfinite(a) isfinite(a)
  #define cel_imin(a,b)  min((a),(b))
  #define cel_imax(a,b)  max((a),(b))
#elif defined(__CUDACC__)
  #define CEL_API_CUDA 1
  #define CEL_DEVICE static __device__ __forceinline__
  #define CEL_GLOBAL
  #define CEL_CONSTANT
  typedef unsigned int cel_uint;
  #define cel_pow(a,b)   powf((a),(b))
  #define cel_exp(a)     expf(a)
  #define cel_exp2(a)    exp2f(a)
  #define cel_log(a)     logf(a)
  #define cel_log2(a)    log2f(a)
  #define cel_log10(a)   log10f(a)
  #define cel_sqrt(a)    sqrtf(a)
  #define cel_floor(a)   floorf(a)
  #define cel_fabs(a)    fabsf(a)
  #define cel_fmin(a,b)  fminf((a),(b))
  #define cel_fmax(a,b)  fmaxf((a),(b))
  #define cel_sin(a)     sinf(a)
  #define cel_cos(a)     cosf(a)
  #define cel_atan2(a,b) atan2f((a),(b))
  #define cel_isfinite(a) isfinite(a)
  #define cel_imin(a,b)  min((a),(b))
  #define cel_imax(a,b)  max((a),(b))
#elif defined(__METAL_VERSION__)
  #define CEL_API_METAL 1
  #include <metal_stdlib>
  #define CEL_DEVICE static inline
  #define CEL_GLOBAL device
  #define CEL_CONSTANT constant
  typedef uint cel_uint;
  #define cel_pow(a,b)   metal::precise::pow((a),(b))
  #define cel_exp(a)     metal::precise::exp(a)
  #define cel_exp2(a)    metal::precise::exp2(a)
  #define cel_log(a)     metal::precise::log(a)
  #define cel_log2(a)    metal::precise::log2(a)
  #define cel_log10(a)   metal::precise::log10(a)
  #define cel_sqrt(a)    metal::precise::sqrt(a)
  #define cel_floor(a)   metal::floor(a)
  #define cel_fabs(a)    metal::fabs(a)
  #define cel_fmin(a,b)  metal::fmin((a),(b))
  #define cel_fmax(a,b)  metal::fmax((a),(b))
  #define cel_sin(a)     metal::precise::sin(a)
  #define cel_cos(a)     metal::precise::cos(a)
  #define cel_atan2(a,b) metal::precise::atan2((a),(b))
  #define cel_isfinite(a) metal::isfinite(a)
  #define cel_imin(a,b)  metal::min((a),(b))
  #define cel_imax(a,b)  metal::max((a),(b))
#else
  #define CEL_API_CPU 1
  #include <cmath>
  #include <cstdint>
  #include <algorithm>
  #define CEL_DEVICE static inline
  #define CEL_GLOBAL
  #define CEL_CONSTANT
  typedef std::uint32_t cel_uint;
  #define cel_pow(a,b)   std::pow((float)(a),(float)(b))
  #define cel_exp(a)     std::exp((float)(a))
  #define cel_exp2(a)    std::exp2((float)(a))
  #define cel_log(a)     std::log((float)(a))
  #define cel_log2(a)    std::log2((float)(a))
  #define cel_log10(a)   std::log10((float)(a))
  #define cel_sqrt(a)    std::sqrt((float)(a))
  /* floor/fmin/fmax inline: without SSE4.1 / -ffinite-math-only, GCC and Clang emit libm calls
   * for std::floor/std::fmin/std::fmax. These keep the exact IEEE results of the C functions
   * (fmin/fmax return the non-NaN operand; floor keeps NaN, +-Inf, -0 and |x| >= 2^23). */
  static inline float cel_cpu_floor(float x) {
      if (!(std::fabs(x) < 8388608.0f)) return x;          /* NaN, Inf, already integral */
      float t = (float)(int)x;                              /* truncation toward zero */
      return t == x ? x : (t > x ? t - 1.0f : t);
  }
  static inline float cel_cpu_fmin(float a, float b) { return (a < b || b != b) ? a : b; }
  static inline float cel_cpu_fmax(float a, float b) { return (a > b || b != b) ? a : b; }
  #define cel_floor(a)   cel_cpu_floor((float)(a))
  #define cel_fabs(a)    std::fabs((float)(a))
  #define cel_fmin(a,b)  cel_cpu_fmin((float)(a),(float)(b))
  #define cel_fmax(a,b)  cel_cpu_fmax((float)(a),(float)(b))
  #define cel_sin(a)     std::sin((float)(a))
  #define cel_cos(a)     std::cos((float)(a))
  #define cel_atan2(a,b) std::atan2((float)(a),(float)(b))
  #define cel_isfinite(a) std::isfinite((float)(a))
  #define cel_imin(a,b)  std::min((int)(a),(int)(b))
  #define cel_imax(a,b)  std::max((int)(a),(int)(b))
#endif

#define CEL_PI 3.14159265358979f

typedef struct { float x, y, z; } CelF3;
typedef struct { float x, y, z, w; } CelF4;

CEL_DEVICE CelF3 cel_f3(float a, float b, float c) { CelF3 r; r.x = a; r.y = b; r.z = c; return r; }
CEL_DEVICE CelF4 cel_f4(float a, float b, float c, float d) { CelF4 r; r.x = a; r.y = b; r.z = c; r.w = d; return r; }
CEL_DEVICE float cel_clamp(float v, float lo, float hi) { return cel_fmin(cel_fmax(v, lo), hi); }
CEL_DEVICE float cel_mix(float a, float b, float t) { return a + (b - a) * t; }
CEL_DEVICE float cel_saturate(float v) { return cel_clamp(v, 0.0f, 1.0f); }
CEL_DEVICE float cel_smoothstep(float e0, float e1, float x) {
    float t = cel_saturate((x - e0) / (e1 - e0)); return t * t * (3.0f - 2.0f * t);
}
/* safe finite: replaces NaN/Inf with fallback */
CEL_DEVICE float cel_finite_or(float v, float fallback) { return cel_isfinite(v) ? v : fallback; }

/* 32-bit integer hash (lowbias32), portable across all APIs */
CEL_DEVICE cel_uint cel_hash_u32(cel_uint x) {
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x;
}
CEL_DEVICE float cel_hash01(cel_uint x) { return (float)(cel_hash_u32(x) >> 8) * (1.0f / 16777216.0f); }

#endif /* CEL_PORTABLE_H */
