#!/usr/bin/env bash
# Read-only validation gate for the vendored ungoogled-chromium patches.
#
# Loops the series (and optionally quarantine.series) and runs `git apply --check` against
# the Chromium checkout — it NEVER modifies the tree. This is Xplorer's lightweight analog of
# ungoogled's devutils/validate_patches.py. Run it whenever you bump the Chromium pin or
# refresh the vendored patches; any newly-failing patch should move to quarantine.series.
#
# Usage: scripts/check_ungoogled_patches.sh [SRC] [--all]
#   SRC     Chromium checkout (default ../chromium/src)
#   --all   also check quarantine.series
set -u

XPLORER="$(cd "$(dirname "$0")/.." && pwd)"
PDIR="$XPLORER/patches/ungoogled"
SRC="$XPLORER/../chromium/src"
CHECK_QUARANTINE=0
for a in "$@"; do
  case "$a" in
    --all) CHECK_QUARANTINE=1 ;;
    *) SRC="$a" ;;
  esac
done
[ -d "$SRC/chrome" ] || { echo "check_ungoogled: Chromium src not found at $SRC" >&2; exit 1; }

check_series() {
  local series="$1" label="$2" applies=0 fails=0
  echo "== $label ($(basename "$series")) =="
  while IFS= read -r raw; do
    local line="${raw%%#*}"; line="$(printf '%s' "$line" | tr -d '[:space:]')"
    [ -n "$line" ] || continue
    local patch="$PDIR/$line"
    if [ ! -f "$patch" ]; then echo "  MISSING  $line"; fails=$((fails+1)); continue; fi
    if git -C "$SRC" apply --reverse --check -p1 --ignore-whitespace "$patch" >/dev/null 2>&1; then
      echo "  APPLIED  $line (already in tree)"; applies=$((applies+1))
    elif git -C "$SRC" apply --check -p1 --ignore-whitespace "$patch" >/dev/null 2>&1; then
      echo "  APPLIES  $line"; applies=$((applies+1))
    else
      echo "  FAILS    $line"; fails=$((fails+1))
    fi
  done < "$series"
  echo "  -> applies=$applies fails=$fails"
  echo
  RET_APPLIES=$applies RET_FAILS=$fails
}

echo "ungoogled patch check against: $SRC"
echo
check_series "$PDIR/series" "ACTIVE"
a_ap=$RET_APPLIES; a_fl=$RET_FAILS
if [ "$CHECK_QUARANTINE" -eq 1 ] && [ -f "$PDIR/quarantine.series" ]; then
  check_series "$PDIR/quarantine.series" "QUARANTINE"
fi
echo "SUMMARY active: applies=$a_ap fails=$a_fl"
# Non-zero exit if any ACTIVE patch fails — useful as a CI/pre-bump gate.
[ "$a_fl" -eq 0 ]
