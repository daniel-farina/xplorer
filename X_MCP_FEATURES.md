# Xplor × X MCP Integration — Build Tracker

**Branch:** `feat/x-mcp-integration` (off master @ ceb9cfa) · **Started:** 2026-06-30 · autonomous build loop

## Vision
Weave X's real-time social layer (X MCP: full-archive search, trends, news, the social graph,
bookmarks, Articles) into Xplor's agentic browser via Grok Build — a real-time-aware default
search page, background X intelligence, a bookmark bridge, an entity lens, and publishing.

## HARD REQUIREMENT — works on NEW machines (distributed app)
Xplor is distributed; it CANNOT assume `~/.grok/config.toml` (or the grok CLI) exists. On first
run the app must self-provision: ensure the grok CLI is available + write `~/.grok/config.toml`
with the `xplorer` MCP server **and** the X MCP servers (`xapi` credential-gated, `x-docs`). This
is Phase 0 and gates everything below.

## TESTING
Everything must be testable via the **browser MCP** (the agent gateway `/api/*`). Where a hook to
drive/verify a feature is missing, ADD it. Use a **mock X-data layer** so the browser/UI flow is
testable WITHOUT live X creds (the real X MCP path is credential-gated; the user adds an X dev-app
`CLIENT_ID`/`CLIENT_SECRET` to go live). **macOS first → then Linux + Windows.** Commit as we go.

## Phases & features
### Phase 0 — Foundation
- [ ] P0.1 **Grok config bootstrap** — app ensures grok CLI + writes `~/.grok/config.toml` on first
      run (provisioner), incl. the `xplorer` MCP + X MCP (`xapi` disabled-until-creds, `x-docs` on).
- [x] P0.2 Canonical grok config template (`sdk/grok_config.template.toml`) — source of truth for the
      provisioner; wired `x-mock`+`x-docs` into dev `~/.grok/config.toml`; `grok mcp list` sees all.
- [x] P0.3 Mock X MCP layer (`sdk/mock_x_mcp.py`, 6 X tools, fixtures) — credential-free testing;
      `grok mcp doctor x-mock` = healthy, 6 tools discovered. (No `/api/x/*` gateway endpoints needed —
      features use the existing grok-CLI invocation + the X MCP in config; that's the elegant path.)
- [x] P0.4 Headless test access — the gateway already supports it. Read `~/.xplorer/gateway.json` {port,token}
      (token also in `~/.xplorer/agent_token`), send `Authorization: Bearer <token>` to drive `/api/tabs`,
      eval, screenshot (works with the display ASLEEP — screen capture does not). Serve live UI edits by
      running the dev build with `XPLORER_COMPANION_UI=<repo>/companion/ui` + a `--user-data-dir` test profile.
- [ ] P0.5 **Real data path (PRIMARY)** — Settings → X integration: store Xplor's X dev-app
      CLIENT_ID/SECRET + per-user OAuth login (localhost:8080/callback) → enable `xapi` (live X data).
      `x-mock` becomes the OFFLINE / not-connected FALLBACK + dev test harness, NOT the primary source.
      Auto-select: use `xapi` when configured+authed, else fall back to `x-mock`.
      BLOCKER (owner, one-time, can't automate): create Xplor's X dev app at developer.x.com (OAuth2,
      redirect localhost:8080/callback, Production env) → paste CLIENT_ID/SECRET. Real data is the goal;
      the mock only ensures features still work when a user hasn't connected X / is offline.

### Phase 1 — Default search page
- [x] P1.1 "On X right now" module — DONE + **macOS-tested**. grok `x` search mode + `𝕏 Live` UI +
      inline streaming panel via `/api/search/stream?mode=x&model=grok-build`. Backend test passed:
      Grok calls `x_search_posts`+`x_trends` (X MCP) and returns the snapshot (posts + answer/sentiment);
      repo UI served live (data-mode=x + #x-panel + renderXSnapshot). Live data when xapi is set.
- [ ] P1.2 "What's the real take" (dissent / primary sources / community notes).
- [ ] P1.3 Agentic answer box (web+X answers + act: open tabs / draft).

### Phase 2 — Research & side panel
- [x] P2.1 Social-graph research — WORKS via the existing side-panel chat (grok-build + X MCP in config).
      Validated: a `grok -m grok-build` agent run calls `x_search_posts`/`x_trends` and synthesizes the
      high-signal voices (proven end-to-end in P1 + P3 tests). No new code — the X tools flow through Grok.
- [ ] P2.2 Entity X-lens (live X on any page's person/company) — needs a sidebar/overlay surface.

### Phase 3 — Background & scheduled
- [x] P3.1 Morning X briefing — DONE + **macOS-tested**. A scheduled task (`POST /api/schedules`, cron
      `0 8 * * *`, `run.model=grok-build`) whose prompt makes Grok use the X MCP. Test: created
      `job_44F28570` + ran it → `last_status: ok`, produced a real Daily X Briefing (top voices via
      x_search_posts + trends via x_trends + a summary), in the background (FocusArbiter, no focus steal).
- [ ] P3.2 Topic radar (alert on X spikes) — same mechanism, a saved X-watch prompt (quick follow-on).

### Phase 4 — Bookmarks & publishing
- [ ] P4.1 Bookmark bridge (X bookmarks ↔ browser, AI-organized).
- [ ] P4.2 Compose from browsing (tabs/research → X Article draft → publish).

### Cross-platform (per feature)
- [ ] macOS build + MCP test · [ ] Linux parity + test · [ ] Windows parity + test

## Status log
- 2026-06-30: branch created; tracker written; parallel codebase-mapping launched.
- 2026-06-30: map done (`X_MCP_MAP.md`) — KEY: most of the integration is NO-REBUILD (grok CLI is
  config-driven; `/search` + side panel are companion UI served live; scheduler runs any grok prompt).
  Only the provisioner (P0.1) + bookmark C++ need a build. Search page = `companion/ui/search.{html,js,css}`,
  served via gateway `GET /search`; `/api/search` + `/api/search/stream` already routed (unused by /search).
- 2026-06-30: P0.2 + P0.3 DONE + verified. Built `sdk/mock_x_mcp.py` (6 X tools, query-aware fixtures) +
  `sdk/grok_config.template.toml`; wired `x-mock`/`x-docs`/`xapi`(disabled) into dev `~/.grok/config.toml`.
  `grok mcp doctor`: x-mock healthy (6 tools), x-docs reachable. Foundation works end-to-end.
- 2026-06-30: VALIDATED the agent path — `grok -m grok-build` actually calls `x_search_posts`+`x_trends`
  (mock) and uses the results (output matched fixtures exactly). Built `sdk/x_login_test.sh` for the live
  path; found xurl's OAuth requests too-broad scopes (write/DM/email) so user can't consent → pivoting to
  an **app-only Bearer token** (read: search/trends/news) which needs no OAuth. Awaiting the Bearer.
- 2026-06-30: P1 search module CODE DONE + committed (d5edd42): grok `x` search mode + `𝕏 Live` UI panel
  that streams `/api/search/stream?mode=x` (Grok+X MCP) inline. Incremental rebuild (grok_native.cc) running;
  will test on macOS via the gateway (launch dev build w/ XPLORER_COMPANION_UI=repo, drive /search) next.
- 2026-06-30: P1 macOS-TESTED ✓. Rebuilt grok_native.cc (x-mode compiled into the framework). Swapped to the
  dev build (XPLORER_COMPANION_UI=repo, /tmp/xplor-test profile); `curl /api/search/stream?mode=x` → Grok
  called `x_search_posts`+`x_trends` and returned mock posts (levelsio/karpathy/swyx/elonmusk) + a live-take
  answer with sentiment. Repo UI served (data-mode=x, #x-panel, renderXSnapshot). Restored the installed app.
  Found the gateway auth for headless DOM tests: `~/.xplorer/gateway.json` {port,token} + `Bearer`.
  NEXT: P3 scheduled X monitors (no-rebuild) + P0.1 provisioner (batch rebuild).
- 2026-06-30: P3.1 macOS-TESTED ✓ + P2.1 confirmed. Created "𝕏 Morning Briefing" (job_44F28570, daily 8am,
  grok-build) + ran it → last_status=ok; the run conversation holds a real Daily X Briefing (top voices via
  x_search_posts, trends via x_trends, a what-matters summary), produced in a background run (no focus steal).
  P2.1 social-graph research works through the same sidebar-chat + X-MCP path. Shipped so far: P0 foundation,
  P1 search module, P2.1, P3.1 — all macOS-tested. NEXT: P0.1 grok provisioner (distributed config, rebuild).
