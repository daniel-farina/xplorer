# Xplorer improvement log

Autonomous 30-minute improvement loop. Each entry: change, test result, commit.

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

## Next loop priorities
- App rename in gallery; duplicate app
- Search: remember last mode in localStorage
- Grok FAB menu: add "Build app from page" shortcut
- Companion chat: model picker persistence across pages