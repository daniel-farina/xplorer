<#
.SYNOPSIS
  Overlay Xplorer onto a Chromium checkout (Windows).

.DESCRIPTION
  The Windows counterpart of apply.sh. Copies the new source files, installs the
  Windows app icon and the Grok toolbar vector icon, then runs the (cross-
  platform) anchor-based integration patcher.

  Unlike macOS, the Windows app icon needs NO actool/Assets.car dance: the .ico
  referenced by chrome_exe.rc / chrome_dll.rc is compiled into chrome.exe at
  build time, so a single file copy is sufficient.

.PARAMETER Src
  Path to the Chromium src checkout. Defaults to ..\chromium\src next to this repo.

.EXAMPLE
  .\apply.ps1
  .\apply.ps1 -Src C:\src\chromium\src
#>
[CmdletBinding()]
param(
  [string]$Src = (Join-Path $PSScriptRoot "..\chromium\src")
)

$ErrorActionPreference = "Stop"
$Xplorer = $PSScriptRoot
$Src = [System.IO.Path]::GetFullPath($Src)

if (-not (Test-Path (Join-Path $Src "chrome"))) {
  throw "Chromium src not found at $Src (expected a 'chrome' subdir)"
}

# Resolve a Python interpreter for the patcher.
$python = (Get-Command python -ErrorAction SilentlyContinue).Source
if (-not $python) { $python = (Get-Command py -ErrorAction SilentlyContinue).Source }
if (-not $python) { throw "Python not found on PATH (needed for apply_integration.py)" }

Write-Host "Copying new source files..."
# robocopy merges xplorer/src/chrome into <src>/chrome (overlay). Exit codes
# 0-7 indicate success; 8+ is a real failure.
robocopy (Join-Path $Xplorer "src\chrome") (Join-Path $Src "chrome") /E /NFL /NDL /NJH /NJS /NP | Out-Null
if ($LASTEXITCODE -ge 8) { throw "robocopy failed copying src\chrome (exit $LASTEXITCODE)" }

# Deletion-sync: /E is additive — a source file DELETED from the overlay would
# linger in the chromium tree and break a clean rebuild (a stale .cc keeps
# compiling, a deleted .h stays #included). The three xplorer source dirs are
# PURE-overlay (they don't exist upstream), so mirror them with /MIR so the
# chromium copy exactly matches the overlay and removed files are purged.
foreach ($d in @("browser\agent_gateway","browser\ui\views\xplorer","browser\grok_companion")) {
  $od = Join-Path $Xplorer ("src\chrome\" + $d)
  if (Test-Path $od) {
    robocopy $od (Join-Path $Src ("chrome\" + $d)) /MIR /NFL /NDL /NJH /NJS /NP | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "robocopy /MIR failed for $d (exit $LASTEXITCODE)" }
  }
}

Write-Host "Installing Xplorer app icon (Windows .ico)..."
# The non-branded (is_chrome_branded=false) build uses theme\chromium\win\*.ico,
# referenced from chrome_exe.rc / chrome_dll.rc as IDR_MAINFRAME. Overwrite the
# primary app icon; the resource compiler bakes it into chrome.exe/chrome.dll.
$winTheme = Join-Path $Src "chrome\app\theme\chromium\win"
$appIco = Join-Path $Xplorer "branding\app.ico"
if ((Test-Path $appIco) -and (Test-Path $winTheme)) {
  Copy-Item $appIco (Join-Path $winTheme "chromium.ico") -Force
  Write-Host "  installed chromium.ico"
} else {
  Write-Warning "  branding\app.ico or theme\chromium\win not found; app icon NOT updated"
}

Write-Host "Installing Grok toolbar vector icon..."
$vectorIcons = Join-Path $Src "chrome\app\vector_icons"
if (Test-Path $vectorIcons) {
  Copy-Item (Join-Path $Xplorer "branding\grok.icon") (Join-Path $vectorIcons "grok.icon") -Force
}

Write-Host "Applying integration edits..."
# The patcher uses Path.read_text()/write_text(), which default to the locale
# encoding (cp1252) on Windows and would corrupt/raise on Chromium's UTF-8
# source. UTF-8 mode makes Python's file I/O UTF-8 regardless of locale.
$env:PYTHONUTF8 = "1"
& $python (Join-Path $Xplorer "patches\apply_integration.py") $Src
if ($LASTEXITCODE -ne 0) { throw "apply_integration.py failed (exit $LASTEXITCODE)" }

Write-Host "Done. Next: .\build.ps1"
