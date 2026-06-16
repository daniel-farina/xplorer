#!/usr/bin/env python3
"""Prove Xplorer lets an agent drive many tabs in parallel while the browser
is NOT the active app. Pure gateway calls; no window focus required."""
import concurrent.futures as cf
import time

from xplorer_sdk import Browser

SITES = [
    "https://example.com",
    "https://www.wikipedia.org",
    "https://news.ycombinator.com",
    "https://www.bbc.com",
]


def drive(args):
    """Each worker navigates its own tab and extracts data — concurrently."""
    tab, site = args
    b = Browser()
    b.navigate(tab, site)
    title = b.eval(tab, "document.title")
    words = b.eval(tab, "document.body.innerText.trim().split(/\\s+/).length")
    return site, tab, title, words


def main():
    b = Browser()
    base = len(b.tabs())
    # Open one tab per site (sequential, to get stable ids).
    for site in SITES:
        b.open(site)
        time.sleep(0.3)
    tabs = [t["id"] for t in b.tabs()][base:base + len(SITES)]

    # Now drive all tabs CONCURRENTLY while Xplorer is in the background.
    t0 = time.time()
    with cf.ThreadPoolExecutor(max_workers=len(SITES)) as ex:
        results = list(ex.map(drive, zip(tabs, SITES)))
    dt = time.time() - t0
    for site, tab, title, words in results:
        print(f"[tab {tab}] {title[:46]:46}  {words:>6} words  <- {site}")
    print(f"\n{len(SITES)} tabs navigated + scraped concurrently in {dt:.1f}s "
          f"while Xplorer was NOT the active app")


if __name__ == "__main__":
    main()
