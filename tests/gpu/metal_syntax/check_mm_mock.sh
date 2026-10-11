#!/usr/bin/env bash
# Reafilm Pro - Linux workbench: Objective-C++ (ARC) syntax/type check of the Metal backend and
# the Metal test environment against MOCK Foundation/Metal headers (metal_syntax/objc_mock).
# Catches C++/ARC/type errors (e.g. __autoreleasing vs __strong out-pointers) without a Mac; it
# cannot catch misspelled Metal API names beyond the mocked subset (macOS CI compiles for real).
# Usage: check_mm_mock.sh <repo-root>
set -euo pipefail
repo="${1:?repo root}"
CXX="${CLANGXX:-clang++}"
mock="$repo/tests/gpu/metal_syntax/objc_mock"
status=0
for f in "$repo/src/gpu/metal/MetalBackend.mm" "$repo/tests/gpu/test_env_metal.mm"; do
  if "$CXX" -x objective-c++ -std=c++17 -fobjc-arc -fblocks -fobjc-runtime=macosx-11.0 -fsyntax-only \
       -Wall -Wextra -Werror -isystem "$mock" -DCEL_GPU_HAVE_METAL=1 "$f"; then
    echo "check_mm_mock: OK  $f"
  else
    echo "check_mm_mock: FAILED  $f" >&2; status=1
  fi
done
exit $status
