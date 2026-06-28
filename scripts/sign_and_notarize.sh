#!/bin/bash
# Developer ID-sign, notarize, and staple a built Xplorer.app so it opens on
# any Mac without the "damaged / unidentified developer" Gatekeeper block.
#
# Usage:
#   scripts/sign_and_notarize.sh <path-to-Xplorer.app> [notary-keychain-profile]
#
# Prereqs (one time): store notarization credentials in the keychain so this
# script (and you) never handle the password inline:
#   xcrun notarytool store-credentials "xplorer-notary" \
#       --apple-id "<your-apple-id-email>" \
#       --team-id  "ST6RKUS2KP" \
#       --password "<app-specific-password>"   # from appleid.apple.com
set -euo pipefail

APP="${1:?usage: sign_and_notarize.sh <Xplorer.app> [notary-profile] [--sign-only]}"
PROFILE="${2:-xplorer-notary}"
SIGN_ONLY=""
for a in "$@"; do [ "$a" = "--sign-only" ] && SIGN_ONLY=1; done
IDENTITY="Developer ID Application: Daniel Farina (ST6RKUS2KP)"
ENT_DIR="$(cd "$(dirname "$0")" && pwd)/entitlements"

# Notarization auth: prefer the App Store Connect API key (from env) — it works
# non-interactively, whereas a keychain profile needs a one-time UI auth approval
# that fails in scripts/cron ("No Keychain password item found ..."). Falls back
# to the keychain profile when the env key isn't provided. No secrets live in the
# repo; the key path/id/issuer come from the environment (see the local build doc).
NOTARY_KEY="${XPLORER_NOTARY_KEY:-}"
NOTARY_KEY_ID="${XPLORER_NOTARY_KEY_ID:-}"
NOTARY_ISSUER="${XPLORER_NOTARY_ISSUER:-}"
if [ -n "$NOTARY_KEY" ] && [ -f "$NOTARY_KEY" ] && [ -n "$NOTARY_KEY_ID" ] && [ -n "$NOTARY_ISSUER" ]; then
  NOTARY_AUTH=(--key "$NOTARY_KEY" --key-id "$NOTARY_KEY_ID" --issuer "$NOTARY_ISSUER")
  NOTARY_AUTH_DESC="API key $NOTARY_KEY_ID"
else
  NOTARY_AUTH=(--keychain-profile "$PROFILE")
  NOTARY_AUTH_DESC="keychain profile $PROFILE"
fi

[ -d "$APP" ] || { echo "No app bundle at $APP" >&2; exit 1; }
command -v xcrun >/dev/null || { echo "xcrun (Xcode CLT) required" >&2; exit 1; }

# Bundle the companion UI INTO the app so the gateway is self-contained on any
# machine. The build does not include it, and UiDir() resolves it from
# Contents/Resources/companion/ui. This MUST run before signing so the files
# are covered by the signature (adding them after would invalidate it). Without
# it, a downloaded app has no UI on disk and /search etc. 401 with
# "missing or invalid bearer token".
UI_SRC="$(cd "$(dirname "$0")/.." && pwd)/companion/ui"
if [ -d "$UI_SRC" ]; then
  echo "==> Bundling companion UI into $APP/Contents/Resources/companion/ui"
  rm -rf "$APP/Contents/Resources/companion"
  mkdir -p "$APP/Contents/Resources/companion"
  cp -R "$UI_SRC" "$APP/Contents/Resources/companion/ui"
else
  echo "WARNING: companion UI not found at $UI_SRC — app will not be self-contained" >&2
fi

echo "==> Signing $APP with: $IDENTITY"

# Pick the right entitlements per bundle, mirroring Chromium's own signing.
# CRITICAL: the renderer and GPU helpers run V8 and need com.apple.security.cs.
# allow-jit under hardened runtime, or they crash on launch ("Can't open this
# page / Error code 5"). The ad-hoc build embeds NO entitlements, so we must
# apply them by part here — reusing embedded ones would grant nothing.
ent_for() {
  case "$1" in
    *"Helper (Renderer).app") echo "$ENT_DIR/helper-renderer.plist" ;;
    *"Helper (GPU).app")      echo "$ENT_DIR/helper-gpu.plist" ;;
    "$APP")                   echo "$ENT_DIR/app.plist" ;;
    *)                        echo "" ;;   # base/alerts helpers, loose exes, dylibs, framework
  esac
}

# Re-sign one Mach-O / bundle with Developer ID + hardened runtime + the
# part-appropriate entitlements.
sign_one() {
  local path="$1" ent
  # XPLORER: Sparkle.framework and all its nested code (Updater.app, Autoupdate,
  # Installer.xpc, Downloader.xpc) ship their OWN entitlements that must survive
  # re-signing. Sign every Sparkle component with hardened runtime + a secure
  # timestamp and --preserve-metadata=entitlements, never the app entitlements.
  # The existing inside-out loop visits these parts deepest-first already, so we
  # only special-case the entitlement handling here.
  case "$path" in
    *"/Sparkle.framework/"*|*"/Sparkle.framework")
      codesign --force --timestamp --options runtime \
               --preserve-metadata=entitlements -s "$IDENTITY" "$path"
      return ;;
  esac
  ent="$(ent_for "$path")"
  if [ -n "$ent" ] && [ -f "$ent" ]; then
    codesign --force --timestamp --options runtime \
             --entitlements "$ent" -s "$IDENTITY" "$path"
  else
    codesign --force --timestamp --options runtime -s "$IDENTITY" "$path"
  fi
}

# Inside-out: sign the deepest nested code first, the outer .app last.
# 1) ALL loose Mach-O files — dylibs, .so, AND bare executables with no
#    extension (e.g. chrome_crashpad_handler, app_mode_loader,
#    web_app_shortcut_copier in the framework's Helpers/). Deepest path first.
while IFS= read -r f; do
  if file "$f" | grep -q "Mach-O"; then sign_one "$f"; fi
done < <(find "$APP" -type f | awk '{print length, $0}' | sort -rn | cut -d" " -f2-)

# 2) nested bundles (helper .app, .xpc, .framework), deepest path first.
while IFS= read -r b; do
  sign_one "$b"
done < <(find "$APP" -type d \( -name "*.app" -o -name "*.xpc" -o -name "*.framework" \) ! -path "$APP" | awk '{print length, $0}' | sort -rn | cut -d" " -f2-)

# 3) the outer app last.
sign_one "$APP"

echo "==> Verifying signature + Gatekeeper"
codesign --verify --deep --strict --verbose=2 "$APP"
spctl -a -vvv -t exec "$APP" 2>&1 | head -4 || true   # may report "rejected" until notarized+stapled

if [ -n "$SIGN_ONLY" ]; then
  echo "==> --sign-only: signed (not notarized). Test locally, then run without --sign-only."
  exit 0
fi

echo "==> Notarizing (zip -> submit -> wait) via $NOTARY_AUTH_DESC"
ZIP="$(mktemp -d)/$(basename "${APP%.app}")-notarize.zip"
ditto -c -k --keepParent "$APP" "$ZIP"
xcrun notarytool submit "$ZIP" "${NOTARY_AUTH[@]}" --wait
rm -rf "$(dirname "$ZIP")"

echo "==> Stapling the notarization ticket to the app"
xcrun stapler staple "$APP"
xcrun stapler validate "$APP"
spctl -a -vvv -t exec "$APP" 2>&1 | head -4

echo "==> Done. $APP is Developer ID-signed, notarized, and stapled."
