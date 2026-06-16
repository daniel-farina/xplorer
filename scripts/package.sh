#!/bin/sh
# Package a built Xplorer.app into distributable .zip + .dmg with checksums.
# Usage: ./scripts/package.sh [path-to-out-dir] [version]
set -eu
XPLORER="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${1:-$XPLORER/../chromium/src/out/xplorer}"
VERSION="${2:-dev}"
APP="$OUT/Xplorer.app"
DIST="$XPLORER/dist"
ARCH="$(uname -m)"           # arm64 or x86_64
NAME="Xplorer-macos-$ARCH"

[ -d "$APP" ] || { echo "No Xplorer.app at $APP — build first." >&2; exit 1; }
mkdir -p "$DIST"

echo "Zipping..."
ditto -c -k --keepParent "$APP" "$DIST/$NAME.zip"

echo "Building DMG..."
TMP="$(mktemp -d)"
cp -R "$APP" "$TMP/"
ln -s /Applications "$TMP/Applications"
hdiutil create -volname "Xplorer" -srcfolder "$TMP" -ov -format UDZO \
  "$DIST/$NAME.dmg" >/dev/null
rm -rf "$TMP"

echo "Checksums..."
( cd "$DIST" && shasum -a 256 "$NAME.zip" "$NAME.dmg" > "$NAME.sha256.txt" )

echo "Artifacts in $DIST:"
ls -lh "$DIST" | grep "$ARCH"
echo "version: $VERSION"
