<#
.SYNOPSIS
  Package a built Xplorer (Windows) into a portable .zip + SHA-256.

.DESCRIPTION
  The Windows counterpart of scripts/package.sh. Windows has no .app bundle, so
  a "portable zip" of the runtime files (chrome.exe + chrome.dll + the DLLs,
  .pak/.bin/.dat data, locales, and the bundled companion UI) is the analog of
  the macOS .zip/.dmg. chrome.exe is staged as Xplorer.exe so the shipped binary
  reads "Xplorer"; chrome.dll keeps its name (the launcher loads it by name).

  The companion UI is copied to <root>\companion\ui so the gateway's UiDir()
  resolves it relative to the executable on Windows (there is no Contents/
  Resources). If you also built mini_installer, pass -Installer to include
  mini_installer.exe in the artifact set.

.PARAMETER OutDir
  The build output dir, e.g. ..\chromium\src\out\xplorer_x64.

.PARAMETER Version
  Release version string (e.g. v0.5.0). Informational.

.PARAMETER Arch
  x64 (default) or arm64 — used in the artifact name.

.PARAMETER Installer
  Also copy mini_installer.exe (if present) next to the zip and into checksums.

.EXAMPLE
  .\scripts\package.ps1 -OutDir ..\chromium\src\out\xplorer_x64 -Version v0.5.0
#>
[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)][string]$OutDir,
  [string]$Version = "dev",
  [ValidateSet("x64", "arm64")][string]$Arch = "x64",
  [switch]$Installer
)

$ErrorActionPreference = "Stop"
$Xplorer = Split-Path $PSScriptRoot -Parent
$OutDir = [System.IO.Path]::GetFullPath($OutDir)
$exe = Join-Path $OutDir "chrome.exe"
if (-not (Test-Path $exe)) { throw "No chrome.exe at $OutDir - build first." }

$Name = "Xplor-windows-$Arch"
$Dist = Join-Path $Xplorer "dist"
New-Item -ItemType Directory -Force -Path $Dist | Out-Null

# Stage the runtime into a clean tree (the out dir also holds GBs of obj/ and
# .pdb/.lib intermediates we must NOT ship).
$Stage = Join-Path ([System.IO.Path]::GetTempPath()) ("xplorer-pkg-" + [System.Guid]::NewGuid().ToString("N"))
$Root = Join-Path $Stage "Xplorer"
New-Item -ItemType Directory -Force -Path $Root | Out-Null

# Stage in a try/finally so the multi-GB temp dir is always removed — including
# on a throw (robocopy failure, Compress-Archive under -ErrorAction Stop, etc.),
# which would otherwise accumulate across CI runs until the disk fills.
try {
  Write-Host "Staging runtime from $OutDir ..."
  # The launcher, renamed so the shipped product is Xplorer.exe.
  Copy-Item $exe (Join-Path $Root "Xplorer.exe") -Force

  # Top-level runtime payload: DLLs, snapshot/data blobs, resource paks, the
  # SwiftShader ICD .json, the versioned SxS .manifest (chrome.exe's embedded
  # manifest declares a dependency on an assembly named after the version —
  # without the matching <version>.manifest beside it, launch fails with
  # "side-by-side configuration is incorrect"), and the VisualElements tile
  # assets (.xml/.png) for the taskbar/Start icon. Whitelisted by extension and
  # non-recursive, so obj\, gen\, *.pdb, *.lib, *.runtime_deps stay excluded.
  Get-ChildItem -Path $OutDir -File | Where-Object {
    $_.Extension -in @('.dll', '.bin', '.dat', '.pak', '.json', '.manifest', '.xml', '.png')
  } | ForEach-Object { Copy-Item $_.FullName (Join-Path $Root $_.Name) -Force }

  # Localized string paks and (if present) the resources tree.
  foreach ($sub in @('locales', 'resources')) {
    $p = Join-Path $OutDir $sub
    if (Test-Path $p) {
      robocopy $p (Join-Path $Root $sub) /E /NFL /NDL /NJH /NJS /NP | Out-Null
      if ($LASTEXITCODE -ge 8) { throw "robocopy failed staging $sub (exit $LASTEXITCODE)" }
    }
  }

  # Bundle the companion UI beside the exe so UiDir() finds <exe_dir>\companion\ui.
  robocopy (Join-Path $Xplorer "companion\ui") (Join-Path $Root "companion\ui") /E /NFL /NDL /NJH /NJS /NP | Out-Null
  if ($LASTEXITCODE -ge 8) { throw "robocopy failed staging companion\ui (exit $LASTEXITCODE)" }
  # robocopy uses exit codes < 8 to signal success (1 = files copied); reset so
  # the lingering code isn't mistaken for a script failure by callers/CI.
  $global:LASTEXITCODE = 0

  # Bundle the MCP servers (sdk\*.py) beside the exe so the grok provisioner finds
  # them (SdkDir() checks DIR_MODULE\sdk and <exe_dir>\sdk on Windows).
  New-Item -ItemType Directory -Force -Path (Join-Path $Root "sdk") | Out-Null
  Copy-Item (Join-Path $Xplorer "sdk\*.py") (Join-Path $Root "sdk") -Force -ErrorAction SilentlyContinue

  # Fail loudly if a load-bearing runtime file is missing rather than shipping a
  # broken zip (extension-whitelist staging can silently drop new file types).
  $required = @('Xplorer.exe', 'chrome.dll', 'chrome_elf.dll', 'icudtl.dat',
                'resources.pak', 'chrome_100_percent.pak')
  $missing = $required | Where-Object { -not (Test-Path (Join-Path $Root $_)) }
  if ($missing) { throw "Staged tree missing required runtime files: $($missing -join ', ')" }
  # chrome.exe's manifest needs the versioned SxS assembly manifest beside it.
  if (-not (Get-ChildItem (Join-Path $Root '*.manifest') -ErrorAction SilentlyContinue)) {
    throw "No <version>.manifest staged - chrome.exe won't launch (SxS assembly missing)."
  }
  if ((Test-Path (Join-Path $Root 'vk_swiftshader.dll')) -and
      -not (Test-Path (Join-Path $Root 'vk_swiftshader_icd.json'))) {
    throw "vk_swiftshader.dll staged without vk_swiftshader_icd.json (software-GL fallback would be broken)."
  }

  $Zip = Join-Path $Dist "$Name.zip"
  Write-Host "Zipping -> $Zip ..."
  if (Test-Path $Zip) { Remove-Item $Zip -Force }
  # Use ZipFile (not Compress-Archive): on Windows PowerShell 5.1 Compress-Archive
  # writes backslash entry separators (non-standard) and `-Path $dir\*` drops the
  # enclosing folder (a tarbomb). CreateFromDirectory with includeBaseDirectory
  # emits forward-slash paths under a top-level "Xplorer/" folder.
  Add-Type -AssemblyName System.IO.Compression.FileSystem
  [System.IO.Compression.ZipFile]::CreateFromDirectory(
    $Root, $Zip, [System.IO.Compression.CompressionLevel]::Optimal, $true)

  # Optional installer artifact.
  $checksumInputs = @($Zip)
  if ($Installer) {
    $mi = Join-Path $OutDir "mini_installer.exe"
    if (Test-Path $mi) {
      $miDest = Join-Path $Dist "$Name-installer.exe"
      Copy-Item $mi $miDest -Force
      $checksumInputs += $miDest
    } else {
      Write-Warning "mini_installer.exe not found in $OutDir; build with .\build.ps1 -Installer"
    }
  }

  Write-Host "Checksums..."
  $lines = foreach ($f in $checksumInputs) {
    $h = (Get-FileHash $f -Algorithm SHA256).Hash.ToLower()
    "$h  $(Split-Path $f -Leaf)"
  }
  Set-Content -Path (Join-Path $Dist "$Name.sha256.txt") -Value $lines -Encoding ascii
}
finally {
  if (Test-Path $Stage) { Remove-Item $Stage -Recurse -Force }
}

Write-Host "Artifacts in $Dist :"
Get-ChildItem $Dist | Where-Object { $_.Name -like "$Name*" } |
  ForEach-Object { "  {0,-34} {1,12:N0} bytes" -f $_.Name, $_.Length }
Write-Host "version: $Version"
