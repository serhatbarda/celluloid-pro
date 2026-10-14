#!/bin/bash
# Reafilm Pro - one-line macOS install without the Gatekeeper prompt.
#
#   curl -fsSL https://raw.githubusercontent.com/serhatbarda/celluloid-pro/main/installers/macos/install.sh | bash
#   (a specific release: ... | bash -s -- v1.3.0)
#
# A file downloaded with curl carries no quarantine flag, so macOS does not block the unsigned
# installer; the installer's postinstall also clears the flag on the plug-in. Asks for the
# administrator password once (the plug-in goes to /Library/OFX/Plugins).
set -euo pipefail

REPO="serhatbarda/celluloid-pro"
TAG="${1:-}"

die() { echo "Reafilm Pro install: $*" >&2; exit 1; }
[ "$(uname -s)" = "Darwin" ] || die "this installer is for macOS"

if [ -z "$TAG" ]; then
  TAG=$(curl -fsSL "https://api.github.com/repos/$REPO/releases/latest" |
        sed -nE 's/^ *"tag_name": *"([^"]+)".*/\1/p' | head -1)
  [ -n "$TAG" ] || die "no release found on github.com/$REPO"
fi
VER="${TAG#v}"
URL="https://github.com/$REPO/releases/download/$TAG/ReafilmPro-$VER-macOS.dmg"

WORK=$(mktemp -d)
MNT="$WORK/mnt"
cleanup() {
  hdiutil detach "$MNT" -quiet 2>/dev/null || true
  rm -rf "$WORK"
}
trap cleanup EXIT

echo "Downloading Reafilm Pro $VER ..."
curl -fL --progress-bar -o "$WORK/reafilm.dmg" "$URL" || die "download failed: $URL"
mkdir -p "$MNT"
hdiutil attach "$WORK/reafilm.dmg" -nobrowse -readonly -mountpoint "$MNT" -quiet || die "cannot open the disk image"
PKG=$(ls "$MNT"/*.pkg 2>/dev/null | head -1)
[ -n "$PKG" ] || die "no installer in the disk image"

echo "Installing (macOS asks for your password) - quit DaVinci Resolve first ..."
sudo installer -pkg "$PKG" -target /
sudo xattr -dr com.apple.quarantine /Library/OFX/Plugins/ReafilmPro.ofx.bundle 2>/dev/null || true
echo "Done. Start Resolve: OpenFX > Reafilm > Reafilm Pro."
