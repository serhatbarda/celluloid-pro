#!/usr/bin/env bash
# Stand-alone build + run of the texture tests (no CMake needed).
#   tests/texture/build.sh [build-dir] [-- test args]      default build dir: /tmp/cel-texture-tests
# Uses the real colour science (src/core + src/kernels/ColorKernels.h) once core::Pipeline is
# implemented; until then it builds against an overlay source tree in which only the colour
# kernels / core::Pipeline are replaced by the stand-ins in build-stubs/texture/.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="${1:-/tmp/cel-texture-tests}"; shift || true
[[ "${1:-}" == "--" ]] && shift
mkdir -p "$OUT"
CXX="${CXX:-g++}"
STUBS="$ROOT/build-stubs/texture"
SRCROOT="$ROOT/src"
EXTRA=()
if grep -qs "Pipeline::prepare" "$ROOT"/src/core/*.cpp && [[ -f "$ROOT/src/kernels/ColorKernels.h" ]]; then
  EXTRA=("$ROOT"/src/core/*.cpp)
  echo "build.sh: using the real colour science (src/core)"
else
  # overlay: src/* symlinked, kernels/ColorKernels.h (and LicenseKernels.h if missing) = stubs
  # (a tree of per-file symlinks, so relative includes like "../kernels/..." stay in the overlay)
  T="$OUT/overlay/src"; rm -rf "$OUT/overlay"; mkdir -p "$OUT/overlay"; cp -rs "$ROOT/src" "$T"
  rm -f "$T/kernels/ColorKernels.h"; ln -s "$STUBS/ColorKernels.h" "$T/kernels/ColorKernels.h"
  [[ -e "$T/kernels/LicenseKernels.h" ]] || ln -s "$STUBS/LicenseKernels.h" "$T/kernels/LicenseKernels.h"
  SRCROOT="$T"; EXTRA=("$STUBS/CoreStub.cpp")
  echo "build.sh: core::Pipeline not implemented yet -> stub colour kernels (build-stubs/texture)"
fi
FLAGS=(-std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter -pthread -I"$SRCROOT" -I"$SRCROOT/kernels" -I"$ROOT/tests/texture")
SRC=("$SRCROOT/engine/Plan.cpp" "$SRCROOT/engine/CpuBackend.cpp" "$SRCROOT"/texture/*.cpp "${EXTRA[@]}")
"$CXX" "${FLAGS[@]}" "$ROOT/tests/texture/test_texture.cpp" "${SRC[@]}" -o "$OUT/cel_texture_tests"
"$CXX" "${FLAGS[@]}" "$ROOT/tests/texture/tex_preview.cpp" "${SRC[@]}" -o "$OUT/cel_tex_preview"
echo "built $OUT/cel_texture_tests and $OUT/cel_tex_preview"
"$OUT/cel_texture_tests" "$@"
