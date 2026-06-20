#!/bin/bash
# Notarize and staple a release DMG (DMGs are not code-signed; notarization only).
# Usage: scripts/notarize_dmg.sh <path-to.dmg> [notary-keychain-profile]
set -euo pipefail

DMG="${1:?usage: notarize_dmg.sh <Xplorer.dmg> [notary-profile]}"
PROFILE="${2:-xplorer-notary}"

[ -f "$DMG" ] || { echo "No DMG at $DMG" >&2; exit 1; }

# Prefer the App Store Connect API key (from env) — works non-interactively,
# whereas the keychain profile needs a UI auth approval that fails in scripts.
NOTARY_KEY="${XPLORER_NOTARY_KEY:-}"
NOTARY_KEY_ID="${XPLORER_NOTARY_KEY_ID:-}"
NOTARY_ISSUER="${XPLORER_NOTARY_ISSUER:-}"
if [ -n "$NOTARY_KEY" ] && [ -f "$NOTARY_KEY" ] && [ -n "$NOTARY_KEY_ID" ] && [ -n "$NOTARY_ISSUER" ]; then
  NOTARY_AUTH=(--key "$NOTARY_KEY" --key-id "$NOTARY_KEY_ID" --issuer "$NOTARY_ISSUER")
  echo "==> Notarizing DMG via API key $NOTARY_KEY_ID: $DMG"
else
  NOTARY_AUTH=(--keychain-profile "$PROFILE")
  echo "==> Notarizing DMG via keychain profile $PROFILE: $DMG"
fi
xcrun notarytool submit "$DMG" "${NOTARY_AUTH[@]}" --wait

echo "==> Stapling notarization ticket to DMG"
xcrun stapler staple "$DMG"
xcrun stapler validate "$DMG"

echo "==> Done. $DMG is notarized and stapled."