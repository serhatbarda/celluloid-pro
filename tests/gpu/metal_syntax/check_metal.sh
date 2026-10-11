#!/usr/bin/env bash
# Reafilm Pro - macOS CI: compile flattened MSL sources with Apple's offline Metal compiler.
# Usage: check_metal.sh <file.metal>...   (run after `cmake --build <dir> --target celgpu_kernel_sources`;
# the files are <build>/src/gpu/generated/reafilm_production.metal and, when tests are built,
# <build>/tests/gpu/generated/reafilm_abi.metal)
# Compiles with the same language version (Metal 2.3 = macOS 11 baseline) and math mode as the
# runtime (MTLCompileOptions: languageVersion 2.3, no fast math). Fails on any error; warnings are
# printed (and fail with CEL_METAL_WERROR=1).
set -euo pipefail
command -v xcrun >/dev/null || { echo "check_metal: xcrun not found (macOS + Xcode required)" >&2; exit 2; }
out="${TMPDIR:-/tmp}/cel-metal-check"
mkdir -p "$out"
werror=()
[[ "${CEL_METAL_WERROR:-0}" == "1" ]] && werror=(-Werror)
status=0
for f in "$@"; do
  b="$(basename "$f" .metal)"
  # ${arr[@]+"${arr[@]}"}: empty-array safe under `set -u` with macOS /bin/bash 3.2
  if xcrun -sdk macosx metal -std=macos-metal2.3 -mmacosx-version-min=11.0 -fno-fast-math ${werror[@]+"${werror[@]}"} \
       -c "$f" -o "$out/$b.air"; then
    # also link a metallib to catch unresolved symbols
    if xcrun -sdk macosx metallib "$out/$b.air" -o "$out/$b.metallib"; then
      echo "check_metal: OK  $f ($(wc -c < "$out/$b.metallib") bytes metallib)"
    else
      echo "check_metal: metallib FAILED  $f" >&2; status=1
    fi
  else
    echo "check_metal: FAILED  $f" >&2
    status=1
  fi
done
exit $status
