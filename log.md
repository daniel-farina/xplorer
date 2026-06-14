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