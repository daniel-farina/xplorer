# Xplor x X MCP — Integration Map

(auto-generated from the parallel codebase map, 2026-06-30)

## grok CLI & ~/.grok/config.toml first-run provisioning for Xplorer
**rebuild:** rebuild - The PostBrowserStart hook is in C++ (chrome_browser_main.cc), and provisioning logic (grok CLI discovery, ~/.grok/config.toml writing) is idiomatic Chromium C++ using base::FilePath, base::WriteFile, base::JSONWriter. No other rebuild impact; existing ResolveGrokBinary() and gateway paths unchanged.

**files:**
- `X_MCP_FEATURES.md` — specification - Phase 0 hard requirement to provision grok CLI and config.toml on first run
- `patches/apply_integration.py` — integration point - applies source edits to Chromium; lines 1050-1080 hook PostBrowserStart where provisioner should be added
- `src/chrome/browser/agent_gateway/agent_gateway.h` — C++ component - AgentGateway singleton started in PostBrowserStart; Start(0) is the hook point for provisioner
- `src/chrome/browser/agent_gateway/grok_native.cc` — grok CLI resolver - ResolveGrokBinary() at line 83 searches PATH, GROK_BIN env, ~/.grok/bin/grok, ~/.local/bin/grok, /opt/homebrew/bin/grok
- `src/chrome/browser/agent_gateway/grok_companion_launcher.cc` — companion discovery - writes ~/.xplorer/companion.json; calls ResolveGrokBinary()
- `src/chrome/browser/agent_gateway/xplorer_paths.h` — path resolution - DataDir() and Resolve() for ~/.xplorer directory
- `src/chrome/browser/agent_gateway/xplorer_paths.cc` — implementation - creates ~/.xplorer on first run (idempotent with legacy .xbrowser/.xplorer migration)
- `sdk/xplorer_mcp.py` — MCP server - needs to be registered in ~/.grok/config.toml [mcp_servers.xplorer]
- `sdk/grok_companion_server.py` — companion UI - expects grok CLI and ~/.grok/auth.json to exist; no provisioning logic
- `AGENTS.md` — documentation - MCP registration examples for Grok CLI at ~/.grok/mcp.json
- `background_and_cronjob_system.md` — architecture - explains grok CLI subprocess launch path (PumpGrokStream at grok_native.cc:1369)
- `grok/xbrowser-companion.toml` — grok profile example - shows TOML agent profile structure but NOT config.toml

**current behavior:** **NO FIRST-RUN PROVISIONING EXISTS.** Xplorer ASSUMES:
1. grok CLI is already installed (via npm globally or manually)
2. ~/.grok/config.toml already exists with MCP servers configured
3. ~/.grok/auth.json exists with OAuth credentials
4. ~/.grok/companion.json has been written (if using custom grok binary path)

Current discovery logic (ResolveGrokBinary at grok_native.cc:83):
- Checks GROK_BIN env var
- Checks ~/.grok/companion.json for grok_bin field  
- Checks ~/.grok/bin/grok, ~/.local/bin/grok (macOS/Linux)
- Checks ~/.grok/bin/grok.exe, ~/.grok/bin/grok.cmd, %APPDATA%\npm\grok.cmd (Wind

**integration points:**
- PostBrowserStart hook in chrome_browser_main.cc (line 1056 in apply_integration.py) — AgentGateway::Start(0) is called here; this is where a provisioner call should be added
- C++ provisioner function (new file: chrome/browser/agent_gateway/grok_provisioner.{h,cc}) will be invoked from PostBrowserStart before/alongside AgentGateway::Start()
- xplorer_paths::DataDir() already creates ~/.xplorer on first run; provisioner will create ~/.grok/ and write ~/.grok/config.toml
- ResolveGrokBinary() at grok_native.cc:83 will then find the provisioned grok CLI on subsequent calls
- grok_companion_launcher.cc WriteCompanionDiscovery() will discover the provisioned grok binary and write it to ~/.xplorer/companion.json
- Browser MCP gateway (/api/grok/status endpoint, new) will report provisioning status and grok CLI availability for UI feedback

**build plan:**
- 1. CREATE grok_provisioner.{h,cc} in src/chrome/browser/agent_gateway/ with C++ function ProvisionGrokCliAndConfig(profile_path, xplorer_sdk_path) that: (a) ensures grok CLI is available (search PATH, check npm global, offer to run 'npm install -g grok' via subprocess or check for prebuilt binary in app bundle), (b) create ~/.grok/ directory, (c) write ~/.grok/config.toml with [mcp_servers] entries for xplorer/xapi/x-docs
- 2. PATCH apply_integration.py to add grok_provisioner.h include and call ProvisionGrokCliAndConfig() in the PostBrowserStart hook (after TRACE_EVENT0 but before AgentGateway::Start(0))
- 3. CREATE grok_config_template.toml (or embed as C++ string literal) with canonical template: [mcp_servers.xplorer] {command='python3', args=['/path/to/sdk/xplorer_mcp.py']}, [mcp_servers.xapi] {disabled=true, comment='enable after X credentials'}, [mcp_servers.x-docs] {command='...', args=[...]}, other grok settings
- 4. PATCH grok_native.cc ResolveGrokBinary() to log provisioning result (for debugging)
- 5. LINK grok_provisioner into BUILD.gn chrome/browser target (add grok_provisioner.{h,cc} to sources)
- 6. TEST via browser MCP gateway (new /api/provision endpoint to trigger provisioner and report status) with credential-free mock mode
- 7. BUILD and test cross-platform: macOS (arm64/x86_64), Linux (x64), Windows (x64)

**test via mcp:**
- ADD new POST /api/provision endpoint to agent_gateway.cc that triggers ProvisionGrokCliAndConfig() and returns {ok: true, grok_found: bool, config_written: bool, grok_path: string, errors: []}
- TEST in browser console: fetch('http://127.0.0.1:9334/api/provision').then(r=>r.json()).then(console.log)
- TEST MCP mock mode (credential-free): mock X API responses at /api/x/* so xapi/x-docs don't require real X credentials for UI testing
- TEST with companion_smoke_test.py after fresh ~/Library/Application Support/Xplorer/ (macOS) to verify grok CLI is found and companion server starts
- TEST grok_companion_server.py can successfully run grok CLI subprocesses (grok -p 'hello' in PostBrowserStart hook test harness)
- VERIFY xplorer_mcp.py is correctly registered in generated ~/.grok/config.toml by reading the file and checking [mcp_servers.xplorer]

**gaps:**
- NO grok CLI binary provisioning strategy defined: (a) assume npm/homebrew globally installed, (b) bundle a prebuilt grok binary in the app distribution (macOS .dmg, Linux tarball, Windows .exe), (c) download on first run from a CDN, or (d) assume user provides GROK_BIN env var. DECISION NEEDED.
- X API [mcp_servers.xapi] credentials are not handled: should be empty/disabled on first run, then user can add CLIENT_ID/CLIENT_SECRET via settings UI. Needs /api/x/credentials endpoint to validate and store them.
- x-docs [mcp_servers.x-docs] MCP server script location unknown: where is the server? Is it in the app bundle, a separate Python script, or an external service? NEEDS SPECIFICATION.
- NO idempotency guard on ~/.grok/config.toml: if user edits it manually and the app restarts, provisioner should NOT overwrite their changes. Need a marker or version field to detect prior provisioning and skip re-creation.
- NO fallback if grok CLI is not found after provisioning: currently ResolveGrokBinary() falls back to bare 'grok' in PATH, which will fail. Should report an error to the sidebar agent so user sees 'grok CLI missing, install via npm' message.
- NO cross-platform grok CLI path handling documented: Windows npm global path (%APPDATA%\npm), macOS homebrew path (/opt/homebrew/bin), Linux ~/.local/bin — all differ. Code must handle all three.
- NO grok --version check: should verify installed grok CLI is a compatible version (not just any 'grok' in PATH that could be something else).
- COMPANION.JSON vs CONFIG.TOML confusion: grok_companion_launcher.cc reads ~/.xplorer/companion.json (browser-side discovery), but grok CLI reads ~/.grok/config.toml (CLI-side config). Both are used; the provisioner must write BOTH correctly.

---

## Grok CLI Gateway Invocation & HTTP Routing for Chromium Agent Gateway
**rebuild:** no-rebuild

**files:**
- `src/chrome/browser/agent_gateway/grok_native.cc` — Core implementation: grok CLI invocation, streaming orchestration, command builders
- `src/chrome/browser/agent_gateway/agent_gateway.cc` — HTTP routing dispatcher (RouteRequest), auth, browser tab APIs
- `src/chrome/browser/agent_gateway/grok_native.h` — Public interface: TryHandleRequest, DispatchScheduledRun, DispatchScheduledAppBuild
- `src/chrome/browser/agent_gateway/agent_gateway.h` — Gateway lifecycle, server thread mgmt, socket binding

**current behavior:** ## Grok CLI Invocation Pattern

**Command Builders (grok_native.cc):**
- BuildGrokSearchCommand(line 1051): grok -p "PROMPT" --output-format streaming-json --always-approve -m MODEL --max-turns 15/20
- BuildGrokChatCommand(line 2199): + --effort EFFORT -r SESSION_ID --rules RULES  
- BuildGrokAgentCommand(line 2228): Chat cmd + --cwd PATH for app-building
- Streaming flag: streaming-json (streamed) vs json (one-shot) via --output-format

**Process Spawning (PumpGrokStream, line 1615):**
- LaunchGrokStdoutProcess() handles cross-platform pipe creation (Windows CreatePipe/POSIX pipe+dup2)
- Envi

**integration points:**
- GrokNative::TryHandleRequest() spawns from AgentGateway::OnHttpRequest after Bearer token auth
- content::GetUIThreadTaskRunner(FROM_HERE) hops to UI thread for browser API calls (BrowserApi::*)
- base::JSONReader::ReadDict(json, JSON_PARSE_RFC) parses all request/response JSON
- base::JSONWriter::Write(dict, json_string) serializes all responses
- base::ThreadPool::CreateSequencedTaskRunner spawns blocking grok runs + page summarization off IO thread
- Scheduler::Get() invokes DispatchScheduledRun/DispatchScheduledAppBuild for background tasks
- TabOwnership stamping links agent identity to opened tabs for focus/resource arbitration
- FocusArbiter::MayActivate(agent_id) gates foreground activation on user grant
- RecordGatewayLog() appends structured events to in-memory ring surfaced by GET /api/logs

**build plan:**
- 1. Create new /api/x/* endpoint family handler in grok_native.cc (e.g., POST /api/x/run-with-mcp)
- 2. Extract request JSON: base::JSONReader::ReadDict(info.data, JSON_PARSE_RFC) to get agent prompt + MCP flags
- 3. Resolve model via ResolveModel() but only accept grok-build (or return error if Composer requested)
- 4. Build command: BuildGrokChatCommand(message, session_id, grok-build, streaming=true, custom_rules) where rules enable X MCP tool imports
- 5. Handle streaming path: spawn via PumpGrokStream(server, io_task_runner, cid, cmd, model, mode=x, GrokStreamKind::kChat, conv_id, app_id)
- 6. For mock mode: inject env var GROK_X_MCP_MOCK=1 in child_env before LaunchGrokStdoutProcess
- 7. Grok child sees env, loads X MCP (or mock fallback), returns tool results in JSON stream
- 8. Gateway parses each streaming JSON line type=(text|tool|end), forwards to client
- 9. No rebuild needed: pure C++ code, no build config changes (same base::CommandLine/LaunchProcess APIs)

**test via mcp:**
- POST /api/x/run with body {prompt: "...", use_mock: true} should return chunked NDJSON with type:meta, type:text, type:result
- Verify GROK_X_MCP_MOCK=1 injected into child env when use_mock=true
- Check /api/logs shows source=chat, event=start/finish for the /api/x run
- Call with grok-composer-2.5-fast model, expect error: model must be grok-build for X MCP
- Verify session_id threading: reply stores new sessionId, next call with same session_id reuses context
- Direct curl: curl -H 'Authorization: Bearer TOKEN' -X POST http://127.0.0.1:9334/api/x/run -d '{...}'

**gaps:**
- grok-build's X MCP integration not yet defined: unclear which X tools are available, signature, mock fallback behavior
- Streaming error handling: current code forwards grok stderr to temp file on exit, but client may close connection mid-stream; buffering/recovery strategy TBD
- Session persistence across X calls: unclear if grok-build's session_id survives X MCP tool invocations or must be refreshed
- Credential injection for real X access: mock works, but production X auth (API keys, OAuth) not yet scoped in design
- Focus/tab ownership for X-driven operations: unclear if X MCP should auto-open tabs or just reason about existing ones

---

## Default Search Page (/search) - Companion UI Architecture
**rebuild:** no-rebuild

**files:**
- `companion/ui/search.html` — HTML - Search page markup with form, search modes (Web/Imagine), brand logo, and mount point for toolbar
- `companion/ui/search.js` — JavaScript - Form submission handler, mode switching, URL syncing, API calls to /api/page/grok-web
- `companion/ui/search.css` — Stylesheet - Hero section layout, search box styling, results grid, cards, markdown rendering
- `companion/ui/common.js` — Shared utilities - Fetch helpers, settings management, storage persistence, toolbar mounting
- `companion/ui/theme.css` — Theme variables - Light/dark mode CSS variables (--bg, --surface, --text, --accent, etc.)
- `companion/ui/toolbar.js` — Toolbar behavior - Navigation pill management, home-page switching, hide/reveal logic
- `companion/ui/newtab-bg.js` — Background/atmosphere - New-tab background image loading, star field animation, theme watcher
- `src/chrome/browser/agent_gateway/grok_native.cc` — Gateway routing - ServeUiFile() serves files from disk; TryHandleRequest() routes /search and /api/* endpoints

**current behavior:** GET /search returns search.html served by the gateway (grok_native.cc:2917-2918, ServeUiFile function). The HTML loads search.css and imports common.js, toolbar.js, newtab-bg.js. Search form submits to /api/page/grok-web, which generates a Grok URL with pending query ID. The page supports two modes (Web/Imagine) persisted in localStorage. Current integration: only the grok-web gateway API endpoint. No "live now" or agentic answer modules exist yet.

**integration points:**
- /api/page/grok-web (POST) - Store search query/prompt and return Grok URL with pending ID
- /api/settings (GET/POST) - Fetch/save user settings including search_home, search_model
- /api/search and /api/search/stream (GET/POST) - Full Grok search + streaming results (already routed, not used by /search page yet)
- /api/models (GET) - List available Grok models (grok-composer, grok-build, etc.)
- /api/status (GET) - Gateway status, configured model, grok binary path
- /api/logs (GET) - Query gateway event log (filtered by source/app)
- /api/theme (GET/POST) - Theme settings (unused in /search currently)
- /api/page/summarize/stream (POST) - Summarize page text via streaming
- /api/screenshot (POST) - Trigger tab screenshot via gateway
- Window/DOM ready - search.js mounts modules after script load; pattern: mount point div + innerHTML/appendChild
- Theme watcher - newtab-bg.js calls startThemeWatcher() to watch OS theme changes and update data-theme attribute

**build plan:**
- NO REBUILD required for companion UI changes: files in /companion/ui/ are served live from disk by grok_native.cc ServeUiFile()
- UiDir() function (grok_native.cc:214-270) resolves companion UI directory - dev builds use cli_experiment/xplorer/companion/ui
- Add mount point divs to search.html for (a) On X now module and (b) Agentic answer box
- Create new JS files for modules (e.g., on-x-now.js, agentic-answer.js) following logs.js pattern for async rendering
- Update search.js to call new module initializers after form setup
- New modules call /api//* endpoints (e.g., /api/logs for activity, /api/models for model selection, /api/settings for config)
- Add CSS for module cards/containers to search.css, using existing theme variables
- Optionally add streaming fetch via /api/search/stream for live search result updates in agentic module
- Test via browser reload - no build needed; gateway auto-serves latest disk files

**test via mcp:**
- GET http://127.0.0.1:9334/search - Verify search.html loads with new module mount points
- GET http://127.0.0.1:9334/api/status - Confirm gateway status endpoint returns models/config
- POST http://127.0.0.1:9334/api/search - Test search query execution (for agentic module to consume)
- GET http://127.0.0.1:9334/api/logs - Poll activity logs for On X now module
- GET http://127.0.0.1:9334/api/models - Fetch available Grok models for agentic box UI
- GET http://127.0.0.1:9334/api/settings - Verify theme/settings persistence affects module styling

**gaps:**
- Live activity source for 'On X right now' module: /api/logs exists but is gateway events only; need browser tab/conversation context API (may need new endpoint or extend /api/status)
- Streaming search results: /api/search/stream exists but requires Request ID / event-stream format (need docs or reverse-engineer from grok_native.cc)
- Model selection UI in agentic module: /api/models lists models but /api/search doesn't take ?model= param explicitly (need to verify if grok.cc respects it or add support)
- Authentication: companion UI is no-auth (localhost only), so MCP tests must run on 127.0.0.1:9334 not through agent gateway bearer token
- Persistent state: modules may need conversation/session storage beyond localStorage (check companion_sessions.json pattern)
- Error handling: gateway JSON errors use {error: string} format; confirm all module error paths match

---

## Grok Side-Panel UI Architecture & MCP Integration
**rebuild:** no-rebuild

**files:**
- `companion/ui/index.html` — Main chat UI entry point (search/chat interface)
- `companion/ui/app.html` — App build view (preview + side-panel chat)
- `companion/ui/app-view.js` — App build chat streaming logic (thought/text/result events)
- `companion/ui/app.js` — Chat panel streaming (both conversation and app-build messages)
- `companion/ui/common.js` — Shared utilities: parseStreamLine(), model selection, storage
- `companion/ui/apps.js` — App gallery UI (list, create, import/export)
- `companion/ui/schedules.js` — Scheduled tasks UI (cron jobs, run-now integration)
- `companion/ui/settings.js` — Settings UI (model selection, effort, max_turns)
- `companion/ui/toolbar.js` — Shared toolbar (web-nav, omnibox, home switching)
- `src/chrome/browser/agent_gateway/grok_native.cc` — Core API router: /api/conversations/*, /api/apps/*, /api/schedules/*, /api/settings, streaming
- `src/chrome/browser/agent_gateway/grok_native.h` — RunGrokAgentStream() declaration, CompanionUiDir(), grok binary resolver
- `src/chrome/browser/agent_gateway/app_store.cc` — TryHandleAppsRequest(): /api/apps/*, /api/apps/{id}/build/stream handler
- `src/chrome/browser/agent_gateway/app_store.h` — App store API (gallery, build dispatch, runtime servers)
- `src/chrome/browser/agent_gateway/agent_gateway.cc` — HTTP server setup, RouteRequest dispatch to grok_native/app_store
- `src/chrome/browser/agent_gateway/browser_api.cc` — Native browser primitives (bookmarks, history, tabs, theme)
- `src/chrome/browser/grok_companion/grok_web_bar.cc` — Native toolbar overlay on web (fetches /toolbar.html live)
- `src/chrome/browser/agent_gateway/BUILD.gn` — Build configuration for gateway (grok_native, app_store, browser_api)

**current behavior:** 
UI Panel Workflow:
1. User loads /api or /app (served from companion/ui/) via AgentGateway
2. Panel fetches /api/conversations (list) or GET /api/apps/{id}
3. User sends message → POST /api/conversations/{id}/message/stream or /api/apps/{id}/build/stream
4. Gateway spawns grok CLI (via RunGrokAgentStream) and streams NDJSON events:
   - thought: Agent reasoning (displayed in collapsible thinking panel)
   - tool/tool_use: MCP tool name (shows "Using X…" status)
   - text: Response chunks (accumulated and rendered as markdown)
   - result: Final reply + session ID
   - error: Failure (thrown a

**integration points:**
- AgentGateway HTTP server (port 9334) routes /api/* to TryHandleRequest (agent_gateway.cc) which dispatches to GrokNative::TryHandleRequest (grok_native.cc) and TryHandleAppsRequest (app_store.cc)
- GrokNative::TryHandleRequest handles: /api/conversations/*, /api/apps/*, /api/schedules/*, /api/settings, /api/page/*, /api/logs, /api/sidepanel/open
- TryHandleAppsRequest handles: /api/apps (gallery), /api/apps/{id}/build/stream, /api/apps/{id}/message/stream (both call RunGrokAgentStream)
- RunGrokAgentStream (grok_native.cc): spawns grok CLI with args, reads stdout as NDJSON, sends via net::HttpServer::Send200 (chunked streaming)
- UI fetches streaming endpoints with fetch() + Response.body.getReader(), parses NDJSON line-by-line in real-time
- Panel-to-Gateway communication: all /api/* routes via fetch(), no WebSocket (HTTP streaming only)
- MCP tool definitions: hardcoded in grok_native.cc::kBrowserChatRules (passed to grok CLI as system prompt)
- Native browser controls (bookmarks, tabs, theme, history) via BrowserApi static methods called from RouteRequest handlers
- App runtime servers (Python http.server) launched per app, served at /run/{appId}/ via TryHandleAppRunRequest
- Grok native toolbar (grok_web_bar.cc): injects JS into grok.com/x.com, fetches /toolbar.html live for shared markup

**build plan:**
- NO REBUILD for UI changes: Modify companion/ui/*.html/.js/.css, changes picked up immediately by live file serving in next request
- REBUILD REQUIRED if: Changing C++ gateway routing, streaming protocol, MCP tool definitions (kBrowserChatRules), RunGrokAgentStream behavior
- To add X MCP tools: Option A (current): Update kBrowserChatRules in grok_native.cc (declare tools, let grok CLI define them); Option B (future): Send tool schema JSON in stream preamble or via separate /api/tools endpoint
- Panel UI controls for X tools: Add to index.html/app.html (new buttons/forms), wire event handlers in app.js/app-view.js, send tool_use events back to grok via /api/conversations/{id}/tool-use POST (if implementing tool-driven UX)
- To test MCP integration: 1) grok models (list grok-build), 2) Send test message in panel, 3) Check stream events for tool/tool_use types, 4) Verify grok CLI receives MCP context

**test via mcp:**
- POST /api/conversations (create conv) → POST /api/conversations/{id}/message/stream with message 'list tabs' → expect tool_use event with xplorer_tabs before text reply
- POST /api/conversations with model=grok-build, message='open google.com' → expect tool_use(xplorer_new_tab) → text (confirmation)
- GET /api/conversations → verify conv.session_id present (persisted from stream result event)
- POST /api/apps/{id}/build/stream with prompt → expect same stream events (thought/tool/text/result) as chat
- Verify streaming stops cleanly: POST /api/conversations/{id}/stop → ensure net::HttpServer closes connection, grok CLI killed, no hanging processes
- Panel UI rendering: Send slow stream (tool_use with 5-second delay before text) → verify status updates incrementally, not buffered
- For new X tools: Add to grok-build system prompt (in test), send message that uses X tool, verify tool_use event flows to panel

**gaps:**
- No direct panel UI for MCP tool selection/configuration: Panel currently just renders whatever grok-build provides (hardcoded in system prompt); no UI to enable/disable specific tools or adjust tool parameters
- No tool schema discovery in panel: Panel doesn't fetch /api/tools or similar to learn available tools and their signatures; it blindly renders tool_use events
- No panel-initiated tool calls: Current flow is one-way (grok decides to call tools); no UI to let user invoke a tool directly (e.g., 'bookmark this tab' button that calls xbrowser_add_bookmark)
- No streaming protocol versioning: If tool schema format changes, no negotiation between panel and gateway (fragile for future MCP evolution)
- Missing /api/tools endpoint: No standard route to list available tools + schemas; panel would need to infer tool availability from stream events
- Panel doesn't track tool execution state: Streams show tool_use name, but no step-by-step tool result feedback (whether click succeeded, what screenshot showed, etc.) before final reply
- No client-side MCP validation: Panel doesn't validate tool parameters or enforce tool constraints (e.g., max concurrent tabs); that's all server-side in grok CLI
- Companion UI not bundled in dev builds: UiDir() falls back to dev checkout path (~cli_experiment/xplorer/companion/ui); if dev path removed, no UI served; no npm build/bundle step

---

## Scheduler + FocusArbiter: Scheduled & Background Task Model
**rebuild:** no-rebuild

**files:**
- `src/chrome/browser/agent_gateway/scheduler.h` — Task model definition: Job struct with triggers (interval, once, cron), persistence, run definition
- `src/chrome/browser/agent_gateway/scheduler.cc` — Poll timer (30s), fire dispatch, cron evaluation (minute-level search), persistence (Load/Save to schedules.json), CRUD handlers
- `src/chrome/browser/agent_gateway/focus_arbiter.h` — Default-deny focus grant model: MayActivate gate on tab activation, SetOwner/ResetToUser/OnRunEnded lifecycle
- `src/chrome/browser/agent_gateway/focus_arbiter.cc` — Focus grant enforcement: lock-guarded owner_ + grant_conv_id_, auto-expire on run end
- `src/chrome/browser/agent_gateway/grok_native.cc` — API handlers (/api/schedules CRUD), dispatch (DispatchScheduledRun/DispatchScheduledAppBuild), agent ID assignment, tab cap injection
- `src/chrome/browser/agent_gateway/agent_gateway.cc` — Scheduler lifecycle (Start/Stop), focus gate on tab open/activate/split, POST /focus user grant endpoint
- `src/chrome/browser/agent_gateway/BUILD.gn` — Build config: both scheduler and focus_arbiter listed in sources

**current behavior:** Scheduled tasks poll every 30s via base::RepeatingTimer on the gateway IO thread. Each task has a trigger (interval_sec, once_at_us, or cron) that computes next_fire_us. When next_fire_us <= now, Poll() fires the job: stamps last_fire_us, recomputes next_fire_us (so long runs don't delay schedule), dispatches to ThreadPool. Dispatch auto-creates or appends to a target conversation, registers in ActiveRuns (busy guard), runs grok headlessly with XPLORER_AGENT_ID="schedule:{job_id}", appends reply. Tabs open as NEW_BACKGROUND_TAB by default. FocusArbiter default-deny: only grant holder can foreg

**integration points:**
- AgentGateway::Start() → Scheduler::Get()->Start() (captures task_runner_, arms poll timer)
- AgentGateway::Shutdown() → Scheduler::Get()->Stop() (cancels timer for clean IO thread shutdown)
- Scheduler::Poll() on gateway IO thread every 30s (same sequence that owns jobs_ and timer)
- POST /api/schedules, POST /api/schedules/{id}/run, DELETE /api/schedules/{id} routed by grok_native.cc to Scheduler CRUD
- POST /api/schedules/{id}/interpret: grok interprets NL user text + current job JSON → change dict → merged back via UpsertJobFromDict
- DispatchScheduledRun(job_id, message, model, target_conv_id, on_done) → ThreadPool task → LoadSessions → auto-create conv → busy guard → append msg + Scheduler::NotifyRunStarted → RunGrokChat (blocking) → on_done callback chains back to scheduler via task_runner_
- XPLORER_AGENT_ID set to 'schedule:{job_id}' so gateway routes tab opens to correct owner
- ScheduledBrowserRules() injected into prompt: tells agent to xplorer_new_tab with task_id parameter, never touch user/bookmark tabs
- IsConversationRunActive(conv_id) prevents concurrent runs on same conversation (same 409 guard as chat message handler)
- FocusArbiter::MayActivate checked on POST /tabs, POST /tabs/{id}/activate, POST /tabs/{id}/split (default-deny unless grant held)
- POST /focus (user-only) → SetOwner(agent_id, grant_conv_id) grants focus until run ends or user resets
- OnRunEnded(conv_id) from grok_native run teardown → FocusArbiter::OnRunEnded revokes grant if conv_id matches
- Tab ownership stamped with task_id via ResolveTaskIdForAgent() → tabs tagged for background result grouping

**build plan:**
- No new source files needed: scheduler.{cc,h} and focus_arbiter.{cc,h} already present in agent_gateway/
- BUILD.gn agent_gateway source_set already lists both (lines 13-14: focus_arbiter; lines 19-20: scheduler)
- Dependencies already present in agent_gateway: //base (JSON, file I/O, threading, timer, synchronization), //net (HTTP server context), //content/public/browser (for conversatoin/session integration)
- No new #include chains or circular dependencies introduced
- Cron evaluation fully implemented in ComputeNextFire (lines 459-495 of scheduler.cc): minute-level search up to 1yr; CronMatches/CronFieldMatches/CronTermMatches for 5-field Vixie cron
- For X-monitor tasks (morning briefing, topic-spike alerts): encode topic selection + source instructions into job.message (e.g., 'Check tech news for {topic}, open result tabs only, no duplicates'); max_concurrent_tabs applies soft cap via injected instruction
- No build reconfiguration needed; compiles as-is into chrome/browser

**test via mcp:**
- Verify cron expressions: job with cron='0 8 * * 1-5' (weekday 8am), confirm next_fire_us lands at next Monday 08:00 local time; test DST boundary, malformed expressions (left at next_fire_us=0 → 'never')
- Verify interval jobs: interval_sec=3600, fire at T, confirm next_fire_us = T+3600; fire again at T+3600+epsilon, next_fire = T+7200
- Verify once_at_us: once_at_us=future_time, fire at future_time, confirm next fire disables job; once_at_us=past_time, poll catches up, fires immediately, disables
- Verify cron + DST: create job 'every Mon 2am', observe behavior during spring-forward gap (search bounds past gap, lands on next valid minute)
- Verify focus isolation: fire job A (opens tab1, tab1.background=true), try tab1.activate() → error 'focus denied'; POST /focus owner:schedule:A granted; retry tab1.activate() → ok; user clicks tab2 (unowned) → ResetToUser() fires → try tab1.activate() → error again
- Verify concurrent run guard: fire job A (conv1, last_status=running), try RunJobNow(A) → 409 'job is already running'; kill run → last_status changes, retry succeeds
- Verify history + persistence: fire job (10 times), verify history[] capped at kMaxHistory=20 (NEWEST first); kill browser; relaunch, verify schedules.json reloaded, next_fire_us preserved, history intact
- Verify app-build path: job with cwd=/path/to/app, fire → DispatchScheduledAppBuild dispatches with --cwd + kAppBuildRules; reply appended to same conv
- Verify tab ownership tagging: job A fires, opens tabs → verify via GET /tabs that each tab has task_id='<job_id>' and owner='schedule:<job_id>'
- Verify NL interpret: POST /api/schedules/{id}/interpret {'text':'make it run at noon'} → grok reads current job + prompt → outputs JSON change {trigger:{cron:'0 12 * * *'}} → merged + upserted → next_fire_us recomputed
- Verify soft tab cap: job max_concurrent_tabs=3, run message gets appended '(For this task, open at most 3 browser tabs.)', observe agent respects instruction (or not—cap is soft, LLM-only)
- Verify run history accuracy: fired_us as string (Windows epoch microseconds), status (ok|failed|running|skipped|deferred|cancelled), conv_id match, newest first

**gaps:**
- No hard tab limit enforcement: max_concurrent_tabs is soft (message instruction only); grok agent does not report which tabs it opens, so gateway cannot attribute/count them per task or enforce hard cap
- No structured topic filtering: job.message is plain text; X-monitor tasks must encode topic selection in message string (no schema for topic field); would need UX extension to select topics vs. free-form message
- No auto-foreground for background results: all scheduled agent tabs stay background by default; user must explicitly POST /focus to grant foreground for interaction; no auto-grant on task completion
- Phase 2 vs 3 documentation: header comments (lines 26-28) claim 'Phase 2 doesn't evaluate cron', but scheduler.cc actually DOES evaluate via CronMatches() at lines 459-495; update comments to reflect Phase 2 + 3 status
- No test suite: no *_test.cc files in agent_gateway; recommend scheduler_test.cc (cron evaluation, persistence round-trip, concurrency guards), focus_arbiter_test.cc (default-deny, grant lifecycle)
- No MCP bidirectional integration: Scheduler is C++ singleton; POST /api/schedules handlers cover HTTP, but no MCP resource types for scheduled tasks (would need ListMcpResourcesTool integration for agent discovery of available tasks)
- No result tab auto-grouping UI: scheduled task tabs are tagged task_id but no dedicated UI to group/label them separately from ad-hoc chat tabs; depends on companion sidebar rendering
- No alert/notification on topic spike: once-fired result tabs remain in browser; no mechanism to flag 'new results for this topic' or proactive re-run on spike detection

---

## Xplorer Bookmark + Bookmark-Tab System Mapping
**rebuild:** rebuild

**files:**
- `src/chrome/browser/agent_gateway/browser_api.h` — API interface: ListBookmarks, AddBookmark, RemoveBookmark
- `src/chrome/browser/agent_gateway/browser_api.cc` — Core bookmark CRUD implementation, integrates with Chromium BookmarkModel
- `src/chrome/browser/agent_gateway/agent_gateway.cc` — HTTP routing: GET/POST /bookmarks, DELETE /bookmarks/{id}; lines 533-552
- `src/chrome/browser/agent_gateway/tab_ownership.h` — Per-tab metadata container: bookmark_node_id field (line 58)
- `src/chrome/browser/agent_gateway/tab_ownership.cc` — Tab ownership tracking implementation
- `xplorer_agent_tab_grouper.h` — Bookmark tab lifecycle: seeding, config reload, group management
- `xplorer_agent_tab_grouper.cc` — Bookmark grouping implementation: SeedDefaultBookmarks (line 598), OpenMissingBookmarkTabs (line 649), ApplyBookmarkConfig (line 715)
- `src/chrome/browser/grok_companion/grok_companion_util.h` — Bookmark config interface: GetBookmarkConfigs, SetBookmarkConfigs, RemoveBookmarkConfig
- `src/chrome/browser/grok_companion/grok_companion_util.cc` — Persistent bookmark config layer: reads/writes grok_settings.json bookmarks array (lines 318-368)
- `xplorer_sidebar_chrome_view.h` — Sidebar UI chrome (now minimal: just section label above vertical tab strip)
- `companion/ui/settings.html` — Settings UI with Bookmarks pane (lines 101-119): bookmark editor, add/save buttons
- `companion/ui/settings.js` — Settings JS: bookmark editor state management and persistence
- `companion/ui/settings.css` — Settings UI styling for bookmark editor
- `X_MCP_FEATURES.md` — Feature tracker: Phase 4.1 is the X bookmark bridge feature

**current behavior:** Xplorer has a two-layer bookmark system:

Layer 1 (Config): grok_settings.json stores "bookmarks" array of {id, label, url} dicts. This is the user-editable configuration backing the UI.

Layer 2 (Runtime Tabs): At launch, AgentTabGrouper seeds background tabs for each bookmark config. Each tab is stamped with TabOwnership::bookmark_node_id (matches config id). The Reconcile() method groups all bookmark tabs into a native "Bookmarks" tab group (yellow color, kYellow).

Lifecycle:
1. SeedDefaultBookmarks() runs once on first window, materializes kDefaultBookmarks[] (7 hardcoded entries) into gr

**integration points:**
- browser_api.h/cc: Core CRUD, talks to BookmarkModel::AddURL/Remove; exposed via HTTP routes in agent_gateway.cc
- agent_gateway.cc: HTTP routing dispatcher; maps /bookmarks to BrowserApi calls
- grok_companion_util.h/cc: Config persistence layer; LoadGrokSettings/SaveGrokSettings; live-reload NotifyBookmarkConfigChanged broadcast
- xplorer_agent_tab_grouper.h/cc: Subscribes to bookmark config changes, drives tab lifecycle; calls OpenMissingBookmarkTabs, ApplyBookmarkConfig
- tab_ownership.h: Per-tab metadata carrier; bookmark_node_id stamps the config id on each tab
- settings.html/js: User-facing editor; fetches/saves via /api/settings gateway endpoint
- Reconcile() in agent_tab_grouper.cc: Watches all tabs, identifies bookmark_node_id != 0 tabs, calls EnsureGroup to form native 'Bookmarks' group

**build plan:**
- Phase 0 (Foundation): X MCP provisioning (not yet started per X_MCP_FEATURES.md). Add /api/x/* gateway family, mock mode for testing.
- Phase 4.1 (Bookmark Bridge) — New work needed:
-   A. Data model: Extend bookmark config [{id, label, url}] → [{id, label, url, source: 'browser'|'x', x_id?, ai_group?}]
-   B. X bookmarks fetching: Add GET /api/x/bookmarks endpoint in agent_gateway.cc; calls Grok MCP xapi.bookmarks or mock layer
-   C. Unified model: Create BookmarkBridge class (browser + X bookmarks); expose at GET /api/bookmarks/unified
-   D. AI organization: Add PromptGrokForBookmarkGroups() → categorize merged bookmarks via Grok Build agent; cache in grok_settings.json['bookmark_groups']
-   E. UI updates:
-       - Extend settings.html Bookmarks pane to show X bookmarks (read-only vs synced)
-       - Add 'Source' column (Browser/X) to bookmark editor
-       - Add 'AI Groups' collapse/expand in sidebar or separate section
-       - Add toggle 'Show X bookmarks' / 'Sync X bookmarks'
-   F. 'Save to X' action: Right-click bookmark → 'Save to X'; calls POST /api/x/bookmarks/{url} via browser_api.cc
-   G. Sync service: Bi-directional polling/notification: browser → X via xapi.add_bookmark; X → browser via pull in ApplyBookmarkConfig()
-   H. Mock layer: For testing without X creds, provide /api/x/bookmarks?mode=mock returning canned X bookmark set

**test via mcp:**
- Mock X bookmarks: GET /api/x/bookmarks?mode=mock → returns [{id, title, url, created_at}]
- Unified fetch: GET /api/bookmarks/unified?include_x=true → merges browser + X bookmarks
- AI grouping: POST /api/bookmarks/organize {bookmarks: [...]} → Grok groups by category, returns with ai_group field
- Save to X: POST /api/bookmarks/save-to-x {url, title} → simulates X API add (mock mode returns ok)
- Config persistence: GET /api/settings → grok_settings.json includes bookmarks + x_bookmarks + bookmark_groups arrays
- Live sync: Edit bookmark in settings UI → SetBookmarkConfigs → X bookmark created (if enabled)
- Fallback: If X creds missing, X bookmarks section hidden/disabled in UI; /api/x/* returns {error: 'X credentials not configured'}
- Integration: Open bookmark tab from unified view → agent_tab_grouper stamps it with bookmark_node_id + source field

**gaps:**
- X MCP not yet integrated: Phase 0 (grok config bootstrap, MCP test hooks) is pending per X_MCP_FEATURES.md; needed before any X fetching works
- No X credentials management: No UI to add X app CLIENT_ID/SECRET; grok CLI config not provisioned on first run
- No unified bookmark model: Currently browser_api.cc bookmarks are separate from config bookmarks; no merging view
- No X bookmarks fetching: No gateway /api/x/bookmarks endpoint; no Grok MCP xapi.bookmarks call
- No AI organization: No prompt to Grok Build asking 'organize these bookmarks'; no ai_group field in config
- No 'Save to X' UI: No context menu action or dedicated button; no two-way sync
- No mock X data layer: Testing /api/x/* endpoints requires live X creds or mock server; neither implemented
- No X source tracking: Config format doesn't distinguish browser vs X origins; bookmarks list is flat
- Settings UI incomplete for X: settings.html Bookmarks pane shows only browser bookmarks; no X section, no sync toggle
- No bookmark group sync service: No polling/notification to keep X and browser bookmarks in sync; one-directional only
- Sidebar doesn't reflect AI groups: Vertical tab strip shows flat Bookmarks group; no collapsible AI-organized categories

---
