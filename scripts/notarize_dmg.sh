#!/bin/bash
# Notarize and staple a release DMG (DMGs are not code-signed; notarization only).
# Usage: scripts/notarize_dmg.sh <path-to.dmg> [notary-keychain-profile]
set -euo pipefail

DMG="${1:?usage: notarize_dmg.sh <Xplorer.dmg> [notary-profile]}"
PROFILE="${2:-xplorer-notary}"

[ -f "$DMG" ] || { echo "No DMG at $DMG" >&2; exit 1; }

echo "==> Notarizing DMG: $DMG"
xcrun notarytool submit "$DMG" --keychain-profile "$PROFILE" --wait

echo "==> Stapling notarization ticket to DMG"
xcrun stapler staple "$DMG"
xcrun stapler validate "$DMG"

echo "==> Done. $DMG is notarized and stapled."