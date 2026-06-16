#!/usr/bin/env python3
"""Test Grok Build conversation stream for native tab organization.

Requires Xplorer running on :9334 and grok CLI authenticated.
"""
from __future__ import annotations

import json
import subprocess
import sys
import urllib.request

BASE = "http://127.0.0.1:9334"
MCP = ["python3", __file__.replace("conversation_tab_test.py", "xplorer_mcp.py")]


def api(method: str, path: str, body=None):
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(
        f"{BASE}{path}", data=data, method=method,
        headers={"Content-Type": "application/json"},
    )
    with urllib.request.urlopen(req, timeout=15) as resp:
        return json.loads(resp.read().decode())


def mcp_group_tool_present() -> bool:
    p = subprocess.Popen(
        MCP, stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1)
    def rpc(method, params):
        rpc.id += 1
        p.stdin.write(json.dumps({"jsonrpc": "2.0", "id": rpc.id,
                                  "method": method, "params": params}) + "\n")
        p.stdin.flush()
        while True:
            msg = json.loads(p.stdout.readline())
            if msg.get("id") == rpc.id:
                return msg
    rpc.id = 0
    rpc("initialize", {})
    p.stdin.write(json.dumps({"jsonrpc": "2.0",
                              "method": "notifications/initialized"}) + "\n")
    p.stdin.flush()
    tools = rpc("tools/list", {})["result"]["tools"]
    p.terminate()
    return any(t["name"] == "xbrowser_group_tabs" for t in tools)


def main() -> int:
    assert mcp_group_tool_present(), "xbrowser_group_tabs missing from xplorer MCP"
    print("mcp xbrowser_group_tabs: OK")

    # Native one-shot API (no grok agent loop).
    req = urllib.request.Request(
        f"{BASE}/api/browser/organize-tabs",
        data=b"{}",
        method="POST",
        headers={"Content-Type": "application/json"},
    )
    with urllib.request.urlopen(req, timeout=30) as resp:
        native = json.loads(resp.read().decode())
    assert native.get("ok") is True, native
    assert native.get("groups_created", 0) >= 1, native
    print(f"native organize-tabs: {native.get('groups_created')} groups")

    conv = api("POST", "/api/conversations", {})
    conv_id = conv["id"]

    req = urllib.request.Request(
        f"{BASE}/api/conversations/{conv_id}/message/stream",
        data=json.dumps({
            "message": "organize my tabs into logical groups",
            "model": "grok-composer-2.5-fast",
        }).encode(),
        method="POST",
        headers={"Content-Type": "application/json"},
    )
    with urllib.request.urlopen(req, timeout=30) as resp:
        body = resp.read().decode("utf-8", errors="replace")

    lines = [ln.strip() for ln in body.splitlines() if ln.strip().startswith("{")]
    assert lines, "no stream events"
    types = set()
    reply = ""
    session_id = None
    for ln in lines:
        evt = json.loads(ln)
        types.add(evt.get("type"))
        if evt.get("type") == "result":
            reply = evt.get("reply") or evt.get("text") or reply
            session_id = evt.get("sessionId") or session_id
        if evt.get("type") == "error":
            raise AssertionError(evt.get("error"))

    assert "text" in types or "result" in types, f"unexpected stream: {types}"
    assert reply.strip(), "empty reply"
    # Fast path uses native organizer — no grok session required.
    if session_id:
        print(f"session_id: {session_id}")
    print(f"stream types: {sorted(types)}")
    print(f"session_id: {session_id}")
    print(f"reply excerpt: {reply[:200]}")

    loaded = api("GET", f"/api/conversations/{conv_id}")
    if session_id:
        assert loaded.get("session_id") == session_id, loaded
    msgs = loaded.get("messages") or []
    assert any(m.get("role") == "assistant" for m in msgs), loaded
    print("session persistence: OK")

    api("DELETE", f"/api/conversations/{conv_id}")
    print("ALL OK")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as e:
        print(f"FAIL: {e}", file=sys.stderr)
        sys.exit(1)