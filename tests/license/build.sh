#!/usr/bin/env bash
# Standalone build + run of the licensing tests (no CMake needed).
#
#   tests/license/build.sh [BUILD_DIR]        default BUILD_DIR: ${TMPDIR:-/tmp}/cel-license-tests
#
# Environment:
#   CXX / CC        compilers (default g++ / gcc)
#   FUZZ_SECONDS    run the libFuzzer harness this long (needs clang + compiler-rt; default 0)
#   SKIP_SAN=1      skip the ASan/UBSan and TSan builds
#
# Steps: Monocypher -> licensegen -> unit tests (dev key accepted, ASan+UBSan) -> unit tests
# (release, dev key rejected) -> TSan run of the persistence tests -> licensegen CLI end-to-end ->
# Python reference implementation cross-check (if PyCA cryptography is installed) -> watermark
# kernel as OpenCL C vs CPU (if an OpenCL platform exists, e.g. POCL) -> watermark kernel as CUDA
# device code (clang, syntax/codegen only) -> optional fuzzing.
# Review images of the watermark are written to BUILD_DIR/images.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
B="${1:-${TMPDIR:-/tmp}/cel-license-tests}"
CXX="${CXX:-g++}"
CC="${CC:-gcc}"
JOBS="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"
WARN=(-Wall -Wextra -Wpedantic -Werror)
mkdir -p "$B/images"

LIB_SRC=("$ROOT/src/license/License.cpp" "$ROOT/src/license/Persist.cpp" "$ROOT/src/license/KeyFormat.cpp" "$ROOT/src/license/KeySigner.cpp")
TEST_SRC=("$HERE/TestMain.cpp" "$HERE/test_keys.cpp" "$HERE/test_persist.cpp" "$HERE/test_watermark.cpp")
DEFS=(-DCEL_LICENSE_REPO_ROOT="\"$ROOT\"")
MONO=("$ROOT/third_party/monocypher/monocypher.c" "$ROOT/third_party/monocypher/monocypher-ed25519.c")

step() { printf '\n== %s\n' "$*"; }
pass=0; skipped=()

step "Monocypher (C99, -Werror)"
"$CC" -std=c99 -O2 -Wall -Wextra -Wpedantic -Werror -c "${MONO[0]}" -o "$B/monocypher.o"
"$CC" -std=c99 -O2 -Wall -Wextra -Wpedantic -Werror -c "${MONO[1]}" -o "$B/monocypher-ed25519.o"

step "licensegen"
"$CXX" -std=c++17 -O2 "${WARN[@]}" -o "$B/licensegen" "$ROOT/tools/licensegen/licensegen.cpp" \
    "${LIB_SRC[@]}" "$B/monocypher.o" "$B/monocypher-ed25519.o"

SAN=(-O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer)
if [[ "${SKIP_SAN:-0}" != 1 ]]; then
  step "unit tests, CEL_DEV_LICENSE=1, ASan + UBSan"
  "$CC" -std=c99 "${SAN[@]}" -c "${MONO[0]}" -o "$B/mono_asan.o"
  "$CC" -std=c99 "${SAN[@]}" -c "${MONO[1]}" -o "$B/mono_ed_asan.o"
  "$CXX" -std=c++17 "${SAN[@]}" "${WARN[@]}" "${DEFS[@]}" -DCEL_DEV_LICENSE=1 -o "$B/tests_dev_asan" \
      "${TEST_SRC[@]}" "${LIB_SRC[@]}" "$B/mono_asan.o" "$B/mono_ed_asan.o" -pthread
  "$B/tests_dev_asan" --out "$B/images"
  pass=$((pass + 1))
else
  step "unit tests, CEL_DEV_LICENSE=1"
  "$CXX" -std=c++17 -O2 "${WARN[@]}" "${DEFS[@]}" -DCEL_DEV_LICENSE=1 -o "$B/tests_dev" \
      "${TEST_SRC[@]}" "${LIB_SRC[@]}" "$B/monocypher.o" "$B/monocypher-ed25519.o" -pthread
  "$B/tests_dev" --out "$B/images"
  pass=$((pass + 1))
fi

step "unit tests, release configuration (development key must be rejected)"
"$CXX" -std=c++17 -O2 "${WARN[@]}" "${DEFS[@]}" -o "$B/tests_release" "${TEST_SRC[@]}" "${LIB_SRC[@]}" \
    "$B/monocypher.o" "$B/monocypher-ed25519.o" -pthread
"$B/tests_release"
# the development public key must not even be present in a release-configured binary
"$CXX" -std=c++17 -O2 "${DEFS[@]}" -DCEL_DEV_LICENSE=1 -c "$ROOT/src/license/License.cpp" -o "$B/License_dev.o"
python3 - "$ROOT/tools/licensegen/dev-keys/celluloid-dev.public.key" "$B/tests_release" "$B/License_dev.o" <<'PYEOF'
import sys
pub = next(l.split(":", 1)[1].strip() for l in open(sys.argv[1]) if l.startswith("CELPUB1:"))
pub = bytes.fromhex(pub)
rel, dev = open(sys.argv[2], "rb").read(), open(sys.argv[3], "rb").read()
assert pub in dev, "self-check failed: dev key not found in the dev object"
assert pub not in rel, "development public key found in a release binary"
print("development key absent from the release binary (and present in the dev object)")
PYEOF
pass=$((pass + 1))

if [[ "${SKIP_SAN:-0}" != 1 ]]; then
  step "persistence tests under ThreadSanitizer"
  if "$CC" -std=c99 -O1 -g -fsanitize=thread -c "${MONO[0]}" -o "$B/mono_tsan.o" 2>/dev/null &&
     "$CC" -std=c99 -O1 -g -fsanitize=thread -c "${MONO[1]}" -o "$B/mono_ed_tsan.o" 2>/dev/null &&
     "$CXX" -std=c++17 -O1 -g -fsanitize=thread "${DEFS[@]}" -DCEL_DEV_LICENSE=1 -o "$B/tests_tsan" \
        "${TEST_SRC[@]}" "${LIB_SRC[@]}" "$B/mono_tsan.o" "$B/mono_ed_tsan.o" -pthread 2>/dev/null; then
    TSAN_OPTIONS="halt_on_error=1" "$B/tests_tsan" --filter persist
    pass=$((pass + 1))
  else
    skipped+=("TSan (not supported by $CXX)")
  fi
fi

step "licensegen end-to-end"
LG="$B/licensegen"
K="$(mktemp -d "${TMPDIR:-/tmp}/cel-lg-XXXXXX")"
trap 'rm -rf "$K"' EXIT
if "$LG" keygen --out-dir "$ROOT/tools/licensegen" >/dev/null 2>&1; then
  echo "keygen wrote a production key into the repository"; exit 1
fi
"$LG" keygen --out-dir "$K" --name shop >/dev/null
[[ "$(stat -c %a "$K/shop.private.key" 2>/dev/null || stat -f %Lp "$K/shop.private.key")" == 600 ]]
if "$LG" keygen --out-dir "$K" --name shop >/dev/null 2>&1; then echo "keygen overwrote a key"; exit 1; fi
"$LG" header --pub "$K/shop.public.key" --out "$K/PublicKey.h" >/dev/null
grep -q "kProductionKeyProvisional = false" "$K/PublicKey.h"
"$LG" pubkey --key "$K/shop.private.key" | grep -q "$(grep CELPUB1 "$K/shop.public.key")"
KEY="$("$LG" sign --key "$K/shop.private.key" --licensee "Jane Colourist <jane@example.com>" --edition studio \
        --seats 3 --id ORDER-1001 --expires 2099-12-31)"
"$LG" verify --pub "$K/shop.public.key" "$KEY" | grep -q "valid:    yes"
"$LG" verify --pub "$K/shop.public.key" --json "$KEY" | grep -q '"seats":3'
echo "$KEY" | "$LG" verify --pub "$K/shop.public.key" - >/dev/null
if "$LG" verify --pub "$K/shop.public.key" --now 2100-01-05 "$KEY" >/dev/null; then echo "expired key accepted"; exit 1; fi
if "$LG" verify "$KEY" >/dev/null; then echo "foreign key accepted by compiled-in keys"; exit 1; fi
DEVKEY="$("$LG" sign --key "$ROOT/tools/licensegen/dev-keys/celluloid-dev.private.key" --licensee "Dev" --json)"
DEVKEY="$(echo "$DEVKEY" | sed -E 's/.*"key":"([^"]+)".*/\1/')"
"$LG" verify "$DEVKEY" | grep -q "DEVELOPMENT KEY"
if "$LG" sign --key "$K/shop.private.key" --licensee " " >/dev/null 2>&1; then echo "blank licensee accepted"; exit 1; fi
if "$LG" sign --key "$K/shop.private.key" --licensee "x" --seats 0 >/dev/null 2>&1; then echo "0 seats accepted"; exit 1; fi
echo "licensegen OK"
pass=$((pass + 1))

if python3 -c "import cryptography" 2>/dev/null; then
  step "Python reference implementation <-> C++ (both directions)"
  PY="$ROOT/tools/licensegen/celluloid_license.py"
  PYKEY="$(python3 "$PY" sign --key "$K/shop.private.key" --licensee "Zoë Øster 映画" --edition indie --id PY-1 --expires 2099-01-31)"
  "$LG" verify --pub "$K/shop.public.key" "$PYKEY" | grep -q "licensee: Zoë Øster 映画"
  python3 "$PY" verify --pub "$K/shop.public.key" "$KEY" | grep -q '"seats": 3'
  # identical inputs -> identical keys (Ed25519 is deterministic)
  A="$("$LG" sign --key "$K/shop.private.key" --licensee "Same" --id SAME-1 --issued @1791504000 --expires @1891504000)"
  P="$(python3 "$PY" sign --key "$K/shop.private.key" --licensee "Same" --id SAME-1 --issued @1791504000 --expires @1891504000)"
  [[ "$A" == "$P" ]] || { echo "Python and C++ produce different keys"; exit 1; }
  echo "Python reference OK"
  pass=$((pass + 1))
else
  skipped+=("Python cross-check (pip install cryptography)")
fi

step "watermark kernel as OpenCL C vs CPU"
if [[ -f /usr/include/CL/cl.h || -n "${OPENCL_INCLUDE:-}" ]] && "$CXX" -std=c++17 -O2 ${OPENCL_INCLUDE:+-I"$OPENCL_INCLUDE"} \
     -o "$B/opencl_check" "$HERE/opencl_watermark_check.cpp" -lOpenCL 2>/dev/null; then
  set +e; "$B/opencl_check" "$ROOT/src/kernels"; rc=$?; set -e
  if [[ $rc == 77 ]]; then skipped+=("OpenCL (no platform)"); elif [[ $rc != 0 ]]; then exit $rc; else pass=$((pass + 1)); fi
else
  skipped+=("OpenCL (no headers/ICD loader)")
fi

step "watermark kernel as CUDA device code (clang, codegen check)"
if command -v clang++ >/dev/null && clang++ -x cuda --cuda-device-only --cuda-gpu-arch=sm_52 -nocudainc -nocudalib \
     -std=c++17 -Wall -Wextra -Werror -Wno-unknown-cuda-version -S -o "$B/watermark.ptx" "$HERE/cuda_syntax_check.cu" 2>"$B/cuda.log"; then
  grep -q "cel_wm_font" "$B/watermark.ptx" && grep -q ".const" "$B/watermark.ptx"
  echo "CUDA device code OK (tables in __constant__ memory)"
  pass=$((pass + 1))
else
  if grep -q "error:" "$B/cuda.log" 2>/dev/null && ! grep -q "cannot find\|unsupported\|No available targets" "$B/cuda.log"; then
    cat "$B/cuda.log"; exit 1
  fi
  skipped+=("CUDA (clang without CUDA support)")
fi

if [[ "${FUZZ_SECONDS:-0}" -gt 0 ]]; then
  step "libFuzzer ${FUZZ_SECONDS}s"
  clang -g -O1 -fsanitize=fuzzer-no-link,address,undefined -c "${MONO[0]}" -o "$B/fz_mono.o"
  clang -g -O1 -fsanitize=fuzzer-no-link,address,undefined -c "${MONO[1]}" -o "$B/fz_mono_ed.o"
  clang++ -std=c++17 -g -O1 -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=undefined -o "$B/fuzz_verify" \
      "$HERE/fuzz_verify.cpp" "$ROOT/src/license/License.cpp" "$ROOT/src/license/KeyFormat.cpp" \
      "$ROOT/src/license/KeySigner.cpp" "$B/fz_mono.o" "$B/fz_mono_ed.o"
  mkdir -p "$B/corpus"
  echo "$KEY" > "$B/corpus/seed1"
  echo "$DEVKEY" > "$B/corpus/seed2"
  "$B/fuzz_verify" -max_total_time="$FUZZ_SECONDS" -max_len=2048 -jobs=1 "$B/corpus" 2>&1 | tail -3
  pass=$((pass + 1))
fi

printf '\n== license tests: %d groups passed' "$pass"
if ((${#skipped[@]})); then printf '; skipped: %s' "$(IFS=';'; echo "${skipped[*]}")"; fi
printf '\n   watermark review images: %s/images\n' "$B"
