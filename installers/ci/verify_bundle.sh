#!/usr/bin/env bash
# Celluloid Pro CI: structural checks of the built OFX bundle.
#   verify_bundle.sh <path/Celluloid.ofx.bundle> <macos|windows|linux>
# - bundle layout (Info.plist, icon, MANUAL.md, licence texts, binary in the OFX architecture folder)
# - exactly two exported symbols: OfxGetNumberOfPlugins, OfxGetPlugin
# - no unexpected runtime dependencies (static MSVC/CUDA runtimes, system libraries only)
# - macOS: universal arm64 + x86_64, deployment target 11.0
#   verify_bundle.sh --dev-key-only [--strict] <bundle>   (only the development-key check)
# - the DEVELOPMENT public key (tools/licensegen/dev-keys) must not be in the binary: a warning on
#   normal builds, an error with --strict (release gate on tags)
set -euo pipefail
DEVONLY=0
STRICT=0
while [[ "${1:-}" == --* ]]; do
  case "$1" in
    --dev-key-only) DEVONLY=1 ;;
    --strict) STRICT=1 ;;
    *) echo "unknown option $1" >&2; exit 2 ;;
  esac
  shift
done
B="${1:?bundle path}"
OS="${2:-}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
fail() { echo "::error::verify_bundle: $*" >&2; exit 1; }
ok() { echo "  ok: $*"; }

# development public key bytes in any plug-in binary of the bundle (all architecture slices of a
# universal binary are in the one file)
devkey_check() {
  local found=0 bin py
  py="$(command -v python3 || command -v python || true)"
  [[ -n "$py" ]] || fail "python3 is needed for the development-key check"
  for bin in "$B"/Contents/*/Celluloid.ofx; do
    [[ -f "$bin" ]] || continue
    if "$py" - "$ROOT/tools/licensegen/dev-keys/celluloid-dev.public.key" "$bin" <<'PYEOF'
import sys
pub = next(l.split(":", 1)[1].strip() for l in open(sys.argv[1]) if l.startswith("CELPUB1:"))
sys.exit(0 if bytes.fromhex(pub) in open(sys.argv[2], "rb").read() else 1)
PYEOF
    then
      found=1
      if [[ $STRICT == 1 ]]; then fail "the DEVELOPMENT public key is compiled into $bin (CEL_DEV_LICENSE build)"; fi
      echo "::warning::verify_bundle: the DEVELOPMENT public key is compiled into $bin (CEL_DEV_LICENSE build - never ship it)"
    fi
  done
  [[ $found == 1 ]] || ok "development public key absent"
}
if [[ $DEVONLY == 1 ]]; then
  devkey_check
  exit 0
fi
[[ -n "$OS" ]] || fail "usage: verify_bundle.sh <bundle> <macos|windows|linux>"

[[ -f "$B/Contents/Info.plist" ]] || fail "missing Contents/Info.plist"
[[ -f "$B/Contents/Resources/de.serhatbarda.CelluloidPro.png" ]] || fail "missing the plug-in icon"
grep -q "de.serhatbarda.CelluloidPro" "$B/Contents/Info.plist" || fail "Info.plist lacks the bundle identifier"
[[ -s "$B/Contents/Resources/MANUAL.md" ]] || fail "missing Contents/Resources/MANUAL.md (the About group points to it)"
for f in THIRD_PARTY_NOTICES.txt OpenFX-LICENSE.txt OpenFX-Support-LICENSE.txt Monocypher-LICENCE.txt; do
  [[ -s "$B/Contents/Resources/Licenses/$f" ]] || fail "missing Contents/Resources/Licenses/$f"
done
ok "layout (Info.plist, icon, MANUAL.md, licences)"

case "$OS" in
  macos)
    BIN="$B/Contents/MacOS/Celluloid.ofx"
    [[ -f "$BIN" ]] || fail "missing $BIN"
    archs=$(lipo -archs "$BIN")
    [[ "$archs" == *arm64* && "$archs" == *x86_64* ]] || fail "not universal: $archs"
    ok "architectures: $archs"
    for a in arm64 x86_64; do
      syms=$(nm -gU -arch "$a" "$BIN" | awk '{print $NF}' | sort | tr '\n' ' ')
      [[ "$syms" == "_OfxGetNumberOfPlugins _OfxGetPlugin " ]] || fail "$a exports: $syms"
      minos=$(otool -arch "$a" -l "$BIN" | awk '/LC_BUILD_VERSION/{f=1} f&&/minos/{print $2; exit}')
      [[ "$minos" == "11.0" ]] || fail "$a minimum macOS is '$minos' (expected 11.0)"
    done
    ok "exports: _OfxGetNumberOfPlugins _OfxGetPlugin; minos 11.0"
    for a in arm64 x86_64; do
      deps=$(otool -arch "$a" -L "$BIN" | tail -n +2 | awk '{print $1}' | grep -vE '^(/usr/lib/|/System/Library/)' || true)
      [[ -z "$deps" ]] || fail "$a: non-system dependencies: $deps"
    done
    ok "dependencies: system only"
    ;;
  windows)
    BIN="$B/Contents/Win64/Celluloid.ofx"
    [[ -f "$BIN" ]] || fail "missing $BIN"
    command -v dumpbin >/dev/null || fail "dumpbin not found (run inside the MSVC developer environment)"
    exports=$(dumpbin -nologo -exports "$BIN" | tr -d '\r' | awk '/ordinal +hint +RVA +name/{f=1;next} f&&NF>=4{print $4}' | sort | tr '\n' ' ')
    # NvOptimusEnablementCuda comes from the static CUDA runtime (cudart_static); it only asks
    # Optimus laptops to prefer the NVIDIA GPU and is expected when the CUDA backend is built.
    [[ "$exports" == "OfxGetNumberOfPlugins OfxGetPlugin " || "$exports" == "NvOptimusEnablementCuda OfxGetNumberOfPlugins OfxGetPlugin " ]] \
      || fail "exports: $exports"
    ok "exports: $exports"
    deps=$(dumpbin -nologo -dependents "$BIN" | tr -d '\r' | grep -iE '^\s+[A-Za-z0-9_.-]+\.dll$' | awk '{print $1}')
    echo "$deps" | sed 's/^/    /'
    bad=$(echo "$deps" | grep -iE '^(msvcp|vcruntime|ucrtbase|api-ms-win-crt|cudart|opencl|nvcuda)' || true)
    [[ -z "$bad" ]] || fail "unexpected DLL dependencies (runtimes must be static, GPU APIs loaded at run time): $bad"
    ok "dependencies: system DLLs only"
    ;;
  linux)
    BIN=$(ls "$B"/Contents/Linux-*/Celluloid.ofx 2>/dev/null | head -1)
    [[ -n "$BIN" ]] || fail "missing Contents/Linux-*/Celluloid.ofx"
    syms=$(nm -D --defined-only "$BIN" | awk '{print $NF}' | sort | tr '\n' ' ')
    [[ "$syms" == "OfxGetNumberOfPlugins OfxGetPlugin " ]] || fail "exports: $syms"
    ok "exports: $syms"
    ;;
  *) fail "unknown OS $OS" ;;
esac
devkey_check
echo "verify_bundle: $B OK"
