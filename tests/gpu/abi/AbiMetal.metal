/* root of the flattened Metal program of the ABI test kernel set */
#include "AbiKernels.h"
#include "../../../src/gpu/kernels/Probe.h"
#include "../../../src/gpu/kernels/EntryMetal.h"
#define CEL_KERNEL(name) CEL_GPU_ENTRY(name)
#include "AbiKernelList.h"
#undef CEL_KERNEL
CEL_GPU_ENTRY(gpu_probe)
