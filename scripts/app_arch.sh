#!/bin/sh
# Print the Mach-O architecture of a built Xplorer.app (arm64 or x86_64).
# Usage: scripts/app_arch.sh <path-to-Xplorer.app>
set -eu
APP="${1:?usage: app_arch.sh <Xplorer.app>}"
BIN="$APP/Contents/MacOS/Xplorer"
[ -f "$BIN" ] || { echo "No binary at $BIN" >&2; exit 1; }
case "$(file -b "$BIN")" in
  *arm64*) echo arm64 ;;
  *x86_64*) echo x86_64 ;;
  *)
    echo "Unknown arch for $BIN: $(file -b "$BIN")" >&2
    exit 1
    ;;
esac