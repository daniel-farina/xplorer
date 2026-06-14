#!/usr/bin/env python3
"""Verify DELETE /api/apps/{id} does not crash Xplorer."""
from __future__ import annotations

import json
import subprocess
import sys
import urllib.error
import urllib.request

BASE = "http://127.0.0.1:9334"


def xplorer_running() -> bool:
    r = subprocess.run(
        ["pgrep", "-f", "/Applications/Xplorer.app/Contents/MacOS/Xplorer"],
        capture_output=True,
        text=True,
    )
    return r.returncode == 0


def main() -> int:
    assert xplorer_running(), "Xplorer not running"
    with urllib.request.urlopen(f"{BASE}/api/apps", timeout=10) as resp:
        apps = json.loads(resp.read().decode())["apps"]
    # Prefer a duplicate tetris app if present.
    victim = next(
        (a for a in apps if a.get("name", "").lower().startswith("tetris")),
        apps[-1] if apps else None,
    )
    if not victim:
        print("SKIP: no apps to delete")
        return 0
    app_id = victim["id"]
    req = urllib.request.Request(
        f"{BASE}/api/apps/{app_id}",
        method="DELETE",
    )
    with urllib.request.urlopen(req, timeout=30) as resp:
        result = json.loads(resp.read().decode())
    assert result.get("ok") is True, result
    assert xplorer_running(), "Xplorer crashed during app delete"
    with urllib.request.urlopen(f"{BASE}/api/apps", timeout=10) as resp:
        remaining = json.loads(resp.read().decode())["apps"]
    assert not any(a["id"] == app_id for a in remaining)
    print(f"deleted {app_id} OK, browser still running")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (urllib.error.URLError, AssertionError) as e:
        print(f"FAIL: {e}", file=sys.stderr)
        sys.exit(1)