#!/usr/bin/env bash
# Stand-alone build + run of the texture tests (no CMake needed).
#   tests/texture/build.sh [build-dir] [-- test args]      default build dir: /tmp/cel-texture-tests
# Builds against the real colour science (src/core + src/kernels/ColorKernels.h).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="${1:-/tmp/cel-texture-tests}"; shift || true
[[ "${1:-}" == "--" ]] && shift
mkdir -p "$OUT"
CXX="${CXX:-g++}"
SRCROOT="$ROOT/src"
EXTRA=("$ROOT"/src/core/*.cpp)
FLAGS=(-std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter -pthread -I"$SRCROOT" -I"$SRCROOT/kernels" -I"$ROOT/tests/texture")
SRC=("$SRCROOT/engine/Plan.cpp" "$SRCROOT/engine/CpuBackend.cpp" "$SRCROOT"/texture/*.cpp "${EXTRA[@]}")
"$CXX" "${FLAGS[@]}" "$ROOT/tests/texture/test_texture.cpp" "${SRC[@]}" -o "$OUT/cel_texture_tests"
"$CXX" "${FLAGS[@]}" "$ROOT/tests/texture/tex_preview.cpp" "${SRC[@]}" -o "$OUT/cel_tex_preview"
echo "built $OUT/cel_texture_tests and $OUT/cel_tex_preview"
"$OUT/cel_texture_tests" "$@"
