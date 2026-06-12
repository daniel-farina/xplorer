"""Generic perceive->act layer for Aether — works on ANY website.

The agent does not need hardcoded CSS selectors per site. It:
  1. observe(tab)  -> a compact list of interactive elements with their
                      accessible role + name + value (read from the live DOM,
                      the same signals a screen reader / a11y tree exposes).
  2. find(...)     -> pick an element by role and/or name regex.
  3. click/type    -> act on it via real browser input events.

This is the model an LLM agent uses: look at the page's semantics, decide,
act. Nothing here knows anything about any specific site.
"""
from __future__ import annotations

import re
import time

from aether_sdk import Browser

# JS that scans the DOM for actionable elements and computes an accessible
# name for each (aria-label > associated <label> > placeholder > text). Every
# match is tagged data-aref=<n> so we can act on it with a stable selector.
_OBSERVE_JS = r"""
(() => {
  const SEL = 'a,button,input,textarea,select,[role=button],[role=link],' +
              '[role=option],[role=tab],[role=checkbox],[role=menuitem],' +
              '[role=gridcell],[role=combobox],[contenteditable=true]';
  const vis = el => {
    const r = el.getBoundingClientRect();
    const s = getComputedStyle(el);
    return r.width > 0 && r.height > 0 && s.visibility !== 'hidden' &&
           s.display !== 'none';
  };
  const name = el => {
    const al = el.getAttribute('aria-label');
    if (al) return al.trim();
    const lb = el.getAttribute('aria-labelledby');
    if (lb) { const t = lb.split(' ').map(i =>
      (document.getElementById(i)||{}).innerText||'').join(' ').trim();
      if (t) return t; }
    if (el.id) { const l = document.querySelector('label[for="'+el.id+'"]');
      if (l && l.innerText.trim()) return l.innerText.trim(); }
    return (el.getAttribute('placeholder') || el.innerText ||
            el.value || el.title || '').trim().replace(/\s+/g, ' ');
  };
  const out = [];
  let n = 0;
  for (const el of document.querySelectorAll(SEL)) {
    if (!vis(el)) continue;
    el.setAttribute('data-aref', n);
    const r = el.getBoundingClientRect();
    out.push({
      ref: n++,
      role: el.getAttribute('role') || el.tagName.toLowerCase(),
      name: name(el).slice(0, 80),
      value: (el.value || '').slice(0, 40),
      x: Math.round(r.x + r.width / 2),
      y: Math.round(r.y + r.height / 2),
    });
  }
  return JSON.stringify(out);
})()
"""


class Page:
    """A perceive->act handle over one tab — site-agnostic."""

    def __init__(self, browser: Browser, tab: str):
        self.b = browser
        self.tab = tab

    def goto(self, url: str):
        self.b.navigate(self.tab, url)
        self.wait_idle()

    def wait_idle(self, timeout=15):
        end = time.time() + timeout
        while time.time() < end:
            if self.b.eval(self.tab, "document.readyState==='complete'") is True:
                break
            time.sleep(0.2)
        time.sleep(0.5)

    def observe(self) -> list[dict]:
        import json
        return json.loads(self.b.eval(self.tab, _OBSERVE_JS))

    def find(self, name=None, role=None, value=None, nth=0) -> dict | None:
        """Pick an element by role and/or accessible-name regex."""
        pat = re.compile(name, re.I) if name else None
        vpat = re.compile(value, re.I) if value else None
        hits = [e for e in self.observe()
                if (not role or e["role"] == role)
                and (not pat or pat.search(e["name"]))
                and (not vpat or vpat.search(e["value"]))]
        return hits[nth] if len(hits) > nth else None

    def click(self, ref: int):
        self.b.click(self.tab, f'[data-aref="{ref}"]')

    def type(self, ref: int, text: str):
        # Resolve the *real* text input: the tagged element may be a
        # role=combobox wrapper, so fall back to a descendant/sibling input.
        # Tag it data-atype, clear it, then type with real keystrokes.
        found = self.b.eval(self.tab, f"""(()=>{{
          const host=document.querySelector('[data-aref="{ref}"]');
          if(!host) return 'no-host';
          let e = (host.matches('input,textarea,[contenteditable=true]'))
            ? host
            : host.querySelector('input,textarea,[contenteditable=true]')
              || host.closest('*').querySelector('input,textarea');
          if(!e) return 'no-input';
          document.querySelectorAll('[data-atype]').forEach(
            x=>x.removeAttribute('data-atype'));
          e.setAttribute('data-atype','1'); e.focus();
          const s=Object.getOwnPropertyDescriptor(
            window.HTMLInputElement.prototype,'value');
          if(s&&s.set){{s.set.call(e,'');
            e.dispatchEvent(new Event('input',{{bubbles:true}}));}}
          return 'ok';}})()""")
        if found != "ok":
            raise RuntimeError(f"type: could not focus input ({found})")
        self.b.type(self.tab, '[data-atype="1"]', text)

    def press(self, key: str):
        self.b.press(self.tab, key)

    def wait_text(self, needle: str, timeout=10) -> bool:
        """Wait until the page's visible text contains needle (any element)."""
        import json
        end = time.time() + timeout
        expr = ("document.body.innerText.toLowerCase()"
                f".includes({json.dumps(needle.lower())})")
        while time.time() < end:
            if self.b.eval(self.tab, expr) is True:
                return True
            time.sleep(0.3)
        return False

    def wait_for(self, name=None, role=None, timeout=10) -> dict | None:
        end = time.time() + timeout
        while time.time() < end:
            hit = self.find(name=name, role=role)
            if hit:
                return hit
            time.sleep(0.3)
        return None
