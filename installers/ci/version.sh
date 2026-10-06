#!/usr/bin/env bash
# Celluloid Pro CI: read the product version from CMakeLists.txt, derive the build id, and make
# sure a release tag matches the version. Writes version= and build_id= to $GITHUB_OUTPUT.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
version=$(sed -nE 's/^project\(CelluloidPro VERSION ([0-9]+\.[0-9]+\.[0-9]+).*/\1/p' "$ROOT/CMakeLists.txt" | head -1)
[[ -n "$version" ]] || { echo "cannot read the project version from CMakeLists.txt" >&2; exit 1; }
sha="${GITHUB_SHA:-$(git -C "$ROOT" rev-parse HEAD 2>/dev/null || echo dev)}"
build_id="${sha:0:10}"
ref="${GITHUB_REF:-}"
if [[ "$ref" == refs/tags/v* ]]; then
  tag="${ref#refs/tags/}"
  if [[ "$tag" != "v$version" ]]; then
    echo "::error::tag $tag does not match the project version $version (expected v$version)" >&2
    exit 1
  fi
fi
echo "version=$version build_id=$build_id"
if [[ -n "${GITHUB_OUTPUT:-}" ]]; then
  echo "version=$version" >> "$GITHUB_OUTPUT"
  echo "build_id=$build_id" >> "$GITHUB_OUTPUT"
fi
