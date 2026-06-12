# Aether — the AI-native browser

Aether is a full **Chromium fork** (Blink, V8, the multiprocess sandbox — the
real engine, not Electron or a wrapper) modified at the C++ source level so any
agent can connect and drive it fast. This file is the guide for **agents and
the people wiring them up**.

---

## Install

### Option A — download a release (recommended)

1. Grab the latest `Aether-macos-arm64.dmg` (or `.zip`) from the
   [Releases](../../releases) page.
2. Open the DMG and drag **Aether** to Applications.
3. First launch: macOS Gatekeeper may warn (the build is self-signed). Right-
   click → Open, or run `xattr -dr com.apple.quarantine /Applications/Aether.app`.

### Option B — build from source

You need macOS + Xcode, ~100 GB free disk, and a few hours.

```sh
git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git
export PATH="$PWD/depot_tools:$PATH"
mkdir chromium && cd chromium && fetch --no-history chromium && cd ..
git clone https://github.com/daniel-farina/aether.git
./aether/apply.sh ./chromium/src      # overlay Aether onto Chromium
./aether/build.sh ./chromium/src      # gn gen + autoninja  (the long step)
open ./chromium/src/out/aether/Aether.app
```

---

## Launch

Aether is a normal browser — double-click it. The **Agent Gateway** starts
automatically and listens on loopback:

| Port | Protocol | Use |
|------|----------|-----|
| 9334 | HTTP + WS | High-level Agent API (below) |
| 9333 | CDP | Raw Chrome DevTools Protocol — point Playwright/Puppeteer here, no flags |

**Auth token** is written to `<profile>/agent_token`
(default `~/Library/Application Support/Aether/Default/agent_token`) or set
`$AETHER_TOKEN`. Send it as `Authorization: Bearer <token>` on every request.

For headless / server use:
```sh
Aether.app/Contents/MacOS/Aether --headless=new --disable-gpu \
  --user-data-dir=/tmp/aether --no-first-run
```

---

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

- **Drive any tab while Aether is in the background** — control is pure CDP
  against the renderer; the window need not be focused or visible.
- **Many tabs in parallel** — background/occlusion throttling is disabled by
  default, so background tabs run full-speed.
- **Screenshot hidden/occluded/inactive tabs** — the gateway holds a
  `WebContents::IncrementCapturerCount()` ref during capture, forcing frame
  production. Stock Chrome hangs here; Aether doesn't.
- **First-class automation** — `navigator.webdriver` stays `false`; no
  "controlled by automation" banner.

See `docs/AGENT_API.md` for the full reference.
