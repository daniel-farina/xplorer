#!/bin/sh
# Configure and build Aether.
set -eu
AETHER="$(cd "$(dirname "$0")" && pwd)"
SRC="${1:-$AETHER/../chromium/src}"
export PATH="$AETHER/../depot_tools:$PATH"

cd "$SRC"
mkdir -p out/aether
cp "$AETHER/build/args.gn" out/aether/args.gn
gn gen out/aether
autoninja -C out/aether chrome
echo "Built: $SRC/out/aether/Chromium.app"
