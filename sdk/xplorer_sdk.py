"""Xplorer agent SDK — minimal Python client for the AgentGateway.

Zero dependencies beyond the stdlib. Any agent (or LLM tool-use loop) can:

    from xplorer_sdk import Browser
    b = Browser()
    tab = b.tabs()[0]["id"]
    b.navigate(tab, "https://example.com")
    print(b.text(tab)["text"])
"""
from __future__ import annotations

import json
import os
import pathlib
import re
import urllib.request


def _discover_gateway() -> dict:
    """Read the canonical discovery file, with the same legacy fallback as MCP."""
    for directory in (".xplorer", ".xbrowser"):
        path = pathlib.Path.home() / directory / "gateway.json"
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
        except FileNotFoundError:
            continue
        except (OSError, ValueError) as exc:
            raise RuntimeError(
                f"Cannot read {path} ({type(exc).__name__}). "
                "Restart Xplorer to refresh gateway.json."
            ) from None
        if not isinstance(data, dict) or any(
            not isinstance(data.get(key), str) or not data[key].strip()
            for key in ("url", "token")
        ):
            raise RuntimeError(
                f"Invalid {path}: expected non-empty url and token strings. "
                "Restart Xplorer to refresh gateway.json."
            )
        # Match a complete loopback origin, not a hostname prefix. Gateway
        # endpoints are rooted at /; credentials, paths and queries don't belong
        # in discovery URLs. Permit trailing slashes and normalize them away.
        origin = re.fullmatch(
            r"http://(?:127\.0\.0\.1|localhost|\[::1\])(?::([0-9]{1,5}))?/*",
            data["url"], re.IGNORECASE | re.ASCII,
        )
        if origin is None or (
            origin.group(1) is not None and not 1 <= int(origin.group(1)) <= 65535
        ):
            raise RuntimeError(
                f"Invalid {path}: expected an HTTP loopback URL "
                "(127.0.0.1, localhost, or [::1], with an optional port). "
                "Restart Xplorer to refresh gateway.json."
            )
        data["url"] = data["url"].rstrip("/")
        return data
    return {}


class Browser:
    def __init__(self, port: int | None = None, token: str | None = None):
        """Discover the gateway at construction, unless fully specified.

        Explicit port/token values win; XPLORER_TOKEN overrides the discovered
        token. With a manual token and no discovery file, the port defaults to
        9334 for compatibility. Recreate Browser after a gateway restart.
        Discovered URLs must be HTTP loopback origins; trailing slashes are
        removed. Authorization is sent only on the initial request, not redirects.
        """
        if token is None:
            token = os.environ.get("XPLORER_TOKEN") or None
        gateway = _discover_gateway() if port is None or token is None else {}
        if token is None:
            token = gateway.get("token")
        if token is None:
            raise RuntimeError(
                "No ~/.xplorer/gateway.json (or ~/.xbrowser/gateway.json). "
                "Launch Xplorer first, or supply a token and port explicitly."
            )
        self.base = (f"http://127.0.0.1:{port}" if port is not None
                     else gateway.get("url", "http://127.0.0.1:9334"))
        self.token = token

    def _req(self, method: str, path: str, body: dict | None = None) -> dict:
        req = urllib.request.Request(
            self.base + path,
            method=method,
            data=json.dumps(body).encode() if body is not None else None,
            headers={
                "Content-Type": "application/json",
            },
        )
        # A gateway redirect must not forward its bearer token to another URL.
        req.add_unredirected_header("Authorization", f"Bearer {self.token}")
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
