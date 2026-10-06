#!/usr/bin/env bash
# Celluloid Pro - standalone build + run of the GPU tests (Linux/macOS workbench).
#   tests/gpu/build.sh [build-dir] [extra cmake args...]
# e.g. tests/gpu/build.sh /tmp/cel-gpu/build -DCEL_ENABLE_CUDA=ON
# During the Build phase, kernel headers that do not exist yet are taken from build-stubs/gpu
# (only when the directory exists).
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build="${1:-$repo/build-gpu-tests}"
shift || true
stubs=()
[[ -d "$repo/build-stubs/gpu/kernels" ]] && stubs=("-DCEL_GPU_KERNEL_INCLUDE_DIRS=$repo/build-stubs/gpu/kernels")
gen=()
command -v ninja >/dev/null && gen=(-G Ninja)
cmake -S "$repo/tests/gpu" -B "$build" "${gen[@]}" -DCMAKE_BUILD_TYPE=Release "${stubs[@]}" "$@"
cmake --build "$build" -j"$(nproc 2>/dev/null || sysctl -n hw.ncpu)"
ctest --test-dir "$build" --output-on-failure
