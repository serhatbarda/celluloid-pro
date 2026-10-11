/* Reafilm Pro - all kernel bodies (portable dialect). FIXED (architect).
 * Included by: CPU backend, CUDA translation unit, and the OpenCL/Metal source flattener. */
#ifndef CEL_ALL_KERNELS_H
#define CEL_ALL_KERNELS_H
#include "KernelABI.h"
#include "ColorKernels.h"    /* core agent:    cel_k_decode, cel_k_color, cel_k_display_decode, cel_k_display_finish */
#include "TextureKernels.h"  /* texture agent: every kernel in TextureKernelList.inc */
#include "LicenseKernels.h"  /* license agent: cel_k_watermark */
#endif
