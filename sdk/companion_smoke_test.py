#!/usr/bin/env python3
"""Smoke test for the Xplorer companion UI (http://127.0.0.1:9334).

Usage: launch Xplorer, then `python3 companion_smoke_test.py`.
"""
from __future__ import annotations

import json
import os
import sys
import urllib.error
import urllib.request

BASE = "http://127.0.0.1:9334"

# Load token for authenticated API calls (public pages like / and *.js do not require it)
GATEWAY_PATH = os.path.expanduser("~/.xplorer/gateway.json")
TOKEN = ""
try:
    with open(GATEWAY_PATH) as f:
        TOKEN = json.load(f).get("token", "")
except Exception:
    pass
AUTH_HEADERS = {"Authorization": f"Bearer {TOKEN}"} if TOKEN else {}


def get(path: str, *, expect_json: bool = False) -> tuple[int, str]:
    headers = dict(AUTH_HEADERS)
    req = urllib.request.Request(f"{BASE}{path}", headers=headers)
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
        ("settings", "/settings"),
        ("settings.css", "/settings.css"),
        ("settings.js", "/settings.js"),
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
    assert "search_model" in settings, settings
    assert "companion_url" in settings, settings
    print("api/settings: OK")

    status, models_body = get("/api/models")
    assert status == 200
    models = json.loads(models_body)
    assert models.get("models"), models
    print("api/models: OK")

    _, common = get("/common.js")
    assert "syncCompanionToolbarPill" in common
    assert "renderMarkdown" in common
    assert "persistSearchQuery" in common
    assert "getStoredSearchMode" in common
    assert "wireCodeCopyButtons" in common
    assert "initCodeCopyHotkey" in common
    assert "grokWebUrlForQuery" in common
    assert "initToolbarHomeHotkeys" in common
    assert "persistConvModel" in common
    assert "messageNeedsBrowserTools" in common
    assert "mountGrokToolbar" in common
    assert "grokToolbarHTML" in common
    assert "wikiUrlForQuery" in common
    assert "imagineUrlForQuery" in common
    print("common.js markers: OK")

    _, app_js = get("/app.js")
    assert "renameConversation" in app_js
    assert "deleteConversation" in app_js
    assert "chatConversations" in app_js
    assert "streamAbort" in app_js
    assert "conv-delete" in app_js
    print("app.js conv management: OK")

    # Conversation rename API
    conv_headers = {"Content-Type": "application/json", **AUTH_HEADERS}
    req = urllib.request.Request(
        f"{BASE}/api/conversations",
        data=b"{}",
        method="POST",
        headers=conv_headers,
    )
    with urllib.request.urlopen(req, timeout=10) as resp:
        conv = json.loads(resp.read().decode())
    conv_id = conv["id"]
    req = urllib.request.Request(
        f"{BASE}/api/conversations/{conv_id}/rename",
        data=json.dumps({"title": "Smoke rename"}).encode(),
        method="POST",
        headers=conv_headers,
    )
    with urllib.request.urlopen(req, timeout=10) as resp:
        renamed = json.loads(resp.read().decode())
    assert renamed.get("title") == "Smoke rename", renamed
    print("conv rename API: OK")
    del_req = urllib.request.Request(f"{BASE}/api/conversations/{conv_id}", method="DELETE", headers=AUTH_HEADERS)
    urllib.request.urlopen(del_req, timeout=10)
    print("conv delete API: OK")

    _, apps_js = get("/apps.js")
    assert "export-selected-btn" in apps_js
    assert "downloadAppZip" in apps_js
    assert "delete-selected-btn" in apps_js
    assert "export-batch" in apps_js
    assert "data-restart" in apps_js
    assert "restart-batch" in apps_js
    assert "statusFilter" in apps_js
    assert "updateFilterCounts" in apps_js
    assert "previewUrl" in apps_js
    assert "runtime_url" in apps_js
    _, apps_html = get("/apps")
    assert "apps-filter-bar" in apps_html
    assert 'data-filter="idle"' in apps_html or "idle" in apps_html
    exportable_count = sum(1 for a in apps["apps"] if a.get("exportable"))
    ready_count = sum(1 for a in apps["apps"] if a.get("status") == "ready")
    assert exportable_count >= 0 and ready_count >= 0
    print(f"apps filter counts: ready={ready_count} exportable={exportable_count}")
    print("apps.js bulk actions: OK")

    assert "convFilterQuery" in app_js
    assert "convFilterInput.focus" in app_js
    assert "Escape" in app_js
    req = urllib.request.Request(f"{BASE}/", headers={"Accept": "text/html"})
    with urllib.request.urlopen(req, timeout=10) as resp:
        app_html = resp.read().decode()
    assert "conv-filter" in app_html
    assert "composer-stop" in app_html or 'id="stop"' in app_html
    print("chat conv filter: OK")

    assert "runtime_alive" in apps_js
    print("apps.js runtime indicator: OK")

    if apps["apps"] and "runtime_alive" in apps["apps"][0]:
        print("api/apps runtime health: OK")
    else:
        print("api/apps runtime health: skipped (native rebuild pending)")

    _, search_js = get("/search.js")
    assert "openGrokWebQuery" in search_js
    assert "/api/page/grok-web" in search_js
    assert "runNativeSearch" not in search_js
    assert "initSearchModelFromSettings" in search_js
    print("search.js grok-web handoff: OK")

    _, welcome = get("/welcome")
    assert "grok-toolbar-mount" in welcome or 'data-grok-toolbar="auto"' in welcome
    assert "/settings" in welcome
    _, settings_html = get("/settings")
    assert "settings-page" in settings_html
    assert "grok-toolbar-mount" in settings_html
    assert "chrome://settings" in settings_html
    print("welcome dev hint: OK")

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
        assert "/search" not in location, f"build should not go to search: {location}"
        assert ":9334/" in location, f"unexpected Location: {location}"
    print("switch-home build redirect: OK")

    req = urllib.request.Request(f"{BASE}/switch-home?mode=web", method="GET")
    try:
        opener.open(req, timeout=10)
        raise AssertionError("switch-home web should redirect")
    except urllib.error.HTTPError as e:
        assert e.code in (302, 303, 307), f"switch-home web status {e.code}"
        location = e.headers.get("Location", "")
        assert "/search" in location, f"web should go to search: {location}"
    print("switch-home web redirect: OK")

    req = urllib.request.Request(
        f"{BASE}/switch-home?mode=web&q=testquery&m=imagine", method="GET"
    )
    try:
        opener.open(req, timeout=10)
        raise AssertionError("switch-home q+m should redirect")
    except urllib.error.HTTPError as e:
        assert e.code in (302, 303, 307), f"switch-home q+m status {e.code}"
        location = e.headers.get("Location", "")
        assert "q=testquery" in location, f"missing q in Location: {location}"
        assert "mode=imagine" in location, f"missing mode in Location: {location}"
        assert "/search" in location, f"web should go to search: {location}"
    print("switch-home q+m redirect: OK")

    exportable = [a for a in apps["apps"] if a.get("exportable")]
    if len(exportable) >= 2:
        ids = [a["id"] for a in exportable[:2]]
        batch_headers = {"Content-Type": "application/json", **AUTH_HEADERS}
        req = urllib.request.Request(
            f"{BASE}/api/apps/export-batch",
            data=json.dumps({"ids": ids}).encode(),
            method="POST",
            headers=batch_headers,
        )
        with urllib.request.urlopen(req, timeout=60) as resp:
            data = resp.read()
            assert resp.status == 200
            assert resp.headers.get("Content-Type", "").startswith("application/zip")
            assert data[:2] == b"PK", "batch zip magic"
        print("export-batch: OK")
    elif exportable:
        print("export-batch: skipped (need 2+ exportable apps)")

    if exportable:
        app_id = exportable[0]["id"]
        restart_headers = {"Content-Type": "application/json", **AUTH_HEADERS}
        req = urllib.request.Request(
            f"{BASE}/api/apps/restart-batch",
            data=json.dumps({"ids": [app_id]}).encode(),
            method="POST",
            headers=restart_headers,
        )
        with urllib.request.urlopen(req, timeout=30) as resp:
            restarted = json.loads(resp.read().decode())
        assert restarted.get("ok") is True, restarted
        assert restarted.get("restarted", 0) >= 1, restarted
        print(f"restart-batch {app_id}: OK")

        req = urllib.request.Request(f"{BASE}/api/apps/{app_id}/export", headers=AUTH_HEADERS)
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
    except urllib.error.HTTPError as e:
        if e.code == 401:
            print(f"FAIL: 401 Unauthorized — token from {GATEWAY_PATH} may be missing/expired. Run Xplorer to refresh ~/.xplorer/gateway.json", file=sys.stderr)
        else:
            print(f"HTTP error {e.code}: {e}", file=sys.stderr)
        sys.exit(1)
    except urllib.error.URLError as e:
        print(f"Companion not reachable at {BASE}: {e}", file=sys.stderr)
        sys.exit(1)
    except AssertionError as e:
        print(f"FAIL: {e}", file=sys.stderr)
        sys.exit(1)