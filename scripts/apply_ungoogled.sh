#!/usr/bin/env bash
# Apply Xplorer's vendored ungoogled-chromium degoogling series to the Chromium checkout.
#
# Runs AFTER patches/apply_integration.py (see apply.sh) so Xplorer's Grok + branding edits
# are authoritative. Application is BEST-EFFORT and IDEMPOTENT:
#   - already applied (reverse-check passes) -> skipped
#   - applies cleanly                        -> applied
#   - drifted against this Chromium pin      -> logged as FAILED, NOT fatal (overlay continues)
# These patches only remove Google phone-home channels; a missing one degrades privacy
# coverage, it does not break Xplorer. See docs/UNGOOGLED.md and patches/ungoogled/series.
set -u

XPLORER="$(cd "$(dirname "$0")/.." && pwd)"
SRC="${1:-$XPLORER/../chromium/src}"
SERIES="${2:-$XPLORER/patches/ungoogled/series}"
PDIR="$XPLORER/patches/ungoogled"

[ -d "$SRC/chrome" ] || { echo "apply_ungoogled: Chromium src not found at $SRC" >&2; exit 1; }
[ -f "$SERIES" ]     || { echo "apply_ungoogled: series not found at $SERIES" >&2; exit 1; }

applied=0 skipped=0 failed=0
failed_list=""

echo "Applying ungoogled degoogling series ($(basename "$SERIES")) -> $SRC"
while IFS= read -r raw; do
  # strip inline '# ...' comments and surrounding whitespace; skip blanks/comments
  line="${raw%%#*}"
  line="$(printf '%s' "$line" | tr -d '[:space:]')"
  [ -n "$line" ] || continue
  patch="$PDIR/$line"
  if [ ! -f "$patch" ]; then
    echo "  MISSING  $line" >&2; failed=$((failed+1)); failed_list="$failed_list $line"; continue
  fi
  if git -C "$SRC" apply --reverse --check -p1 --ignore-whitespace "$patch" >/dev/null 2>&1; then
    echo "  skip     $line (already applied)"; skipped=$((skipped+1))
  elif git -C "$SRC" apply --check -p1 --ignore-whitespace "$patch" >/dev/null 2>&1; then
    git -C "$SRC" apply -p1 --ignore-whitespace "$patch"
    echo "  apply    $line"; applied=$((applied+1))
  else
    echo "  FAILED   $line (does not apply to this Chromium pin — left unapplied)" >&2
    failed=$((failed+1)); failed_list="$failed_list $line"
  fi
done < "$SERIES"

echo "ungoogled: applied=$applied skipped=$skipped failed=$failed"
if [ "$failed" -gt 0 ]; then
  echo "ungoogled: NOTE — these did not apply (run scripts/check_ungoogled_patches.sh):$failed_list" >&2
fi
# Best-effort by design: never fail the overlay over a drifted privacy patch.
exit 0
