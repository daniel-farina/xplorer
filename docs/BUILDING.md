# Building Xplorer

Xplorer is a **source-level Chromium overlay**: this repo contains only the Xplorer
sources (`src/chrome/**`), the integration patcher (`patches/apply_integration.py`), the
GN build args (`build/args.gn*`), and the apply/build/package scripts. You bring your own
Chromium checkout; the overlay is copied/patched onto it at build time.

The same checkout is reused across builds, so **the first build is slow (hours) and every
build after that is incremental (minutes)** — keep your Chromium checkout around.

> Platforms can't cross the OS boundary: macOS builds need macOS, Windows needs Windows,
> Linux needs Linux. macOS *can* build both Apple-Silicon (arm64) and Intel (x86_64) targets.

---

## 1. One-time: get a Chromium checkout (per build host)

Follow Chromium's [Get the code](https://www.chromium.org/developers/how-tos/get-the-code/)
for your OS, then **pin the revision Xplorer targets** so the patch anchors match:

```sh
# depot_tools must be a FULL clone (not --depth 1) or its CIPD bins won't bootstrap.
git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git
export PATH="$PWD/depot_tools:$PATH"      # (Windows: add depot_tools to PATH via System env)

mkdir chromium && cd chromium
fetch --nohooks chromium                  # ~1-1.5h, ~63 GB

cd src
git checkout cd1d42cba19c64f3386d5dfa1475d620b6efb6a4   # = Chromium 151.0.7897.0
./build/install-build-deps.sh --no-prompt # Linux only
gclient sync -D --force --reset
gclient runhooks
```

The pinned revision is also recorded in `scripts/linux_buildbox_bootstrap.sh` (`PIN=`).

---

## 2. Apply the overlay + build

From this repo, pointing at your checkout's `src` dir:

### macOS (Apple Silicon **and** Intel — both build on a Mac)

```sh
./apply.sh /path/to/chromium/src
./build.sh /path/to/chromium/src arm64   # → src/out/aether       (Apple Silicon)
./build.sh /path/to/chromium/src x64     # → src/out/aether_x64   (Intel, cross-compiled)
```

Output is `Xplorer.app` in each `out/` dir.

### Windows (x64) — PowerShell

```powershell
.\apply.ps1 C:\path\to\chromium\src
.\build.ps1 C:\path\to\chromium\src      # → src\out\aether_win, builds Xplorer.exe
```

### Linux (x64)

```sh
./apply.sh /path/to/chromium/src
./build.sh /path/to/chromium/src linux   # → src/out/aether_linux, builds the `chrome` ELF
```

If `depot_tools`' gn/ninja wrappers misbehave (`python3_bin_reldir.txt not found`), call the
real binaries directly: `src/buildtools/linux64/gn gen out/aether_linux` then
`src/third_party/ninja/ninja -C out/aether_linux chrome`.

---

## 3. Package

| Platform | Command | Produces |
|----------|---------|----------|
| macOS    | `./scripts/package.sh <arch> <ver> /path/to/src` | `.dmg` + `.zip` (+ sign/notarize via `sign_and_notarize.sh`) |
| Windows  | `.\scripts\package.ps1` | portable `.zip` + `-installer.exe` |
| Linux    | `./scripts/package_linux.sh <out-dir> <ver> x64` | portable `.tar.gz` |
| Linux    | `./scripts/package_linux_installers.sh <out-dir> <ver> x64` | `.deb` + `.AppImage` |

`build/args.gn*` are plain GN configs copied verbatim into `out/<dir>/args.gn` by `build.sh`;
edit them to change build flags (release config, codecs, privacy args, etc.).

---

## 4. Remote / CI build hosts

Linux builds are typically run on a throwaway cloud VM — see `do-chromium-buildbox.sh`
(`provision` / `snapshot` / `restore` / `teardown`) for a DigitalOcean example that snapshots
the warm checkout so later builds restore in minutes instead of re-fetching. Any Linux host
with the pinned checkout works the same way. Windows/macOS builds run on their respective hosts.
