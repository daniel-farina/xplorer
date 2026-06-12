# XBrowser Agent API

Two endpoints, both loopback-only, both live the moment the browser starts:

| Port | Protocol | What |
|------|----------|------|
| 9333 | CDP (WebSocket) | Full Chrome DevTools Protocol — point Playwright, Puppeteer, or any CDP client at `ws://127.0.0.1:9333`. No launch flags needed. |
| 9334 | HTTP + WS | High-level Agent API (below). One round trip per primitive. |

**Auth:** `Authorization: Bearer <token>` where the token is in
`<profile dir>/agent_token` (e.g. `~/Library/Application Support/Chromium/agent_token`)
or `$AETHER_TOKEN`. The server binds 127.0.0.1 only; the token defends
against cross-origin/localhost-probing attacks from web pages.

## HTTP routes

Tab ids look like `"12:0"` (browser session id : tab index), from `GET /tabs`.

| Route | Body | Returns |
|-------|------|---------|
| `GET /tabs` | — | `{"tabs": [{id, url, title}]}` |
| `POST /tabs` | `{"url"}` | opens a tab |
| `POST /tabs/{id}/navigate` | `{"url"}` | resolves after `load` |
| `POST /tabs/{id}/text` | — | `{title, url, text}` — readability extraction, the fast path for reading pages |
| `POST /tabs/{id}/axtree` | — | full accessibility tree (grounding for click targets) |
| `POST /tabs/{id}/screenshot` | — | `{"data": <base64 png>}` |
| `POST /tabs/{id}/click` | `{"selector"}` | trusted input-event click |
| `POST /tabs/{id}/type` | `{"selector", "text"}` | focus + insert text |
| `POST /tabs/{id}/eval` | `{"expression"}` | `Runtime.evaluate`, awaits promises |

## WebSocket

Connect to `ws://127.0.0.1:9334/session` with the same bearer header. Frames:
`{"id": 1, "tab": "12:0", "verb": "text", ...params}` → replies echo `id`.

## Native AI-native capabilities (no launch flags)

These are compiled into XBrowser — an agent gets them automatically, whether or
not XBrowser is the foreground app:

- **Drive any tab while XBrowser is in the background.** DOM, JS, navigation,
  click, and type all run against the renderer over CDP and never require the
  window to be focused or even visible. You can use your Mac normally while an
  agent works in XBrowser.
- **Many tabs in parallel.** XBrowser disables renderer backgrounding,
  occluded-window backgrounding, and background timer throttling by default
  (`chrome_main_delegate.cc`, applied at startup), so background tabs keep
  running scripts and timers at full speed. Measured: 4 tabs navigated +
  scraped concurrently in ~1.2s with XBrowser *not* the active app.
- **Screenshot any tab, even hidden/occluded/inactive.** The gateway holds a
  `WebContents::IncrementCapturerCount()` ref for the duration of a capture
  (`agent_session.cc`), forcing the renderer to produce compositor frames the
  way tab-mirroring does. Stock Chrome's `Page.captureScreenshot` hangs on an
  occluded window waiting for a frame that the OS never composites; XBrowser
  doesn't. Measured: a background tab captured in 0.48s, and 3 background tabs
  captured concurrently in 0.4s, all while XBrowser was inactive.

## Running headless / in automation contexts

For agent workloads with no display (CI, a server, a daemon), launch with:

```sh
Chromium.app/Contents/MacOS/Chromium --headless=new --disable-gpu \
  --user-data-dir=/path/to/profile --no-first-run
```

`--disable-gpu` selects software (SwiftShader) compositing, which is the most
portable choice on headless servers. In a normal desktop launch it is
unnecessary — XBrowser's built-in capturer-hold (see above) already makes
screenshots of background/occluded tabs work without any flags.

## Design notes

- Commands execute against the tab's **in-process** `DevToolsAgentHost` —
  there's no internal websocket hop, which is why primitives are fast.
- Clicks/typing go through `Input.dispatchMouseEvent`/`insertText`, so they
  are trusted events, indistinguishable from a human.
- `navigator.webdriver` stays `false`: XBrowser never sets the automation
  flag for gateway sessions. Agents are first-class users.
