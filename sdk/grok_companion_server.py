#!/usr/bin/env python3
"""Xplorer Grok Companion — local chat UI + Grok Build backend.

Serves a sidebar-ready chat interface on http://127.0.0.1:9345 and proxies
messages to `grok` with per-conversation session IDs. Uses OAuth credentials
already stored in ~/.grok/auth.json (same as Hermes / grok login --oauth).

Writes ~/.xplorer/companion.json for discovery by the browser side panel.
"""
from __future__ import annotations

import argparse
import json
import os
import pathlib
import shutil
import subprocess
import sys
import threading
import urllib.error
import urllib.request
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PORT = 9345
XPLORER_DIR = pathlib.Path.home() / ".xplorer"
SESSIONS_FILE = XPLORER_DIR / "companion_sessions.json"
GROK_BIN = os.environ.get("GROK_BIN", shutil.which("grok") or "grok")
SDK_DIR = pathlib.Path(__file__).resolve().parent
UI_DIR = SDK_DIR.parent / "companion" / "ui"

SYSTEM_RULES = """You are Grok, the native AI companion built into Xplorer.
You can control the browser through MCP tools (xplorer_* for pages, xbrowser_*
for bookmarks, history, tabs, groups, splits, and theme). Be proactive: organize
tabs, manage bookmarks, search history, and help the user browse efficiently.
When you act on the browser, briefly explain what you did."""


def load_sessions() -> dict:
    if SESSIONS_FILE.exists():
        return json.loads(SESSIONS_FILE.read_text())
    return {"conversations": []}


def save_sessions(data: dict) -> None:
    XPLORER_DIR.mkdir(parents=True, exist_ok=True)
    SESSIONS_FILE.write_text(json.dumps(data, indent=2))


def write_companion_json() -> None:
    XPLORER_DIR.mkdir(parents=True, exist_ok=True)
    payload = {
        "url": f"http://127.0.0.1:{PORT}",
        "title": "Grok",
        "model": "grok-build",
    }
    gw = XPLORER_DIR / "gateway.json"
    if gw.exists():
        payload["gateway"] = json.loads(gw.read_text())
    (XPLORER_DIR / "companion.json").write_text(json.dumps(payload, indent=2))


def gateway_status() -> dict:
    gw = XPLORER_DIR / "gateway.json"
    if not gw.exists():
        return {"running": False}
    try:
        g = json.loads(gw.read_text())
        req = urllib.request.Request(g["url"] + "/tabs", method="GET",
                                     headers={"Authorization": "Bearer " + g["token"]})
        with urllib.request.urlopen(req, timeout=3) as r:
            tabs = json.load(r)
        return {"running": True, "tabs": len(tabs.get("tabs", []))}
    except Exception as e:
        return {"running": False, "error": str(e)}


SEARCH_PROMPTS = {
    "web": (
        "You are Grok Search for Xplorer. Answer the user's web search query "
        "concisely using current web knowledge. End with a JSON block on its own "
        'line: {"links":[{"title":"...","url":"https://...","snippet":"..."}]} '
        "with up to 5 real relevant links."
    ),
    "images": (
        "You are Grok Image Search for Xplorer. Describe and list image search "
        "results for the query. Include image URLs when known. End with JSON: "
        '{"links":[{"title":"...","url":"https://...","snippet":"..."}]}'
    ),
    "videos": (
        "You are Grok Video Search for Xplorer. Find and summarize video results "
        "for the query with titles and URLs. End with JSON: "
        '{"links":[{"title":"...","url":"https://...","snippet":"..."}]}'
    ),
    "imagine": (
        "Generate an image for this prompt using your image generation capability. "
        "Return a brief caption then any image URLs produced."
    ),
}


def _parse_links(text: str) -> list:
    import re
    m = re.search(r'\{[\s\S]*"links"[\s\S]*\}\s*$', text)
    if not m:
        return []
    try:
        return json.loads(m.group()).get("links", [])
    except json.JSONDecodeError:
        return []


def grok_search(query: str, mode: str = "web") -> dict:
    prompt = SEARCH_PROMPTS.get(mode, SEARCH_PROMPTS["web"])
    full = f"{prompt}\n\nQuery: {query}"
    cmd = [
        GROK_BIN, "-p", full,
        "--output-format", "json",
        "--always-approve",
        "-m", "grok-build",
        "--max-turns", "15",
    ]
    if mode != "imagine":
        pass  # web search enabled by default
    else:
        cmd.append("--disable-web-search")
    proc = subprocess.run(cmd, capture_output=True, text=True, timeout=300)
    if proc.returncode != 0:
        raise RuntimeError(proc.stderr.strip() or proc.stdout.strip() or "search failed")
    try:
        data = json.loads(proc.stdout)
    except json.JSONDecodeError:
        data = {"text": proc.stdout.strip()}
    text = data.get("text", "")
    links = _parse_links(text)
    answer = text
    if links:
        answer = text[: text.rfind("{")].strip()
    result = {"mode": mode, "answer": answer, "text": answer, "links": links}
    if mode == "imagine":
        import re
        urls = re.findall(r'https?://\S+\.(?:png|jpg|jpeg|webp|gif)', text, re.I)
        result["images"] = urls
    return result


def grok_chat(message: str, session_id: str | None) -> dict:
    cmd = [
        GROK_BIN,
        "-p", message,
        "--output-format", "json",
        "--always-approve",
        "-m", "grok-build",
        "--max-turns", "25",
        "--rules", SYSTEM_RULES,
    ]
    if session_id:
        cmd.extend(["-r", session_id])
    env = os.environ.copy()
    env["XPLORER_AGENT_ID"] = "grok-companion"
    env["XPLORER_AGENT_MODEL"] = "Grok"
    proc = subprocess.run(cmd, capture_output=True, text=True, env=env, timeout=600)
    if proc.returncode != 0:
        err = proc.stderr.strip() or proc.stdout.strip() or "grok failed"
        if "auth" in err.lower() or "login" in err.lower():
            err += " — run: grok login --oauth"
        raise RuntimeError(err)
    try:
        return json.loads(proc.stdout)
    except json.JSONDecodeError:
        return {"text": proc.stdout.strip(), "sessionId": session_id}


class Handler(BaseHTTPRequestHandler):
    server_version = "XplorerGrokCompanion/0.2"

    def log_message(self, fmt, *args):
        sys.stderr.write(f"[companion] {fmt % args}\n")

    def _json(self, code: int, obj: dict) -> None:
        body = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(body)

    def _read_json(self) -> dict:
        n = int(self.headers.get("Content-Length", 0))
        return json.loads(self.rfile.read(n)) if n else {}

    def do_OPTIONS(self):
        self.send_response(HTTPStatus.NO_CONTENT)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.end_headers()

    def do_GET(self):
        path = self.path.split("?", 1)[0]
        # Static assets must be checked before the /search page route — otherwise
        # /search.css and /search/search.css incorrectly return search.html.
        if path.endswith(".js") or path.endswith(".css"):
            return self._serve_ui(path.rsplit("/", 1)[-1])
        if path in ("/", "/index.html"):
            return self._serve_ui("index.html")
        if path in ("/search", "/search/"):
            return self._serve_ui("search.html")
        if self.path == "/api/status":
            return self._json(200, {
                "ok": True,
                "gateway": gateway_status(),
                "grok": shutil.which("grok") or GROK_BIN,
            })
        if self.path == "/api/conversations":
            return self._json(200, load_sessions())
        self._json(404, {"error": "not found"})

    def do_POST(self):
        if self.path == "/api/search":
            body = self._read_json()
            query = body.get("query", "").strip()
            mode = body.get("mode", "web")
            if not query:
                return self._json(400, {"error": "empty query"})
            try:
                return self._json(200, grok_search(query, mode))
            except Exception as e:
                return self._json(500, {"error": str(e)})
        if self.path == "/api/conversations":
            data = load_sessions()
            conv = {
                "id": os.urandom(8).hex(),
                "title": "New chat",
                "session_id": None,
                "messages": [],
            }
            data["conversations"].insert(0, conv)
            save_sessions(data)
            return self._json(200, conv)
        if self.path.startswith("/api/conversations/") and self.path.endswith("/message"):
            conv_id = self.path.split("/")[3]
            body = self._read_json()
            message = body.get("message", "").strip()
            if not message:
                return self._json(400, {"error": "empty message"})
            data = load_sessions()
            conv = next((c for c in data["conversations"] if c["id"] == conv_id), None)
            if not conv:
                return self._json(404, {"error": "conversation not found"})
            conv["messages"].append({"role": "user", "content": message})
            if conv["title"] == "New chat":
                conv["title"] = message[:48] + ("…" if len(message) > 48 else "")
            try:
                result = grok_chat(message, conv.get("session_id"))
            except Exception as e:
                return self._json(500, {"error": str(e)})
            reply_text = result.get("text", "")
            if result.get("sessionId"):
                conv["session_id"] = result["sessionId"]
            conv["messages"].append({"role": "assistant", "content": reply_text})
            save_sessions(data)
            return self._json(200, {
                "reply": reply_text,
                "session_id": conv.get("session_id"),
            })
        self._json(404, {"error": "not found"})

    def do_DELETE(self):
        if self.path.startswith("/api/conversations/"):
            conv_id = self.path.split("/")[3]
            data = load_sessions()
            data["conversations"] = [c for c in data["conversations"]
                                     if c["id"] != conv_id]
            save_sessions(data)
            return self._json(200, {"ok": True})
        self._json(404, {"error": "not found"})

    def _serve_ui(self, name: str):
        path = UI_DIR / name
        if name == "search.html" and not path.exists():
            path = UI_DIR / "search.html"
        if not path.exists():
            return self._json(404, {"error": f"missing {name}"})
        content = path.read_bytes()
        ctype = "text/html" if name.endswith(".html") else (
            "text/css" if name.endswith(".css") else "application/javascript")
        self.send_response(200)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(content)))
        self.end_headers()
        self.wfile.write(content)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=PORT)
    args = parser.parse_args()
    if not UI_DIR.exists():
        sys.exit(f"UI not found: {UI_DIR}")
    write_companion_json()
    httpd = ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    print(f"Grok Companion at http://127.0.0.1:{args.port}", flush=True)
    threading.Thread(target=write_companion_json, daemon=True).start()
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()