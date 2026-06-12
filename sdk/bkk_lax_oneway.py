#!/usr/bin/env python3
"""BKK -> LAX one-way on a specific date via Google Flights (generic Page layer)."""
import json
import sys
import time
from datetime import date, timedelta

from aether_sdk import Browser
from agent_context import Page


def fill_place(p: Page, field_name: str, query: str):
    box = p.wait_for(name=field_name, role=None, timeout=10)
    assert box, f"no field {field_name!r}"
    p.type(box["ref"], query)
    p.wait_text(query.split()[0], timeout=8)
    time.sleep(0.4)
    p.press("ArrowDown")
    p.press("Enter")
    print(f"  {field_name} -> {query}")
    time.sleep(0.7)


def pick_date_named(p: Page, day_label: str):
    """Click a calendar cell whose accessible name contains the day label."""
    fld = p.wait_for(name="Departure", timeout=8)
    assert fld, "no Departure field"
    p.click(fld["ref"])
    time.sleep(1.0)
    cells = [e for e in p.observe() if e["role"] == "gridcell" and e["name"]]
    target = next((c for c in cells if day_label in c["name"]), None)
    if not target:
        # fallback: first cell mentioning the year/month
        target = next((c for c in cells if "2026" in c["name"]), cells[min(14, len(cells) - 1)])
    print(f"  Departure -> {target['name'][:60]}")
    p.click(target["ref"])
    time.sleep(0.6)


def extract_flights(b: Browser, tab: str):
    return b.eval(tab, r"""
      (() => {
        const rows = [...document.querySelectorAll('li')]
          .map(li => li.innerText.replace(/\s+/g,' ').trim())
          .filter(t => /\$\d/.test(t) && /(hr|min)/.test(t));
        const prices = [...document.body.innerText.matchAll(/\$[\d,]+/g)].map(m => m[0]);
        const cheapest = document.body.innerText.match(/from\s+\$[\d,]+/i);
        return JSON.stringify({rows: rows.slice(0, 8), prices: [...new Set(prices)].slice(0, 12), cheapest});
      })()
    """)


def main() -> int:
    tomorrow = date.today() + timedelta(days=1)
    day_label = tomorrow.strftime("%-d")  # e.g. "13"
    month_year = tomorrow.strftime("%B %Y")  # e.g. "June 2026"

    b = Browser(token=open(
        __import__("pathlib").Path.home() / "Library/Application Support/Chromium/Default/agent_token"
    ).read().strip())
    b.open("https://www.google.com/travel/flights?hl=en&curr=USD")
    tab = b.tabs()[-1]["id"]
    p = Page(b, tab)
    t0 = time.time()
    c = p.find(name="Accept all|Reject all|I agree")
    if c:
        p.click(c["ref"])
        time.sleep(0.5)

    # One-way
    trip = p.find(name="Round trip", role="button") or p.find(name="Round trip")
    if trip:
        p.click(trip["ref"])
        time.sleep(0.5)
        one = p.find(name="One way", role="option") or p.find(name="One way")
        assert one, "no One way option"
        p.click(one["ref"])
        print("  trip type -> One way")
        time.sleep(0.5)

    fill_place(p, "Where from\\?", "Bangkok")
    fill_place(p, "Where to\\?", "Los Angeles")

    # Open calendar and pick tomorrow
    fld = p.wait_for(name="Departure", timeout=8)
    p.click(fld["ref"])
    time.sleep(1.0)
    # advance month if needed
    for _ in range(6):
        cells = [e for e in p.observe() if e["role"] == "gridcell" and e["name"]]
        hit = next((c for c in cells if re_match_day(c["name"], tomorrow)), None)
        if hit:
            print(f"  Departure -> {hit['name'][:60]}")
            p.click(hit["ref"])
            break
        nxt = p.find(name="Next", role="button") or p.find(name="Next")
        if nxt:
            p.click(nxt["ref"])
            time.sleep(0.4)
    else:
        raise RuntimeError(f"could not find day {tomorrow}")

    done = p.find(name="^Done$", role="button")
    if done:
        p.click(done["ref"])
        time.sleep(0.4)

    search = p.find(name="^Search$", role="button") or p.find(name="^Search$")
    assert search, "no Search button"
    p.click(search["ref"])
    print("  clicked Search")

    ok = False
    for _ in range(50):
        txt = b.eval(tab, "document.body.innerText") or ""
        if "Best departing" in txt or "Top departing" in txt or "sorted by" in txt.lower():
            ok = True
            break
        time.sleep(0.5)

    data = json.loads(extract_flights(b, tab) or "{}")
    url = b.eval(tab, "location.href")
    print(f"\nresults loaded: {ok}")
    print(f"date: {tomorrow.isoformat()} (one-way BKK -> LAX)")
    print(f"url: {url[:120]}")
    print("cheapest:", data.get("cheapest"))
    print("top flights:")
    for row in data.get("rows", []):
        print(" ", row[:200])
    print(f"elapsed: {time.time()-t0:.1f}s")
    return 0 if ok else 1


def re_match_day(name: str, d: date) -> bool:
    import re
    # match "Fri, Jun 13" or "13" in June 2026 context
    patterns = [
        d.strftime("%b %-d"),      # Jun 13
        d.strftime("%B %-d"),      # June 13
        f", {d.strftime('%b')} {d.day}",
        f"{d.day}, {d.strftime('%A')}",
    ]
    return any(p.lower() in name.lower() for p in patterns)


if __name__ == "__main__":
    sys.exit(main())