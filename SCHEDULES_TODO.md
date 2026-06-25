# Background-cron + Schedules — remaining TODO (handoff)

Branch: `feat/background-cron` (worktree `/Users/dan/cli_experiment/xplorer-bgcron`), based off `feat/sidebar-integration`. Local build only (out/aether), not released. Design: `background_and_cronjob_system.md`. Full architecture + gotchas: memory `background-cron-focus-system.md`.

## DONE (built + verified via the gateway)
- Background agent tabs (no focus steal); default-deny FocusArbiter (one focus owner; no agent of any kind can foreground without a user grant); screenshot no longer foregrounds; `focus:true` gated.
- Scheduler: CRUD, interval/one-shot/**cron** (5-field, verified), headless chat **and app-build** fire (app-build verified: wrote BUILT_OK to a test app), run history, per-job soft `max_concurrent_tabs` (prompt-level, no hard enforcement).
- Native "Scheduled" sidebar section (renders below Tabs).
- `/schedules` management page (served: list→detail, talk-to-it NL box, schedule form, run history, pause/run/cancel/delete, back-to-chat) + backend (`/schedules` route, `POST /api/schedules/{id}/interpret`, `/run`, `OpenGrokSidePanelAt`, row-click→panel nav).
- Bookmark click = interim switch-to-existing-or-open (no longer replaces the active tab).

## TODO
1. **[BLOCKER for testing the new UI] /schedules 401 — served-path mismatch.** The gateway serves `companion/ui` from `/Users/dan/cli_experiment/xplorer/companion/ui` (the MAIN worktree, `feat/sidebar-integration`), not this one. The new `schedules.html/css/js` exist only here, so `/schedules`, `/schedules.html`, `/schedules.js` all 401. Fix: merge `feat/background-cron` into the branch the served worktree uses (or copy `companion/ui/schedules.*` into the served dir). Then the page + the row-click→panel flow can be tested.
2. **Verify the schedules UI end-to-end** (once served): row-click→`/schedules` in the panel; NL `/interpret` reschedules ("every weekday at 8am" → cron); run-now; run history rows; pause/cancel/delete; back-to-chat.
3. **True-Arc bookmark model** (only the interim is in). Goal: a bookmark IS a tab, hidden from the vertical Tabs strip, detaches to a normal tab when it navigates off the bookmark URL. Feasibility = fork-side (investigated). Hook points:
   - Hide row: add `HideTabRow/ShowTabRow(TabHandle)` to `VerticalTabStripRegionView` (already a patched fork file) → `GetNodeForHandle(h)->view()->SetVisible(false)`. Re-assert on `OnTabStripModelChanged`/child-moved (layout churn re-shows it).
   - Tag: add `bookmark_url` to `agent_gateway/tab_ownership.h`; set in `XplorerSidebarBookmarksView::OnBookmarkPressed`.
   - Detach: a `WebContentsObserver` on the bookmark-tab; on committed URL off the bookmark host → clear tag + ShowTabRow.
   - Open/active highlight: `XplorerSidebarRowButton::SetSelected` driven by a `TabStripModelObserver` in the bookmarks view.
   - Risks: hidden tab still focusable (Ctrl+Tab/Ctrl+W/a11y); scroll-to-active when a bookmark-tab is active (no laid-out row) — test these.
4. **Merge `feat/background-cron`** into the main line (`feat/sidebar-integration` / `branding-phase3`) — should merge cleanly (branched off it).
