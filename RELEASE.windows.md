# Building & releasing Xplorer on Windows

The Windows counterpart of [`RELEASE.md`](RELEASE.md). Same overlay model — a
thin set of `src/` files + `patches/` edits applied on top of a full Chromium
checkout — but built with the Windows toolchain and packaged as a portable zip
(and optionally a `mini_installer.exe`).

> Layout note: the overlay lives in `xplorer\`; the Chromium checkout sits next
> to it at `..\chromium\src`. Keep both **near a drive root** (e.g.
> `C:\src\xplorer` and `C:\src\chromium`) — Chromium's paths blow past the
> legacy 260-char `MAX_PATH` limit.

---

## 0. Prerequisites (one time)

- **Windows 10/11 x64**, ~**150 GB** free on an NTFS volume, 16 GB RAM (32 GB+
  recommended — linking `chrome.dll` is memory-hungry).
- **Visual Studio 2022** (17.x) with the **"Desktop development with C++"**
  workload plus the **ATL/MFC** component (`Microsoft.VisualStudio.Component.VC.ATLMFC`).
  Chromium requires ATL.
- **Windows 11 SDK** matching the branch you sync (recent `main` wants
  `10.0.26100.x`), **including the "Debugging Tools for Windows"** feature
  (SDK ▸ Modify ▸ check Debugging Tools) — the build's hooks require it.
- **Python 3** on `PATH` (used by the patcher and, optionally, the per-app
  preview server). `py` (the launcher in `C:\Windows`) is fine.
- **depot_tools** on `PATH`, and the env var **`DEPOT_TOOLS_WIN_TOOLCHAIN=0`**
  so the build uses your *local* Visual Studio rather than Google's internal
  toolchain package (which external contributors can't fetch).

```powershell
# git (configure for Chromium)
git config --global core.autocrlf false
git config --global core.filemode false
git config --global core.longpaths true   # required for deep paths

# enable OS-level long paths (admin), once:
Set-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Control\FileSystem' LongPathsEnabled 1

# depot_tools — extract the bundle (do NOT just git-clone the zip on Windows),
# or clone it, then put it FIRST on PATH:
git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git C:\src\depot_tools
$env:PATH = "C:\src\depot_tools;$env:PATH"
$env:DEPOT_TOOLS_WIN_TOOLCHAIN = "0"
gclient            # run once with no args to bootstrap (pulls gn/ninja/siso)
```

> `build.ps1` adds `..\depot_tools` to `PATH` and sets
> `DEPOT_TOOLS_WIN_TOOLCHAIN=0` for you, so if you cloned depot_tools as a
> sibling of this repo you only need it on `PATH` for the initial `fetch`.

Also **exclude the checkout from antivirus** — Defender scanning every `.obj`
the build writes slows it 2–5× and can lock files mid-write:

```powershell
Add-MpPreference -ExclusionPath C:\src\chromium
```

---

## 1. Fetch the Chromium source

```powershell
mkdir C:\src\chromium; cd C:\src\chromium
fetch chromium          # gclient sync + runhooks (toolchain, clang, etc.)
cd src
```

`fetch`/`runhooks` is the long download (~40–60 GB after sync). It must finish
before `gn gen`.

---

## 2. Overlay Xplorer onto Chromium

```powershell
.\xplorer\apply.ps1 -Src C:\src\chromium\src
```

`apply.ps1` (the Windows counterpart of `apply.sh`):

1. Copies `xplorer\src\chrome\...` (the `agent_gateway` + `grok_companion`
   components) into the tree.
2. Installs the **Windows app icon**: overwrites
   `chrome\app\theme\chromium\win\chromium.ico` with `branding\app.ico`. Unlike
   macOS there is **no `actool`/`Assets.car` dance** — `chrome_exe.rc` /
   `chrome_dll.rc` reference the `.ico` and the resource compiler bakes it into
   `chrome.exe`/`chrome.dll` at build time, so a single file copy is enough.
3. Installs the Grok toolbar vector icon (`grok.icon`).
4. Runs `patches\apply_integration.py` — the **same** cross-platform patcher as
   macOS. On Windows it additionally sets the `COMPANY_*` BRANDING keys (for the
   exe VERSIONINFO) and, best-effort, makes `version_updater_basic.cc` report
   "up to date" (the unbranded Windows updater otherwise shows "disabled").

The patcher is idempotent (safe to re-run).

---

## 3. Build

```powershell
.\xplorer\build.ps1 -Src C:\src\chromium\src             # x64 -> out\xplorer_x64
.\xplorer\build.ps1 -Src C:\src\chromium\src -Arch arm64 # Windows-on-ARM
.\xplorer\build.ps1 -Src C:\src\chromium\src -Installer  # also mini_installer.exe
```

`build.ps1` writes `out\xplorer_<arch>\args.gn` from `build\args.gn.win`, runs
`gn gen`, then `autoninja -C out\xplorer_<arch> chrome`. gn args mirror the mac
release config (`is_debug=false`, `is_component_build=false`, `symbol_level=0`,
`is_chrome_branded=false`); on Windows `use_lld` is left default (lld-link is
already the default) and `target_os="win"`.

The first full build is ~2–5 h on a fast desktop (longer on a laptop);
incremental rebuilds are minutes. **Use `autoninja`** (it selects ninja/siso and
the right parallelism). External forks can't use Google's remote execution, so
this is a fully local build.

The output is a **flat** set of files in `out\xplorer_<arch>\` — there is no
`.app` bundle. The launcher is **`chrome.exe`** (its VERSIONINFO/ProductName and
embedded icon say "Xplorer", but the file name stays `chrome.exe`; packaging
renames it to `Xplorer.exe`). Resources (`*.pak`, `locales\`, `icudtl.dat`,
`*.bin`) and DLLs must stay alongside it.

Run it:

```powershell
.\out\xplorer_x64\chrome.exe --user-data-dir=C:\Temp\xplorer-profile
Get-Content $env:USERPROFILE\.xplorer\gateway.json   # confirm the gateway came up
```

---

## 4. Package

```powershell
.\xplorer\scripts\package.ps1 -OutDir C:\src\chromium\src\out\xplorer_x64 -Version v0.5.0
# or the one-command pipeline (apply -> build -> package):
.\xplorer\scripts\release_win.ps1 -Arch x64 -Version v0.5.0 -Src C:\src\chromium\src
```

`package.ps1` stages a **portable zip** — the Windows analog of the mac
`.zip`/`.dmg`:

- `chrome.exe` staged as **`Xplorer.exe`**, plus `chrome.dll` and the runtime
  DLLs, `*.pak`, `*.bin`, `*.dat`, and `locales\`.
- the **companion UI** copied to `companion\ui` beside the exe, so the gateway's
  `UiDir()` resolves it relative to the executable (Windows has no
  `Contents/Resources`).

Produces in `xplorer\dist\`:
- `Xplorer-windows-x64.zip`
- `Xplorer-windows-x64.sha256.txt`
- `Xplorer-windows-x64-installer.exe` (only with `-Installer`)

---

## 5. Code signing (fixes "unknown publisher" + SmartScreen/Defender)

Unsigned binaries are the cause of: SmartScreen "unknown publisher", the
"Windows protected your PC" block, **and** Defender sometimes quarantining a DLL
from the download — which then surfaces as a missing-DLL **error on launch**.
The fix is a real Authenticode code-signing certificate; a self-signed cert does
*not* help SmartScreen.

**Certificate options** (you obtain this from a CA — DigiCert, Sectigo, SSL.com,
GlobalSign, Certum):
- **EV (Extended Validation)** — *instant* SmartScreen reputation (no warning
  from day one). Ships on a USB hardware token or cloud HSM (Azure Key Vault).
  Best choice. ~$300–700/yr.
- **OV (Organization Validation)** — cheaper (~$200–400/yr) but SmartScreen
  reputation accrues over downloads/time (early users still warned for a while).
  Now also delivered on a token/HSM (no more exportable PFX for new issuances).

**Signing** (`scripts/sign_win.ps1`, wraps `signtool` / `AzureSignTool` with
SHA-256 + RFC-3161 timestamp). Sign in this order so the zip *and* installer
contain signed binaries:

```powershell
.\build.ps1 -Src C:\src\chromium\src                       # 1. build chrome
.\scripts\sign_win.ps1 -OutDir ...\out\xplorer_x64 <cert>  # 2. sign binaries
.\build.ps1 -Src C:\src\chromium\src -Installer            # 3. installer repacks signed bins
.\scripts\sign_win.ps1 -OutDir ...\out\xplorer_x64 -InstallerOnly <cert>  # 4. sign installer
.\scripts\package.ps1 -OutDir ...\out\xplorer_x64 -Installer             # 5. package
```

`<cert>` is one of:
- PFX file: `-PfxPath cert.pfx -PfxPassword '…'`
- Azure Key Vault (cloud HSM, CI-friendly): `-KeyVaultUrl … -KeyVaultCert … -KeyVaultClientId … -KeyVaultClientSecret … -KeyVaultTenantId …` (needs `dotnet tool install --global AzureSignTool`)
- Hardware token: plug it in and omit the cert args — `signtool /a` auto-selects from the cert store.

---

## 6. Publish

The CI workflow (`.github/workflows/release.yml`) builds Windows on a
self-hosted runner labelled **`xplorer-builder-win`** (with depot_tools + a
synced checkout at `$XPLORER_CHROMIUM_SRC`) and uploads the artifacts alongside
the macOS ones. For a manual publish, mirror `RELEASE.md` §7 with the
`Xplorer-windows-*` files.

---

## Gotchas (Windows-specific)

- **`MAX_PATH` / long paths.** Do BOTH `git config --global core.longpaths true`
  and set `LongPathsEnabled=1` (above). Otherwise `gclient sync`/`ninja` fail
  with cryptic path-too-long / file-not-found errors. Keep the checkout near a
  drive root and **never** under a OneDrive-synced folder.
- **`DEPOT_TOOLS_WIN_TOOLCHAIN=0` is mandatory** for a non-Google build, and
  depot_tools must be **first** on `PATH` so its `python3`/`gn`/`ninja` win over
  any system Python.
- **Antivirus exclusion** for the checkout — big build-time speedup, avoids
  mid-write file locks.
- **File locked during rebuild** ("cannot open output file … Access is denied"):
  a `chrome.exe`/`Xplorer.exe` is still running. Close it (`Stop-Process -Name
  chrome,Xplorer`) before rebuilding.
- **siso, not jumbo.** Use `autoninja` (selects siso). The old `use_jumbo_build`
  flag has been removed from Chromium — don't set it; rely on `symbol_level=0`.
- **The exe name stays `chrome.exe`** from the build; only packaging renames it
  to `Xplorer.exe`. The user-visible name (window title, taskbar, About) comes
  from the patched product strings, not the filename.
- **Upstream API drift** breaks the overlay periodically (same as macOS) — when
  re-syncing Chromium, expect to fix a few renamed base/UI APIs and moved
  patcher anchors.
