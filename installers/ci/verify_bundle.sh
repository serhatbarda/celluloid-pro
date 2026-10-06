#!/usr/bin/env bash
# Celluloid Pro CI: structural checks of the built OFX bundle.
#   verify_bundle.sh <path/Celluloid.ofx.bundle> <macos|windows|linux>
# - bundle layout (Info.plist, icon, binary in the OFX architecture folder)
# - exactly two exported symbols: OfxGetNumberOfPlugins, OfxGetPlugin
# - no unexpected runtime dependencies (static MSVC/CUDA runtimes, system libraries only)
# - macOS: universal arm64 + x86_64, deployment target 11.0
set -euo pipefail
B="${1:?bundle path}"
OS="${2:?macos|windows|linux}"
fail() { echo "::error::verify_bundle: $*" >&2; exit 1; }
ok() { echo "  ok: $*"; }

[[ -f "$B/Contents/Info.plist" ]] || fail "missing Contents/Info.plist"
[[ -f "$B/Contents/Resources/de.serhatbarda.CelluloidPro.png" ]] || fail "missing the plug-in icon"
grep -q "de.serhatbarda.CelluloidPro" "$B/Contents/Info.plist" || fail "Info.plist lacks the bundle identifier"
ok "layout"

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
    [[ "$exports" == "OfxGetNumberOfPlugins OfxGetPlugin " ]] || fail "exports: $exports"
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
echo "verify_bundle: $B OK"
