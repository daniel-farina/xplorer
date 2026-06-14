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

## Next loop priorities
- Apps: idle filter tab + exportable-only filter
- Chat: Esc clears conv filter
- Search: error fallback uses imagine URL in imagine mode
- Native: grok.com search page Imagine submenu handoff
- FAB: open app preview in new tab from builder link
- Smoke test: filter tab switches ready count in live UI