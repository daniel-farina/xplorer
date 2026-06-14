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

## Next loop priorities
- Web search: optional native stream (currently Grok Web handoff)
- App export: disable button when folder missing; show error toast
- Companion smoke test script (curl-based API checks in sdk/)
- Chat: code blocks + syntax highlighting in markdown
- Toolbar: highlight active sub-route (Conversations vs Apps vs App builder)
- Migrate stale ~/.aether/apps registry paths to ~/.xplorer