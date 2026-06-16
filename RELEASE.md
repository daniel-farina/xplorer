# Building & releasing XBrowser

This is the end-to-end process used to build XBrowser (the AI-native Chromium
fork) from source and publish a macOS release to GitHub. It also records the
gotchas hit along the way so you don't rediscover them.

> Layout note: the overlay lives in the `xplorer/` directory (kept as the
> internal codename); the Chromium checkout sits next to it at `../chromium/src`.
> Repo: https://github.com/daniel-farina/xplorer (private)

---

## 0. Prerequisites (one time)

- macOS + Xcode (full Xcode, not just CLT) on Apple Silicon.
- ~100 GB free disk, several hours for the first build.
- `depot_tools` on `PATH`.

```sh
git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git
export PATH="$PWD/depot_tools:$PATH"
```

---

## 1. Fetch the Chromium source

```sh
mkdir chromium && cd chromium
fetch --no-history chromium          # ~30 GB, the long download
cd src
gclient runhooks                     # toolchain, clang, etc.
cd ../..
```

`--no-history` keeps the checkout much smaller. `runhooks` must finish before
`gn gen` or the build will fail on missing toolchain.

---

## 2. Overlay XBrowser onto Chromium

The fork is an *overlay*: new files are copied in, existing Chromium files are
edited by an idempotent, anchor-based Python patcher (so it survives upstream
churn better than context diffs).

```sh
./xplorer/apply.sh ./chromium/src
```

`apply.sh` does three things:
1. Copies `xplorer/src/chrome/...` (the `agent_gateway` component) into the tree.
2. Installs the app icon (`xplorer/branding/app.icns` → `chrome/app/theme/chromium/mac/app.icns`).
3. Runs `xplorer/patches/apply_integration.py` to wire the gateway into
   `chrome_browser_main.cc` + `chrome/browser/BUILD.gn`, default CDP on :9333,
   set the AI-native runtime flags, and apply XBrowser branding.

The patcher is **idempotent** — safe to re-run; it skips edits already present.

---

## 3. Build

```sh
./xplorer/build.sh ./chromium/src
# = gn gen out/xplorer (with xplorer/build/args.gn) + autoninja -C out/xplorer chrome
```

`args.gn` highlights: `is_debug=false`, `is_component_build=false`,
`symbol_level=0`, `is_chrome_branded=false`, `target_cpu="arm64"`, `use_lld=true`.

The output bundle is `chromium/src/out/xplorer/XBrowser.app` (plus the
`XBrowser Helper*.app` child-process bundles).

### Incremental rebuilds

After editing source, just re-run `autoninja -C out/xplorer chrome`. Two notes:
- The agent gateway and all UI code compile into **`XBrowser Framework.framework`**,
  not the launcher executable — so the `XBrowser.app/Contents/MacOS/XBrowser`
  binary's timestamp does NOT change on a rebuild. Check the *framework*
  timestamp to confirm a fresh build:
  ```sh
  stat -f "%Sm" "out/xplorer/XBrowser.app/Contents/Frameworks/XBrowser Framework.framework/Versions/Current/XBrowser Framework"
  ```
- Editing a `.grd` (string) file triggers a resource regen — expected.

---

## 4. Install locally for testing

```sh
pkill -9 -f "XBrowser.app"            # kill any running instance first
rm -rf /Applications/XBrowser.app
ditto out/xplorer/XBrowser.app /Applications/XBrowser.app
xattr -dr com.apple.quarantine /Applications/XBrowser.app   # self-signed build
open -a /Applications/XBrowser.app --args \
  --user-data-dir=/tmp/xb --no-first-run --password-store=basic
```

Verify it's healthy:
```sh
cat ~/.xplorer/gateway.json            # written at startup: {url, token, ...}
curl -s http://127.0.0.1:9334/        # gateway discovery endpoint
```

---

## 5. Package installers

```sh
./xplorer/scripts/package.sh "$(pwd)/chromium/src/out/xplorer" v0.2.0
```

Produces in `xplorer/dist/`:
- `XBrowser-macos-arm64.dmg` (drag-to-Applications installer; mount-tested)
- `XBrowser-macos-arm64.zip`
- `XBrowser-macos-arm64.sha256.txt` (checksums)

---

## 6. Publish the GitHub release

```sh
cd xplorer
git tag v0.2.0 && git push origin v0.2.0
gh release create v0.2.0 --title "XBrowser v0.2.0" --notes "…release notes…"
# upload assets ONE AT A TIME, in the foreground (see gotcha below)
gh release upload v0.2.0 dist/XBrowser-macos-arm64.dmg
gh release upload v0.2.0 dist/XBrowser-macos-arm64.zip
gh release upload v0.2.0 dist/XBrowser-macos-arm64.sha256.txt
# verify
gh release view v0.2.0 --json url,assets -q '.url, (.assets[]|"  \(.name)  \(.size)")'
```

---

## Gotchas (learned the hard way)

- **Large asset uploads stall.** `gh release create … <files>` and bundled
  multi-file uploads frequently hung partway, leaving only the small `.sha256`
  attached. Fix: create the release first (notes only), then `gh release upload`
  **each large file separately**, and poll `gh release view … --json assets`
  until both >1 MB assets show. Don't `pkill` a slow upload and assume failure —
  give it a few minutes.

- **CI can't build Chromium on GitHub-hosted runners** (~14 GB disk, 6 h cap vs.
  Chromium's ~100 GB / hours). `.github/workflows/release.yml` therefore targets
  a **self-hosted** macOS runner (label `xplorer-builder`) with a pre-synced
  checkout. Until one is registered, package + publish from a local build (steps
  5–6). Tagging `v*` triggers the workflow, so if there's no runner it just
  queues — cancel it: `gh run cancel <id>`.

- **The macOS icon cache is sticky.** After changing the app icon, the Dock may
  keep showing the old/blue icon even though the bundle is correct. The
  bundle-level `Contents/Resources/app.icns` is authoritative (verify with
  `sips -s format png …/app.icns --out /tmp/i.png`). To force a refresh you need
  the *system* cache, which requires sudo:
  ```sh
  sudo rm -rf /Library/Caches/com.apple.iconservices.store; killall Dock Finder
  ```
  (A logout/login or reboot does the same.) Clearing only the user cache + Dock
  restart is often not enough.

- **Keychain prompt can block gateway startup.** On a normal launch macOS may
  prompt "XBrowser wants to use Confidential information stored in … Safe
  Storage". Until it's answered, profile init stalls and the gateway never
  writes `~/.xplorer/gateway.json`. Launching with `--password-store=basic`
  sidesteps the keychain for local testing.

- **Self-signed = Gatekeeper friction.** Users must
  `xattr -dr com.apple.quarantine /Applications/XBrowser.app` on first launch.
  Proper `codesign` + `notarytool` (needs an Apple Developer ID) would remove
  this; not yet wired into `package.sh`/the workflow.

- **Internal names kept stable on purpose.** The overlay dir (`xplorer/`), the
  MCP tool prefix (`xplorer_*`), and the discovery path (`~/.xplorer/gateway.json`)
  remain under the `xplorer` codename even though the product is "XBrowser" — so
  existing agent configs don't break. Only the user-visible product/app name and
  the repo were renamed.

- **Upstream API drift breaks the overlay periodically.** Building against
  tip-of-tree Chromium has hit renames like `base::Value::Dict` →
  `base::DictValue`, `JSONReader::ReadDict` gaining a required options arg, and
  `BrowserList` → `BrowserWindowInterface` / `GetAllBrowserWindowInterfaces()`.
  Expect to fix a few of these when re-syncing Chromium.
</content>
