#!/usr/bin/env python3
"""End-to-end smoke test against a running Xplorer build.

Usage: launch out/xplorer/Chromium.app, then `python3 smoke_test.py`.
"""
import sys

from xplorer_sdk import Browser


def main() -> int:
    b = Browser()
    b.open("https://example.com")
    tab = b.tabs()[-1]["id"]
    b.navigate(tab, "https://example.com")

    page = b.text(tab)
    assert "Example Domain" in page["text"], page
    print("text extraction: OK")

    assert b.eval(tab, "1 + 1") == 2
    print("eval: OK")

    png = b.screenshot(tab)
    assert png[:8] == b"\x89PNG\r\n\x1a\n"
    print("screenshot: OK")

    b.click(tab, "a")  # the IANA 'More information' link
    print("click: OK")

    tree = b.axtree(tab)
    assert "nodes" in tree
    print("axtree: OK")

    # AI-native check: agents must not be flagged as automation.
    assert b.eval(tab, "navigator.webdriver") in (False, None)
    print("webdriver flag absent: OK")
    print("ALL OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
