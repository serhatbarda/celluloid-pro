// Celluloid Pro - GPU tests: production kernels with worst-case GPU math (PerturbedKernels.cpp).
#pragma once
#include "RefBackend.h"
#include <vector>

namespace celtest {
// kernel table in KernelList.h order, for RefBackend
std::vector<KernelFn> perturbedProductionKernels();
// scale of the ulp allowances (1 = OpenCL 1.2 full-profile limits, 0 = exact); not thread-safe
void setPerturbedMathScale(float s);
} // namespace celtest
