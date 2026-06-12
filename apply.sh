#!/bin/sh
# Overlay Aether onto the Chromium checkout.
set -eu
AETHER="$(cd "$(dirname "$0")" && pwd)"
SRC="${1:-$AETHER/../chromium/src}"

[ -d "$SRC/chrome" ] || { echo "Chromium src not found at $SRC" >&2; exit 1; }

echo "Copying new source files..."
cp -R "$AETHER/src/chrome" "$SRC/"

echo "Installing Aether app icon..."
cp "$AETHER/branding/app.icns" "$SRC/chrome/app/theme/chromium/mac/app.icns"

echo "Applying integration edits..."
python3 "$AETHER/patches/apply_integration.py" "$SRC"

echo "Done. Next: ./build.sh"
