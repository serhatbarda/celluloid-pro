#!/usr/bin/env bash
# Celluloid Pro - macOS installer (.pkg) builder.
#
# Builds a signed (or ad-hoc signed) product archive that installs Celluloid.ofx.bundle into
# /Library/OFX/Plugins, the OFX plug-in folder DaVinci Resolve scans.
#
#   installers/macos/build_pkg.sh --bundle build/Celluloid.ofx.bundle --version 1.0.0 --out dist \
#       [--app-identity "Developer ID Application: NAME (TEAMID)"] \
#       [--installer-identity "Developer ID Installer: NAME (TEAMID)"] \
#       [--keychain /path/to/build.keychain] [--notarize]
#
# Notarization (--notarize) needs APPLE_ID, APPLE_TEAM_ID and APPLE_APP_PASSWORD (an app-specific
# password) in the environment and a Developer ID Installer identity. Without identities the
# plug-in is ad-hoc signed and the .pkg unsigned (fine for testing; Gatekeeper will warn).
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUNDLE=""
VERSION=""
OUT="dist"
APP_ID=""
INST_ID=""
KEYCHAIN=""
NOTARIZE=0
PKG_ID="de.serhatbarda.CelluloidPro.plugin"
BUNDLE_NAME="Celluloid.ofx.bundle"
INSTALL_DIR="/Library/OFX/Plugins"

die() { echo "build_pkg: $*" >&2; exit 1; }

while [[ $# -gt 0 ]]; do
  case "$1" in
    --bundle) BUNDLE="$2"; shift 2 ;;
    --version) VERSION="$2"; shift 2 ;;
    --out) OUT="$2"; shift 2 ;;
    --app-identity) APP_ID="$2"; shift 2 ;;
    --installer-identity) INST_ID="$2"; shift 2 ;;
    --keychain) KEYCHAIN="$2"; shift 2 ;;
    --notarize) NOTARIZE=1; shift ;;
    -h|--help) sed -n '2,16p' "$0"; exit 0 ;;
    *) die "unknown argument $1" ;;
  esac
done

[[ -n "$BUNDLE" && -d "$BUNDLE" ]] || die "--bundle <path to $BUNDLE_NAME> is required"
[[ -n "$VERSION" ]] || die "--version is required"
[[ "$(basename "$BUNDLE")" == "$BUNDLE_NAME" ]] || die "bundle must be named $BUNDLE_NAME"
BIN="$BUNDLE/Contents/MacOS/Celluloid.ofx"
[[ -f "$BIN" ]] || die "missing $BIN"
command -v pkgbuild >/dev/null || die "pkgbuild not found (run on macOS)"

mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/celluloid-pkg.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT

echo "== Architectures"
lipo -info "$BIN"

# ---- payload -------------------------------------------------------------------------------
mkdir -p "$WORK/root"
ditto "$BUNDLE" "$WORK/root/$BUNDLE_NAME"
find "$WORK/root" -name .DS_Store -delete
xattr -cr "$WORK/root/$BUNDLE_NAME" || true

# ---- code signing of the plug-in -------------------------------------------------------------
KC_ARGS=()
[[ -n "$KEYCHAIN" ]] && KC_ARGS=(--keychain "$KEYCHAIN")
if [[ -n "$APP_ID" ]]; then
  echo "== Signing plug-in with: $APP_ID"
  codesign --force --timestamp --options runtime ${KC_ARGS[@]+"${KC_ARGS[@]}"} --sign "$APP_ID" "$WORK/root/$BUNDLE_NAME"
else
  echo "== No Developer ID Application identity: ad-hoc signing the plug-in"
  codesign --force --sign - "$WORK/root/$BUNDLE_NAME"
fi
codesign --verify --strict --verbose=2 "$WORK/root/$BUNDLE_NAME"

# ---- component package -----------------------------------------------------------------------
mkdir -p "$WORK/scripts"
install -m 0755 "$HERE/scripts/preinstall" "$WORK/scripts/preinstall"
install -m 0755 "$HERE/scripts/postinstall" "$WORK/scripts/postinstall"

# The plug-in must always land in /Library/OFX/Plugins: never "relocate" it to another copy of
# a bundle with the same identifier that Installer.app may find elsewhere on the disk.
pkgbuild --analyze --root "$WORK/root" "$WORK/component.plist"
n=$(/usr/libexec/PlistBuddy -c "Print" "$WORK/component.plist" | grep -c "RootRelativeBundlePath" || true)
for ((i = 0; i < n; i++)); do
  # newer pkgbuild versions omit keys that are at their default: Set if present, else Add
  /usr/libexec/PlistBuddy -c "Set :$i:BundleIsRelocatable false" "$WORK/component.plist" 2>/dev/null ||
    /usr/libexec/PlistBuddy -c "Add :$i:BundleIsRelocatable bool false" "$WORK/component.plist"
  /usr/libexec/PlistBuddy -c "Set :$i:BundleOverwriteAction upgrade" "$WORK/component.plist" 2>/dev/null ||
    /usr/libexec/PlistBuddy -c "Add :$i:BundleOverwriteAction string upgrade" "$WORK/component.plist"
done

pkgbuild --root "$WORK/root" \
  --component-plist "$WORK/component.plist" \
  --identifier "$PKG_ID" \
  --version "$VERSION" \
  --install-location "$INSTALL_DIR" \
  --scripts "$WORK/scripts" \
  "$WORK/CelluloidPro-plugin.pkg"

# ---- product archive -------------------------------------------------------------------------
sed -e "s/@VERSION@/$VERSION/g" -e "s/@PKG_ID@/$PKG_ID/g" "$HERE/distribution.xml.in" > "$WORK/distribution.xml"
PRODUCT="$OUT/CelluloidPro-$VERSION-macOS.pkg"
rm -f "$PRODUCT"
PB_ARGS=(--distribution "$WORK/distribution.xml" --package-path "$WORK" --resources "$HERE/resources" --version "$VERSION")
if [[ -n "$INST_ID" ]]; then
  echo "== Signing installer with: $INST_ID"
  productbuild "${PB_ARGS[@]}" --sign "$INST_ID" ${KC_ARGS[@]+"${KC_ARGS[@]}"} --timestamp "$PRODUCT"
  pkgutil --check-signature "$PRODUCT"
else
  echo "== No Developer ID Installer identity: installer is unsigned"
  productbuild "${PB_ARGS[@]}" "$PRODUCT"
fi

# ---- notarization ------------------------------------------------------------------------------
if [[ "$NOTARIZE" == 1 ]]; then
  [[ -n "$INST_ID" ]] || die "--notarize requires --installer-identity"
  for v in APPLE_ID APPLE_TEAM_ID APPLE_APP_PASSWORD; do
    [[ -n "${!v:-}" ]] || die "--notarize requires \$$v"
  done
  echo "== Notarizing (this can take a few minutes)"
  set +e
  xcrun notarytool submit "$PRODUCT" --apple-id "$APPLE_ID" --team-id "$APPLE_TEAM_ID" \
    --password "$APPLE_APP_PASSWORD" --wait --timeout 45m --output-format json > "$WORK/notary.json"
  rc=$?
  set -e
  cat "$WORK/notary.json"; echo
  status=$(/usr/bin/plutil -extract status raw -o - "$WORK/notary.json" 2>/dev/null || echo "unknown")
  sub_id=$(/usr/bin/plutil -extract id raw -o - "$WORK/notary.json" 2>/dev/null || echo "")
  if [[ $rc -ne 0 || "$status" != "Accepted" ]]; then
    if [[ -n "$sub_id" ]]; then
      xcrun notarytool log "$sub_id" --apple-id "$APPLE_ID" --team-id "$APPLE_TEAM_ID" --password "$APPLE_APP_PASSWORD" || true
    fi
    die "notarization failed (status: $status)"
  fi
  xcrun stapler staple "$PRODUCT"
  xcrun stapler validate "$PRODUCT"
  spctl --assess --type install --verbose=2 "$PRODUCT" || true
fi

echo "== Package contents"
pkgutil --payload-files "$WORK/CelluloidPro-plugin.pkg" | head -50 || true
shasum -a 256 "$PRODUCT" | tee "$PRODUCT.sha256"
echo "Created $PRODUCT"
