#!/bin/bash
# Unattended Linux build of Xplorer, run ON a fresh Ubuntu 24.04 DO droplet.
#
#   curl -fsSL <raw>/scripts/linux_buildbox_bootstrap.sh | bash    (or scp + run)
#
# Pins Chromium to the SAME revision as the macOS/Windows builds so the
# apply_integration.py anchors match (avoids "ANCHOR NOT FOUND" from tip-of-tree
# drift). Idempotent-ish: skips steps already completed so a re-run after a
# transient failure resumes rather than restarting. Writes a final marker line
# (BUILD_RESULT: SUCCESS|FAILED) and the artifact path to the log.
set -uo pipefail

PIN=242a04c867e0486e1f26f0f3bc0c42f23b9da347   # chromium 151.0.7890.0 (matches mac/win)
BRANCH=feat/linux-build
REPO=https://github.com/daniel-farina/xplorer.git
VER=v0.7.0
ROOT="$HOME/cli_experiment"
LOG="$HOME/xplorer-build.log"

exec > >(tee -a "$LOG") 2>&1
echo "================ BOOTSTRAP START $(date -u) ================"
fail() { echo "BUILD_RESULT: FAILED ($1) at $(date -u)"; exit 1; }

export DEBIAN_FRONTEND=noninteractive
echo "--- [1/7] apt deps ---"
sudo apt-get update -y || fail apt-update
sudo apt-get install -y git python3 python3-pip curl lsb-release file ca-certificates pkg-config || fail apt-deps

mkdir -p "$ROOT" && cd "$ROOT"

echo "--- [2/7] depot_tools ---"
# NOTE: clone depot_tools FULL (not --depth 1): a shallow clone leaves its CIPD
# self-bootstrap (gn/ninja/vpython/luci-auth) incomplete, so gclient's hooks
# can't fetch the toolchain and `gn gen` later fails with
# "python3_bin_reldir.txt not found" / "Unable to find gn".
[ -d depot_tools ] || git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git || fail depot_tools
export PATH="$ROOT/depot_tools:$PATH"
export DEPOT_TOOLS_UPDATE=1
gclient --version >/dev/null 2>&1 || true   # trigger CIPD bin bootstrap

echo "--- [3/7] fetch chromium (full history, needed to pin the revision) ---"
mkdir -p chromium && cd chromium
if [ ! -d src ]; then
  fetch --nohooks chromium || fail fetch
fi

echo "--- [4/7] pin to $PIN + sync deps ---"
cd src
git fetch origin "$PIN" 2>/dev/null || true
git checkout "$PIN" || fail "checkout $PIN"
echo "HEAD: $(git rev-parse HEAD)  VERSION: $(tr '\n' '.' < chrome/VERSION)"

echo "--- [5/7] linux build deps + hooks ---"
./build/install-build-deps.sh --no-prompt || ./build/install-build-deps.sh --no-prompt --no-chromeos-fonts || fail install-build-deps
gclient sync -D --force --reset || fail "gclient sync"
gclient runhooks || fail runhooks

echo "--- [6/7] overlay (apply -> build -> package) ---"
cd "$ROOT"
if [ ! -d xplorer ]; then
  git clone -b "$BRANCH" "$REPO" xplorer || fail clone
fi
( cd xplorer && git fetch origin "$BRANCH" && git checkout "$BRANCH" && git reset --hard "origin/$BRANCH" ) || fail "branch sync"

"$ROOT/xplorer/apply.sh" "$ROOT/chromium/src" || fail apply
"$ROOT/xplorer/build.sh" "$ROOT/chromium/src" linux || fail build
"$ROOT/xplorer/scripts/package_linux.sh" "$ROOT/chromium/src/out/aether_linux" "$VER" x64 || fail package

echo "--- [7/7] done ---"
ls -lh "$ROOT/xplorer/dist/" || true
echo "ARTIFACT: $ROOT/xplorer/dist/Xplorer-linux-x64.tar.gz"
echo "BUILD_RESULT: SUCCESS at $(date -u)"
