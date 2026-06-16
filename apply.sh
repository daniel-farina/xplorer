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

# Modern macOS prefers the asset-catalog icon (CFBundleIconName=AppIcon, compiled
# into Assets.car) over app.icns (CFBundleIconFile). Regenerate the appiconset
# PNGs from our icon too, or the build bakes the stock Chromium icon into the
# asset catalog and macOS shows that instead.
echo "Installing Xplorer asset-catalog app icon..."
ICONSET="$SRC/chrome/app/theme/chromium/mac/Assets.xcassets/AppIcon.appiconset"
if [ -d "$ICONSET" ]; then
  sips -s format png -z 1024 1024 "$XPLORER/branding/app.icns" --out "$XPLORER/branding/.appicon_master.png" >/dev/null 2>&1
  for sz in 16 32 64 128 256 512 1024; do
    sips -s format png -z "$sz" "$sz" "$XPLORER/branding/.appicon_master.png" \
      --out "$ICONSET/appicon_${sz}.png" >/dev/null 2>&1
  done
  rm -f "$XPLORER/branding/.appicon_master.png"
fi

echo "Installing Grok toolbar vector icon..."
cp "$XPLORER/branding/grok.icon" "$SRC/chrome/app/vector_icons/grok.icon"

echo "Applying integration edits..."
python3 "$XPLORER/patches/apply_integration.py" "$SRC"

echo "Done. Next: ./build.sh"
