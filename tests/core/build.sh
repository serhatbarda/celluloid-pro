#!/usr/bin/env bash
# Standalone build + run of the core unit tests (Linux / macOS).
#   tests/core/build.sh [builddir]        (default /tmp/cel-core/tests)
# Regenerates the NumPy golden data first when python3 with numpy is available.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD="${1:-/tmp/cel-core/tests}"
if python3 -c "import numpy" 2>/dev/null; then
  python3 "$ROOT/reference/golden.py"
fi
mkdir -p "$BUILD"
cat > "$BUILD/CMakeLists.txt" <<CM
cmake_minimum_required(VERSION 3.20)
project(celcore_tests CXX)
set(CMAKE_CXX_STANDARD 17)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
enable_testing()
add_subdirectory("$ROOT/tests/core" core)
CM
cmake -S "$BUILD" -B "$BUILD/b" -G Ninja -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$BUILD/b" -j"$(nproc 2>/dev/null || echo 4)"
"$BUILD/b/core/test_core" --tmp "$BUILD" "$@"
