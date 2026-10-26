# Apply Xplorer's vendored ungoogled-chromium degoogling series (Windows).
# Mirror of scripts/apply_ungoogled.sh: runs AFTER apply_integration.py, best-effort and
# idempotent (already-applied -> skip, drifted -> log + continue, never fatal).
# See patches/ungoogled/series and docs/UNGOOGLED.md.
param(
  [string]$Src,
  [string]$Series
)
$ErrorActionPreference = "Continue"
$Xplorer = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if (-not $Src)    { $Src = Join-Path $Xplorer "..\chromium\src" }
if (-not $Series) { $Series = Join-Path $Xplorer "patches\ungoogled\series" }
$PDir = Join-Path $Xplorer "patches\ungoogled"

if (-not (Test-Path (Join-Path $Src "chrome"))) { throw "apply_ungoogled: Chromium src not found at $Src" }
if (-not (Test-Path $Series)) { throw "apply_ungoogled: series not found at $Series" }

$applied = 0; $skipped = 0; $failed = 0; $failedList = @()
Write-Host "Applying ungoogled degoogling series -> $Src"
foreach ($raw in Get-Content $Series) {
  $line = ($raw -replace '#.*$', '').Trim()
  if ([string]::IsNullOrWhiteSpace($line)) { continue }
  $patch = Join-Path $PDir $line
  if (-not (Test-Path $patch)) { Write-Warning "  MISSING  $line"; $failed++; $failedList += $line; continue }
  & git -C $Src apply --reverse --check -p1 --ignore-whitespace $patch 2>$null
  if ($LASTEXITCODE -eq 0) { Write-Host "  skip     $line (already applied)"; $skipped++; continue }
  & git -C $Src apply --check -p1 --ignore-whitespace $patch 2>$null
  if ($LASTEXITCODE -eq 0) {
    & git -C $Src apply -p1 --ignore-whitespace $patch
    Write-Host "  apply    $line"; $applied++
  } else {
    Write-Warning "  FAILED   $line (does not apply to this Chromium pin — left unapplied)"
    $failed++; $failedList += $line
  }
}
Write-Host "ungoogled: applied=$applied skipped=$skipped failed=$failed"
if ($failed -gt 0) { Write-Warning ("ungoogled: did not apply: " + ($failedList -join ' ')) }
# Best-effort by design.
exit 0
