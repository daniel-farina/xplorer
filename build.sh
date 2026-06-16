#!/bin/sh
# Configure and build Xplorer.
set -eu
XPLORER="$(cd "$(dirname "$0")" && pwd)"
SRC="${1:-$XPLORER/../chromium/src}"
export PATH="$XPLORER/../depot_tools:$PATH"

cd "$SRC"
mkdir -p out/xplorer
cp "$XPLORER/build/args.gn" out/xplorer/args.gn
gn gen out/xplorer
autoninja -C out/xplorer chrome
echo "Built: $SRC/out/xplorer/Chromium.app"
