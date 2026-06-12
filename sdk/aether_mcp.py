#!/usr/bin/env python3
"""Aether MCP server — exposes the browser to any MCP-capable agent (Claude,
Grok, Cursor, …) as native tools, so the agent never hand-rolls curl/JSON.

Transport: stdio, newline-delimited JSON-RPC 2.0 (the MCP stdio standard).
Zero dependencies — stdlib only.

Register it (example, Claude Code / Cursor mcp config):
    {
      "mcpServers": {
        "aether": { "command": "python3",
                    "args": ["/path/to/aether/sdk/aether_mcp.py"] }
      }
    }

It auto-discovers the running browser via ~/.aether/gateway.json, so there is
nothing to configure — start Aether, add this server, done.
"""
import base64
import json
import os
import pathlib
import sys
import urllib.request

PROTO = "2024-11-05"


def gateway():
    """Read the fixed discovery file the browser writes at startup."""
    p = pathlib.Path.home() / ".aether" / "gateway.json"
    if not p.exists():
        raise RuntimeError(
            "Aether is not running (no ~/.aether/gateway.json). Launch Aether "
            "first.")
    return json.loads(p.read_text())


def api(method, path, body=None):
    g = gateway()
    req = urllib.request.Request(
        g["url"] + path, method=method,
        data=json.dumps(body).encode() if body is not None else None,
        headers={"Authorization": "Bearer " + g["token"],
                 "Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=60) as r:
        return json.load(r)


# -- tool definitions: name -> (description, input schema, handler) ----------

def _first_tab():
    tabs = api("GET", "/tabs")["tabs"]
    return tabs[0]["id"] if tabs else None


def t_tabs(_):
    tabs = api("GET", "/tabs")["tabs"]
    return text(json.dumps(tabs, indent=2))


def t_new_tab(a):
    r = api("POST", "/tabs", {"url": a.get("url", "about:blank"),
                              "owner": a.get("owner", "mcp"),
                              "label": a.get("label", "")})
    return text(json.dumps(r))


def t_navigate(a):
    tab = a.get("tab") or _first_tab()
    api("POST", f"/tabs/{tab}/navigate", {"url": a["url"]})
    return text(f"navigated tab {tab} to {a['url']}")


def t_read_text(a):
    tab = a.get("tab") or _first_tab()
    r = api("POST", f"/tabs/{tab}/text")
    val = r.get("result", {}).get("value")
    if val:
        o = json.loads(val)
        return text(f"# {o['title']}\n{o['url']}\n\n{o['text']}")
    return text(json.dumps(r))


def t_observe(a):
    """Return interactive elements (role + accessible name) — site-agnostic."""
    tab = a.get("tab") or _first_tab()
    js = ("(()=>{const S='a,button,input,textarea,select,[role=button],"
          "[role=link],[role=option],[role=tab],[role=combobox],[role=gridcell]';"
          "const out=[];let n=0;for(const e of document.querySelectorAll(S)){"
          "const r=e.getBoundingClientRect();if(r.width<=0||r.height<=0)continue;"
          "const nm=(e.getAttribute('aria-label')||e.innerText||e.value||"
          "e.placeholder||'').trim().replace(/\\s+/g,' ').slice(0,80);"
          "if(!nm)continue;e.setAttribute('data-aref',n);"
          "out.push({ref:n++,role:e.getAttribute('role')||e.tagName.toLowerCase(),"
          "name:nm});}return JSON.stringify(out.slice(0,60));})()")
    r = api("POST", f"/tabs/{tab}/eval", {"expression": js})
    return text(r.get("result", {}).get("value", "[]"))


def t_click(a):
    tab = a.get("tab") or _first_tab()
    sel = a.get("selector") or f'[data-aref="{a["ref"]}"]'
    return text(json.dumps(api("POST", f"/tabs/{tab}/click", {"selector": sel})))


def t_type(a):
    tab = a.get("tab") or _first_tab()
    sel = a.get("selector") or f'[data-aref="{a["ref"]}"]'
    return text(json.dumps(
        api("POST", f"/tabs/{tab}/type", {"selector": sel, "text": a["text"]})))


def t_press(a):
    tab = a.get("tab") or _first_tab()
    return text(json.dumps(api("POST", f"/tabs/{tab}/press", {"key": a["key"]})))


def t_screenshot(a):
    tab = a.get("tab") or _first_tab()
    r = api("POST", f"/tabs/{tab}/screenshot")
    data = r.get("data")
    if not data:
        return text("screenshot failed: " + json.dumps(r))
    return {"content": [{"type": "image", "data": data, "mimeType": "image/png"}]}


def t_eval(a):
    tab = a.get("tab") or _first_tab()
    r = api("POST", f"/tabs/{tab}/eval", {"expression": a["expression"]})
    return text(json.dumps(r.get("result", r)))


def text(s):
    return {"content": [{"type": "text", "text": s}]}


TAB = {"type": "string", "description": "tab id (default: first tab)"}
TOOLS = {
    "aether_tabs": ("List open browser tabs with context (url, title, owner, "
                    "active, loading).", {"type": "object", "properties": {}},
                    t_tabs),
    "aether_new_tab": ("Open a NEW tab (optionally owned/labelled).",
                       {"type": "object", "properties": {
                           "url": {"type": "string"},
                           "owner": {"type": "string"},
                           "label": {"type": "string"}}}, t_new_tab),
    "aether_navigate": ("Navigate a tab to a URL (waits for load).",
                        {"type": "object", "properties": {
                            "url": {"type": "string"}, "tab": TAB},
                         "required": ["url"]}, t_navigate),
    "aether_read_text": ("Read clean readability text of a tab's page.",
                         {"type": "object", "properties": {"tab": TAB}},
                         t_read_text),
    "aether_observe": ("List interactive elements (role + name + ref) on the "
                       "page. Use the ref with aether_click/aether_type.",
                       {"type": "object", "properties": {"tab": TAB}}, t_observe),
    "aether_click": ("Click an element by ref (from aether_observe) or CSS "
                     "selector.", {"type": "object", "properties": {
                         "ref": {"type": "integer"},
                         "selector": {"type": "string"}, "tab": TAB}}, t_click),
    "aether_type": ("Type text into an element by ref or selector (real "
                    "keystrokes).", {"type": "object", "properties": {
                        "ref": {"type": "integer"},
                        "selector": {"type": "string"},
                        "text": {"type": "string"}, "tab": TAB},
                     "required": ["text"]}, t_type),
    "aether_press": ("Press a key (Enter, ArrowDown, Tab, Escape, …) on the "
                     "focused element.", {"type": "object", "properties": {
                         "key": {"type": "string"}, "tab": TAB},
                      "required": ["key"]}, t_press),
    "aether_screenshot": ("Screenshot a tab's viewport (works on hidden tabs).",
                          {"type": "object", "properties": {"tab": TAB}},
                          t_screenshot),
    "aether_eval": ("Evaluate a JS expression in the page and return the value.",
                    {"type": "object", "properties": {
                        "expression": {"type": "string"}, "tab": TAB},
                     "required": ["expression"]}, t_eval),
}


# -- JSON-RPC over stdio -----------------------------------------------------

def reply(id, result=None, error=None):
    msg = {"jsonrpc": "2.0", "id": id}
    if error is not None:
        msg["error"] = error
    else:
        msg["result"] = result
    sys.stdout.write(json.dumps(msg) + "\n")
    sys.stdout.flush()


def main():
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            req = json.loads(line)
        except Exception:
            continue
        m, id = req.get("method"), req.get("id")
        if m == "initialize":
            reply(id, {"protocolVersion": PROTO,
                       "capabilities": {"tools": {}},
                       "serverInfo": {"name": "aether", "version": "0.1.0"}})
        elif m == "notifications/initialized":
            pass  # no response to notifications
        elif m == "tools/list":
            reply(id, {"tools": [
                {"name": n, "description": d, "inputSchema": s}
                for n, (d, s, _) in TOOLS.items()]})
        elif m == "tools/call":
            p = req.get("params", {})
            name, args = p.get("name"), p.get("arguments", {})
            if name not in TOOLS:
                reply(id, error={"code": -32601, "message": f"no tool {name}"})
                continue
            try:
                reply(id, TOOLS[name][2](args))
            except Exception as e:
                reply(id, {"content": [{"type": "text",
                                        "text": f"error: {e}"}],
                           "isError": True})
        elif id is not None:
            reply(id, error={"code": -32601, "message": f"unknown {m}"})


if __name__ == "__main__":
    main()
