#!/usr/bin/env bash
# Reafilm Pro CI: create the GitHub Release for a tag with all installers at once (run by the
# "release" job after both platform jobs succeeded); re-runs replace the assets of an existing
# release.
#   release_upload.sh <tag> <file>...
set -euo pipefail
TAG="${1:?tag}"; shift
[[ $# -gt 0 ]] || { echo "no files to upload" >&2; exit 1; }
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
VERSION="${TAG#v}"
NOTES="$(mktemp)"
if [[ -f "$ROOT/CHANGELOG.md" ]]; then
  # the section of this version: from "## [x.y.z]" or "## x.y.z" to the next "## "
  awk -v v="$VERSION" '
    /^## / { if (p) exit; if (index($0, v)) { p = 1; next } }
    p { print }' "$ROOT/CHANGELOG.md" > "$NOTES"
fi
if [[ ! -s "$NOTES" ]]; then
  printf 'Reafilm Pro %s\n\nInstallers for macOS (universal, Metal) and Windows (x64, CUDA/OpenCL).\n' "$VERSION" > "$NOTES"
fi
for attempt in 1 2 3 4 5; do
  if gh release view "$TAG" >/dev/null 2>&1; then
    gh release upload "$TAG" "$@" --clobber && exit 0
  else
    gh release create "$TAG" "$@" --title "Reafilm Pro $VERSION" --notes-file "$NOTES" --verify-tag && exit 0
  fi
  sleep $((attempt * 5))
done
echo "release upload failed" >&2
exit 1
