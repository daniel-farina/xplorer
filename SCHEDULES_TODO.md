# Background-cron + Schedules — status

Branch: `feat/background-cron` (worktree `/Users/dan/cli_experiment/xplorer-bgcron`), merged into `feat/sidebar-integration` for local dev serving.

## Done
- Background agent tabs + FocusArbiter + scheduler (interval/one-shot/cron, run history, NL interpret, `/schedules` UI)
- Cron re-fire fix (`ComputeNextFire` after cron fires; skip jobs with `last_status == "running"`)
- Sidebar bookmark rows: transparent background (no white card on the rail)
- Arc-style bookmark tabs: tag + hide tab row + detach on host navigation + active row highlight
- `companion/ui/schedules.*` committed on branch (served from main worktree after merge)

## Verify manually (build green, installed to /Applications/Xplorer.app)
- `/schedules` in side panel: list, NL interpret, run-now, history, pause/delete
- Sidebar scheduled row → panel detail
- Bookmark click: opens/switches, hides tab from Tabs strip, reappears on navigate away
- Bookmarks section: no white card background on the sidebar rail