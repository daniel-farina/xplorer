# AI_DOCS.md — How to work on Xplorer (read this first)

This is the engineering reference for **AI models (and humans) editing this project**.
It captures the hard-won, non-obvious knowledge about how to develop, build, sign,
notarize, distribute, and modify Xplorer correctly. Most of these lessons cost
hours to learn the first time — read the relevant section before you touch that area.

> Companion docs:
> - **`RELEASE.md`** — the canonical end-to-end build/sign/notarize/publish runbook (commands).
> - **`AGENTS.md`** / **`docs/AGENT_API.md`** — the runtime Agent API + MCP (how agents *drive* the browser).
> - **This file** — how to *develop* the project itself.
>
> Naming note: the product/repo is **Xplorer**. `aether` is the old codename that
> survives in the **build output dir** (`out/aether`) on purpose (renaming it forces
> a multi-hour clean rebuild). A few stale `XBrowser`/`aether` strings linger in
> branding assets, `AGENTS.md`, and `sdk/xbrowser_mcp.py` — prefer the `xplorer`
> spellings (`sdk/xplorer_mcp.py`, `sdk/xplorer_sdk.py`, `~/.xplorer/`).

---

## 0. TL;DR for an AI editing this repo

1. **It's a Chromium overlay fork, not a standalone app.** Source lives here; it's
   copied/patched onto a separate `../chromium/src` checkout (~35M lines, ~100 GB).
2. **Know which kind of change you're making** — it decides your whole loop:
   - **Companion UI** (`companion/ui/*.html/.css/.js`) → served **live from disk**.
     Just **reload the tab (Cmd-R)**. No build, no reinstall.
   - **Native C++** (`src/chrome/browser/...`) or patches → `./apply.sh` →
     `./build.sh` → kill app → reinstall → relaunch. Minutes if incremental.
   - **Landing site** (`site/*`) → static, open via `file://`. Not served by the gateway.
3. **The patch engine (`patches/apply_integration.py`) is insert-with-replace, not a
   diff tool.** It silently does nothing on a malformed edit. See §3 — this is the #1
   source of "my change vanished" / "old build" bugs.
4. **Three "works on my machine" bugs already bit us** (icon, `/search` 401, toolbar
   CSS) — all the same root cause: code that only looked in dev paths and was never
   bundled into the `.app`. See §8.
5. **Always verify the *compiled/running* artifact, not the source you edited.**
   Confirm the framework rebuilt, the gateway on :9334 is serving your new file, the
   `Assets.car` renditions are the right size. "I edited the file" ≠ "it shipped".

---

## 1. Architecture

Xplorer is a **real Chromium fork** (Blink, V8, multiprocess sandbox — not Electron).
This repo is an **overlay** applied onto a sibling Chromium checkout:

```
cli_experiment/
├── chromium/src/          # the upstream checkout (NOT in this repo; ~100 GB)
│   └── out/aether/        # build output → Xplorer.app
├── depot_tools/           # on PATH for gn/autoninja
└── xplorer/               # THIS repo (the overlay)
```

Two integration mechanisms:

- **New C++ files** copied verbatim into the tree (`cp -R src/chrome → chromium/src/chrome`):
  - `src/chrome/browser/agent_gateway/` — the **AgentGateway** service compiled into
    the browser process; auto-starts at profile load. Serves the Agent API (HTTP+WS
    on :9334), the companion UI, omnibox/search redirect, `/switch-home`, `/api/logs`.
    Key file: `grok_native.cc`.
  - `src/chrome/browser/grok_companion/` — the **native toolbar overlay** injected on
    third-party sites, the new-tab→Grok redirect, "Ask Grok about this page". Key
    file: `grok_web_bar.cc`.
- **String-patches to existing Chromium files** via `patches/apply_integration.py`
  (anchor-based, idempotent — see §3).

Both subsystems compile into **`Xplorer Framework.framework`**, *not* the launcher
executable (important for verifying rebuilds — see §4).

### Runtime endpoints & paths

| What | Value |
|------|-------|
| Agent API (HTTP+WS) | `http://127.0.0.1:9334` |
| Raw CDP (Playwright/Puppeteer) | `ws://127.0.0.1:9333` |
| Discovery file | `~/.xplorer/gateway.json` → `{url, cdp_url, token, port}` |
| Auth | `Authorization: Bearer <token>` (token is in the discovery file) |
| App data root | `~/.xplorer/` (apps: `~/.xplorer/apps/<id>/`, registry `apps/registry.json`) |

> **`is_chrome_branded=false`** (unbranded source build). Consequences that are
> **normal, not bugs**: the keychain item reads "Chromium Safe Storage", the
> "Google API keys are missing" banner appears (only disables Sign-in/Sync), and the
> token dir is under `~/Library/Application Support/Chromium/` (says "Chromium", not
> "Xplorer"). Always read `~/.xplorer/gateway.json` for the token — don't hunt the profile dir.

---

## 2. Which change → which loop (do not skip this)

| You changed… | What it takes to see it | Why |
|--------------|------------------------|-----|
| `companion/ui/*` (HTML/CSS/JS) | **Cmd-R reload the tab** | Gateway serves these live from disk via `UiDir()`. No rebuild. |
| `src/chrome/browser/...` (C++) | `./apply.sh` → `./build.sh` → kill → reinstall → relaunch | Compiled into the framework. |
| `patches/apply_integration.py` | same as C++ (re-apply + build) | Edits the Chromium tree. |
| App icon (`branding/app.icns`) | `./apply.sh` (recompiles `Assets.car`) → build → reinstall | See §7 — build ships a prebuilt `Assets.car`. |
| `site/*` (landing page) | open the file via `file://` | Static; not served by the gateway. |

**Confirm the live UI is actually the new one** (it's easy to fool yourself):
```sh
TOK=$(python3 -c "import json;print(json.load(open('$HOME/.xplorer/gateway.json'))['token'])")
curl -s -H "Authorization: Bearer $TOK" http://127.0.0.1:9334/toolbar.css | grep -c "<some-token-from-your-edit>"
```

---

## 3. The patch engine — `patches/apply_integration.py` (CRITICAL)

This is the single biggest source of silent breakage. **It is not a diff/patch tool.**

- `edit(path, anchor, insertion)` **inserts `insertion` after `anchor`**, with a
  first-line idempotency skip. The hardened version also **replaces** a block when
  the insertion *restates the anchor* (heuristic: the insertion's **first or last line
  matches the anchor** — rewrites typically restate a closing line like `return true;`).
  Otherwise it inserts.
- **Failure mode:** if your "replacement" insertion doesn't trip the restate
  heuristic, the edit **silently inserts (or skips entirely) and your intended change
  never happens.** Classic outcome: an early/unconditional `return` gets inserted
  ahead of existing code → orphaned dead code → the build dies on
  `-Werror,-Wunreachable-code` or `-Werror,-Wunused-function`. This already bit
  `version_updater_mac.mm` (fixed with `[[maybe_unused]]`) and
  `ai_mode_page_action_icon_view.cc`.
- **A revert-to-pristine + re-apply can DESTROY working changes** that were only in
  the tree (manually applied earlier) and that the patch engine can't reproduce. This
  happened once and wiped CSS/redirects — *"a lot of changes are gone… seems like an
  older build."* Be very careful re-applying onto a tree that has manual edits.

**Rules when editing patches:**
1. For a true replacement that doesn't fit the restate heuristic, do an explicit
   inline `read_text()` / `.replace()` / `write_text()` instead of `edit()`.
2. After `./apply.sh`, **open the patched files in the Chromium tree and verify they
   actually look right.** Do not assume `apply.sh` produced what you intended.
3. **Proactively scan for the "unconditional `return` mid-function followed by code"
   antipattern** before building — each failed build costs ~12 min (or far more on a
   clean tree).

**Patches currently applied** (wire-in + branding + Grok-as-default-search):
- `chrome_browser_main.cc` + `chrome/browser/BUILD.gn` — start the gateway, link the components.
- Default CDP on :9333, AI-native runtime flags, register the Grok toolbar vector icon.
- `prepopulated_engines.json` — Google entry → **Grok**; `search_url =
  http://127.0.0.1:9334/omnibox?q={searchTerms}`; bump `kCurrentDataVersion` (e.g.
  206→207) so the new default engine re-merges into **existing** profiles.
- `settings_chromium_strings.grdp` — "About Xplorer".
- `omnibox_strings.grdp` — "Ask Google" → "Ask Grok".
- `chrome_autocomplete_provider_client.cc` — `OpenLensOverlay` → `grok_companion::AskGrokAboutPage(...)`.
- `version_updater_mac.mm` — report UPDATED + `[[maybe_unused]]`.

> Upstream drift: building against tip-of-tree Chromium periodically breaks anchors
> and APIs (e.g. `base::Value::Dict`→`base::DictValue`, `BrowserList`→
> `BrowserWindowInterface`). Expect to fix a few of these when re-syncing.

---

## 4. The build / install / relaunch loop

```sh
./apply.sh                          # overlay files + recompile icon + run patches (idempotent)
./build.sh > /tmp/build.log 2>&1    # gn gen out/aether + autoninja -C out/aether chrome

# CLEAN KILL — you MUST confirm :9334 is free or a stale gateway serves old code:
osascript -e 'quit app "Xplorer"' 2>/dev/null; sleep 2
pkill -9 -f "Xplorer.app/Contents" 2>/dev/null; sleep 2
lsof -nP -iTCP:9334 -sTCP:LISTEN >/dev/null 2>&1 && echo "STILL BOUND — wait" || echo "port free"

# INSTALL:
rsync -a --delete ../chromium/src/out/aether/Xplorer.app/ /Applications/Xplorer.app/
xattr -dr com.apple.quarantine /Applications/Xplorer.app   # pre-signing builds only

# RELAUNCH and wait for the gateway:
open -a /Applications/Xplorer.app
for i in $(seq 1 30); do test -f ~/.xplorer/gateway.json && lsof -nP -iTCP:9334 -sTCP:LISTEN >/dev/null 2>&1 && break; sleep 1; done
```

**Gotchas:**
- **Build dir is `out/aether`, bundle is `Xplorer.app`.** (`build.sh` still *echoes*
  "Chromium.app" — stale text, ignore it.) **Do NOT rename the build dir or product
  name** — it invalidates the ~58k-object cache and forces a ~1.5 h clean rebuild,
  and litters `out/aether` with stale bundles from old names.
- **Confirm :9334 is free before relaunch.** A surviving old process keeps serving old
  code and makes it look like your change didn't take.
- **The launcher binary's timestamp does NOT change on rebuild** — the code is in the
  framework. To confirm a fresh build, check the *framework*:
  ```sh
  stat -f "%Sm" "../chromium/src/out/aether/Xplorer.app/Contents/Frameworks/Xplorer Framework.framework/Versions/Current/Xplorer Framework"
  ```
- **Incremental builds are minutes; a clean build is the long pole (~1.5 h).** Only do
  a clean build to recover a polluted output dir or after a dir/name rename.
- **Keychain prompt can stall startup.** macOS may prompt about "Safe Storage"; until
  answered, the gateway never writes `gateway.json`. For local testing add
  `--password-store=basic`.

**Live verification via the SDK:**
```sh
export XPLORER_TOKEN=$(python3 -c "import json;print(json.load(open('$HOME/.xplorer/gateway.json'))['token'])")
python3 - <<'PY'
from xplorer_sdk import Browser
b = Browser(); tab = b.open("https://example.com")["owner"]
print(b.eval(tab, "document.title"))
PY
```

---

## 5. Code signing (`scripts/sign_and_notarize.sh`)

**Why it's mandatory:** Chromium's build only *ad-hoc* (linker-)signs the app
(`TeamIdentifier=not set`). That opens locally after `xattr -dr com.apple.quarantine`,
but when **downloaded to another Mac** the quarantine flag + missing Developer ID →
Gatekeeper rejects it as **"Xplorer is damaged and can't be opened."** The fix is
Developer ID signing + notarization + stapling.

- Identity on this machine: **`Developer ID Application: Daniel Farina`**, Team ID
  **`ST6RKUS2KP`**. Find it: `security find-identity -v -p codesigning | grep "Developer ID Application"`.

### The three signing lessons (in the order they bit us)

1. **Sign EVERY Mach-O, regardless of extension.** Three helpers are *loose Mach-O
   executables with no extension* — `chrome_crashpad_handler`, `app_mode_loader`,
   `web_app_shortcut_copier`. A `*.dylib`/`*.app`-only `find` sweep misses them, they
   keep ad-hoc signatures, and **notarization comes back "Invalid."**

2. **`allow-jit` entitlement — the worst one.** The ad-hoc build embeds **no
   entitlements**, so you cannot "reuse the binary's existing entitlements." If the
   **renderer** and **GPU** helpers are signed under hardened runtime *without*
   `com.apple.security.cs.allow-jit`, **V8 can't allocate JIT memory and every page
   fails with "Can't open this page / Error code 5."** Notarization passes; the app is
   broken. Fix: apply entitlements **by helper type** (as Chromium intends), vendored
   in `scripts/entitlements/`:
   - `helper-renderer.plist`, `helper-gpu.plist` → include **`allow-jit`**.
   - `app.plist` → main app device/personal-info entitlements.
   - base/alerts helpers, loose Mach-O execs, dylibs, the framework → **hardened
     runtime only, no entitlements**.

3. **Sign inside-out, app last.** Sign nested helpers/frameworks/dylibs first, then
   deep-sign the app. **Bundle the companion UI BEFORE signing** (§8) so it's covered.

Sign with `--options runtime` (hardened runtime) + the Developer ID cert + secure
timestamp. Use the script's **`--sign-only` mode** to verify locally before paying the
notarization round-trip.

```sh
# verify allow-jit actually landed on the renderer helper:
codesign -d --entitlements - --xml \
  "/Applications/Xplorer.app/Contents/Frameworks/Xplorer Framework.framework/Versions/Current/Helpers/Xplorer Helper (Renderer).app" \
  | grep -q allow-jit && echo "allow-jit OK"
# whole-bundle validity:
codesign --verify --deep --strict /Applications/Xplorer.app
```

---

## 6. Notarization

- **Use the App Store Connect API key** (preferred), not an app-specific password.
  Key on disk: `~/.appstoreconnect/private_keys/AuthKey_<KEYID>.p8`. The `.p8` is the
  secret; the **Issuer ID is not secret**.
- **Store credentials once** in the keychain:
  ```sh
  xcrun notarytool store-credentials "xplorer-notary" \
    --key   ~/.appstoreconnect/private_keys/AuthKey_<KEYID>.p8 \
    --key-id <KEYID> \
    --issuer "<issuer-uuid>"
  xcrun notarytool history --keychain-profile xplorer-notary   # confirm it authenticates
  ```
- **Submit → wait → staple → validate:**
  ```sh
  xcrun notarytool submit <app-or-dmg> --keychain-profile xplorer-notary --wait   # Accepted | Invalid
  xcrun notarytool log <submission-id> --keychain-profile xplorer-notary /tmp/notary.json   # failure reasons
  xcrun stapler staple "$APP"; xcrun stapler validate "$APP"
  spctl -a -vvv -t exec "$APP"    # → accepted / source=Notarized Developer ID
  ```
- **Notarize the app AND the dmg.** Notarize/staple the app, package it, then
  notarize/staple the dmg. A dmg is **not code-signed itself** — `spctl … no usable
  signature` on the dmg is **expected/cosmetic**; `stapler validate` passing is what
  counts. The `.zip` can't be stapled, but the app inside it is — sufficient.
- **Notarization is fast (minutes); the build is the bottleneck.** Don't notarize an
  interim artifact that still has known bugs (e.g. wrong icon) — sign/notarize once on
  the final build.

---

## 7. The app icon (it WILL fight you)

Two layered root causes — both must be handled, or the stock blue Chromium icon ships:

1. **macOS prefers the asset catalog over `app.icns`.** `Info.plist` declares both
   `CFBundleIconFile` (`app.icns`) **and** `CFBundleIconName: AppIcon` (in
   `Assets.car`). Modern macOS uses **`Assets.car`**, so replacing only `app.icns` does
   nothing.
2. **The build does NOT run `actool` — it ships a prebuilt, checked-in `Assets.car`.**
   `chrome/BUILD.gn`'s `chrome_asset_catalog` is a `bundle_data` whose source is
   `app/theme/chromium/mac/Assets.car`. It's **copied verbatim**; the
   `AppIcon.appiconset` is **never compiled.** So editing the appiconset PNGs alone
   also does nothing.

**The fix (already baked into `apply.sh`):** regenerate the appiconset PNGs from
`branding/app.icns` with `sips`, then **recompile `Assets.car` with `actool` and
overwrite the prebuilt file**. (If `actool` fails, `apply.sh` prints a WARNING and the
icon will be wrong — watch for it.)

**Verify the COMPILED output, not the source.** Headless renderers (`qlmanage`,
`NSWorkspace`, JXA) gave garbage/empty output even for *Safari* — do not trust them.
Trust **rendition byte sizes**: our mostly-black X compresses tiny (~6 KB at 512px);
Chromium's colorful icon is ~190 KB.
```sh
xcrun assetutil --info "/Applications/Xplorer.app/Contents/Resources/Assets.car" \
  | python3 -c "import sys,json;[print(r['PixelWidth'],r['SizeOnDisk']) for r in json.load(sys.stdin) if r.get('Name')=='AppIcon' and r.get('AssetType')=='Icon Image']"
# 512 ~6000 = our X ✓   |   512 ~190000 = still Chromium ✗
```

**Distinguish the two icon problems:** the asset-catalog bug affects **every machine**.
A sticky **local Dock icon** is cosmetic and local-only — refresh it:
```sh
sudo rm -rf /Library/Caches/com.apple.iconservices.store
/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister -f /Applications/Xplorer.app
killall Dock Finder
```
Don't let "probably a stale download/cache" excuse hide a real `Assets.car` bug —
check the renditions.

---

## 8. Distribution & the "works on my machine" bug class

The gateway serves the companion UI (`/search`, toolbar, apps) **from files on disk**.
The resolvers historically only checked **dev paths**, and the build never bundled the
UI — so on **any other Mac** the files don't exist. This same class of bug surfaced
three times:

- **(a) Icon** — §7 (asset catalog not compiled).
- **(b) `/search` shows raw JSON / 401** — `UiDir()` (`grok_native.cc`) couldn't find
  `search.html`, the request fell through to the auth-required API path, and the
  gateway's `{"error":"missing or invalid bearer token"}` (401) rendered as a page.
  Looked *"as if the browser has no internet."*
- **(c) Oversized/unstyled toolbar logo on x.com/grokipedia** — `CompanionUiDir()`
  (`grok_web_bar.cc`, a **separate duplicate** of the same resolver) couldn't find
  `toolbar.css`, so the native overlay fell back to minimal hardcoded CSS that doesn't
  size the logo SVG.

**The fix (committed):** both resolvers now check the **bundled** location first —
`Contents/Resources/companion/ui` (computed from the executable dir) — with dev paths
as fallback. **`sign_and_notarize.sh` copies `companion/ui` into the bundle BEFORE
signing** so it's covered by the signature.

> There are **two** copies of the resolver (`UiDir` and `CompanionUiDir`) — **keep
> them in sync.** TODO worth doing: unify them, and have the *build* bundle the UI via
> GN `bundle_data` so self-containment doesn't depend on the signing script.

**Clean-room test** (simulate another machine): remove/rename the dev UI paths, then
confirm `/search` returns **HTTP 200** from the bundle. (Launching with a fake empty
`HOME` also works but triggers a spurious "Keychain Not Found" dialog — a test
artifact, not a real-user issue.)

**Publishing releases (`gh`):** large uploads stall and 404 intermittently. Create the
release with notes only, then upload **one file at a time** with retries + `--clobber`;
**regenerate the checksum after stapling** (the dmg bytes changed). See `RELEASE.md` §7.
Note: CI can't build Chromium on GitHub-hosted runners (disk/time limits) — a
self-hosted macOS runner (`xplorer-builder`) is required; until then, build/publish locally.

---

## 9. The toolbar (native overlay + companion)

**One markup source, two surfaces:**
- **Companion pages** mount the toolbar via JS — `mountGrokToolbar()` in
  `companion/ui/common.js` fetches `/toolbar.html`.
- **Native overlay** on third-party sites (x.com, grok.com, grokipedia, xchat) is
  injected by `grok_web_bar.cc` as a self-contained IIFE in an **isolated world**
  (`ExecuteJavaScriptInIsolatedWorld`).

**Single-source rule:** canonical markup is `companion/ui/toolbar.html`.
- **DEAD END:** having the native bar `fetch('/toolbar.html')` cross-origin. Third-
  party **CSP `connect-src` blocks it** (grokipedia → `Failed to fetch`), even though
  the gateway sends `Access-Control-Allow-Origin: *`.
- **CORRECT:** C++ reads `toolbar.html`/`toolbar.css` from disk (`LoadToolbarHtml()` /
  `LoadToolbarCss()`) and **bakes the markup into the injected IIFE** — no cross-origin
  fetch, still live-editable (re-read on every injection). A minimal hardcoded HTML/CSS
  fallback stays in C++ for when the file can't be read.
- `LoadToolbarHtml()` rewrites root-relative `href="/..."` to absolute gateway URLs via
  `base::ReplaceSubstringsAfterOffset` — it's a naive string replace, **only the
  canonical double-quote form is guaranteed safe.**

**"Push content down" (offset) — the transform trap:**
- Pad `<html>` **OR** `<body>`, **not both** (double offset otherwise).
- Sites with `position:fixed` chrome at `top:0` (x.com) ignore document padding.
- **DEAD END:** an "adaptive" `transform: translateY()` fallback. On scroll,
  `getBoundingClientRect().top` goes negative → misdetected as a fixed-layout page →
  applies `transform` to `<html>`, but the toolbar is a *child* of `<html>`, so it
  pushes the bar itself down → **gap at top on scroll**.
- **FIX: padding-only, no transform** (`applyPadding()` in `grok_web_bar.cc`).
  Re-assert padding idempotently on SPA navigations (sites strip inline padding).
  Accept that ~20px of x.com's fixed shell stays covered.

**Pill navigation parity:** home pills (`build`/`web`/`wiki`) must go through
`http://127.0.0.1:9334/switch-home?mode=X` (not directly to `/` or external
grokipedia) to preserve the home-switch side effect and keep wiki in the same window.
Canonical: `<a data-home data-pill href="/switch-home?mode=X">`. `/switch-home`
redirects build→`/`, web→`/search`, wiki→grokipedia (in `grok_native.cc`).

**Native fallback `StringPrintf` trap:** the C++ fallback HTML uses
`base::StringPrintf` — the `%s` count must exactly match the `c_str()` arg list, and
every button the JS wires (`.grok-toolbar-hide`, `.grok-settings-btn`) must exist, or
you crash / lose controls.

**Logo / hover-reveal:** an X mark (SVG) + "plorer" text revealed on hover
(`span{max-width:0;opacity:0}` → `:hover span{max-width:8ch}`). Same logo on the hero
(`.hero__wordmark`), nav (`.nav__brand`), and toolbar (`.grok-logo`). Minimized bar
shows a **draggable reveal pill** (localStorage `xplorer_toolbar_reveal_pos`); hidden
state in `xplorer_toolbar_hidden`. Glass uses `backdrop-filter` — include an
`@supports not(...)` opaque fallback (light + dark).

---

## 10. Other reusable gotchas

- **A companion page that looks "broken/unstyled" usually just lacks the shared base
  `body` rule** — not a JS failure. (`settings.css` rendered in Times serif because it
  never set base `body` styles; other pages inherit them from `apps.css`.) Check the
  CSS base before debugging "JS not loading."
- **Apps gallery iframe flicker (~1 s):** `runtime_alive` is a live process check that
  genuinely flaps; it was in the render signature, so `grid.innerHTML=''` rebuilt every
  card each poll. **Fix: drop `runtime_alive` from the render signature** (update it in
  place). Don't change the poll interval.
- **Blank iframe thumbnails:** `loading="lazy"` never fires for a scaled/clipped iframe
  → blank `about:blank`. **Remove `loading="lazy"`.**
- **"Grok build failed" on app edit/resume:** the edit path passed `-r <session_id>`,
  but grok's one-shot sessions aren't persisted (`Session does not exist`, exit 1).
  **Fix: don't pass `-r` on edits** — `--cwd` already gives grok the current files.
- **Build stderr is discarded** in `PumpGrokStream` (stderr would inject ANSI into
  NDJSON). Failure reasons were unrecoverable until a separate stderr capture +
  `RecordGatewayLog` ring buffer + `GET /api/logs` + a `/logs` page were added.
- **Omnibox → Grok:** `GET /omnibox?q=...` url-decodes, stores a pending id, and
  302s to `grok.com/#xplorer_grok=<id>` (auto-submits). Verify the default engine via
  the profile keyword DB (`prepopulate_id=1` → Grok), **not** the Settings WebUI
  (shadow DOM → `innerText` empty).
- **Landing page (`site/`)** is static — verify via `file://`. Dark mode via
  `:root[data-theme="dark"]` + localStorage `xplorer-theme` (NOT `prefers-color-scheme`).
- **Transparent-PNG framing:** `box-shadow`+`border-radius` on a window-on-transparent-
  canvas screenshot draws a rectangular frame through the alpha margin. Use
  `filter: drop-shadow()` (follows the alpha) and drop `border-radius`.
- **`requestAnimationFrame` is throttled in background tabs** → animate-in elements stay
  invisible. Add a `setTimeout(reveal, 150)` fallback.
- **Keep the landing-page wordmark alignment tool** (gated by `?align`) — do NOT remove
  it after baking values. (It was removed once and that was the wrong call.)

---

## 11. Key files

| Area | Files |
|------|-------|
| Overlay / build | `apply.sh`, `build.sh`, `build/args.gn` → `out/aether` |
| Patch engine | `patches/apply_integration.py` |
| Gateway / UI / omnibox / logs / apps | `src/chrome/browser/agent_gateway/grok_native.cc` (`UiDir`, `/omnibox`, `/switch-home`, `/api/logs`, `PumpGrokStream`), `agent_gateway.cc`, `app_store.cc` |
| Native toolbar overlay | `src/chrome/browser/grok_companion/grok_web_bar.cc` (`CompanionUiDir`, `LoadToolbarHtml/Css`, `applyPadding`), `grok_companion_util.{cc,h}` (NTP redirect, `AskGrokAboutPage`) |
| Companion UI (live, no rebuild) | `companion/ui/*` — `toolbar.html/.css`, `common.js`, `apps.js`, `app-view.js`, `search.*`, `settings.*`, `logs.*`, `newtab-bg.js` |
| Signing | `scripts/sign_and_notarize.sh`, `scripts/entitlements/{app,helper-renderer,helper-gpu}.plist`, `scripts/package.sh` |
| Icon | `branding/app.icns`; in the tree: `chrome/app/theme/chromium/mac/{app.icns, Assets.xcassets/AppIcon.appiconset, Assets.car}` |
| SDK / MCP | `sdk/xplorer_sdk.py`, `sdk/xplorer_mcp.py`, `sdk/agent_context.py` |
| Docs | `RELEASE.md` (runbook), `AGENTS.md` + `docs/AGENT_API.md` (runtime API), this file |

---

## 12. The meta-lesson

Forking Chromium means a ~35M-line / ~100 GB codebase, ~1.5 h clean builds, an
insert-based patch engine whose anchors drift with every upstream release, and the long
tail of signing / notarization / Gatekeeper / self-contained bundling. The bugs that
hurt most weren't logic bugs — they were **"works on my machine"** bugs (icon, UI
bundling, entitlements) that only appear on a *second* computer, and **"I edited the
source but never verified the compiled artifact"** bugs. When in doubt: verify the
thing that actually ships (the framework timestamp, the running gateway's response, the
`Assets.car` renditions, the signed entitlements), not the file you typed into.
