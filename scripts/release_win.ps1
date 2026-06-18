<#
.SYNOPSIS
  One-command Windows release pipeline: apply -> build -> package.

.DESCRIPTION
  The Windows counterpart of scripts/release_arch.sh (minus signing/notarization,
  which on Windows is optional Authenticode signing — see RELEASE.windows.md).
  Runs the overlay applier, the build, and the portable-zip packager for one
  architecture, leaving artifacts in dist\.

.PARAMETER Arch
  x64 (default) or arm64.

.PARAMETER Version
  Release version (e.g. v0.5.0).

.PARAMETER Src
  Chromium src checkout. Defaults to ..\chromium\src.

.PARAMETER Installer
  Also build + package mini_installer.exe.

.EXAMPLE
  .\scripts\release_win.ps1 -Arch x64 -Version v0.5.0
#>
[CmdletBinding()]
param(
  [ValidateSet("x64", "arm64")][string]$Arch = "x64",
  [string]$Version = "dev",
  [string]$Src = (Join-Path (Split-Path $PSScriptRoot -Parent) "..\chromium\src"),
  [switch]$Installer
)

$ErrorActionPreference = "Stop"
$Xplorer = Split-Path $PSScriptRoot -Parent
$Src = [System.IO.Path]::GetFullPath($Src)

Write-Host "==> apply ($Arch)"
& (Join-Path $Xplorer "apply.ps1") -Src $Src

Write-Host "==> build ($Arch)"
$buildArgs = @{ Src = $Src; Arch = $Arch }
if ($Installer) { $buildArgs.Installer = $true }
& (Join-Path $Xplorer "build.ps1") @buildArgs

Write-Host "==> package ($Arch)"
$outDir = Join-Path $Src "out\xplorer_$Arch"
$pkgArgs = @{ OutDir = $outDir; Version = $Version; Arch = $Arch }
if ($Installer) { $pkgArgs.Installer = $true }
& (Join-Path $Xplorer "scripts\package.ps1") @pkgArgs

Write-Host "==> done ($Arch $Version). Artifacts in $Xplorer\dist"
