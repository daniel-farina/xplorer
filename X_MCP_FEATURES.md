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
- [ ] P0.4 MCP test hooks — extend the xplorer MCP / gateway to drive+verify every X feature headlessly.
- [ ] P0.5 **Real data path (PRIMARY)** — Settings → X integration: store Xplor's X dev-app
      CLIENT_ID/SECRET + per-user OAuth login (localhost:8080/callback) → enable `xapi` (live X data).
      `x-mock` becomes the OFFLINE / not-connected FALLBACK + dev test harness, NOT the primary source.
      Auto-select: use `xapi` when configured+authed, else fall back to `x-mock`.
      BLOCKER (owner, one-time, can't automate): create Xplor's X dev app at developer.x.com (OAuth2,
      redirect localhost:8080/callback, Production env) → paste CLIENT_ID/SECRET. Real data is the goal;
      the mock only ensures features still work when a user hasn't connected X / is offline.

### Phase 1 — Default search page
- [ ] P1.1 "On X right now" module (web + live X blend).
- [ ] P1.2 "What's the real take" (dissent / primary sources / community notes).
- [ ] P1.3 Agentic answer box (web+X answers + act: open tabs / draft).

### Phase 2 — Research & side panel
- [ ] P2.1 Social-graph research (high-signal voices + cross-ref web).
- [ ] P2.2 Entity X-lens (live X on any page's person/company).

### Phase 3 — Background & scheduled
- [ ] P3.1 Morning X briefing (scheduled background digest → tabs, no focus steal).
- [ ] P3.2 Topic radar (alert on X spikes).

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
