#!/bin/bash
# End-to-end release pipeline for one architecture: build → sign app → package →
# notarize DMG → write checksums.
#
# Usage: scripts/release_arch.sh <arm64|x64> <version> [chromium-src] [--sign-only]
#
# Example:
#   scripts/release_arch.sh x64 v0.5.0 ../chromium/src
#   scripts/release_arch.sh arm64 v0.5.0
set -euo pipefail

ARCH="${1:?usage: release_arch.sh <arm64|x64> <version> [chromium-src] [--sign-only]}"
VERSION="${2:?usage: release_arch.sh <arm64|x64> <version> [chromium-src] [--sign-only]}"
XPLORER="$(cd "$(dirname "$0")/.." && pwd)"
SRC="${3:-$XPLORER/../chromium/src}"
SIGN_ONLY=""
for a in "$@"; do [ "$a" = "--sign-only" ] && SIGN_ONLY=1; done

case "$ARCH" in
  arm64) OUT_DIR=aether ;;
  x64|x86_64|intel) ARCH=x64; OUT_DIR=aether_x64 ;;
  *) echo "Unknown arch: $ARCH" >&2; exit 1 ;;
esac

APP="$SRC/out/$OUT_DIR/Xplorer.app"
DIST="$XPLORER/dist"

echo "==> [$ARCH] Applying overlay"
"$XPLORER/apply.sh" "$SRC"

echo "==> [$ARCH] Building"
"$XPLORER/build.sh" "$SRC" "$ARCH"

echo "==> [$ARCH] Signing app"
if [ -n "$SIGN_ONLY" ]; then
  "$XPLORER/scripts/sign_and_notarize.sh" "$APP" xplorer-notary --sign-only
else
  "$XPLORER/scripts/sign_and_notarize.sh" "$APP" xplorer-notary
fi

echo "==> [$ARCH] Packaging"
"$XPLORER/scripts/package.sh" "$SRC/out/$OUT_DIR" "$VERSION"

BIN_ARCH="$("$XPLORER/scripts/app_arch.sh" "$APP")"
NAME="Xplorer-macos-$BIN_ARCH"

if [ -z "$SIGN_ONLY" ]; then
  echo "==> [$ARCH] Notarizing DMG"
  "$XPLORER/scripts/notarize_dmg.sh" "$DIST/$NAME.dmg"
  echo "==> [$ARCH] Regenerating checksums (post-staple)"
  ( cd "$DIST" && shasum -a 256 "$NAME.zip" "$NAME.dmg" > "$NAME.sha256.txt" )
fi

echo "==> [$ARCH] Artifacts:"
ls -lh "$DIST/$NAME".*