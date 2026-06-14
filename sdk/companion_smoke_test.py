#!/usr/bin/env python3
"""Smoke test for the Xplorer companion UI (http://127.0.0.1:9334).

Usage: launch Xplorer, then `python3 companion_smoke_test.py`.
"""
from __future__ import annotations

import json
import sys
import urllib.error
import urllib.request

BASE = "http://127.0.0.1:9334"


def get(path: str, *, expect_json: bool = False) -> tuple[int, str]:
    req = urllib.request.Request(f"{BASE}{path}")
    with urllib.request.urlopen(req, timeout=10) as resp:
        body = resp.read().decode("utf-8", errors="replace")
        return resp.status, body if not expect_json else body


def check_status(name: str, path: str, expected: int = 200) -> None:
    status, _ = get(path)
    assert status == expected, f"{name}: expected {expected}, got {status}"
    print(f"{name}: OK ({status})")


def main() -> int:
    checks = [
        ("home", "/"),
        ("search page", "/search"),
        ("apps page", "/apps"),
        ("welcome", "/welcome"),
        ("common.js", "/common.js"),
        ("search.js", "/search.js"),
    ]
    for name, path in checks:
        check_status(name, path)

    status, apps_body = get("/api/apps")
    assert status == 200, f"api/apps status {status}"
    apps = json.loads(apps_body)
    assert apps.get("ok") is True, apps
    assert "apps" in apps, apps
    print("api/apps: OK")

    status, settings_body = get("/api/settings")
    assert status == 200
    settings = json.loads(settings_body)
    assert "search_home" in settings, settings
    print("api/settings: OK")

    status, models_body = get("/api/models")
    assert status == 200
    models = json.loads(models_body)
    assert models.get("models"), models
    print("api/models: OK")

    _, common = get("/common.js")
    assert "syncCompanionToolbarPill" in common
    assert "renderMarkdown" in common
    print("common.js markers: OK")

    class NoRedirect(urllib.request.HTTPRedirectHandler):
        def redirect_request(self, req, fp, code, msg, headers, newurl):
            return None

    opener = urllib.request.build_opener(NoRedirect)
    req = urllib.request.Request(f"{BASE}/switch-home?mode=build", method="GET")
    try:
        opener.open(req, timeout=10)
        raise AssertionError("switch-home should redirect, not 200")
    except urllib.error.HTTPError as e:
        assert e.code in (302, 303, 307), f"switch-home status {e.code}"
        location = e.headers.get("Location", "")
        assert "search" in location, f"unexpected Location: {location}"
    print("switch-home redirect: OK")

    exportable = [a for a in apps["apps"] if a.get("exportable")]
    if exportable:
        app_id = exportable[0]["id"]
        req = urllib.request.Request(f"{BASE}/api/apps/{app_id}/export")
        with urllib.request.urlopen(req, timeout=30) as resp:
            data = resp.read()
            assert resp.status == 200
            assert resp.headers.get("Content-Type", "").startswith("application/zip")
            assert data[:2] == b"PK", "zip magic"
        print(f"export {app_id}: OK")
    else:
        print("export: skipped (no exportable apps)")

    print("ALL OK")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except urllib.error.URLError as e:
        print(f"Companion not reachable at {BASE}: {e}", file=sys.stderr)
        sys.exit(1)
    except AssertionError as e:
        print(f"FAIL: {e}", file=sys.stderr)
        sys.exit(1)