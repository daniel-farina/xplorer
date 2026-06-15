# Xplorer improvement log

Autonomous 15-minute improvement loop. Each entry: change, test result, commit.

---

## 2026-06-14 — Loop session start

**Baseline:** Rebrand to Xplorer pending commit. Prioritized fixes from codebase audit:
1. Search image attachment never sent
2. Apps gallery missing delete
3. App builder chat missing Enter-to-send
4. Search home toggle desync
5. switch-home drops query context
6. Stale app "building" status after restart

### Commit 6199d0e — Xplorer rebrand baseline
- Migrated branding, ~/.xplorer data dir, welcome screen

### Commit (search) — feat(search): mode pills, vision search, inline results
- **search.js/html/css:** Mode pills, native vision search when image attached,
  imagine uses native API, web/videos use Grok Web handoff, results panel with
  media grids, URL sync for q/mode, home toggle uses settings not hardcoded web
- **Test:** Companion UI only — no native rebuild required for static files

### Commit 2ed53b4 — feat(search)
See above. Verified: search page serves mode pills + results section (curl).

### Commit 1d69fa8 — feat(apps)
- Delete button, Enter-to-send in app builder, reconcile stale builds, switch-home q/m
- **Test:** `switch-home?mode=build&q=testquery&m=images` → `Location: .../search?q=testquery&mode=images` ✓
- Build succeeded (40s), reinstalled to /Applications/Xplorer.app

### Commit 1d417f4 — fix(nav)
- Grok Web handoff polling capped; legacy hash support; app Preview button
- Build succeeded (42s), reinstall + relaunch

## Loop 1 test summary (2026-06-14 ~01:30)
| Check | Result |
|-------|--------|
| Gateway discovery | `xplorer-agent-gateway` ✓ |
| /search | 200, mode pills present ✓ |
| /welcome | Xplorer h1 ✓ |
| /api/apps | 6 apps listed ✓ |
| switch-home query preserve | q + mode in Location ✓ |
| Data dir | ~/.xplorer ✓ |

## Loop 2 (2026-06-14)

### Commit 94fec7c — feat(ui)
- App builder delete button; search streaming UI; welcome tour links

### Commit d53a3b1 — fix(native)
- Runtime relaunch via WaitForExitWithTimeout; toolbar SPA nav; switch-home Connection: close

### Commit (apps) — fix WaitForExitWithTimeout build
- Replaced nonexistent Process::IsRunning() with Chromium API

**Test:** Build 41s ✓ | switch-home returns 302 immediately ✓ | smoke test passed ✓

## Loop 3 (2026-06-14)

### Commit afcfa33 — feat(apps) UI
- Rename + duplicate gallery buttons; search mode localStorage; chat model from settings

### Commit 4ab8426 — feat(fab) native
- POST /api/apps/{id}/rename and /duplicate
- Grok FAB "Build app from page" menu item
- switch-home 302 with explicit empty body

**Test:** Build 40s ✓ | rename API ✓ | smoke passed ✓

## Loop config (2026-06-14)

- Scheduler task `019ec4b284f0`: **15m** recurring (`intervalSecs: 900`)
- GitHub repo renamed **xbrowser → xplorer**, visibility set to **private**
- Remote: `https://github.com/daniel-farina/xplorer.git`

## Loop 4 (2026-06-14 ~02:00)

### Commit 5bce270 — feat(ui)
- **Videos:** `usesNativeSearch()` includes `videos` → `/api/search/stream` + results grid
- **Chat:** `renderMessages()` renders assistant replies with `renderMarkdown()`
- **Toolbar:** `syncCompanionToolbarPill()` highlights Grok Web on `/search`, Build on `/` `/apps`
- **Apps:** Export button in gallery (links to export API)
- **apps.html:** Search link in Grok Web pill menu
- **Test:** curl `/search.js` shows video native path ✓ (no native rebuild for UI)

### Commit ad170d3 — feat(apps) native
- `GET /api/apps/{id}/export` → `application/zip` with `Content-Disposition`
- Zip runs on `MayBlock` thread pool (fixes IO-thread deadlock with `/usr/bin/zip`)
- **Test:** Build 41s ✓ | reinstall Xplorer.app ✓ | export `B7A73401C992B4D9` → 200, 220-byte zip with index.html ✓

**Loop 4 test summary**
| Check | Result |
|-------|--------|
| Companion /api/apps | 200 ✓ |
| Export zip | 200, valid zip archive ✓ |
| search.js video native | `mode === 'videos'` in usesNativeSearch ✓ |
| common.js toolbar sync | syncCompanionToolbarPill present ✓ |
| Build + reinstall | 41s, relaunch OK ✓ |

## Loop 5 (2026-06-14 ~02:15)

### Commit dc0b8de — feat(ui)
- Export button disabled when folder missing; fetch+blob download with toast errors
- `syncCompanionToolbarPill()` highlights Conversations/Apps/Search sub-routes
- `renderMarkdown()` fenced code blocks + `pre.code-block` styling
- `sdk/companion_smoke_test.py` — companion API/page smoke checks
- **Test:** live curl common.js/apps.js markers ✓

### Commit 03071b1 — feat(apps) native
- `exportable` bool on each app in `/api/apps` (folder exists on disk)
- `MigrateAppPaths()` rewrites `/.aether/` and `/.xbrowser/` → `/.xplorer/`
- Smoke test: no-redirect check for switch-home
- **Test:** Build 41s ✓ | reinstall ✓ | `companion_smoke_test.py` ALL OK ✓ | migration: paths now under `~/.xplorer/apps/` ✓

**Loop 5 test summary**
| Check | Result |
|-------|--------|
| companion_smoke_test.py | ALL OK (export zip included) |
| exportable API field | present on all apps with folders ✓ |
| Path migration | `.aether` → `.xplorer` in registry ✓ |
| Code blocks in renderMarkdown | placeholder + pre/code path ✓ |

## Loop 6 (2026-06-14 ~06:35)

### Commit (ui) — feat(ui): native web search, app export, empty states, code highlight
- **Web search:** `usesNativeSearch()` includes `web` → native stream + link grid
- **Search UX:** `enrichSearchResults()` extracts URLs from answer text; dashed empty-state cards per mode
- **App builder:** Export button in app subtoolbar (`exportAppZip`, respects `exportable`)
- **Markdown:** `highlightCodeLight()` — keywords/strings/comments/numbers in code fences
- **Test:** companion_smoke_test.py ALL OK ✓ | live curl search.js/app-view.js ✓

## Loop 7 (2026-06-14 ~06:50)

### Commit — feat(ui+fab): query persistence, search fallback, export shortcuts
- **Search:** `persistSearchQuery` / `getStoredSearchQuery` — query survives home toggle; restore from localStorage when URL has no `q`
- **Search:** native stream errors show "Try Grok Web instead →" button (grok-web handoff fallback)
- **Markdown:** copy button on code fences (`wireCodeCopyButtons`) in search, chat, app builder
- **App builder:** Cmd+Shift+E exports zip (`exportAppZip`)
- **FAB:** "Export app" menu item on localhost app runtime or `/run/{id}/` pages
- **Welcome:** dev hint for `python3 sdk/companion_smoke_test.py`
- **Test:** companion_smoke_test.py ALL OK ✓ | apply.sh + build 43s ✓ | reinstall Xplorer.app ✓

**Loop 7 test summary**
| Check | Result |
|-------|--------|
| companion_smoke_test.py | ALL OK |
| persistSearchQuery in common.js | ✓ |
| search grok-web fallback button | ✓ |
| FAB exportapp in grok_fab.cc | ✓ |
| Build + reinstall | 43s, relaunch OK ✓ |

## Loop 8 (2026-06-14 ~07:00)

### Commit — feat(ui+fab): mode persistence, bulk export, builder shortcut
- **Search:** `getStoredSearchMode` / `persistSearchMode` — mode survives home toggle via switch-home `m` param
- **Chat:** ⌘⇧C copies hovered/focused code block (`initCodeCopyHotkey`)
- **Apps:** bulk select + "Export selected" for exportable apps (`downloadAppZip`)
- **FAB:** "Open in builder" on app runtime pages (`openappbuilder`)
- **Markdown:** language-aware highlighting (python/rust/go/bash) + `#` comments
- **Smoke:** switch-home `q+m` redirect check; apps.js bulk export markers
- **Test:** companion_smoke_test.py ALL OK ✓ | build 41s ✓ | reinstall Xplorer.app ✓

**Loop 8 test summary**
| Check | Result |
|-------|--------|
| companion_smoke_test.py | ALL OK (switch-home q+m) |
| getStoredSearchMode | ✓ |
| apps bulk export UI | ✓ |
| FAB openappbuilder | ✓ |
| Build + reinstall | 41s, relaunch OK ✓ |

## Loop 9 (2026-06-14 ~07:15)

### Commit — feat(ui+native+fab): handoff, bulk delete, runtime health
- **Search:** `grokWebUrlForQuery` — switching to Grok Web carries query via `#xplorer_grok=` pending id
- **Toolbar:** Alt+←/→ cycles Build / Web / Wiki home pills (`initToolbarHomeHotkeys`)
- **Chat:** per-conversation model in localStorage (`persistConvModel` / `getConvModel`)
- **Apps:** bulk delete with name confirmation; runtime alive/ready dots on cards
- **Native:** `runtime_alive` + `runtime_ready` on `/api/apps` entries
- **FAB:** "Rename app" on runtime pages (`renameapp`)
- **Test:** companion_smoke_test.py ALL OK ✓ | build 41s ✓ | reinstall ✓

**Loop 9 test summary**
| Check | Result |
|-------|--------|
| companion_smoke_test.py | ALL OK (runtime_alive API) |
| grokWebUrlForQuery handoff | ✓ |
| bulk delete + runtime dots | ✓ |
| FAB renameapp | ✓ |
| Build + reinstall | 41s, relaunch OK ✓ |

## Loop 10 (2026-06-14 ~07:30)

### Commit — feat(ui+native+fab): wiki handoff, batch export, conv rename
- **Search:** `wikiUrlForQuery` — wiki home switch carries `?q=` to Grokipedia; web handoff from search unchanged
- **Toolbar:** home pill tooltips show Alt+←/→ hint
- **Chat:** double-click sidebar to rename; `POST /api/conversations/{id}/rename`
- **Apps:** Restart button when runtime stopped; multi-select uses `POST /api/apps/export-batch` zip
- **Native:** batch export copies app folders into single zip; `POST /api/apps/{id}/restart`
- **FAB:** Duplicate app on runtime pages (`duplicateapp`)
- **Test:** companion_smoke_test.py ALL OK ✓ | build 42s ✓ | reinstall ✓

**Loop 10 test summary**
| Check | Result |
|-------|--------|
| companion_smoke_test.py | ALL OK |
| wikiUrlForQuery + conv rename | ✓ |
| export-batch + restart API | ✓ |
| FAB duplicateapp | ✓ |
| Build + reinstall | 42s, relaunch OK ✓ |

## Loop 11 (2026-06-14 ~07:45)

### Commit — feat(ui+native+fab): imagine handoff, bulk restart, conv delete
- **Search:** `imagineUrlForQuery` — imagine mode switches to `grok.com/imagine` with xplorer_grok context
- **Native bar:** grokipedia → Grok Web pill intercepts `?q=` and calls `/api/page/grok-web` handoff
- **Chat:** right-click sidebar to delete conversation
- **Apps:** bulk **Restart selected** via `POST /api/apps/restart-batch`
- **FAB:** **Modify app** opens builder with autobuild prompt (`modifyapp`)
- **Smoke:** conv rename/delete API + export-batch zip when 2+ apps
- **Test:** companion_smoke_test.py ALL OK ✓ | build 43s ✓ | reinstall ✓

**Loop 11 test summary**
| Check | Result |
|-------|--------|
| companion_smoke_test.py | ALL OK (rename, export-batch) |
| imagineUrlForQuery | ✓ |
| grok_web_bar wiki→web handoff | ✓ |
| restart-batch + FAB modifyapp | ✓ |
| Build + reinstall | 43s, relaunch OK ✓ |

## Loop 12 (2026-06-14 ~08:00)

### Commit — feat(ui+native+fab): filters, imagine open, share link
- **Search:** `openGrokWebQuery` uses `imagineUrlForQuery` in imagine mode
- **Apps:** status filter tabs (All / Ready / Building / Error)
- **Chat:** sidebar filter input for conversations
- **Native bar:** grokipedia handoff falls back to `localStorage` `xplorer_search_query`
- **FAB:** **Copy app link** on runtime pages (`shareapp`)
- **Smoke:** conv filter HTML, restart-batch API, Accept header for `/` HTML
- **Test:** companion_smoke_test.py ALL OK ✓ | build 42s ✓ | reinstall ✓

**Loop 12 test summary**
| Check | Result |
|-------|--------|
| companion_smoke_test.py | ALL OK (restart-batch) |
| apps status filters | ✓ |
| chat conv filter | ✓ |
| FAB shareapp + wiki localStorage | ✓ |
| Build + reinstall | 42s, relaunch OK ✓ |

## Loop 13 (2026-06-14 ~08:15)

### Commit — feat(ui+native+fab): filter counts, imagine handoff, builder link
- **Apps:** status tab labels show counts (`Ready (5)`, etc.) via `updateFilterCounts`
- **Chat:** `/` focuses conversation filter (when not in an input)
- **Search:** results button reads **Continue in Imagine →** in imagine mode (uses `openGrokWebQuery`)
- **Native bar:** Imagine menu link handoff with query + mode-aware prompts; wiki→web uses `pageSearchMode`
- **FAB:** **Copy builder link** (`/app?id=`) on runtime pages
- **Smoke:** filter count markers, imagine button text, conv `/` shortcut
- **Test:** companion_smoke_test.py ALL OK ✓ | build 42s ✓ | reinstall ✓

**Loop 13 test summary**
| Check | Result |
|-------|--------|
| companion_smoke_test.py | ALL OK (filter counts) |
| updateFilterCounts | ✓ |
| grok_web_bar imagine handoff | ✓ |
| FAB copybuilderlink | ✓ |
| Build + reinstall | 42s, relaunch OK ✓ |

## Loop restarted (2026-06-14)

User ran `/loop 15m improve our browser…`. Background conductor task `task-BFB359D6` started for recurring 15m cycles.

## Loop 14 (2026-06-14 ~11:30)

### Commit — feat(ui+native): uniform toolbar, settings page, welcome redesign
- **Toolbar:** `mountGrokToolbar()` + `grokToolbarHTML()` in `common.js` — single source for all companion pages; Settings button on every page
- **Pages migrated:** index, search, apps, app, welcome, new `/settings`
- **Settings:** `/settings` UI — default home, chat model, search model, browser theme (POST `/api/theme`), gateway/CDP/grok_bin read-only
- **API:** `search_model` in `grok_settings.json`; `EnrichSettingsResponse` adds companion/gateway/cdp URLs
- **Welcome:** redesigned hero, cards, tour links; toolbar + link to settings
- **FAB:** “Grok settings” menu item → `/settings`
- **Test:** companion_smoke_test.py ALL OK ✓ | build ~80s ✓ | reinstall ✓

**Loop 14 test summary**
| Check | Result |
|-------|--------|
| companion_smoke_test.py | ALL OK |
| /settings + settings.css/js | ✓ |
| mountGrokToolbar on all pages | ✓ |
| api/settings search_model + companion_url | ✓ |
| welcome redesign + toolbar | ✓ |
| Build + reinstall | OK ✓ |

## Next loop priorities
- xAI: expose grok.com tool handoffs in settings (Imagine, voice)
- Native: grok.com toolbar Settings submenu
- Apps: exportable-only filter (checkbox or tab)
- Chat: persist conv filter query across refresh?
- Search: error fallback uses imagine URL in imagine mode
- Native: grok.com search page Imagine submenu handoff
- FAB: open app preview in new tab from builder link
- Smoke test: filter tab switches ready count in live UI (interactive)

## Loop 15 (2026-06-14)

### Commit — feat(ui): search model from settings, Esc clears conv filter, apps idle tab, chrome://settings link
- **Search:** `search.js` init now runs `initSearchModelFromSettings()` which fetches `/api/settings` and calls `persistSearchModel(settings.search_model)` (syncs server default into localStorage used by getStoredSearchMode etc).
- **Chat:** Esc (on filter input or document when filter active) clears `convFilterQuery`, resets the `#conv-filter` input, and re-renders the conversation list.
- **Apps:** Filter tabs now include "Idle" (added to `apps.html` buttons + `FILTER_LABELS` in `apps.js`); idle status was already computed in counts/status logic.
- **Settings:** New "Chrome" card on `/settings` with `<a href="chrome://settings">` link (relies on Chromium's native chrome:// handler for full browser prefs).
- **Test:** `python3 sdk/companion_smoke_test.py` → ALL OK (serves fresh JS/HTML with new code paths; enhanced asserts for initSearchModel, Escape, idle filter, chrome:// link). UI files loaded live from `~/cli_experiment/aether/companion/ui` (UiDir lookup, no native rebuild or reinstall required). Verified via curl + smoke. Revert not needed (smoke green).

**Loop 15 test summary**
| Check | Result |
|-------|--------|
| companion_smoke_test.py | ALL OK |
| search.js uses settings.search_model | initSearchModelFromSettings + persist ✓ |
| Esc clears conv filter | key handler in app.js + input ✓ |
| apps idle filter tab | data-filter=idle + label in html/js ✓ |
| chrome://settings link | present in settings.html ✓ |

## Loop 16 (2026-06-15)

### feat(ui+native): unified glass toolbar — single canonical source for all surfaces
Redesigned the top toolbar and **consolidated the two divergent copies** (companion JS
`grokToolbarHTML()` vs hardcoded C++ string in `grok_web_bar.cc`) into ONE canonical
file consumed by every surface.

- **New canonical source:** `companion/ui/toolbar.html` — baked SVG icons, root-relative
  hrefs, pills carry BOTH `data-home` (companion home-switch) and `data-pill` (native
  active-state/handoff). Served by the gateway at `GET /toolbar.html` + `/partials/toolbar`
  (`grok_native.cc`, unauthenticated, CORS `*`).
- **Companion side (`common.js`):** `mountGrokToolbar()` is now async — fetches
  `/toolbar.html` same-origin; `grokToolbarHTML()` kept only as an aligned inline fallback.
  Home pills `preventDefault` (now `<a>`). New `initToolbarHideToggle()` → hide/show with a
  floating `#grok-toolbar-reveal` handle, persisted in `localStorage.xplorer_toolbar_hidden`.
- **Native side (`grok_web_bar.cc`):** removed the CSP-blocked cross-origin fetch; new
  `LoadToolbarHtml(gw)` reads the SAME file from disk each injection (live, like
  `LoadToolbarCss`), rewrites root-relative hrefs → absolute gateway URLs, bakes it into the
  isolated-world injection. Baked C++ string remains a complete fallback (now incl. settings +
  hide buttons + `data-home`). `wireHideToggle()` mirrors companion hide/show.
- **Design asks delivered:** logo→**Xplorer** (compass icon), **gear SVG** settings,
  **glass** bar (`backdrop-filter` + `@supports` opaque fallback), **semi-glass** pills +
  dropdowns, **icons** on every item, **Wiki→Groki**, **hide/show** with persistence — all
  identical on companion pages AND the x.com/grok/grokipedia overlay.
- **Parity:** home pills use `/switch-home?mode=build|web|wiki` so native click behavior
  (home-switch + redirect) matches companion exactly; grokipedia `data-pill="web"` query
  handoff preserved.
- **Review:** two background multi-agent adversarial reviews (14 raw → 11 confirmed → all
  HIGH/MEDIUM merge-blockers fixed; follow-up re-verify of the fixes).
- **Test:** native build (apply→build→reinstall, clean relaunch). Verified LIVE via SDK
  `eval` on grokipedia + x.com (canonical markup, 15 icons, switch-home hrefs, hide/show,
  glass computed style) and companion `/settings` + `/apps`. `companion_smoke_test.py` ALL OK.

**Loop 16 test summary**
| Check | Result |
|-------|--------|
| companion_smoke_test.py | ALL OK |
| GET /toolbar.html (+/partials/toolbar) | 200, canonical markup ✓ |
| native overlay = canonical (x.com, grokipedia) | logo/icons/Groki/hide/gear ✓ |
| href rewrite root-relative → absolute GW | switch-home?mode=X ✓ |
| native↔companion parity (switch-home) | both /switch-home?mode=X ✓ |
| hide/show toggle + persistence | class + reveal + localStorage ✓ |
| glass (backdrop-filter) + @supports fallback | computed saturate(1.8) blur(20px) ✓ |
| adversarial review (2 workflows) | merge-blockers resolved ✓ |
| UI only (no build) | smoke passed live ✓ |

## Loop 17 (2026-06-15)

### assets + fix: logo badge, Grok-It menu, app-page polish
Logo + four bugs (root-caused by a background multi-agent diagnostic workflow, all
reproduced live).

- **Logo (`site/assets/logo.png`):** rebuilt from the transparent X mark — cropped to the
  alpha bbox (was off-center), recentered, composited onto a rounded dark-glow badge so it
  reads on light OR dark backgrounds (white-X-on-transparent vanished on light). Verified
  24–128px on white/gray/dark. Added `logo-mark.png` (transparent) for dark-only contexts.
- **Bug — Grok FAB menu dead (`grok_fab.cc`, native):** `buildMenu()` clears+recreates the
  menu items on every open, but `onclick` was wired once in `ensureFab` (when the menu was
  still empty) → every item had `onclick===null`. Fixed with event delegation on the stable
  `#xplorer-grok-menu` container. Verified live: all 7 actions fire, menu closes, grok-web
  handoff reaches the backend.
- **Bug — rename shows stale "grok build failed" (`apps.js`):** rename succeeds (200 ok) but
  the gallery re-rendered the prior failed build's `last_error` unconditionally, reading as a
  rename error. Now gated on `status === 'error'` and labeled `Last build failed: …`.
- **Bug — collapsed chat re-open hard to click (`app.css`):** collapsed rule kept the base
  `translate(50%)` with `right:0`, pushing the 22px handle half off-screen (11px visible).
  Now `right:8px; transform:translateY(-50%); 32×64`. Mobile equivalent un-clipped too.
- **Bug — preview scrollbars unstyled (`app-view.js`):** preview iframe is same-origin, so
  inject a theme-aware `::-webkit-scrollbar` + `scrollbar-color` `<style>` into its
  `contentDocument` on load (both create + reuse branches, try/catch guard).
- **Test:** companion fixes live (no rebuild); native `grok_fab.cc` rebuilt + reinstalled
  (clean relaunch). Verified each fix via SDK `eval` on the live app. `companion_smoke_test.py`
  ALL OK.

**Loop 17 test summary**
| Check | Result |
|-------|--------|
| companion_smoke_test.py | ALL OK |
| logo centered + works light/dark | 24–128px verified ✓ |
| Grok FAB menu actions fire (delegation) | 7 items, menu closes, handoff ✓ |
| rename stale-error badge | gated on status=error + labeled ✓ |
| collapsed sidebar re-open handle | 32×64, fully on-screen ✓ |
| preview iframe scrollbars themed | style injected into contentDocument ✓ |

## Loop 18 (2026-06-15)

### fix + feat: app edit/resume build failure, settings page, new /logs page
- **Settings page (`settings.css`, committed 676fbed):** added the base `html.grok-build body`
  rule (font/bg/color) that was missing — the page rendered in Times-serif on a transparent
  bg, looking like broken JS/CSS though the JS ran fine.
- **"Grok build failed" on edit/resume (`app_store.cc`):** ROOT CAUSE found by running the
  exact grok command manually with stderr visible → `Couldn't create session: Session does
  not exist` (exit 1). grok one-shot `-p` (streaming-json) sessions are NOT persisted, so the
  edit path's `-r <saved session_id>` always failed (new apps worked because they had no `-r`).
  Fix: stop resuming the saved session on edits — run fresh; `--cwd` gives grok the app's
  current files so edits apply. Verified: editing the previously-failing tetrisv3 now streams
  exit 0, status error→ready, and the real file edit landed.
- **Build stderr was discarded (`grok_native.cc` PumpGrokStream):** only stdout was piped, so
  failures were a black box. Now capture stderr to a temp file and surface it 3 ways: the chat
  error bubble (`grok failed (exit N): <reason>`), the registry `last_error_detail`, and
  `/api/logs`.
- **New `/logs` page:** in-memory event ring + `RecordGatewayLog()` (thread-safe) in
  `grok_native.cc`; records build start/finish (+exit+stderr) and runtime server start/fail.
  `GET /api/logs?source=&app=&limit=` JSON contract; `/logs` route; themed, auto-refreshing,
  filterable `logs.html`/`logs.css`/`logs.js` with expandable detail; **Logs** added to the
  toolbar Grok Build menu (canonical `toolbar.html` + `common.js` fallback).
- **Test:** native rebuild (apply→build→reinstall, clean relaunch). Verified LIVE: edit fix
  (exit 0, file changed, status ready), `/api/logs` build events, `/logs` 200,
  `companion_smoke_test.py` ALL OK.

**Loop 18 test summary**
| Check | Result |
|-------|--------|
| companion_smoke_test.py | ALL OK |
| settings page base styles | white bg + apple-system font ✓ |
| app edit/resume (was "Session does not exist") | exit 0, status ready, file edited ✓ |
| build stderr surfaced (error bubble + last_error_detail) | captured ✓ |
| /logs page + /api/logs | 200 + build start/finish events ✓ |

## Loop 19 (2026-06-15)

### feat: toolbar pushes page content down; apps gallery + builder redesign
- **Native overlay no longer covers page UI (`grok_web_bar.cc`):** the fixed bar now
  offsets page content by its height. Was padding BOTH `<html>`+`<body>` (double gap);
  now a single root offset, re-asserted on SPA route changes + the 400ms watchdog (sites
  that strip our inline style no longer re-cover content). Adaptive: if a page's body still
  sits under the bar (fixed/absolute app shell), fall back to `transform: translateY` on the
  root, which offsets fixed descendants too. Verified: grokipedia + x.com content now starts
  below the bar (was 31px under on x.com). (Edge case: x.com's single `position:fixed` logo
  remains — inherent to an injected bar; a native chrome bar is the full fix.)
- **Apps gallery (`apps.js`/`apps.css`) — designed by a background workflow:** each card now
  shows a **live scaled-down `<iframe>` preview** of the built app (always current — no stale
  screenshot, no tab-flash capture) with an icon/"Not built yet" placeholder; actions reduced
  to **Build** + **Preview** + a **⋯ menu** (Restart/Modify/Rename/Duplicate/Export/Delete).
  Added a render-signature guard so the 2s poll doesn't reload the iframes; ⋯ menus close on
  outside-click and pause the poll while open.
- **Opened builder (`app.html`/`app.css`/`app-view.js`):** the heavy actions
  (Open-in-tab/Copy-CLI/Export/Delete) moved into a compact **⋯ overflow menu**; added a small
  **↻ Refresh** (re-renders the live preview) and a low-emphasis Gallery link, keeping focus on
  the preview + chat.
- **Screenshot decision:** chose the live-iframe preview over the native raster-capture path —
  the only capture mechanism foregrounds/flashes a tab (no headless path) and is fragile;
  the iframe is always-current and needs no rebuild.
- **Test:** native rebuild for the toolbar offset (apply→build→reinstall, clean relaunch);
  apps UI is live. Verified via SDK eval: toolbar offset on grokipedia + x.com, gallery
  thumbnail/menu, builder compact toolbar + overflow.

**Loop 19 test summary**
| Check | Result |
|-------|--------|
| toolbar offsets content (grokipedia/x.com) | bodyTop=barH, single offset ✓ |
| offset re-asserted on SPA + watchdog | applyPadding on tick/route ✓ |
| gallery: live iframe thumbnail | .app-thumb-frame renders ✓ |
| gallery: Build + Preview + ⋯ menu (6 items) | actionCount=3, menu opens ✓ |
| builder: ↻ refresh + ⋯ overflow (4 items) + Gallery | compact toolbar ✓ |