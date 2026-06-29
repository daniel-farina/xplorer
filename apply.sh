#!/bin/sh
# Overlay Xplorer onto the Chromium checkout.
set -eu
XPLORER="$(cd "$(dirname "$0")" && pwd)"
SRC="${1:-$XPLORER/../chromium/src}"

[ -d "$SRC/chrome" ] || { echo "Chromium src not found at $SRC" >&2; exit 1; }

echo "Copying new source files..."
cp -R "$XPLORER/src/chrome" "$SRC/"

# Deletion-sync: cp -R is additive — a source file DELETED from the overlay would
# linger in the chromium tree forever and break a clean rebuild (a stale .cc keeps
# compiling, a deleted .h stays #included). The three xplorer source dirs are
# PURE-overlay (they don't exist upstream), so mirror them with rsync --delete so
# the chromium copy exactly matches the overlay. (Keep the cp -R above for the
# non-pure overlay files — branding, theme images — where delete-sync is unsafe.)
for d in browser/agent_gateway browser/ui/views/xplorer browser/grok_companion; do
  if [ -d "$XPLORER/src/chrome/$d" ]; then
    rsync -a --delete "$XPLORER/src/chrome/$d/" "$SRC/chrome/$d/"
  fi
done

# macOS-only icon work (sips, xcrun actool, .icns, chrome/app/theme/chromium/mac
# paths) — skipped on Linux/Windows, which don't have these tools/paths.
if [ "$(uname)" = "Darwin" ]; then
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
fi  # end macOS-only app-icon block

# Cross-platform: the Grok toolbar vector icon is referenced by BUILD.gn (added
# by apply_integration.py), so it must be copied on every platform.
echo "Installing Grok toolbar vector icon..."
cp "$XPLORER/branding/grok.icon" "$SRC/chrome/app/vector_icons/grok.icon"

# Product logo shown on chrome://settings/help (About page). The About logo is
# chrome://theme/current-channel-logo -> IDR_PRODUCT_LOGO_32, a chrome_scaled_image
# whose REAL grit sources are the scaled theme dirs (default_100_percent =NxN,
# default_200_percent =2Nx2N), NOT chrome/app/theme/chromium/. We overwrite all
# three from the transparent Xplorer mark.
# The same Xplorer product logos are committed verbatim into the overlay
# (src/chrome/app/theme/.../product_logo_*.png), so EVERY platform — including
# Linux, where `sips` isn't available — gets the Xplorer About-page logo when
# apply.sh copies src/chrome. The macOS `sips` pass below just re-renders
# identical output over those copied files (kept so the mark stays the source).
if [ "$(uname)" = "Darwin" ]; then
echo "Installing Xplorer product logos..."
LOGO_SRC="$XPLORER/branding/xplorer-mark-1024.png"
if [ -f "$LOGO_SRC" ]; then
  # Base theme dir (packaging / misc UI surfaces).
  for sz in 16 24 32 48 64 128 256; do
    sips -s format png -z "$sz" "$sz" "$LOGO_SRC" \
      --out "$SRC/chrome/app/theme/chromium/product_logo_${sz}.png" >/dev/null 2>&1
  done
  # Scaled grit sources actually used by IDR_PRODUCT_LOGO_* (incl. the About
  # page). 100% dir = NxN; 200% dir = 2Nx2N. Only 16 and 32 exist here.
  for sz in 16 32; do
    sips -s format png -z "$sz" "$sz" "$LOGO_SRC" \
      --out "$SRC/chrome/app/theme/default_100_percent/chromium/product_logo_${sz}.png" >/dev/null 2>&1
    sips -s format png -z "$((sz * 2))" "$((sz * 2))" "$LOGO_SRC" \
      --out "$SRC/chrome/app/theme/default_200_percent/chromium/product_logo_${sz}.png" >/dev/null 2>&1
  done
  echo "  installed product logos (base + default_100/200_percent scaled dirs)"
else
  echo "  WARNING: $LOGO_SRC not found — product logos NOT updated" >&2
fi
fi  # end macOS-only product-logo block

# macOS-only: stage the vendored Sparkle.framework INTO the chromium tree so GN
# can resolve it at gn-gen/link time. chrome/browser/BUILD.gn (patched by
# apply_integration.py) sets framework_dirs = //third_party/sparkle and links
# Sparkle.framework, which gives xplorer_sparkle_updater.mm <Sparkle/Sparkle.h>
# at compile time and emits the LC_LOAD_DYLIB (@rpath/Sparkle.framework/...) at
# link time. This is the LINK-time copy; build.sh / release_arch.sh separately
# ditto the SAME framework into the bundle's Contents/Frameworks for RUNTIME
# presence (different purpose). third_party/sparkle is untracked in the chromium
# tree, so `git checkout -- .` leaves it; ditto preserves the Versions/Current
# symlink layout a plain cp -R would mangle. (Linux/Windows don't ship Sparkle.)
if [ "$(uname)" = "Darwin" ]; then
echo "Staging Sparkle.framework into chromium tree (link-time)..."
SPARKLE_TREE="$SRC/third_party/sparkle"
mkdir -p "$SPARKLE_TREE"
rm -rf "$SPARKLE_TREE/Sparkle.framework"
ditto "$XPLORER/third_party/Sparkle/Sparkle.framework" "$SPARKLE_TREE/Sparkle.framework"
echo "  staged -> $SPARKLE_TREE/Sparkle.framework"
fi  # end macOS-only Sparkle link-time staging

echo "Applying integration edits..."
python3 "$XPLORER/patches/apply_integration.py" "$SRC"

echo "Done. Next: ./build.sh"
