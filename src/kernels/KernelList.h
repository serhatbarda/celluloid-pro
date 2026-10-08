/* Celluloid Pro - the list of all kernels (X-macro).  Shared file:
 *   - COLOR section: owned by the core agent (names fixed below; bodies in ColorKernels.h)
 *   - TEXTURE section: owned by the texture agent (add entries as needed; bodies in TextureKernels.h)
 *   - LICENSE section: owned by the license agent (bodies in LicenseKernels.h)
 * Each entry: CEL_KERNEL(name) -> device function `void cel_k_<name>(CEL_KERNEL_PARAMS)` and
 * KernelId enumerator `KID_<name>`. ORDER = KernelId values; append only within a section is fine
 * because ids are only used at runtime within one build.
 *
 * Usage:  #define CEL_KERNEL(name) ...   #include "KernelList.h"   #undef CEL_KERNEL
 */
/* ---- COLOR (core) ---- */
CEL_KERNEL(decode)          /* in0 = host source RGBA (encoded) -> out = scene-linear working RGB, alpha copied */
CEL_KERNEL(color)           /* in0 = scene-linear RGBA, in1 = grain noise (xyz) or unused, in2 = host source (alpha), in3 = film damage field or unused (v1.1) -> out = encoded output */
CEL_KERNEL(display_decode)  /* Texture-Only: in0 = host source (display-encoded) -> out = display-linear */
CEL_KERNEL(display_finish)  /* Texture-Only: in0 = display-linear (after effects), in1 = grain noise or unused, in2 = host source (alpha), in3 = film damage field or unused (v1.1) -> out = encoded */
/* ---- TEXTURE (texture agent) ---- */
#include "TextureKernelList.inc"
/* ---- LICENSE (license agent) ---- */
CEL_KERNEL(watermark)       /* in0 = image to stamp (host dst layout) -> out (may alias in0? NO: separate buffer) */
