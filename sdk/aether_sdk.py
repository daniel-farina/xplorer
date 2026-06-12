"""Aether agent SDK — minimal Python client for the AgentGateway.

Zero dependencies beyond the stdlib. Any agent (or LLM tool-use loop) can:

    from aether_sdk import Browser
    b = Browser()
    tab = b.tabs()[0]
    b.navigate(tab, "https://example.com")
    print(b.text(tab)["text"])
"""
from __future__ import annotations

import json
import os
import pathlib
import urllib.request


class Browser:
    def __init__(self, port: int = 9334, token: str | None = None):
        self.base = f"http://127.0.0.1:{port}"
        if token is None:
            token_file = (
                pathlib.Path.home()
                / "Library/Application Support/Chromium/agent_token"
            )
            token = os.environ.get("AETHER_TOKEN") or token_file.read_text().strip()
        self.token = token

    def _req(self, method: str, path: str, body: dict | None = None) -> dict:
        req = urllib.request.Request(
            self.base + path,
            method=method,
            data=json.dumps(body).encode() if body is not None else None,
            headers={
                "Authorization": f"Bearer {self.token}",
                "Content-Type": "application/json",
            },
        )
        with urllib.request.urlopen(req, timeout=60) as r:
            return json.load(r)

    # -- primitives ---------------------------------------------------------
    def tabs(self) -> list[dict]:
        return self._req("GET", "/tabs")["tabs"]

    def open(self, url: str) -> dict:
        return self._req("POST", "/tabs", {"url": url})

    def navigate(self, tab: str, url: str) -> dict:
        return self._req("POST", f"/tabs/{tab}/navigate", {"url": url})

    def text(self, tab: str) -> dict:
        r = self._req("POST", f"/tabs/{tab}/text")
        return json.loads(r["result"]["value"])

    def axtree(self, tab: str) -> dict:
        return self._req("POST", f"/tabs/{tab}/axtree")

    def screenshot(self, tab: str) -> bytes:
        import base64
        r = self._req("POST", f"/tabs/{tab}/screenshot")
        return base64.b64decode(r["data"])

    def click(self, tab: str, selector: str) -> dict:
        return self._req("POST", f"/tabs/{tab}/click", {"selector": selector})

    def type(self, tab: str, selector: str, text: str) -> dict:
        return self._req(
            "POST", f"/tabs/{tab}/type", {"selector": selector, "text": text}
        )

    def press(self, tab: str, key: str) -> dict:
        return self._req("POST", f"/tabs/{tab}/press", {"key": key})

    def eval(self, tab: str, expression: str):
        r = self._req("POST", f"/tabs/{tab}/eval", {"expression": expression})
        return r.get("result", {}).get("value")
