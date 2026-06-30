#!/usr/bin/env python3
"""Mock X MCP server — a fixture-backed stand-in for the real X API MCP
(api.x.com/mcp via the xurl bridge), so Xplor's X-integration features are fully
testable WITHOUT live X credentials. It exposes the same tool surface the real X
MCP does (post search, trends, news, user lookup, bookmarks) with deterministic,
query-aware fixtures. Swap in the real `xapi` server (xurl bridge) once an X
dev-app CLIENT_ID/SECRET is configured — the prompts are tool-name-agnostic so the
features work against either. Stdio JSON-RPC, modeled on sdk/xplorer_mcp.py.

Run standalone for a self-test:  echo '<json-rpc>' | python3 mock_x_mcp.py
"""
import sys
import json

PROTO = "2024-11-05"

# -- fixtures ----------------------------------------------------------------

_VOICES = [
    ("@levelsio", "Pieter Levels"), ("@karpathy", "Andrej Karpathy"),
    ("@sama", "Sam Altman"), ("@swyx", "swyx"), ("@elonmusk", "Elon Musk"),
    ("@emollick", "Ethan Mollick"), ("@simonw", "Simon Willison"),
]


def _posts(query, n=6):
    base = (query or "ai agents").strip()
    takes = [
        f"{base} is moving faster than anyone expected — the next 6 months are wild.",
        f"hot take: most people are sleeping on what {base} actually unlocks.",
        f"shipped something with {base} today. the demos don't do it justice.",
        f"the {base} discourse is 80% noise. here's the signal in one thread \U0001f9f5",
        f"counterpoint: {base} is overhyped for now, but the trajectory is real.",
        f"if you're not paying attention to {base} yet, start this week.",
    ]
    out = []
    for i in range(min(n, len(_VOICES))):
        h, name = _VOICES[i]
        out.append({
            "id": f"mock{i:02d}",
            "author": h, "name": name,
            "text": f"[mock] {takes[i % len(takes)]}",
            "likes": 1820 - i * 210, "reposts": 430 - i * 55, "replies": 120 - i * 14,
            "verified": i < 4,
            "created_at": f"2026-06-30T{(7 + i) % 24:02d}:1{i}:00Z",
            "url": f"https://x.com/{h.lstrip('@')}/status/{1900000000 + i}",
            "_mock": True,
        })
    return out


def _trends(location):
    loc = location or "Worldwide"
    base = ["Grok 5", "#AIagents", "Apple Silicon", "open source", "MCP",
            "Chromium", "real-time search", "xAI"]
    return [{"name": t, "post_count": 50000 - i * 4200, "category": "Technology",
             "location": loc, "_mock": True} for i, t in enumerate(base[:7])]


def _news(query):
    q = (query or "technology").strip()
    return [{
        "title": f"[mock] What the latest {q} moves mean for builders",
        "summary": f"A roundup of the {q} story dominating X over the past few hours, "
                   f"with the key voices and the contrarian takes.",
        "url": f"https://x.com/i/news/mock-{i}",
        "post_count": 9000 - i * 1500, "_mock": True,
    } for i in range(3)]


def _user(handle):
    h = (handle or "levelsio").lstrip("@")
    return {"handle": f"@{h}", "name": h.title(), "verified": True,
            "followers": 512000, "following": 900, "bias": "builder",
            "bio": f"[mock] building things in public. all opinions about {h}.",
            "url": f"https://x.com/{h}", "_mock": True}


# -- tools -------------------------------------------------------------------

def t_search_posts(a):
    q = a.get("query", "")
    n = int(a.get("limit", 6) or 6)
    return text(json.dumps({"query": q, "count": min(n, 7),
                            "posts": _posts(q, n), "_mock": True}, indent=2))


def t_trends(a):
    return text(json.dumps({"location": a.get("location", "Worldwide"),
                            "trends": _trends(a.get("location")), "_mock": True}, indent=2))


def t_news(a):
    return text(json.dumps({"query": a.get("query", ""),
                            "stories": _news(a.get("query")), "_mock": True}, indent=2))


def t_user_lookup(a):
    return text(json.dumps({"user": _user(a.get("handle", "")), "_mock": True}, indent=2))


def t_user_posts(a):
    h = a.get("handle", "")
    return text(json.dumps({"handle": h, "posts": _posts(h or "updates",
                            int(a.get("limit", 5) or 5)), "_mock": True}, indent=2))


def t_bookmarks(a):
    return text(json.dumps({"bookmarks": _posts("saved reading", 4), "_mock": True}, indent=2))


def text(s):
    return {"content": [{"type": "text", "text": s}]}


STR = {"type": "string"}
INT = {"type": "integer"}
TOOLS = {
    "x_search_posts": ("Search X (Twitter) posts (full-archive). Returns top posts "
                       "with author, engagement, and links for a query.",
                       {"type": "object", "properties": {"query": STR, "limit": INT},
                        "required": ["query"]}, t_search_posts),
    "x_trends": ("Get trending topics on X for a location (default Worldwide).",
                 {"type": "object", "properties": {"location": STR}}, t_trends),
    "x_news": ("Get news stories surfacing on X, optionally filtered by query.",
               {"type": "object", "properties": {"query": STR}}, t_news),
    "x_user_lookup": ("Look up an X user by handle (profile, followers, bio).",
                      {"type": "object", "properties": {"handle": STR},
                       "required": ["handle"]}, t_user_lookup),
    "x_user_posts": ("Get an X user's recent posts by handle.",
                     {"type": "object", "properties": {"handle": STR, "limit": INT},
                      "required": ["handle"]}, t_user_posts),
    "x_bookmarks": ("List the current user's X bookmarks.",
                    {"type": "object", "properties": {}}, t_bookmarks),
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
                       "serverInfo": {"name": "x-mock", "version": "0.1.0"}})
        elif m == "notifications/initialized":
            pass
        elif m == "tools/list":
            reply(id, {"tools": [{"name": n, "description": d, "inputSchema": s}
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
