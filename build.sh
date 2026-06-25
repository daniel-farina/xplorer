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
# Xplorer privacy: never bake a developer's real Google key into the build.
# Pairs with the empty google_* GN args in build/args.gn*. google_api_keys.cc
# reads these env vars at gn-gen/compile time, so an exported key would
# otherwise be compiled in. Empty = ungoogled (sign-in/sync already inert).
export GOOGLE_API_KEY=""
export GOOGLE_DEFAULT_CLIENT_ID=""
export GOOGLE_DEFAULT_CLIENT_SECRET=""
gn gen "out/$OUT_DIR"
autoninja -C "out/$OUT_DIR" chrome
if [ "$ARCH" = "linux" ]; then
  echo "Built ($ARCH): $SRC/out/$OUT_DIR/chrome"
else
  APP="$SRC/out/$OUT_DIR/Xplorer.app"
  # Stage companion UI into the bundle so GrokNative can serve /schedules etc.
  # without falling through to the auth-gated agent API (missing file -> 401).
  UI_DST="$APP/Contents/Resources/companion/ui"
  mkdir -p "$(dirname "$UI_DST")"
  rm -rf "$UI_DST"
  cp -R "$XPLORER/companion/ui" "$UI_DST"
  echo "Bundled companion UI -> $UI_DST"
  echo "Built ($ARCH): $APP"
fi