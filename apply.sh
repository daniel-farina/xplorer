#!/bin/sh
# Overlay Xplorer onto the Chromium checkout.
set -eu
XPLORER="$(cd "$(dirname "$0")" && pwd)"
SRC="${1:-$XPLORER/../chromium/src}"

[ -d "$SRC/chrome" ] || { echo "Chromium src not found at $SRC" >&2; exit 1; }

echo "Copying new source files..."
cp -R "$XPLORER/src/chrome" "$SRC/"

echo "Installing Xplorer app icon..."
cp "$XPLORER/branding/app.icns" "$SRC/chrome/app/theme/chromium/mac/app.icns"

echo "Installing Grok toolbar vector icon..."
cp "$XPLORER/branding/grok.icon" "$SRC/chrome/app/vector_icons/grok.icon"

echo "Applying integration edits..."
python3 "$XPLORER/patches/apply_integration.py" "$SRC"

echo "Done. Next: ./build.sh"
