#!/bin/sh
# Configure and build Xplorer.
# Usage: ./build.sh [chromium-src] [arm64|x64]
set -eu
XPLORER="$(cd "$(dirname "$0")" && pwd)"
SRC="${1:-$XPLORER/../chromium/src}"
ARCH="${2:-arm64}"
export PATH="$XPLORER/../depot_tools:$PATH"

case "$ARCH" in
  arm64)
    OUT_DIR=aether
    ARGS_FILE=args.gn
    ;;
  x64|x86_64|intel)
    OUT_DIR=aether_x64
    ARGS_FILE=args.gn.x64
    ARCH=x64
    ;;
  linux)
    OUT_DIR=aether_linux
    ARGS_FILE=args.gn.linux
    ;;
  *)
    echo "Unknown arch: $ARCH (use arm64, x64, or linux)" >&2
    exit 1
    ;;
esac

cd "$SRC"
mkdir -p "out/$OUT_DIR"
cp "$XPLORER/build/$ARGS_FILE" "out/$OUT_DIR/args.gn"
gn gen "out/$OUT_DIR"
autoninja -C "out/$OUT_DIR" chrome
if [ "$ARCH" = "linux" ]; then
  echo "Built ($ARCH): $SRC/out/$OUT_DIR/chrome"
else
  echo "Built ($ARCH): $SRC/out/$OUT_DIR/Xplorer.app"
fi