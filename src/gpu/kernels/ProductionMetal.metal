/* Reafilm Pro - root of the flattened Metal Shading Language library (production kernel set).
 * Flattened at build time by cmake/FlattenKernels.cmake into an embedded string. */
#include "../../kernels/AllKernels.h"
#include "Probe.h"
#include "EntryMetal.h"
#define CEL_KERNEL(name) CEL_GPU_ENTRY(name)
#include "../../kernels/KernelList.h"
#undef CEL_KERNEL
CEL_GPU_ENTRY(gpu_probe)
