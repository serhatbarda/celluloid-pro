#!/usr/bin/env bash
# Reafilm Pro render tests (SPEC §10).
#
#   tools/render_tests/run_all.sh <ReafilmPro.ofx.bundle> <outdir> [run_all.py options]
#
# Options are passed to run_all.py: --quick, --full, --no-gpu, --threads N, --license-key KEY,
# --dev-license, --only GROUPS, -v.  The ofxhost binary is taken from $OFXHOST, else from the
# usual build directories, else it is built into <outdir>/_ofxhost_build.
# Requires python3 with numpy and Pillow (and 'cryptography' for --dev-license).
set -euo pipefail

if [[ $# -lt 2 ]]; then
  sed -n '2,9p' "$0" | sed 's/^# \{0,1\}//'
  exit 2
fi
BUNDLE="$1"
OUT="$2"
shift 2
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
mkdir -p "$OUT"

python3 - <<'EOF'
import sys
try:
    import numpy, PIL  # noqa: F401
except ImportError as e:
    sys.exit("render tests need numpy and Pillow: %s (pip install numpy Pillow)" % e)
EOF

HOSTBIN="${OFXHOST:-}"
if [[ -z "$HOSTBIN" ]]; then
  for c in "$ROOT/build/tools/ofxhost/ofxhost" "$ROOT/build/ofxhost" /tmp/cel-host/ofxhost "$OUT/_ofxhost_build/ofxhost"; do
    if [[ -x "$c" ]]; then HOSTBIN="$c"; break; fi
  done
fi
if [[ -z "$HOSTBIN" || ! -x "$HOSTBIN" ]]; then
  echo "building ofxhost into $OUT/_ofxhost_build ..."
  GEN=()
  command -v ninja >/dev/null 2>&1 && GEN=(-G Ninja)
  cmake -S "$ROOT/tools/ofxhost" -B "$OUT/_ofxhost_build" "${GEN[@]}" -DCMAKE_BUILD_TYPE=Release \
        -DOFXHOST_BUILD_SDK_PLUGINS=OFF >/dev/null
  cmake --build "$OUT/_ofxhost_build" --target ofxhost -j"$(nproc 2>/dev/null || echo 4)" >/dev/null
  HOSTBIN="$OUT/_ofxhost_build/ofxhost"
fi
echo "ofxhost: $HOSTBIN"
export PYTHONDONTWRITEBYTECODE=1
exec python3 "$HERE/run_all.py" --host "$HOSTBIN" --bundle "$BUNDLE" --out "$OUT" "$@"
