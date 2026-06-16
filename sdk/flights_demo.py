#!/usr/bin/env python3
"""Book-search a flight BKK -> LAX on Google Flights using ONLY the generic
perceive->act layer (agent_context.Page). No site-specific selectors: every
step is "find an element by role/accessible-name, then act" — the same code
would drive any site. The only Google-specific things are the human-level
target names ('Where from?', 'Los Angeles', a date), i.e. the task itself.
"""
import sys
import time

from xplorer_sdk import Browser
from agent_context import Page


def fill_place(p: Page, field_name: str, query: str):
    """Generic autocomplete pattern: focus field, type, then commit the top
    suggestion with the keyboard (ArrowDown, Enter). No option-scraping."""
    box = p.wait_for(name=field_name, role=None, timeout=10)
    assert box, f"no field {field_name!r}"
    p.type(box["ref"], query)
    # Wait until a suggestion actually mentions our query (any element).
    p.wait_text(query.split()[0], timeout=8)
    time.sleep(0.4)
    p.press("ArrowDown")
    p.press("Enter")
    print(f"  {field_name} -> {query} (committed via keyboard)")
    time.sleep(0.7)


def pick_date(p: Page, which: str):
    """Open a date field and click the first selectable day cell."""
    fld = p.wait_for(name=which, timeout=8)
    assert fld, f"no {which} field"
    p.click(fld["ref"])
    time.sleep(1.0)
    cells = [e for e in p.observe()
             if e["role"] == "gridcell" and e["name"]]
    if not cells:
        cells = [e for e in p.observe()
                 if any(m in e["name"] for m in ("2026", "2027"))
                 and "Find flights" not in e["name"]]
    assert cells, f"no day cells for {which}"
    target = cells[min(14, len(cells) - 1)]
    print(f"  {which} -> {target['name'][:50]}")
    p.click(target["ref"])
    time.sleep(0.6)


def main() -> int:
    b = Browser()
    tab = b.tabs()[0]["id"]
    p = Page(b, tab)
    t0 = time.time()

    p.goto("https://www.google.com/travel/flights?hl=en&curr=USD")
    # consent, if any
    c = p.find(name="Accept all|Reject all|I agree")
    if c:
        p.click(c["ref"]); time.sleep(0.5)

    fill_place(p, "Where from\\?", "Bangkok")
    fill_place(p, "Where to\\?", "Los Angeles")

    pick_date(p, "Departure")
    pick_date(p, "Return")

    # Confirm the calendar (a "Done" button) if present, then Search.
    done = p.find(name="^Done$", role="button")
    if done:
        p.click(done["ref"]); time.sleep(0.4)

    search = p.find(name="^Search$", role="button") or \
        p.find(name="^Search$")
    assert search, "no Search button"
    p.click(search["ref"])
    print("  clicked Search")

    # Wait for results to render.
    ok = False
    for _ in range(40):
        txt = b.eval(tab, "document.body.innerText")
        if txt and ("Best departing" in txt or "Top departing" in txt or
                    "sorted by" in txt.lower()):
            ok = True
            break
        time.sleep(0.5)

    url = b.eval(tab, "location.href")
    print(f"\nresults loaded: {ok}")
    print(f"url: {url[:80]}")
    flights = b.eval(tab, r"""
      (() => {
        const rows = [...document.querySelectorAll('li')]
          .map(li => li.innerText.replace(/\s+/g,' ').trim())
          .filter(t => /\$\d/.test(t) && /(hr|min)/.test(t));
        return JSON.stringify(rows.slice(0, 4));
      })()
    """)
    print("top flights:", flights)
    print(f"elapsed: {time.time()-t0:.1f}s")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
