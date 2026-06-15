<p align="center">
  <img src="branding/xplorer-mark-512.png" alt="Xplorer" width="96" height="96">
</p>

<h1 align="center">Xplorer</h1>

<p align="center"><b>An open-source, Grok-native web browser.</b></p>

Xplorer is a full web browser: a fork of Chromium (the real thing — Blink, V8,
the multiprocess sandbox, the whole content layer), modified at the C++ source
level to be **AI-native**. It is not a wrapper, not Electron, not CEF.

## What "AI-native" means here

Stock Chrome only exposes the Chrome DevTools Protocol (CDP) when launched
with `--remote-debugging-port`, and treats automation as a second-class debug
mode. XBrowser bakes agent access into the browser process itself:

1. **AgentGateway service** (`chrome/browser/agent_gateway/`) — a component
   compiled into the browser process that starts automatically at profile
   load. It exposes:
   - the full CDP over `ws://127.0.0.1:9333` (always on, token-authenticated)
   - a higher-level HTTP/WS **Agent API** on `127.0.0.1:9334` with fast
     primitives agents actually want: `navigate`, `text` (readability-style
     extraction), `axtree` (accessibility tree snapshot), `click`, `type`,
     `screenshot`, `eval`, `tabs` — each one round-trip instead of a CDP
     session dance.
2. **No automation banner / no `navigator.webdriver` poisoning** for gateway
   sessions — agents are first-class users, not intruders.
3. **Branding**: ships as "XBrowser" (`chrome/app/theme` + GN branding), so it
   installs side-by-side with Chrome.

## Repo layout

This is an *overlay repo*: Chromium is too large to vendor, so XBrowser is
`upstream chromium checkout` + `aether/` applied on top.

```
aether/
  patches/         numbered .patch files applied to the chromium tree
  src/             new files copied verbatim into the tree (agent_gateway/)
  build/args.gn    release build configuration
  sdk/             Python client SDK for agents
  docs/            agent API reference
  apply.sh         copies src/, applies patches/
  build.sh         gn gen + autoninja
```

## Building

```sh
./aether/apply.sh   # overlay onto ../chromium/src
./aether/build.sh   # gn gen out/aether && autoninja -C out/aether chrome
```

Requires: macOS + Xcode, depot_tools on PATH, ~80 GB disk, several hours.

## Connecting an agent

```python
from aether_sdk import Browser
b = Browser()                # connects to ws://127.0.0.1:9334
page = b.new_tab("https://example.com")
print(page.text())           # clean extracted text, one round trip
page.click("a#more")
tree = page.axtree()         # accessibility tree for grounding
```

Raw CDP (Playwright/Puppeteer compatible) is on port 9333 — point any
existing tool at `ws://127.0.0.1:9333` with the token from
`~/Library/Application Support/XBrowser/agent_token`.
