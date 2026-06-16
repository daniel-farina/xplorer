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

# Modern macOS prefers the asset-catalog icon (CFBundleIconName=AppIcon, in
# Assets.car) over app.icns (CFBundleIconFile). CRITICAL: the build does NOT run
# actool on the appiconset — chrome/BUILD.gn's chrome_asset_catalog is a
# bundle_data that COPIES a prebuilt, checked-in Assets.car
# (app/theme/chromium/mac/Assets.car) straight into the bundle. So we must (1)
# regenerate the appiconset PNGs from our icon AND (2) recompile Assets.car
# ourselves and overwrite that prebuilt file — editing the appiconset alone does
# nothing, which is why the stock Chromium icon kept showing.
echo "Installing Xplorer asset-catalog app icon..."
ASSETS_XC="$SRC/chrome/app/theme/chromium/mac/Assets.xcassets"
ICONSET="$ASSETS_XC/AppIcon.appiconset"
if [ -d "$ICONSET" ]; then
  sips -s format png -z 1024 1024 "$XPLORER/branding/app.icns" --out "$XPLORER/branding/.appicon_master.png" >/dev/null 2>&1
  for sz in 16 32 64 128 256 512 1024; do
    sips -s format png -z "$sz" "$sz" "$XPLORER/branding/.appicon_master.png" \
      --out "$ICONSET/appicon_${sz}.png" >/dev/null 2>&1
  done
  rm -f "$XPLORER/branding/.appicon_master.png"
  # Recompile Assets.car from our appiconset and replace the prebuilt one the
  # build actually ships.
  CAR_TMP="$(mktemp -d)"
  if xcrun actool --compile "$CAR_TMP" --app-icon AppIcon --platform macosx \
       --minimum-deployment-target 11.0 \
       --output-partial-info-plist "$CAR_TMP/partial.plist" \
       "$ASSETS_XC" >/dev/null 2>&1 && [ -f "$CAR_TMP/Assets.car" ]; then
    cp "$CAR_TMP/Assets.car" "$SRC/chrome/app/theme/chromium/mac/Assets.car"
    echo "  recompiled Assets.car ($(stat -f%z "$SRC/chrome/app/theme/chromium/mac/Assets.car") bytes)"
  else
    echo "  WARNING: actool failed — Assets.car NOT updated; icon will be wrong" >&2
  fi
  rm -rf "$CAR_TMP"
fi

echo "Installing Grok toolbar vector icon..."
cp "$XPLORER/branding/grok.icon" "$SRC/chrome/app/vector_icons/grok.icon"

echo "Applying integration edits..."
python3 "$XPLORER/patches/apply_integration.py" "$SRC"

echo "Done. Next: ./build.sh"
