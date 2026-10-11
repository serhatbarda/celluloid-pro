#!/usr/bin/env bash
# Reafilm Pro - standalone build + run of the GPU tests (Linux/macOS workbench).
#   tests/gpu/build.sh [build-dir] [extra cmake args...]
# e.g. tests/gpu/build.sh /tmp/cel-gpu/build -DCEL_ENABLE_CUDA=ON
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build="${1:-$repo/build-gpu-tests}"
shift || true
gen=()
command -v ninja >/dev/null && gen=(-G Ninja)
cmake -S "$repo/tests/gpu" -B "$build" "${gen[@]}" -DCMAKE_BUILD_TYPE=Release "$@"
cmake --build "$build" -j"$(nproc 2>/dev/null || sysctl -n hw.ncpu)"
ctest --test-dir "$build" --output-on-failure
