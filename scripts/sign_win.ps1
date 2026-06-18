<#
.SYNOPSIS
  Authenticode-sign Xplorer's Windows binaries (SHA-256 + RFC-3161 timestamp).

.DESCRIPTION
  Unsigned downloads trigger SmartScreen "unknown publisher" + Defender blocks
  (and Defender may quarantine an unsigned DLL, which then surfaces as a
  missing-DLL error on launch). Signing with a real Authenticode cert fixes all
  of that. EV certs get instant SmartScreen reputation; OV reputation accrues.

  Run order for a signed release:
    1. .\build.ps1 -Src <src>            # build chrome
    2. .\scripts\sign_win.ps1 -OutDir <out> <cert args>   # sign the binaries
    3. .\build.ps1 -Src <src> -Installer # mini_installer repacks SIGNED binaries
    4. .\scripts\sign_win.ps1 -OutDir <out> -InstallerOnly <cert args>  # sign installer
    5. .\scripts\package.ps1 -OutDir <out> -Installer     # zip + installer artifacts

  Two cert backends:
    * PFX file:        -PfxPath cert.pfx -PfxPassword '...'
    * Azure Key Vault: -KeyVaultUrl https://<vault>.vault.azure.net
                       -KeyVaultCert <name> -KeyVaultClientId <id>
                       -KeyVaultClientSecret <secret> -KeyVaultTenantId <tenant>
      (Key Vault uses AzureSignTool: dotnet tool install --global AzureSignTool.
       This is the CI-friendly path for cloud-HSM EV certs. A USB hardware token
       instead uses signtool /a on the machine with the token plugged in.)

.NOTES
  signtool.exe ships with the Windows SDK (Debugging/Signing tools).
#>
[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)][string]$OutDir,
  [string]$PfxPath,
  [string]$PfxPassword,
  [string]$KeyVaultUrl,
  [string]$KeyVaultCert,
  [string]$KeyVaultClientId,
  [string]$KeyVaultClientSecret,
  [string]$KeyVaultTenantId,
  [string]$TimestampUrl = "http://timestamp.digicert.com",
  [switch]$InstallerOnly
)

$ErrorActionPreference = "Stop"
$OutDir = [System.IO.Path]::GetFullPath($OutDir)

# Our binaries (NOT the Microsoft-signed VC runtime / dxcompiler, which are
# already signed). chrome.exe is signed in place; package.ps1 renames it to
# Xplorer.exe afterward (the signature travels with the bytes).
$ourBinaries = @(
  'chrome.exe', 'chrome.dll', 'chrome_elf.dll', 'chrome_proxy.exe',
  'elevation_service.exe', 'notification_helper.exe', 'chrome_pwa_launcher.exe',
  'libEGL.dll', 'libGLESv2.dll', 'vk_swiftshader.dll', 'vulkan-1.dll'
)
$installerBinaries = @('mini_installer.exe', 'setup.exe')

$targets = if ($InstallerOnly) { $installerBinaries } else { $ourBinaries }
$files = foreach ($b in $targets) {
  $p = Join-Path $OutDir $b
  if (Test-Path $p) { $p } else { Write-Warning "skip (not built): $b" }
}
if (-not $files) { throw "No target binaries found in $OutDir" }

if ($KeyVaultUrl) {
  # Azure Key Vault (cloud HSM) via AzureSignTool.
  $astCmd = Get-Command azuresigntool -ErrorAction SilentlyContinue
  $azuresigntool = if ($astCmd) { $astCmd.Source } else { $null }
  if (-not $azuresigntool) { throw "AzureSignTool not found. Install: dotnet tool install --global AzureSignTool" }
  foreach ($f in $files) {
    & $azuresigntool sign -kvu $KeyVaultUrl -kvc $KeyVaultCert `
      -kvi $KeyVaultClientId -kvs $KeyVaultClientSecret -kvt $KeyVaultTenantId `
      -tr $TimestampUrl -td sha256 -fd sha256 -v "$f"
    if ($LASTEXITCODE -ne 0) { throw "AzureSignTool failed on $f" }
  }
}
else {
  # signtool (PFX file, or a hardware token via the cert store with /a).
  $signtool = Get-ChildItem "C:\Program Files (x86)\Windows Kits\10\bin" -Recurse -Filter signtool.exe -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -match '\\x64\\' } | Sort-Object FullName -Descending | Select-Object -First 1
  if (-not $signtool) { throw "signtool.exe not found (install the Windows SDK Signing Tools)" }
  $args = @('sign', '/fd', 'sha256', '/tr', $TimestampUrl, '/td', 'sha256')
  if ($PfxPath) {
    $args += @('/f', $PfxPath)
    if ($PfxPassword) { $args += @('/p', $PfxPassword) }
  } else {
    $args += '/a'  # auto-select from the cert store (e.g. a plugged-in token)
  }
  foreach ($f in $files) {
    & $signtool.FullName @args "$f"
    if ($LASTEXITCODE -ne 0) { throw "signtool failed on $f" }
  }
}

Write-Host "Signed $($files.Count) binary file(s). Verify with: signtool verify /pa /v <file>"
