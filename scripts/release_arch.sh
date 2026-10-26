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

APP="$SRC/out/$OUT_DIR/Xplor.app"
DIST="$XPLORER/dist"

echo "==> [$ARCH] Applying overlay"
"$XPLORER/apply.sh" "$SRC"

echo "==> [$ARCH] Building"
"$XPLORER/build.sh" "$SRC" "$ARCH"

# XPLOR: the build emits Xplorer.app (PRODUCT_FULLNAME=Xplorer is kept so the
# user-data dir, Keychain ACL, bundle ID, and Sparkle identity are all unchanged).
# Rename only the VISIBLE bundle to Xplor.app for the shipped app — the executable,
# framework, and bundle ID inside stay Xplorer. The signature seals Contents/, not
# the folder name, so this rename keeps any existing signature/staple valid.
BUILT_APP="$SRC/out/$OUT_DIR/Xplorer.app"
if [ -d "$BUILT_APP" ]; then rm -rf "$APP"; mv "$BUILT_APP" "$APP"; fi

# The build does not refresh the bundled companion UI (it persists across builds),
# so copy the current companion/ui into the bundle BEFORE signing — otherwise the
# release ships stale sidebar UI.
echo "==> [$ARCH] Refreshing bundled companion UI"
UI_DST="$APP/Contents/Resources/companion/ui"
mkdir -p "$(dirname "$UI_DST")"
rm -rf "$UI_DST"
cp -R "$XPLORER/companion/ui" "$UI_DST"

# XPLOR: bundle the MCP servers (sdk/*.py) so the grok provisioner can write a
# ~/.grok/config.toml pointing at them on machines without a dev checkout.
echo "==> [$ARCH] Bundling sdk (MCP servers for the grok provisioner)"
SDK_DST="$APP/Contents/Resources/sdk"
rm -rf "$SDK_DST"
mkdir -p "$SDK_DST"
cp "$XPLORER/sdk"/*.py "$SDK_DST/" 2>/dev/null || true

# Stage Sparkle.framework into the bundle BEFORE signing so it is covered by the
# signature. ditto preserves the Versions/Current symlink layout that a plain
# cp -R would mangle.
echo "==> [$ARCH] Staging Sparkle.framework"
SPARKLE_DST="$APP/Contents/Frameworks/Sparkle.framework"
rm -rf "$SPARKLE_DST"
ditto "$XPLORER/third_party/Sparkle/Sparkle.framework" "$SPARKLE_DST"

# Marketing version for the bundle's CFBundleShortVersionString (what Sparkle's
# update dialog displays + what generate_appcast puts in sparkle:shortVersionString).
# Chromium defaults it to the full engine version (151.0.7897.x); set it to OUR
# version (e.g. 0.8.7) so the dialog reads "Xplorer 0.8.7", not "151.0.7897.807".
# CFBundleVersion (the 7897.<patch> Sparkle COMPARES) is left untouched. Must run
# BEFORE signing so the edited Info.plist is covered by the signature.
echo "==> [$ARCH] Setting CFBundleShortVersionString -> $VERSION"
/usr/libexec/PlistBuddy -c "Set :CFBundleShortVersionString $VERSION" "$APP/Contents/Info.plist"

echo "==> [$ARCH] Signing app"
if [ -n "$SIGN_ONLY" ]; then
  "$XPLORER/scripts/sign_and_notarize.sh" "$APP" xplorer-notary --sign-only
else
  "$XPLORER/scripts/sign_and_notarize.sh" "$APP" xplorer-notary
fi

echo "==> [$ARCH] Packaging"
"$XPLORER/scripts/package.sh" "$SRC/out/$OUT_DIR" "$VERSION"

BIN_ARCH="$("$XPLORER/scripts/app_arch.sh" "$APP")"
NAME="Xplor-macos-$BIN_ARCH"

if [ -z "$SIGN_ONLY" ]; then
  echo "==> [$ARCH] Notarizing DMG"
  "$XPLORER/scripts/notarize_dmg.sh" "$DIST/$NAME.dmg"
  echo "==> [$ARCH] Regenerating checksums (post-staple)"
  ( cd "$DIST" && shasum -a 256 "$NAME.zip" "$NAME.dmg" > "$NAME.sha256.txt" )
fi

echo "==> [$ARCH] Artifacts:"
ls -lh "$DIST/$NAME".*