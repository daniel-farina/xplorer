#!/bin/sh
# Configure and build Xplorer.
set -eu
XPLORER="$(cd "$(dirname "$0")" && pwd)"
SRC="${1:-$XPLORER/../chromium/src}"
export PATH="$XPLORER/../depot_tools:$PATH"

cd "$SRC"
mkdir -p out/aether
cp "$XPLORER/build/args.gn" out/aether/args.gn
gn gen out/aether
autoninja -C out/aether chrome
echo "Built: $SRC/out/aether/Chromium.app"
