# XBrowser — the AI-native browser

XBrowser is a full **Chromium fork** (Blink, V8, the multiprocess sandbox — the
real engine, not Electron or a wrapper) modified at the C++ source level so any
agent can connect and drive it fast. This file is the guide for **agents and
the people wiring them up**.

---

## Install

### Option A — download a release (recommended)

1. Grab the latest `XBrowser-macos-arm64.dmg` (or `.zip`) from the
   [Releases](../../releases) page.
2. Open the DMG and drag **XBrowser** to Applications.
3. First launch: macOS Gatekeeper may warn (the build is self-signed). Right-
   click → Open, or run `xattr -dr com.apple.quarantine /Applications/XBrowser.app`.

### Option B — build from source

You need macOS + Xcode, ~100 GB free disk, and a few hours.

```sh
git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git
export PATH="$PWD/depot_tools:$PATH"
mkdir chromium && cd chromium && fetch --no-history chromium && cd ..
git clone https://github.com/daniel-farina/xplorer.git
./aether/apply.sh ./chromium/src      # overlay XBrowser onto Chromium
./aether/build.sh ./chromium/src      # gn gen + autoninja  (the long step)
open ./chromium/src/out/aether/XBrowser.app
```

---

## Launch

XBrowser is a normal browser — double-click it. The **Agent Gateway** starts
automatically and listens on loopback:

| Port | Protocol | Use |
|------|----------|-----|
| 9334 | HTTP + WS | High-level Agent API (below) |
| 9333 | CDP | Raw Chrome DevTools Protocol — point Playwright/Puppeteer here, no flags |

### Connecting — start here

**Read one fixed file: `~/.aether/gateway.json`.** XBrowser writes it at startup:

```json
{ "url": "http://127.0.0.1:9334", "token": "…", "cdp_url": "ws://127.0.0.1:9333" }
```

Send the token as `Authorization: Bearer <token>` on every request. Do **not**
hunt for the token under the profile dir — always read `~/.aether/gateway.json`.
(`GET /` is unauthenticated and tells you this; a missing/bad token returns
**401** with a `fix` message, not a 404.)

The **easiest** way to drive XBrowser is the bundled **MCP server** — your agent
gets native `aether_*` tools and never touches curl/JSON. See "MCP" below.

For headless / server use:
```sh
XBrowser.app/Contents/MacOS/XBrowser --headless=new --disable-gpu \
  --user-data-dir=/tmp/aether --no-first-run
```

---

## MCP (recommended for agents)

`sdk/aether_mcp.py` is a zero-dependency MCP server (stdio, Python 3.9+, stdlib
only — nothing to `pip install`). Register it once and your agent gets native
tools: `aether_tabs`, `aether_new_tab`, `aether_navigate`, `aether_read_text`,
`aether_observe`, `aether_click`, `aether_type`, `aether_press`,
`aether_screenshot`, `aether_eval`. It auto-discovers the running browser via
`~/.aether/gateway.json`, so there is no token or port to configure.

First, note the absolute path to the server (substitute below):

```sh
AETHER_MCP="$(cd "$(dirname "$(git rev-parse --show-toplevel)")"; pwd)/aether/sdk/aether_mcp.py"
# …or just: /full/path/to/aether/sdk/aether_mcp.py
```

### Claude Code
```sh
claude mcp add aether -- python3 /full/path/to/aether/sdk/aether_mcp.py
claude mcp list           # verify it shows "aether"
```

### Cursor — `~/.cursor/mcp.json` (or a project `.cursor/mcp.json`)
```json
{ "mcpServers": {
    "aether": { "command": "python3",
                "args": ["/full/path/to/aether/sdk/aether_mcp.py"] } } }
```

### Grok CLI — add to its MCP config (`~/.grok/mcp.json` or via its MCP add command)
```json
{ "mcpServers": {
    "aether": { "command": "python3",
                "args": ["/full/path/to/aether/sdk/aether_mcp.py"] } } }
```

### Claude Desktop — `~/Library/Application Support/Claude/claude_desktop_config.json`
```json
{ "mcpServers": {
    "aether": { "command": "python3",
                "args": ["/full/path/to/aether/sdk/aether_mcp.py"] } } }
```

Any other MCP client: run `python3 /full/path/to/aether/sdk/aether_mcp.py` as a
stdio server.

**Verify** (no client needed) — this lists the tools straight from the server:
```sh
printf '%s\n' \
 '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}' \
 '{"jsonrpc":"2.0","method":"notifications/initialized"}' \
 '{"jsonrpc":"2.0","id":2,"method":"tools/list"}' \
 | python3 /full/path/to/aether/sdk/aether_mcp.py
```

Once registered, the typical loop is `aether_navigate` → `aether_observe` (get
element `ref`s) → `aether_click` / `aether_type` / `aether_press` →
`aether_read_text`. No CSS selectors or shell escaping required. (Start XBrowser
first, or the tools return "XBrowser is not running".)

## Drive it (Python SDK)

```python
from aether_sdk import Browser
from agent_context import Page

b = Browser()                      # connects to 127.0.0.1:9334
tab = b.open("https://example.com", )["owner"]  # opens your own tab
p = Page(b, b.tabs()[-1]["id"])    # generic perceive->act handle

p.goto("https://news.google.com")
link = p.find(name="Top stories", role="link")   # find by role + name
p.click(link["ref"])
print(p.observe()[:5])             # list interactive elements (any site)
print(b.text(tab))                 # clean readability extraction
```

The `Page` layer is **site-agnostic**: `observe()` reads the live DOM's
roles + accessible names (what a screen reader sees), `find()` picks an element
by role/name, and `click/type/press` act on it. No per-site selectors.

---

## Agent API (port 9334)

Tab ids look like `"892053753:0"` (browser session id : tab index).

| Verb | Body | Does |
|------|------|------|
| `GET  /tabs` | — | list tabs **with context** (see below) |
| `POST /tabs` | `{url, owner?, label?}` | open a **new owned** tab |
| `POST /tabs/{id}/navigate` | `{url}` | reuse a tab (waits for load) |
| `POST /tabs/{id}/own` | `{owner?, label?}` | claim / relabel a tab |
| `POST /tabs/{id}/text` | — | readability text extraction |
| `POST /tabs/{id}/axtree` | — | full accessibility tree |
| `POST /tabs/{id}/screenshot` | — | PNG of the viewport (works on hidden tabs) |
| `POST /tabs/{id}/click` | `{selector}` | trusted mouse click |
| `POST /tabs/{id}/type` | `{selector, text}` | real keystrokes |
| `POST /tabs/{id}/press` | `{key}` | a key (Enter, ArrowDown, Tab, …) |
| `POST /tabs/{id}/eval` | `{expression}` | `Runtime.evaluate` |

### Identify yourself (shows in the on-screen HUD)

When an agent drives a tab, XBrowser shows a live overlay in that tab — an
animated badge with the model name and a metrics bar (calls, KB in/out, clicks,
reads…). It only appears while you're acting and fades after ~6s idle. To make
it show **who** is driving, send these headers on every request:

```
X-Agent-Id: grok-cli         # your agent's name
X-Agent-Model: Grok          # the model — shown on the badge
```

Without them the badge still appears but reads "🤖 AI agent". Live counters are
also at `GET /stats`. (Via the MCP server, set `AETHER_AGENT_ID` /
`AETHER_AGENT_MODEL` in the server's `env` and it sends these for you.)

### Live action highlighting

While an agent works, XBrowser flashes a color-coded box over what it touches —
**clicks** (pink), **typed** fields (blue), **read** regions (green), and every
element it **scans/links** it identifies (cyan / gold). Toggle it from the
**✦ highlights** button in the HUD badge (on by default, remembered per-site).
It's purely visual and never appears in the agent's own screenshots.

### Multi-agent: ownership & context

Send `X-Agent-Id: <name>` on requests. Tabs you open are stamped with you as
owner (stored as `WebContents` user-data, so it follows the tab for life).
`GET /tabs` returns per-tab context:

```json
{ "id":"892053753:1", "url":"...", "title":"Example Domain",
  "active":false, "loading":false, "audible":false,
  "owner":"researcher", "label":"baseline", "mine":true }
```

So multiple agents can share one browser: each works in its **own** tabs
(`POST /tabs`), sees who owns every tab (`owner`/`mine`), scopes itself to
"my tabs", and hands off by reassigning ownership (`/own`). The user's own
tabs show `owner: ""`.

---

## What makes it AI-native (built into the C++, no flags)

- **Drive any tab while XBrowser is in the background** — control is pure CDP
  against the renderer; the window need not be focused or visible.
- **Many tabs in parallel** — background/occlusion throttling is disabled by
  default, so background tabs run full-speed.
- **Screenshot hidden/occluded/inactive tabs** — the gateway holds a
  `WebContents::IncrementCapturerCount()` ref during capture, forcing frame
  production. Stock Chrome hangs here; XBrowser doesn't.
- **First-class automation** — `navigator.webdriver` stays `false`; no
  "controlled by automation" banner.

See `docs/AGENT_API.md` for the full reference.
