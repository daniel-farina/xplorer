# Xplorer Background-Task + Cronjob System — Design Doc

Status: Reviewed + revised (ready for implementation review)
Owner: Xplorer sidebar/agent team
Scope: agent_gateway (C++) + served sidebar UI (hot-patchable)

> Revision note: every file:line reference below was re-verified against the working tree on 2026-06-25. Two stale refs in the prior draft were corrected (grouper instantiation `apply_integration.py:429-430`, startup hook `apply_integration.py:469-476`). Two genuine correctness holes were fixed: (a) the user-gesture focus reset must use a `TabStripModelObserver` — the "no-header `/activate` = user" shortcut does **not** detect a direct user tab click and would strand focus on an agent (violates G3); it is now v1, not a follow-up. (b) `FocusArbiter` ownership must auto-expire when the owning run ends, otherwise a finished foreground task holds the focus-grant forever. Both are detailed in §3.2.

---

## 1. Overview + Goals

Xplorer's sidebar agent (the `grok` CLI driven through the local AgentGateway) can already open tabs, navigate, click, type, read, and screenshot. What it **cannot** do today:

1. Run on a **schedule** (cron / interval) without a human sitting in a chat turn.
2. Run **independently in the background** across one or more tabs without **stealing the user's focus**.

This doc specifies a background-task + cron system that adds both, built almost entirely out of primitives that already exist in the tree.

### Goals

- **G1 — Scheduling.** A user can register a recurring or one-shot task: `{cron/interval, prompt, model, target}`. It fires on time, headlessly (no chat window open), and persists across browser restarts.
- **G2 — Background execution.** A scheduled task (or a user-started "go do this") runs in one or more tabs that are **open but inactive**. The agent drives them via CDP without those tabs ever becoming the active tab or raising the window.
- **G3 — Never steal focus (the critical requirement).** While the user is browsing, no agent task may yank the active tab or raise/focus the window. The moment the user touches a tab, any agent's focus-grant is revoked. Today every agent-opened tab steals focus (`agent_gateway.cc:383`, `NEW_FOREGROUND_TAB`).
- **G4 — One focus activity at a time.** Multiple concurrent agents/tasks must never fight over the active tab. There is exactly one "focus owner" at any moment; default is the **user**.
- **G5 — Deliberate foreground only.** A task can take focus **only** via an explicit user gesture ("focus this task") — never by surprise, never automatically, even for a task the user flagged `focus:true`.
- **G6 — Reuse over rebuild.** Lean on `TabOwnership`, `AgentSession`, the grok run machinery, the JSON-file persistence pattern, and Chromium's own `NEW_BACKGROUND_TAB` semantics. Do not patch Chromium core (`TabStripModel`, `browser_navigator.cc`).

### Non-goals

- A distributed/server-side scheduler. This is per-install, in-process.
- Replacing the single-active-tab invariant of `TabStripModel` (that invariant is what *guarantees* G4 for free).
- A general workflow/DAG engine. Tasks are a prompt + a schedule.
- Suppressing **page-initiated** focus theft (`window.focus()`, autofocus, `target=_blank` popups). The arbiter governs *agent-initiated* activation only; page-initiated theft is a separate, lower-priority concern (§7) with a known upstream answer (the actor predicate).

---

## 2. Current State (how agent tabs + focus work today)

### 2.1 The agent never touches tabs directly

The sidebar agent is the `grok` CLI run as a subprocess (`grok_native.cc`, launch path `PumpGrokStream` at `grok_native.cc:1369`, child launch at `:1444` win / `:1461` posix). The CLI does not manipulate tabs. It gets MCP tools (`sdk/xplorer_mcp.py`) that make HTTP calls back into the browser's localhost **AgentGateway** (`127.0.0.1:9334`). **All** agent tab control therefore flows through the REST routes in `agent_gateway.cc`. `grok_native.cc` only *reads* tabs for its own screenshot/page-action handoffs (`FindWebContentsByTabId` `:1657`, `FindActiveWebContents` `:1675`, `FindScreenshotTargetWebContents` `:1690`); it never creates or activates tabs.

HTTP and WebSocket share one routing entry point: `OnWebSocketMessage` (`agent_gateway.cc:558`) synthesizes a request and calls the same `RouteRequest` (`agent_gateway.cc:185`, `:572`). So there is exactly **one** new-tab code path to govern for both transports.

### 2.2 Tab creation steals focus today (the bug)

The single new-tab path is `POST /tabs` (`agent_gateway.cc:375-398`). It hardcodes:

```
agent_gateway.cc:383  params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
agent_gateway.cc:384  Navigate(&params);
```

`NEW_FOREGROUND_TAB` makes the new tab the **active** tab, yanking focus from whatever the user is doing. The MCP tool `xplorer_new_tab` maps straight to this (`sdk/xplorer_mcp.py:70`). The system prompt actively encourages fan-out: *"For a batch task (e.g. 'open N sites'), just issue the N new_tab/navigate calls"* (`grok_native.cc:397-398`) — so N tasks today each grab focus in turn. This is exactly the "agents fighting over the active tab" the requirements forbid.

### 2.3 Everything *except* tab-open is already focus-safe

- **Navigate within a tab**: `POST /tabs/{id}/navigate` → `AgentSession::Navigate` (`agent_session.cc:138`) issues a CDP `Page.navigate` against the tab's in-process `DevToolsAgentHost` (bound at `agent_session.cc:79`). It does **not** call `ActivateTabAt`, so it never foregrounds the tab.
- **click / type / press / eval / text / axtree**: all CDP via `DevToolsAgentHost`, no activation.
- **Screenshot**: works on hidden/occluded tabs because `AgentSession` holds `capture_hold_` to keep compositor frames flowing (`agent_session.h:67`). A background tab is **fully driveable** — no screenshot changes needed.

So agents driving an *existing* tab are already focus-safe. The only problem is tab *creation*.

### 2.4 The only deliberate focus-takes

- `POST /tabs/{id}/activate` → `BrowserApi::ActivateTab` → `model->ActivateTabAt(index)` (`browser_api.cc:220-230`; route at `agent_gateway.cc:433-440`). MCP: `xbrowser_activate_tab`.
- `SplitTab` also activates (`browser_api.cc:419`).

There is **no arbitration**: any number of `activate`/`new_tab` calls can fire, last one wins. Multiple agents *will* fight. There is no focus-owner concept anywhere in the tree (grep found none).

### 2.5 Ownership model (the reusable substrate)

Per-tab metadata is `TabOwnership` (`tab_ownership.h:21-53`), a `base::SupportsUserData::Data` on the `WebContents`, so it survives reorders/lifetime (unlike a fragile `sessionId:index` handle). Fields (confirmed at `tab_ownership.h:34-39`): `owner` (agent id; empty == user/unowned), `label`, `model`, `last_action`, plus per-tab HUD counters. Set at creation in `POST /tabs` (`agent_gateway.cc:388-394`, owner stamped at `:391`) and claimable via `POST /tabs/{id}/own` (`agent_gateway.cc:451-466`). It is also stamped lazily on any verb whose request carries an `X-Agent-Id` against an unowned tab (`agent_gateway.cc:507-508`). `GET /tabs` already reports `active`, `loading`, `owner`, `label`, `mine` per tab (`agent_gateway.cc:354-361`).

**Gap for scheduling:** ownership is keyed by a free-form agent-id **string** only. There is no task/run id, no conversation→tab linkage, and no `background` flag. `conv_id` exists only inside `grok_native.cc` to track the live subprocess (`ActiveRuns` map, `RegisterActiveRun` `:1339` / `StopActiveRun` `:1353`) and to persist replies (`SaveChatAssistantReply` `:1216`) — it is **never** written onto `TabOwnership`. So today you cannot answer "which tabs belong to scheduled task X."

### 2.6 No scheduler, no timers, no headless run path

- No `base::Timer` / `RepeatingTimer` / `PostDelayedTask` / cron anywhere in `agent_gateway`. The only delayed-task precedent is `grok_fab.cc:874 ScheduleStartupBurst()` (UI thread, outside the gateway). The scheduler introduces the **first** timer in this process.
- Every run path is wired to a **live HTTP `connection_id`** and writes chunked NDJSON (`SendHttpChunk` `grok_native.cc:1107`, `BeginNdjsonStream` `:1144`). A cron-fired run has no inbound request, so it needs a connection-less sink.
- Persistence is **JSON files only** in `~/.xplorer/` (`kDataDir = ".xplorer"`, `xplorer_paths.h:12`) via `xplorer_paths::Resolve(name)` (`xplorer_paths.cc:56`). E.g. `companion_sessions.json` via `LoadSessions`/`SaveSessions` (`grok_native.cc:353/364`, plain `ReadFileToString` + `WriteFile`), `grok_settings.json`. No prefs, no SQLite.

### 2.7 Agent tabs are visually grouped (foundation already present)

`AgentTabGrouper` (a `TabStripModelObserver`, `xplorer_agent_tab_grouper.{h,cc}`) keeps all agent-owned tabs in one collapsible group titled "Agent tabs (N)". Predicate `IsAgentTab()` = `own && !own->owner.empty()` (`xplorer_agent_tab_grouper.cc:26-29`). It reacts to every `OnTabStripModelChanged` with `Reconcile()` and **never touches the active index** — it only groups. One grouper per `BrowserView`/`TabStripModel`, instantiated via the patch script (`patches/apply_integration.py:429-430`, member decl `:402`, include `:409`). **This observer is the natural host for the user-gesture focus reset (§3.2).**

### 2.8 Threading

Gateway HTTP runs on a dedicated IO thread (`server_thread_`, `MessagePumpType::IO`, `agent_gateway.cc:74-76`). Every tab/browser mutation hops to the UI thread via `content::GetUIThreadTaskRunner({})` (`agent_gateway.cc:179`, `:571`). Any scheduler firing tab actions must do the same. This thread split is load-bearing for the `FocusArbiter` design (§3.2): the activate **gate** runs on the IO thread, but the user-gesture **reset** fires on the UI thread, so the arbiter must be thread-safe.

---

## 3. Architecture

Three subsystems, each minimal:

```
                          +---------------------------------------------+
                          |  Served sidebar UI (hot-patchable HTML/JS)  |
                          |  task editor, task list, "focus this task"  |
                          +-------------------+-------------------------+
                                              | REST (localhost:9334)
                                              v
   +----------------------------------------------------------------------------+
   |  AgentGateway (C++, process-lifetime singleton)                            |
   |                                                                            |
   |   +--------------+    fires    +------------------+                        |
   |   |  Scheduler   | ----------> |  Headless runner | -- grok CLI subproc    |
   |   | (RepeatingTimer on server_thread_)            |   (connection-less)    |
   |   +------+-------+             +--------+---------+                         |
   |          | Load/Save                   | MCP tool calls back in            |
   |   schedules.json                       v                                   |
   |          |                  +------------------------+                     |
   |          |                  |  POST /tabs (BACKGROUND|  <-- the fix        |
   |          |                  |  by default) + AgentSession (CDP)            |
   |          |                  +-----------+------------+                     |
   |          |                              | activate? -> consult             |
   |          |                              v                                  |
   |   +--------------------------------------------------------+              |
   |   |  FocusArbiter (single owner; thread-safe; auto-expires) |  <-- G3/4/5  |
   |   |  default = kUser; reset on any user tab gesture         |              |
   |   +--------------------------------------------------------+              |
   |                                                                            |
   |   TabOwnership (per-WebContents) extended: + task_id + background          |
   +----------------------------------------------------------------------------+
```

### 3.1 Background execution model

**Principle: open background, drive via CDP, never activate, never raise the window.**

1. **Flip the disposition.** Change `POST /tabs` so agent/scheduled tabs open with `WindowOpenDisposition::NEW_BACKGROUND_TAB` instead of `NEW_FOREGROUND_TAB` (`agent_gateway.cc:383`; the header is already included at `agent_gateway.cc:44`). Chromium then does the right thing for free, verified in the fork's chromium checkout:
   - `NormalizeDisposition` strips `AddTabTypes::ADD_ACTIVE` for `NEW_BACKGROUND_TAB` — *"Disposition trumps add types"* (`chromium .../browser_navigator.cc:374-378`).
   - `CreateTargetContents` sets `create_params.initially_hidden = true` (`browser_navigator.cc:512-513`), so the `WebContents` starts HIDDEN — the renderer produces no pixels until shown.
   - `TabStripModel::InsertTabAtImpl` computes `active = (add_types & ADD_ACTIVE) != 0 || empty()` (`tab_strip_model.cc:3547`); with `ADD_ACTIVE` cleared the tab is inserted with **no selection change**, and the window is **not** raised (no `window->Activate()` / `Show()` path runs for a background insert).

   Make this a **per-request option**, not a hard flip: read `body["focus"]` (or `?foreground=1`); default to background for any request, and **always** background for scheduled runs. Note `focus:true` only flags *intent* — it does **not** activate on its own (see G5 / §3.2); a `focus:true` open is still a background open until a user gesture promotes it.

2. **Avoid the foreground-promotion gotchas.** `NormalizeDisposition` rewrites background→foreground when the strip is **empty**, when OTR mismatches, or `CURRENT_TAB` with no source (`browser_navigator.cc:356-371`); and the **first tab in a strip is force-active** (`|| empty()` at `tab_strip_model.cc:3547`). The runner must therefore always set `params.browser`/source to target an **existing, non-empty** normal window — never a fresh/empty window. `POST /tabs` currently builds `NavigateParams(ProfileManager::GetLastUsedProfile(), ...)` (`agent_gateway.cc:380`) which lets Chromium pick the last-active browser; the scheduler must pin an explicit eligible window (§7). If none exists, defer (see Risks).

3. **Drive tabs via the existing focus-safe layer.** All in-tab actions go through `AgentSession` (CDP via `DevToolsAgentHost`), unchanged. Background/hidden tabs are fully driveable; screenshots work via `capture_hold_` (`agent_session.h:67`).

4. **Pace from the browser process, not in-page timers.** A HIDDEN background tab still runs JS/network/event-loop, but Blink throttles it: `setTimeout`/`setInterval`/rAF coalesced to ~1 Hz after 10 s (`page_scheduler_impl.cc:49-50`), to ~1/min after ~5 min (`page_scheduler_impl.h:55-56`), and the page may be **frozen** ~1 min after backgrounding (`page_scheduler_impl.cc:55-56`). Our pacing is already browser-side (the grok CLI step loop drives discrete MCP calls), so this is fine **as long as** task logic never relies on in-page timers. Opting a `kActing` tab out of freezing is a Phase 4 enhancement.

**Why this satisfies G2/G3:** tabs exist, are owned, and are driveable, but the active index never moves, the window is never raised, and the renderer starts hidden.

### 3.2 Focus arbitration ("one focus at a time", user-priority)

The single-active-tab invariant of `TabStripModel` already guarantees a window has exactly one active tab (`selection_model_`; `ActivateTabAt` swaps it, `tab_strip_model.cc:1022-1051`). So "one focus activity" is physically true *within a window* the moment we stop auto-activating. What we add is a **policy** so agents can't *request* activation out of turn, plus a **revocation** so the user always wins.

Introduce a process-wide thread-safe singleton **`FocusArbiter`** (`agent_gateway/focus_arbiter.{h,cc}`) holding:

```
focus_owner ∈ { kUser (default)  |  <agent_id string> }   // guarded by base::Lock
grant_conv_id                                              // the run that owns the grant, for auto-expiry
```

Rules:

- **Background open is unconditional.** `POST /tabs` (background) never consults the arbiter — concurrent agents all open background tabs freely, each stamped `TabOwnership.owner = agent_id`. Because none of them activate, they cannot fight (G4). This holds across windows too: the arbiter is process-wide, so an agent in window 2 cannot activate while the user works in window 1.

- **Activation is gated.** `POST /tabs/{id}/activate` (and `SplitTab`'s activate) consults `FocusArbiter::MayActivate(agent_id)` **when an `X-Agent-Id` header is present** (header parsed at `agent_gateway.cc:216-218`):
  - Allow if the request has **no** `X-Agent-Id` (it's the user/UI), **or** `focus_owner == requesting agent_id` (a task the user already promoted to foreground).
  - Otherwise return `403 {"error":"focus denied: another activity owns focus"}`.
  - The gate runs on the IO thread inside `RouteRequest`, *before* the UI-thread hop that calls `ActivateTabAt`. (Equivalently, gate inside `BrowserApi::ActivateTab` at `browser_api.cc:220` and pass the agent_id through; gating at the route is simpler and keeps `BrowserApi` Chromium-agnostic.)

- **User priority / reset — via observer, not via the gateway.** This is the fix to the prior draft's hole. A direct user tab click **never reaches the gateway**, so "treat a no-header `/activate` as a user gesture" would leave `focus_owner` stuck on an agent after the user clicks away — violating G3. Instead, `AgentTabGrouper` (already a `TabStripModelObserver`, one per window, §2.7) handles `OnTabStripModelChanged` for `selection`/`kSelectionOnly` changes: when the active tab changes due to a real user gesture — i.e. `TabStripSelectionChange` with a `TabStripUserGestureDetails.type != kNone` (the model derives `CHANGE_REASON` from gesture type at `tab_strip_model.cc:1047`) — it calls `FocusArbiter::Get()->ResetToUser()`. Programmatic activations (the agent's own granted `/activate`) carry `kNone` and do **not** reset. This guarantees: the instant the user touches any tab, every agent focus-grant is revoked.

- **Auto-expiry on run end.** A grant is tied to its run's `conv_id`. When that run finishes/fails/cancels (the `UnregisterActiveRun` path, `grok_native.cc:1345`), the arbiter clears the grant if `grant_conv_id` matches, so a completed foreground task does not hold focus rights indefinitely. (Independent of the user-gesture reset; covers the case where the user never clicks away.)

- **Deliberate "focus this task" (G5).** A new route `POST /focus {"owner": "<agent_id>"}`, callable only from the user-driven sidebar UI (which sends **no** `X-Agent-Id`), sets `focus_owner = that agent` (+ records `grant_conv_id`) and then activates that agent's tab (still one at a time). This is the **only** way an agent tab becomes foreground, and it is always a user action.

**Why this is the right hook:** we never patch Chromium core. `TabStripModel::ActivateTabAt` cannot distinguish "user click" from "agent call" (it only sees `TabStripUserGestureDetails`, `tab_strip_model.cc:1047`), so the *allow/deny* discrimination must live at the gateway where the `X-Agent-Id` header identifies the requester, while the *user-won* detection must live at the `TabStripModelObserver` where the gesture type is visible. The fork already owns the only two agent focus-stealing call sites: tab-open (`agent_gateway.cc:383`) and activate (`agent_gateway.cc:433` → `browser_api.cc:230`).

**Reference / future option:** Chromium upstream ships `chrome/browser/actor/` (present in the fork's chromium checkout) which already implements exactly this policy — `actor::IsRunningBackgroundActorTask` returns true only when a task is active *and* its tab is not the active tab *and* its conversation UI isn't showing (`actor_util.cc:31-75`), and `Browser::AddNewContents` demotes `NEW_FOREGROUND_TAB → NEW_BACKGROUND_TAB` / popups to `kShowWindowInactive` for such tasks behind a default-on flag (`browser.cc:1709-1719`, flag at `:335-336`). Our `FocusArbiter` is the lightweight equivalent. Adopting the actor predicate is the clean path **if** we later need to suppress *page-initiated* focus (`window.focus()` via `Browser::ActivateContents`, `browser.cc:1726-1735`) — out of scope for v1 (§7).

### 3.3 CRON / scheduler

**Where it lives:** a `Scheduler` singleton **owned by `AgentGateway`**, not a profile `KeyedService`. Rationale: `AgentGateway` is already a process-lifetime singleton started once in `PostBrowserStart` (`patches/apply_integration.py:469-476`, `AgentGateway::Start(0)`), `GetInstance()` is global (`agent_gateway.h:48-49`), and all the run machinery + JSON persistence already lives in this target. Nothing here is profile-keyed, so a `KeyedService` would add cross-target wiring for no benefit.

**Timer:** arm a `base::RepeatingTimer` on the gateway's IO `server_thread_` task runner (`agent_gateway.cc:74`). Poll every ~30 s; compare `base::Time::Now()` to each job's `next_fire_us`. On match, dispatch.

**Persistence:** new file `schedules.json` via `xplorer_paths::Resolve("schedules.json")`, with `LoadSchedules`/`SaveSchedules` mirroring `LoadSessions`/`SaveSessions` (`grok_native.cc:353/364`). On `AgentGateway::Start`, load it and re-arm. No new BUILD deps (`//base`, profiles, ui, content already present).

**Dispatch (the headless run):** a cron-fired job has no `connection_id`. Two reuse options:

- **Simplest (v1):** call the **blocking** `RunGrokChat(message, session_id, model)` (`grok_native.cc:2035`, via `base::GetAppOutputWithExitCode` at `:2045`) on a `base::ThreadPool` `{MayBlock, USER_VISIBLE}` task, then persist via `SaveChatAssistantReply` (`grok_native.cc:1216`). The full template is the `POST /api/conversations/{id}/message` handler (`grok_native.cc:2967`), including the busy guard (`409` if `ActiveRuns().count(conv_id)`, `:3017`) and the existing in-handler `RunGrokChat` call (`:3056`).
- **Better (v1.1):** a connection-less variant of `PumpGrokStream` (`grok_native.cc:1369`) that drops `SendHttpChunk`/`Begin`/`EndNdjsonStream` and streams into the conversation store instead, so the user can watch progress later.

**Concurrency + cancel (reuse):** register each fired job in the global `ActiveRuns` map (`grok_native.cc:1335`, lock at `:1331`) via `RegisterActiveRun`/`UnregisterActiveRun` (`:1339`/`:1345`), so `POST /api/conversations/{id}/stop` (`grok_native.cc:2954` → `StopActiveRun` cross-platform `Terminate`, `:1353`) already cancels scheduled jobs, and the 409 guard already enforces "one run per conversation." The same `UnregisterActiveRun` path drives the `FocusArbiter` grant auto-expiry (§3.2).

**Tab linkage:** the runner stamps each opened tab's `TabOwnership` with the job's `task_id` (and `background = true`) so `GET /tabs` can answer "which tabs belong to task X" and the UI can offer per-task "focus" (§4).

**CRUD endpoints:** `GET/POST/DELETE /api/schedules` added in `GrokNative::TryHandleRequest` (`grok_native.cc:2145`), mirroring the `/api/conversations` handlers (GET `:2666`, POST `:2934`, DELETE `:3131-3133`).

---

## 4. Data Model

### 4.1 Schedule / job (`schedules.json`)

```jsonc
{
  "version": 1,
  "jobs": [
    {
      "id": "job_8f3a",              // stable id
      "label": "Morning news digest",
      "enabled": true,
      "trigger": {                   // exactly one of cron|interval|once
        "cron": "0 8 * * *",         // optional cron expr
        "interval_sec": null,        // optional fixed interval
        "once_at_us": null           // optional one-shot epoch us
      },
      "next_fire_us": 1750000000000000,
      "last_fire_us": 1749900000000000,
      "last_status": "ok",           // ok | failed | running | skipped | deferred
      "run": {
        "message": "Summarize today's top 5 stories from <sites>",
        "model": "grok-4",
        "cwd": null,                 // for app-build style runs
        "target_conv_id": "conv_..." // where replies are appended; auto-created if null
      },
      "window_target": null,         // explicit profile/window pin; null = last-used (see §7)
      "max_concurrent_tabs": 5
    }
  ]
}
```

Notes: `trigger` is a tagged union; the scheduler computes `next_fire_us` from whichever field is set. `once_at_us` jobs self-disable after firing. **There is intentionally no `focus` field on a scheduled job** — scheduled runs are *always* background (G2/G5); foreground is a live user gesture, not a persisted property. Cron parsing is a small in-process matcher (no new dep) — see Risks for the build-vs-vendor decision.

### 4.2 Run record (per fire)

Reuse the existing **conversation** as the run record. Each fire appends a user message + assistant reply to `target_conv_id` in `companion_sessions.json` (existing flow). No new file. A lightweight in-memory `RunState { task_id, conv_id, pid, state, started_us }` mirrors the `ActorTask` lifecycle for the UI: `kCreated/kActing/kFinished/kFailed/kCancelled` (modeled on `actor_task.h:129-140`).

### 4.3 Tab ↔ task mapping (extend `TabOwnership`)

`TabOwnership` (`tab_ownership.h:34-39`) gains two fields:

```cpp
std::string task_id;    // which scheduled/user task owns this tab ("" == ad-hoc)
bool background = true;  // opened as background agent tab
```

These are stamped in `POST /tabs` alongside the existing `owner`/`label` (`agent_gateway.cc:388-394`) from `body["task_id"]` / `body["focus"]`, and surfaced by `GET /tabs` next to the existing `active`/`owner`/`mine` fields (`agent_gateway.cc:354-361`). This is the *only* schema change to an existing struct; everything else is additive.

### 4.4 Focus owner (`FocusArbiter`, in-memory only)

```cpp
class FocusArbiter {                  // process-wide singleton, thread-safe
 public:
  static FocusArbiter* Get();
  // requester == "" (no X-Agent-Id) => user => always true.
  bool MayActivate(const std::string& requester_agent_id);
  // POST /focus: grant foreground to one agent, tied to its run for auto-expiry.
  void SetOwner(const std::string& agent_id, const std::string& grant_conv_id);
  void ResetToUser();                                  // any real user tab gesture
  void OnRunEnded(const std::string& conv_id);         // called from UnregisterActiveRun
 private:
  base::Lock lock_;
  std::string focus_owner_;      // "" == kUser (default)
  std::string grant_conv_id_;
};
```

Not persisted — focus is a live-session concept; on restart it resets to `kUser`, which is correct (nothing is foreground at startup). Accessed from both the IO thread (gate in `MayActivate`) and the UI thread (`ResetToUser` from the observer), hence the `base::Lock`.

---

## 5. Key Integration Points (exact files/classes/hooks)

| Concern | File:line | Change |
|---|---|---|
| **Focus-steal fix** | `agent_gateway.cc:383` | `NEW_FOREGROUND_TAB` → background by default; read `body["focus"]`/`?foreground=1` to stamp intent only (does not activate) |
| Pin an eligible window | `agent_gateway.cc:380` | set `params.browser` to an existing non-empty normal window (avoid empty-strip foreground promotion) |
| Stamp task/background on new tab | `agent_gateway.cc:388-394` | also set `own->task_id`, `own->background` |
| New tab struct fields | `tab_ownership.h:34-39` | add `task_id`, `background` |
| Surface task/bg in listing | `agent_gateway.cc:354-361` | add `task_id`, `background` to `GET /tabs` JSON |
| Gate activation | route `agent_gateway.cc:433-440` (preferred) or `browser_api.cc:220` | consult `FocusArbiter::MayActivate(agent_id)` when `X-Agent-Id` present; 403 on deny |
| Gate split activate | `browser_api.cc:419` | same gate |
| Requester identity | `agent_gateway.cc:216-218` | reuse parsed `x-agent-id` for the gate |
| FocusArbiter (new) | `agent_gateway/focus_arbiter.{h,cc}` | new thread-safe singleton |
| User-gesture reset (observer) | `xplorer_agent_tab_grouper.cc` `OnTabStripModelChanged` | on active-tab change with `TabStripUserGestureDetails.type != kNone`, call `ResetToUser()` |
| Grant auto-expiry | `grok_native.cc:1345` (`UnregisterActiveRun`) | call `FocusArbiter::OnRunEnded(conv_id)` |
| `POST /focus` (new) | router in `agent_gateway.cc` (alongside `:433`) | user-only "focus this task" → `SetOwner` + activate |
| Scheduler (new) | `agent_gateway/scheduler.{h,cc}`, owned by `AgentGateway` | `RepeatingTimer` on `server_thread_` (`agent_gateway.cc:74`) |
| Schedule persistence (new) | `schedules.json` via `xplorer_paths::Resolve` (`xplorer_paths.cc:56`) | `Load/SaveSchedules` mirror `grok_native.cc:353/364` |
| Startup load + arm | `patches/apply_integration.py:469-476` (after `AgentGateway::Start`) | `Scheduler::Load()` + arm timer |
| Headless dispatch | `RunGrokChat` `grok_native.cc:2035` (v1) or fork of `PumpGrokStream` `:1369` (v1.1) | connection-less run, persist via `SaveChatAssistantReply` `:1216` |
| Run flow template | `POST /api/conversations/{id}/message` `grok_native.cc:2967` (busy guard `:3017`, run call `:3056`) | copy append→load→run→save |
| Concurrency/cancel reuse | `ActiveRuns` `grok_native.cc:1335`, `StopActiveRun` `:1353`, `/stop` `:2954` | register fired jobs; cancel for free |
| Schedule CRUD endpoints (new) | `GrokNative::TryHandleRequest` `grok_native.cc:2145` | `GET/POST/DELETE /api/schedules` mirroring `:2666/:2934/:3131` |
| Background-tab driver (no change) | `AgentSession` `agent_session.cc:138-153`, `agent_session.h:67` | already focus-safe + works on hidden tabs |
| UI thread hop (reuse) | `content::GetUIThreadTaskRunner({})` `agent_gateway.cc:179/571` | all tab mutations from scheduler must hop here |

**Do NOT touch:** `TabStripModel::ActivateTabAt`, `browser_navigator.cc`, `Browser::ActivateContents` — Chromium core. The fork owns the only two agent focus-stealing call sites; gate those.

---

## 6. Phased Implementation Plan

### Phase 0 — Background tabs (smallest valuable slice, no scheduler)
*Outcome: agent work stops stealing focus. Satisfies G2/G3 minimally.*
1. `agent_gateway.cc:383`: background by default, `focus` opt-in (intent flag only); pin an eligible non-empty window at `:380`.
2. `tab_ownership.h`: add `background` (+ `task_id` plumbed but unused yet).
3. `GET /tabs`: surface `background`.
- **Test:** with the user typing in tab A, fire `POST /tabs` → new tab appears in the "Agent tabs" group, inactive; tab A keeps focus and caret. Run two concurrent fan-outs → neither moves the active tab nor raises the window.

### Phase 1 — Focus arbitration (G3/G4/G5)
1. `FocusArbiter` thread-safe singleton.
2. Gate `/activate` + `SplitTab` activate when `X-Agent-Id` present.
3. User-gesture reset via the `AgentTabGrouper` observer (real reset, not the no-header shortcut).
4. Grant auto-expiry from `UnregisterActiveRun`.
5. `POST /focus` (user "focus this task").
- **Test:** agent `A` calls `/activate` with no prior `/focus` → 403. User clicks "focus task A" → `/focus` sets owner, A's tab activates. Concurrent `B` `/activate` → 403. **User clicks tab C directly (no gateway call)** → observer fires `ResetToUser()`; A `/activate` → 403 again. A's run finishes with the grant still held and the user idle → `OnRunEnded` clears the grant.

### Phase 2 — Scheduler core (G1, background-only)
1. `schedules.json` + `Load/SaveSchedules`; load+arm in `PostBrowserStart`.
2. `Scheduler` with `RepeatingTimer`; interval + one-shot triggers first (cron parser in Phase 3).
3. Headless dispatch via blocking `RunGrokChat`; register in `ActiveRuns`; persist reply.
4. `GET/POST/DELETE /api/schedules`.
- **Test:** create an interval job (every 2 min) that opens 3 sites and summarizes; verify it fires on time, tabs open background, reply lands in the conversation, restart re-arms it, `/stop` cancels a running fire.

### Phase 3 — Cron expressions + task↔tab linkage UI
1. Cron-expr trigger (`next_fire_us` from a small matcher).
2. Stamp `task_id` on opened tabs; `GET /tabs?task_id=` filter.
3. Served sidebar UI: task editor, task list with last-run status, per-task "Focus this task" button (calls `/focus`).

### Phase 4 — Hardening (optional / v1.1+)
1. Connection-less streaming runner (watch progress live).
2. Opt active-task tabs out of Blink freezing while `kActing`.
3. Evaluate adopting `chrome/browser/actor/` for OS-window-focus / page-initiated-focus suppression and the richer task state machine.

---

## 7. Risks / Open Questions

- **Empty/new window foreground-promotion.** `NormalizeDisposition` promotes background→foreground on an empty strip, and the first tab in a strip is force-active (`browser_navigator.cc:356-371`, `tab_strip_model.cc:3547`). The runner must pin an existing non-empty normal window (`params.browser` at `agent_gateway.cc:380`). **Open:** behavior if no normal window is open at fire time — options: defer the run (`last_status:"deferred"`, retry next poll), or open a window inactively. v1: defer + retry. Persist `window_target` per job for multi-window/multi-profile installs (default last-used).
- **Background throttling/freezing.** Hidden tabs throttle timers to ~1 Hz / ~1/min and may freeze after ~1 min (`page_scheduler_impl.cc:49-56`). Safe today because pacing is browser-side, but any task relying on in-page timers will stall. **Mitigation:** document the constraint; Phase 4 freeze opt-out.
- **Page-initiated focus theft.** A page can self-activate via `Browser::ActivateContents` (`browser.cc:1726-1735`, e.g. `window.focus()`/autofocus) or spawn a foreground popup via `AddNewContents`. The `FocusArbiter` gate covers *agent-initiated* activation only. **Decision:** v1 accepts this (rare on the curated sites tasks target); full coverage = adopt the actor predicate which already demotes these (`browser.cc:1709-1719`, default-on).
- **Cron parser: build vs vendor.** No cron lib in the tree. A 5-field matcher is ~100 lines and avoids a dep. **Decision needed.** v1 ships interval + one-shot; cron in Phase 3.
- **Headless run output sink.** v1 reuses blocking `RunGrokChat` (no live progress). If live progress is required for v1, do the connection-less `PumpGrokStream` fork instead (more work; Phase 4).
- **Missed fires while browser closed.** Cron jobs whose time passed during downtime. **Proposed:** fire once on next start if `now - next_fire_us` < one interval (catch-up), else skip and roll forward.
- **Arbiter vs. observer thread race.** The gate (`MayActivate`, IO thread) and the reset (`ResetToUser`, UI thread) touch the same scalar; the `base::Lock` serializes them. Worst case is a benign one-poll-stale read (an agent's `/activate` allowed microseconds before a user click resets) — the user's next gesture corrects it, and the agent's single activation cannot fight the user's because the model already moved the active tab.
- **Per-window vs process-wide focus.** "One focus" is per-window in `TabStripModel`; the OS focuses one window. `FocusArbiter` is process-wide, so it already prevents an agent in window 2 from activating while the user works in window 1.

---

## 8. Served-UI / Hot-Patchable vs C++ Rebuild

| Component | Surface | Hot-patchable? |
|---|---|---|
| Task editor, task list, status badges, "Focus this task" button | Served sidebar HTML/JS (same pattern as the existing Grok side panel) | **Yes** — ships without a Chromium rebuild; talks to `/api/schedules` + `/focus` over localhost |
| MCP tool surface (`xplorer_new_tab` passing `focus`/`task_id`) | `sdk/xplorer_mcp.py` (Python) | **Yes** — Python, no rebuild |
| System-prompt guidance ("open background tabs; don't request focus") | prompt strings | In-binary prompt at `grok_native.cc:397` needs a rebuild; served prompt is hot |
| `schedules.json` format + cron semantics | data file | **Yes** — versioned file |
| **Disposition flip** (`agent_gateway.cc:383`) | C++ | **No — requires Chromium rebuild** |
| **`FocusArbiter`** + activation gate + observer reset | C++ | **No — rebuild** |
| **`Scheduler`** + timer + headless dispatch | C++ | **No — rebuild** |
| `TabOwnership` new fields + `GET /tabs` surfacing | C++ | **No — rebuild** |
| Schedule CRUD + `/focus` routes | C++ (`grok_native.cc` router) | **No — rebuild** |

**Implication:** the focus/background/scheduler core is unavoidably a C++ change (one batched Chromium rebuild covers Phases 0–2). After that single rebuild ships the REST surface (`/api/schedules`, `/focus`, background `POST /tabs`, `task_id` in `GET /tabs`), **all** task UX, MCP wiring, prompt tuning, and cron-format iteration become hot-patchable. Design the C++ REST contract once, conservatively, so the served UI can evolve without further rebuilds.

---

## 9. Requirements Traceability (does it actually satisfy the hard requirement?)

| Requirement | Mechanism | Guarantee |
|---|---|---|
| Agent work never steals focus | `POST /tabs` → `NEW_BACKGROUND_TAB` (`agent_gateway.cc:383`); `ADD_ACTIVE` cleared + `initially_hidden` (`browser_navigator.cc:378/513`); no `ActivateTabAt`, no `window->Activate()` on a background insert | New tab is created hidden/inactive; user's active tab and window are untouched |
| One focus activity at a time | `TabStripModel` single `selection_model_` (one active tab per window) + process-wide `FocusArbiter` single owner | Physically impossible to have two active tabs in a window; arbiter prevents cross-window agent activation |
| No agent-vs-agent focus fighting | Agents only ever call background `POST /tabs`; `/activate` is gated and only one owner can hold the grant | N concurrent agents open background tabs; none can activate unless it is the single grant holder |
| User browsing never interrupted | In-tab actions are CDP-only and never activate (`agent_session.cc:138`); any real user tab gesture revokes the grant via the observer (`tab_strip_model.cc:1047` gesture type) | Agent driving never foregrounds; the instant the user touches a tab, agent focus rights are gone |
| Deliberate foreground only | `POST /focus` (user-only, no `X-Agent-Id`) is the sole path to a grant; `focus:true` is intent metadata only, not activation | A task reaches the foreground only by an explicit user gesture, never by surprise |
| Cron persistence is real | `schedules.json` via `xplorer_paths::Resolve` + `Load/SaveSchedules`; re-armed in `PostBrowserStart` (`apply_integration.py:469-476`) | Tasks survive restart and re-arm on launch |
| Headless triggering is real | `RepeatingTimer` on `server_thread_` → connection-less `RunGrokChat` (`grok_native.cc:2035`) → `SaveChatAssistantReply` (`:1216`); tracked in `ActiveRuns`, cancellable via `/stop` | A run fires with no chat window / no live `connection_id`, persists its reply, and is cancellable |