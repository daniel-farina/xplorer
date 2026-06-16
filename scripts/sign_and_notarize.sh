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

APP="${1:?usage: sign_and_notarize.sh <Xplorer.app> [notary-profile]}"
PROFILE="${2:-xplorer-notary}"
IDENTITY="Developer ID Application: Daniel Farina (ST6RKUS2KP)"

[ -d "$APP" ] || { echo "No app bundle at $APP" >&2; exit 1; }
command -v xcrun >/dev/null || { echo "xcrun (Xcode CLT) required" >&2; exit 1; }

echo "==> Signing $APP with: $IDENTITY"

WORKDIR="$(mktemp -d)"
trap 'rm -rf "$WORKDIR"' EXIT

# Re-sign one Mach-O / bundle with Developer ID + hardened runtime, reusing the
# entitlements the Chromium build already embedded (correct per-process set).
sign_one() {
  local path="$1"
  local ent="$WORKDIR/ent.plist"
  rm -f "$ent"
  # Dump existing entitlements as XML; reuse them if present, else sign plain.
  if codesign -d --entitlements - --xml "$path" 2>/dev/null > "$ent" \
     && [ -s "$ent" ] && grep -q "<plist" "$ent"; then
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

echo "==> Notarizing (zip -> submit -> wait)"
ZIP="$(mktemp -d)/$(basename "${APP%.app}")-notarize.zip"
ditto -c -k --keepParent "$APP" "$ZIP"
xcrun notarytool submit "$ZIP" --keychain-profile "$PROFILE" --wait
rm -rf "$(dirname "$ZIP")"

echo "==> Stapling the notarization ticket to the app"
xcrun stapler staple "$APP"
xcrun stapler validate "$APP"
spctl -a -vvv -t exec "$APP" 2>&1 | head -4

echo "==> Done. $APP is Developer ID-signed, notarized, and stapled."
