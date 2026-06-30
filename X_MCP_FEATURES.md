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
- [ ] P0.2 Canonical grok config template (one source of truth: dev config + the bootstrap default).
- [ ] P0.3 Gateway `/api/x/*` family — proxy Grok+X MCP; **mock mode** for credential-free testing.
- [ ] P0.4 MCP test hooks — gateway endpoints to drive/verify every feature headlessly.

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
