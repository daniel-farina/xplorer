<#
.SYNOPSIS
  Configure and build Xplorer on Windows.

.DESCRIPTION
  The Windows counterpart of build.sh. Puts depot_tools on PATH, forces the
  local Visual Studio toolchain (DEPOT_TOOLS_WIN_TOOLCHAIN=0), writes args.gn
  from build\args.gn.win, then runs `gn gen` + `autoninja`.

  Produces out\<dir>\chrome.exe (branded "Xplorer" via the patches; the file
  name stays chrome.exe — package.ps1 stages it as Xplorer.exe).

.PARAMETER Src
  Chromium src checkout. Defaults to ..\chromium\src next to this repo.

.PARAMETER Arch
  x64 (default) or arm64.

.PARAMETER Installer
  Also build the mini_installer target (mini_installer.exe + setup.exe).

.EXAMPLE
  .\build.ps1
  .\build.ps1 -Src C:\src\chromium\src -Installer
#>
[CmdletBinding()]
param(
  [string]$Src = (Join-Path $PSScriptRoot "..\chromium\src"),
  [ValidateSet("x64", "arm64")]
  [string]$Arch = "x64",
  [switch]$Installer
)

$ErrorActionPreference = "Stop"
$Xplorer = $PSScriptRoot
$Src = [System.IO.Path]::GetFullPath($Src)

# depot_tools at the front of PATH (gn/ninja/autoninja/gclient live there), and
# use the locally-installed Visual Studio rather than Google's internal package.
$depot = [System.IO.Path]::GetFullPath((Join-Path $Xplorer "..\depot_tools"))
if (Test-Path $depot) {
  $env:PATH = "$depot;$env:PATH"
}
$env:DEPOT_TOOLS_WIN_TOOLCHAIN = "0"

if (-not (Get-Command autoninja -ErrorAction SilentlyContinue)) {
  throw "autoninja not found. Put depot_tools on PATH (expected at $depot). See RELEASE.windows.md."
}

$OutDir = "xplorer_$Arch"
$outPath = Join-Path $Src "out\$OutDir"
New-Item -ItemType Directory -Force -Path $outPath | Out-Null

# Write args.gn from the template, retargeting target_cpu for arm64. (Avoid the
# automatic $args variable name.)
$gnArgs = Get-Content (Join-Path $Xplorer "build\args.gn.win") -Raw
if ($Arch -eq "arm64") {
  $gnArgs = $gnArgs -replace 'target_cpu = "x64"', 'target_cpu = "arm64"'
}
Set-Content -Path (Join-Path $outPath "args.gn") -Value $gnArgs -Encoding ascii

Push-Location $Src
try {
  Write-Host "gn gen out\$OutDir ..."
  & gn gen "out\$OutDir"
  if ($LASTEXITCODE -ne 0) { throw "gn gen failed (exit $LASTEXITCODE)" }

  Write-Host "autoninja -C out\$OutDir chrome ..."
  & autoninja -C "out\$OutDir" chrome
  if ($LASTEXITCODE -ne 0) { throw "autoninja chrome failed (exit $LASTEXITCODE)" }

  if ($Installer) {
    # Stage the companion UI into the out dir so mini_installer packages it
    # (chrome.release ships it to <ChromeDir>\companion\ui, where the gateway's
    # UiDir() resolves it; without it the installed app's /search etc. 401).
    robocopy (Join-Path $Xplorer "companion\ui") (Join-Path $outPath "companion\ui") /E /NFL /NDL /NJH /NJS /NP | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "robocopy failed staging companion\ui for installer (exit $LASTEXITCODE)" }
    $global:LASTEXITCODE = 0
    Write-Host "autoninja -C out\$OutDir mini_installer ..."
    & autoninja -C "out\$OutDir" mini_installer
    if ($LASTEXITCODE -ne 0) { throw "autoninja mini_installer failed (exit $LASTEXITCODE)" }
  }
}
finally {
  Pop-Location
}

Write-Host "Built ($Arch): $outPath\chrome.exe"
