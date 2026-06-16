#!/usr/bin/env python3
"""Xplorer MCP alias — same tools as xplorer_mcp.py (page + browser-level)."""
import importlib.util
import pathlib
import sys

_DIR = pathlib.Path(__file__).resolve().parent
_spec = importlib.util.spec_from_file_location("xplorer_mcp", _DIR / "xplorer_mcp.py")
_xplorer = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_xplorer)

TOOLS = _xplorer.TOOLS
PROTO = _xplorer.PROTO
reply = _xplorer.reply


def main():
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            req = __import__("json").loads(line)
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