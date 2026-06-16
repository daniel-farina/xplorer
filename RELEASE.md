# Building & releasing Xplorer

This is the end-to-end process used to build Xplorer (the AI-native Chromium
fork) from source and publish a macOS release to GitHub. It also records the
gotchas hit along the way so you don't rediscover them.

> Layout note: the overlay lives in the `xplorer/` directory; the Chromium
> checkout sits next to it at `../chromium/src`. The product, app bundle, and
> repo are all "Xplorer". (`xplorer` is also the internal codename — the MCP
> tool prefix `xplorer_*` and the discovery path `~/.xplorer/gateway.json` use
> it too.) Repo: https://github.com/daniel-farina/xplorer (private)

---

## 0. Prerequisites (one time)

- macOS + Xcode (full Xcode, not just CLT) on Apple Silicon.
- ~100 GB free disk, several hours for the first build.
- `depot_tools` on `PATH`.
- For signed/notarized releases (§6): an Apple **Developer ID Application**
  certificate in the login keychain, and a stored notarytool keychain profile
  (see §6).

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

## 2. Overlay Xplorer onto Chromium

The fork is an *overlay*: new files are copied in, existing Chromium files are
edited by an idempotent, anchor-based Python patcher (so it survives upstream
churn better than context diffs).

```sh
./xplorer/apply.sh ./chromium/src
```

`apply.sh` does four things:
1. Copies `xplorer/src/chrome/...` (the `agent_gateway` + `grok_companion`
   components) into the tree.
2. Installs the **`.icns`** app icon (`xplorer/branding/app.icns` →
   `chrome/app/theme/chromium/mac/app.icns`).
3. Installs the **asset-catalog** app icon — the one macOS actually shows. It
   regenerates the `AppIcon.appiconset` PNGs from our icon AND recompiles
   `chrome/app/theme/chromium/mac/Assets.car` with `actool`, overwriting the
   prebuilt file. Both steps are **required** — see the icon gotcha below;
   replacing only `app.icns` (or only the appiconset) leaves the stock Chromium
   icon showing.
4. Runs `xplorer/patches/apply_integration.py` to wire the gateway into
   `chrome_browser_main.cc` + `chrome/browser/BUILD.gn`, default CDP on :9333,
   set the AI-native runtime flags, register the Grok toolbar icon, and apply
   Xplorer branding + Grok-as-default-search.

The patcher is **idempotent** — safe to re-run; it skips edits already present.
Its `edit()` helper *replaces* a block when the insertion restates the anchor's
first or last line, and *inserts* otherwise. (A first cut was insert-only and
silently skipped every replace-style patch — leaving e.g. an unconditional
`return` followed by dead code, which fails `-Werror,-Wunreachable-code`.)

---

## 3. Build

```sh
./xplorer/build.sh ./chromium/src
# = gn gen out/aether (with xplorer/build/args.gn) + autoninja -C out/aether chrome
```

`args.gn` highlights: `is_debug=false`, `is_component_build=false`,
`symbol_level=0`, `is_chrome_branded=false`, `target_cpu="arm64"`, `use_lld=true`.

The output bundle is `chromium/src/out/aether/Xplorer.app` (plus the
`Xplorer Helper*.app` child-process bundles). The build dir is kept as
`out/aether` on purpose: renaming it forces a from-scratch rebuild (~1.5 h on an
M-series Mac), whereas reusing it keeps rebuilds incremental.

### Incremental rebuilds

After editing source, just re-run `autoninja -C out/aether chrome`. Notes:
- The agent gateway and all UI code compile into **`Xplorer Framework.framework`**,
  not the launcher executable — so the `Xplorer.app/Contents/MacOS/Xplorer`
  binary's timestamp does NOT change on a rebuild. Check the *framework*
  timestamp to confirm a fresh build:
  ```sh
  stat -f "%Sm" "out/aether/Xplorer.app/Contents/Frameworks/Xplorer Framework.framework/Versions/Current/Xplorer Framework"
  ```
- Editing a `.grd` (string) file triggers a resource regen — expected.
- The build does **not** run `actool` on the appiconset — it copies the
  prebuilt `chrome/app/theme/chromium/mac/Assets.car` (see icon gotcha). So
  changing the appiconset PNGs has no effect unless `apply.sh` also recompiles
  that `Assets.car` (it does). After a rebuild, sanity-check the bundle icon by
  rendition size, not just mtime (commands in the icon gotcha).

---

## 4. Install locally for testing

```sh
osascript -e 'quit app "Xplorer"'; pkill -9 -f "/Applications/Xplorer.app"  # kill running instance
rsync -a --delete out/aether/Xplorer.app/ /Applications/Xplorer.app/
# Pre-signing builds only — strip quarantine so Gatekeeper allows the unsigned app:
xattr -dr com.apple.quarantine /Applications/Xplorer.app
open -a /Applications/Xplorer.app
```

Verify it's healthy:
```sh
cat ~/.xplorer/gateway.json            # written at startup: {url, token, ...}
curl -s http://127.0.0.1:9334/         # gateway discovery endpoint
```

Companion UI is served live from disk (no rebuild needed for HTML/CSS/JS edits).
`UiDir()` resolves it from `~/cli_experiment/xplorer/companion/ui` first, falling
back to `~/.xplorer/companion/ui`. If the running app serves stale UI, make sure
the repo folder is named `xplorer` (not an old codename) and that no stale copy
shadows it under `~/.xplorer/companion/ui`.

---

## 5. Package installers

```sh
./xplorer/scripts/package.sh "$(pwd)/chromium/src/out/aether" v0.5.0
```

Produces in `xplorer/dist/`:
- `Xplorer-macos-arm64.dmg` (drag-to-Applications installer; mount-tested)
- `Xplorer-macos-arm64.zip`
- `Xplorer-macos-arm64.sha256.txt` (checksums)

> **Package AFTER signing/notarizing (§6)**, so the artifacts contain the
> signed + stapled app. If you package first, re-run package after signing
> (or just sign the app in `out/aether`, then package).

---

## 6. Sign + notarize (REQUIRED for distribution)

Without this, downloaded copies are blocked on other Macs as **"Xplorer is
damaged and can't be opened"** — because Chromium's build only *ad-hoc*
(linker-)signs the app, and Gatekeeper rejects an un-notarized app that carries
the download quarantine flag.

**One-time:** store notarization credentials in the keychain (so the password /
key never lives in a script). Either an App Store Connect API key:

```sh
xcrun notarytool store-credentials "xplorer-notary" \
  --key   ~/.appstoreconnect/private_keys/AuthKey_XXXXXXXXXX.p8 \
  --key-id XXXXXXXXXX \
  --issuer "<issuer-uuid>"          # App Store Connect → Users and Access → Integrations
```

…or an Apple ID + app-specific password (from appleid.apple.com):

```sh
xcrun notarytool store-credentials "xplorer-notary" \
  --apple-id "<apple-id-email>" --team-id "<TEAMID>" --password "<app-specific-pw>"
```

**Each release:** sign the built app, then notarize + staple it:

```sh
./xplorer/scripts/sign_and_notarize.sh \
  "$(pwd)/chromium/src/out/aether/Xplorer.app" xplorer-notary
```

`sign_and_notarize.sh`:
0. Copies `companion/ui` into `Contents/Resources/companion/ui` **before
   signing** so the app is self-contained (see the companion-UI gotcha).
1. Deep-signs **every Mach-O** in the bundle, inside-out, with the Developer ID
   cert + hardened runtime (`--options runtime`) + secure timestamp, applying
   the right entitlements **per part** (vendored under `scripts/entitlements/`):
   the renderer + GPU helpers get `com.apple.security.cs.allow-jit` (V8 crashes
   without it under hardened runtime → every page "Can't open this page / Error
   code 5"); the main app gets the device/personal-info entitlements; everything
   else gets hardened runtime only. This includes the **bare framework helpers
   with no extension** — `chrome_crashpad_handler`, `app_mode_loader`,
   `web_app_shortcut_copier` — which a naive `*.dylib`/`*.app`-only sweep misses
   (and whose omission makes notarization fail "Invalid").
2. `codesign --verify --deep --strict` + `spctl`.
3. Zips and submits to Apple (`notarytool submit --wait`), then `stapler staple`s
   the ticket to the `.app`.

Then **(re)package** (§5) so the dmg/zip contain the signed+stapled app, and
also notarize + staple the **dmg** itself for a clean download:

```sh
xcrun notarytool submit dist/Xplorer-macos-arm64.dmg --keychain-profile xplorer-notary --wait
xcrun stapler staple dist/Xplorer-macos-arm64.dmg
# the .zip can't be stapled, but the app INSIDE it is — that's sufficient
# regenerate checksums AFTER stapling (the dmg bytes changed):
( cd dist && shasum -a 256 Xplorer-macos-arm64.zip Xplorer-macos-arm64.dmg > Xplorer-macos-arm64.sha256.txt )
```

Verify acceptance:
```sh
spctl -a -vvv -t exec out/aether/Xplorer.app    # -> accepted / source=Notarized Developer ID
xcrun stapler validate out/aether/Xplorer.app
```

If notarization comes back **Invalid**, pull the reason:
```sh
xcrun notarytool log <submission-id> --keychain-profile xplorer-notary /tmp/notary.json
```

---

## 7. Publish the GitHub release

```sh
cd xplorer
gh release create v0.5.0 --target master --title "Xplorer v0.5.0" --notes "…release notes…"
# upload assets ONE AT A TIME, in the foreground (see gotcha below)
gh release upload v0.5.0 dist/Xplorer-macos-arm64.dmg        --clobber
gh release upload v0.5.0 dist/Xplorer-macos-arm64.zip        --clobber
gh release upload v0.5.0 dist/Xplorer-macos-arm64.sha256.txt --clobber
# verify
gh release view v0.5.0 --json url,assets -q '.url, (.assets[]|"  \(.name)  \(.size)")'
```

---

## Gotchas (learned the hard way)

- **The app icon: the build SHIPS A PREBUILT `Assets.car` — it does NOT run
  `actool`.** This one cost hours. Chromium's `Info.plist` declares both
  `CFBundleIconFile` (`app.icns`) and `CFBundleIconName` (`AppIcon` in
  `Contents/Resources/Assets.car`), and modern macOS **prefers the asset
  catalog**. The trap: `chrome/BUILD.gn`'s `chrome_asset_catalog` is a
  `bundle_data` whose source is a **prebuilt, checked-in file** —
  `sources = [ "app/theme/<branding>/mac/Assets.car" ]`. It is copied verbatim;
  **the `AppIcon.appiconset` is never compiled by the build.** So editing
  `app.icns` *and* editing the appiconset PNGs both do nothing on their own —
  the prebuilt `Assets.car` (Chromium's blue icon) is what ships. `apply.sh` now
  regenerates the appiconset PNGs **and recompiles `Assets.car` with `actool`,
  overwriting the prebuilt file** (§2).
  **Verify the COMPILED output, not the source** (the lesson — source PNGs being
  right proves nothing). Compare rendition byte sizes: our simple X is tiny,
  Chromium's colorful icon is ~10–30× larger:
  ```sh
  xcrun assetutil --info out/aether/Xplorer.app/Contents/Resources/Assets.car \
    | python3 -c "import sys,json;[print(r['PixelWidth'],r['SizeOnDisk']) for r in json.load(sys.stdin) if r.get('Name')=='AppIcon' and r.get('AssetType')=='Icon Image']"
  # 512px ~6 KB = our X ✓   |   512px ~190 KB = still Chromium ✗
  ```
  (`qlmanage`/`NSWorkspace` icon rendering are unreliable headless — they
  produced empty/garbage output even for Safari — so don't trust a "rendered"
  thumbnail; trust the rendition sizes.)

- **Distribution requires sign + notarize (§6).** Ad-hoc/linker-signed builds
  open locally (after `xattr -dr com.apple.quarantine`) but are rejected as
  "damaged" when downloaded to another Mac. The fix is Developer ID signing +
  Apple notarization + stapling — not yet wired into `package.sh`/CI, so run
  `scripts/sign_and_notarize.sh` manually each release.

- **The companion UI must be bundled into the app.** The gateway serves the UI
  (the `/search` new tab, the injected toolbar on grok.com/x.com/grokipedia,
  the apps pages) from files on disk via `UiDir()` / `CompanionUiDir()`. Those
  resolvers check the bundle (`Contents/Resources/companion/ui`) first, then dev
  paths (`~/cli_experiment/xplorer/companion/ui`, `~/.xplorer/...`). On the build
  machine the dev paths exist so everything works; on **any other Mac they don't**
  — and the build does not bundle the UI. Symptoms when it's missing: `/search`
  returns the gateway's 401 `missing or invalid bearer token` as a page, and the
  injected toolbar logo on third-party sites renders **oversized/unstyled**
  (falls back to minimal hardcoded CSS). `sign_and_notarize.sh` copies
  `companion/ui` into the bundle before signing. NOTE: there are **two** copies
  of the resolver (`grok_native.cc::UiDir` and `grok_web_bar.cc::CompanionUiDir`)
  — keep them in sync. TODO: have the build bundle the UI via `bundle_data` and
  unify the resolver so self-containment doesn't depend on the signing script.

- **The macOS icon cache is sticky (local only).** After changing the icon, your
  *own* Dock may keep the old one even when the bundle is correct (this is
  cosmetic and does NOT affect other machines). Force a refresh:
  ```sh
  sudo rm -rf /Library/Caches/com.apple.iconservices.store
  /System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister -f /Applications/Xplorer.app
  killall Dock Finder
  ```
  (A logout/login or reboot does the same.) Don't confuse this with the
  asset-catalog bug above — that one *does* affect every machine.

- **Large asset uploads stall.** `gh release create … <files>` and bundled
  multi-file uploads frequently hung partway, leaving only the small `.sha256`
  attached. Fix: create the release first (notes only), then `gh release upload`
  **each large file separately** with `--clobber`, and poll
  `gh release view … --json assets` until both >1 MB assets show. Don't `pkill`
  a slow upload and assume failure — give it a few minutes.

- **CI can't build Chromium on GitHub-hosted runners** (~14 GB disk, 6 h cap vs.
  Chromium's ~100 GB / hours). `.github/workflows/release.yml` therefore targets
  a **self-hosted** macOS runner (label `xplorer-builder`) with a pre-synced
  checkout. Until one is registered, package + publish from a local build (steps
  5–7). Tagging `v*` triggers the workflow, so if there's no runner it just
  queues — cancel it: `gh run cancel <id>`.

- **Keychain prompt can block gateway startup.** On a normal launch macOS may
  prompt "Xplorer wants to use Confidential information stored in … Safe
  Storage". Until it's answered, profile init stalls and the gateway never
  writes `~/.xplorer/gateway.json`. Launching with `--password-store=basic`
  sidesteps the keychain for local testing.

- **A clean rebuild is slow but sometimes necessary.** A full build is ~1.5 h
  locally (no remote-exec; `--offline`). It's the price of renaming `out/<dir>`
  or recovering from a polluted output dir (stale bundles from old product
  names). Day-to-day, incremental rebuilds are minutes — avoid clean builds
  unless the output dir is wrong.

- **Upstream API drift breaks the overlay periodically.** Building against
  tip-of-tree Chromium has hit renames like `base::Value::Dict` →
  `base::DictValue`, `JSONReader::ReadDict` gaining a required options arg, and
  `BrowserList` → `BrowserWindowInterface` / `GetAllBrowserWindowInterfaces()`.
  Anchors in `apply_integration.py` can also move. Expect to fix a few of these
  when re-syncing Chromium.
