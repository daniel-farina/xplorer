#!/usr/bin/env python3
"""Xplorer MCP — page control (aether_*) plus browser-level tools for Grok.

Registers with Grok Build as the native browser companion MCP server.
Stdlib only. Auto-discovers ~/.xplorer/gateway.json.
"""
import importlib.util
import json
import pathlib
import sys

_DIR = pathlib.Path(__file__).resolve().parent
_spec = importlib.util.spec_from_file_location("aether_mcp", _DIR / "aether_mcp.py")
_aether = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_aether)

api = _aether.api
text = _aether.text
PROTO = _aether.PROTO
reply = _aether.reply


def t_bookmarks(_):
    return text(json.dumps(api("GET", "/bookmarks"), indent=2))


def t_add_bookmark(a):
    body = {"url": a["url"], "title": a.get("title", "")}
    if a.get("parent_id"):
        body["parent_id"] = a["parent_id"]
    return text(json.dumps(api("POST", "/bookmarks", body)))


def t_remove_bookmark(a):
    return text(json.dumps(api("DELETE", f"/bookmarks/{a['id']}")))


def t_history(a):
    body = {"query": a.get("query", ""), "limit": int(a.get("limit", 50))}
    return text(json.dumps(api("POST", "/history", body), indent=2))


def t_activate_tab(a):
    return text(json.dumps(api("POST", f"/tabs/{a['tab']}/activate")))


def t_close_tab(a):
    return text(json.dumps(api("DELETE", f"/tabs/{a['tab']}")))


def t_group_tabs(a):
    body = {"tab_ids": a["tab_ids"], "title": a.get("title", "")}
    return text(json.dumps(api("POST", "/tabs/group", body)))


def t_split_tab(a):
    tab = a.get("tab") or _aether._first_tab()
    layout = a.get("layout", "side_by_side")
    return text(json.dumps(api("POST", f"/tabs/{tab}/split",
                               {"layout": layout})))


def t_get_theme(_):
    return text(json.dumps(api("GET", "/theme"), indent=2))


def t_set_theme(a):
    return text(json.dumps(api("POST", "/theme",
                               {"color_scheme": a["color_scheme"]})))


TAB = {"type": "string", "description": "tab id (sessionId:index)"}

BROWSER_TOOLS = {
    "xbrowser_bookmarks": (
        "List all bookmarks (bar + other folders) as a flat tree.",
        {"type": "object", "properties": {}}, t_bookmarks),
    "xbrowser_add_bookmark": (
        "Add a bookmark. Optional parent_id (folder node id).",
        {"type": "object", "properties": {
            "url": {"type": "string"},
            "title": {"type": "string"},
            "parent_id": {"type": "string"}},
         "required": ["url"]}, t_add_bookmark),
    "xbrowser_remove_bookmark": (
        "Remove a bookmark by node id.",
        {"type": "object", "properties": {"id": {"type": "string"}},
         "required": ["id"]}, t_remove_bookmark),
    "xbrowser_history": (
        "Search browsing history. Empty query returns recent visits.",
        {"type": "object", "properties": {
            "query": {"type": "string"},
            "limit": {"type": "integer"}}}, t_history),
    "xbrowser_activate_tab": (
        "Focus/switch to a tab by id.",
        {"type": "object", "properties": {"tab": TAB},
         "required": ["tab"]}, t_activate_tab),
    "xbrowser_close_tab": (
        "Close a tab by id.",
        {"type": "object", "properties": {"tab": TAB},
         "required": ["tab"]}, t_close_tab),
    "xbrowser_group_tabs": (
        "Group tabs together with an optional title.",
        {"type": "object", "properties": {
            "tab_ids": {"type": "array", "items": {"type": "string"}},
            "title": {"type": "string"}},
         "required": ["tab_ids"]}, t_group_tabs),
    "xbrowser_split_tab": (
        "Open split view from a tab (side_by_side or stacked).",
        {"type": "object", "properties": {
            "tab": TAB,
            "layout": {"type": "string",
                       "enum": ["side_by_side", "stacked"]}}}, t_split_tab),
    "xbrowser_get_theme": (
        "Get browser theme (color_scheme: dark/light/system).",
        {"type": "object", "properties": {}}, t_get_theme),
    "xbrowser_set_theme": (
        "Set browser color scheme: dark, light, or system.",
        {"type": "object", "properties": {
            "color_scheme": {"type": "string",
                             "enum": ["dark", "light", "system"]}},
         "required": ["color_scheme"]}, t_set_theme),
}

TOOLS = {**_aether.TOOLS, **BROWSER_TOOLS}


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
                       "serverInfo": {"name": "xbrowser", "version": "0.3.0"}})
        elif m == "notifications/initialized":
            pass
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
                reply(id, {"content": [{"type": "text", "text": f"error: {e}"}],
                           "isError": True})
        elif id is not None:
            reply(id, error={"code": -32601, "message": f"unknown {m}"})


if __name__ == "__main__":
    main()