#!/bin/sh
# Package the built Xplor.app into distributable .zip + .dmg with checksums.
# (The build emits Xplorer.app — PRODUCT_FULLNAME, kept for profile/update identity;
# release_arch.sh renames the visible bundle to Xplor.app before packaging.)
# Usage: ./scripts/package.sh [path-to-out-dir] [version]
set -eu
XPLORER="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${1:-$XPLORER/../chromium/src/out/aether}"
VERSION="${2:-dev}"
APP="$OUT/Xplor.app"
DIST="$XPLORER/dist"

[ -d "$APP" ] || { echo "No Xplor.app at $APP — build first." >&2; exit 1; }
ARCH="$("$XPLORER/scripts/app_arch.sh" "$APP")"
NAME="Xplor-macos-$ARCH"
mkdir -p "$DIST"

echo "Packaging $NAME (version $VERSION)..."
echo "Zipping..."
ditto -c -k --keepParent "$APP" "$DIST/$NAME.zip"

echo "Building DMG..."
TMP="$(mktemp -d)"
cp -R "$APP" "$TMP/"
ln -s /Applications "$TMP/Applications"
hdiutil create -volname "Xplor" -srcfolder "$TMP" -ov -format UDZO \
  "$DIST/$NAME.dmg" >/dev/null
rm -rf "$TMP"

echo "Checksums..."
( cd "$DIST" && shasum -a 256 "$NAME.zip" "$NAME.dmg" > "$NAME.sha256.txt" )

echo "Artifacts in $DIST:"
ls -lh "$DIST" | grep "$ARCH"
echo "version: $VERSION"