#!/usr/bin/env bash
# Reafilm Pro CI: licence gate for releases of the LICENSED build (tags v*, CEL_LICENSING=ON).
# Normal main/PR builds and the personal build never run it.
#   release_gate.sh <build dir> <path/ReafilmPro.ofx.bundle>
# Refuses to publish when
#   - src/license/PublicKey.h still holds the PROVISIONAL production key (its private half was
#     generated on a development machine; regenerate with tools/licensegen on the owner's machine),
#   - the build was configured with CEL_DEV_LICENSE=ON (accepts keys anyone with access to the
#     repository can sign with the committed development key),
#   - the development public key bytes occur in the plug-in binary (every architecture slice).
set -euo pipefail
BUILD="${1:?build dir}"
B="${2:?bundle path}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
fail() { echo "::error::release gate: $*" >&2; exit 1; }

if grep -q 'kProductionKeyProvisional = true' "$ROOT/src/license/PublicKey.h"; then
  fail "src/license/PublicKey.h holds the PROVISIONAL production key - regenerate the production keypair with tools/licensegen on the owner's machine (see tools/licensegen/README.md) before tagging a release"
fi
echo "  ok: production public key is final"

CACHE="$BUILD/CMakeCache.txt"
[[ -f "$CACHE" ]] || fail "no $CACHE"
if grep -qE '^CEL_DEV_LICENSE:BOOL=(ON|1|TRUE|YES)' "$CACHE"; then
  fail "the build was configured with CEL_DEV_LICENSE=ON (development key accepted)"
fi
echo "  ok: CEL_DEV_LICENSE is off"

bash "$ROOT/installers/ci/verify_bundle.sh" --dev-key-only --strict "$B"
echo "release gate: OK"
