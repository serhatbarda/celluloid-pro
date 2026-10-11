#!/usr/bin/env bash
# Reafilm Pro - Linux workbench: syntax/dialect check of flattened MSL sources with clang++ and
# the address-space shim in tests/gpu/metal_syntax/shim. Usage: check_metal_shim.sh <file.metal>...
# Catches: C++-isms the dialect forbids that MSL also rejects, address-space mismatches, double
# promotion (MSL has no double), undeclared metal:: functions. Exit code != 0 on any diagnostic.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CXX="${CLANGXX:-clang++}"
command -v "$CXX" >/dev/null || { echo "check_metal_shim: $CXX not found" >&2; exit 2; }
status=0
for f in "$@"; do
  if "$CXX" -x c++ -std=c++14 -fsyntax-only -D__METAL_VERSION__=230 -isystem "$here/shim" \
       -Wall -Wextra -Werror -Wdouble-promotion -Wfloat-conversion -Wno-unknown-attributes \
       -Wno-unused-function -Wno-unused-parameter -Wno-unused-variable "$f"; then
    echo "check_metal_shim: OK  $f"
  else
    echo "check_metal_shim: FAILED  $f" >&2
    status=1
  fi
done
exit $status
