// Copyright 2026 The Xplorer Authors.
// Use of this source code is governed by a BSD-style license.
//
// SINGLE SOURCE OF TOOLBAR BEHAVIOR. Consumed by BOTH surfaces:
//   - Companion pages (Grok Build/Web/Wiki): loaded via <script src="/toolbar.js">
//     before common.js; common.js builds the markup and calls
//     XplorerToolbar.wireHideToggle(bar) for the shared hide/show + drag handle.
//   - Native overlay on third-party sites (x.com, grok.com, grokipedia, xchat):
//     grok_web_bar.cc reads this file live from disk and bakes it into the
//     isolated-world injection, then calls XplorerToolbar.mountNative({...}).
//
// The MARKUP lives in toolbar.html and the STYLES in toolbar.css — this file is
// behavior only. It performs NO network fetches: the caller passes markup/css
// in (host.baseHtml / host.baseCss), so the native path is CSP-safe in the
// isolated world (third-party connect-src blocks any loopback fetch).
(function () {
  'use strict';

  var HIDE_KEY = 'xplorer_toolbar_hidden';
  var REVEAL_POS_KEY = 'xplorer_toolbar_reveal_pos';

  // Grip + X mark + chevron, shown on the floating reveal pill when hidden.
  var REVEAL_SVG =
    '<svg class="gi grok-reveal-grip" viewBox="0 0 24 24" fill="currentColor" stroke="none" aria-hidden="true"><circle cx="9" cy="6" r="1.6"></circle><circle cx="15" cy="6" r="1.6"></circle><circle cx="9" cy="12" r="1.6"></circle><circle cx="15" cy="12" r="1.6"></circle><circle cx="9" cy="18" r="1.6"></circle><circle cx="15" cy="18" r="1.6"></circle></svg>' +
    '<svg class="gi" viewBox="0 0 32 32" fill="none" stroke="currentColor" stroke-width="3.4" stroke-linecap="round" stroke-linejoin="round"><path d="M9.5 9.5 L22.5 22.5 M22.5 9.5 L9.5 22.5"></path></svg>' +
    '<svg class="gi" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="6 9 12 15 18 9"></polyline></svg>';

  // Let the floating reveal pill be dragged out of the way; persists position.
  // A real drag (>4px) repositions and suppresses the reveal click.
  function makeRevealDraggable(reveal) {
    if (reveal.dataset.dragWired) return;
    reveal.dataset.dragWired = '1';
    reveal.style.touchAction = 'none';
    var clamp = function (v, max) { return Math.max(0, Math.min(v, max)); };
    var positionAt = function (x, y) {
      var r = reveal.getBoundingClientRect();
      reveal.style.left = clamp(x, window.innerWidth - r.width) + 'px';
      reveal.style.top = clamp(y, window.innerHeight - r.height) + 'px';
      reveal.style.right = 'auto';
    };
    try {
      var p = JSON.parse(localStorage.getItem(REVEAL_POS_KEY) || 'null');
      if (p && typeof p.x === 'number') {
        requestAnimationFrame(function () { positionAt(p.x, p.y); });
      }
    } catch (e) {}
    var start = null, moved = false;
    reveal.addEventListener('pointerdown', function (e) {
      var r = reveal.getBoundingClientRect();
      start = { px: e.clientX, py: e.clientY, ox: r.left, oy: r.top };
      moved = false;
      try { reveal.setPointerCapture(e.pointerId); } catch (x) {}
    });
    reveal.addEventListener('pointermove', function (e) {
      if (!start) return;
      var dx = e.clientX - start.px, dy = e.clientY - start.py;
      if (!moved && Math.hypot(dx, dy) < 4) return;
      moved = true;
      reveal.classList.add('dragging');
      positionAt(start.ox + dx, start.oy + dy);
    });
    var end = function (e) {
      if (!start) return;
      try { reveal.releasePointerCapture(e.pointerId); } catch (x) {}
      reveal.classList.remove('dragging');
      if (moved) {
        var r = reveal.getBoundingClientRect();
        try {
          localStorage.setItem(REVEAL_POS_KEY,
            JSON.stringify({ x: Math.round(r.left), y: Math.round(r.top) }));
        } catch (x) {}
      }
      start = null;
    };
    reveal.addEventListener('pointerup', end);
    reveal.addEventListener('pointercancel', end);
    // Suppress the reveal click when the pointerup ended a drag.
    reveal.addEventListener('click', function (e) {
      if (moved) { e.stopImmediatePropagation(); e.preventDefault(); }
    }, true);
  }

  // Hide/show the toolbar; persists state and drops a floating reveal handle.
  // |onApply(hidden)| is an optional hook the native surface uses to re-assert
  // its page padding when the bar is hidden/shown (companion needs nothing).
  function wireHideToggle(bar, onApply) {
    if (!bar) return;
    var reveal = document.getElementById('grok-toolbar-reveal');
    if (!reveal) {
      reveal = document.createElement('button');
      reveal.id = 'grok-toolbar-reveal';
      reveal.type = 'button';
      reveal.title = 'Show toolbar (drag the grip to move)';
      reveal.setAttribute('aria-label', 'Show toolbar');
      reveal.innerHTML = REVEAL_SVG;
      (document.body || document.documentElement).appendChild(reveal);
    }
    makeRevealDraggable(reveal);
    var apply = function (hidden) {
      bar.classList.toggle('grok-toolbar-hidden', hidden);
      reveal.classList.toggle('show', hidden);
      try { localStorage.setItem(HIDE_KEY, hidden ? '1' : '0'); } catch (e) {}
      if (onApply) onApply(hidden);
    };
    var stored = '0';
    try { stored = localStorage.getItem(HIDE_KEY) || '0'; } catch (e) {}
    apply(stored === '1');
    var hideBtn = bar.querySelector('.grok-toolbar-hide');
    if (hideBtn && !hideBtn.dataset.wired) {
      hideBtn.dataset.wired = '1';
      hideBtn.addEventListener('click', function () { apply(true); });
    }
    if (!reveal.dataset.wired) {
      reveal.dataset.wired = '1';
      reveal.addEventListener('click', function () { apply(false); });
    }
  }

  // --------------------------------------------------------------------------
  // NATIVE overlay runtime — injected onto third-party sites in an isolated
  // world. This is the former grok_web_bar.cc IIFE, now a live-editable file.
  // --------------------------------------------------------------------------
  function mountNative(host) {
    if (!document.documentElement) return;
    var BAR_ID = 'xplorer-grok-bar', STYLE_ID = 'xplorer-grok-toolbar-style';
    var CSS = host.baseCss || '';
    var BAR_HTML = host.baseHtml || '';
    var FALLBACK_PILL = host.fallbackPill || '';
    var GW = host.gatewayOrigin || '';
    var THEME = host.theme || '';

    function applyTheme() {
      if (THEME) document.documentElement.setAttribute('data-theme', THEME);
    }
    function isXHost(h) {
      return h === 'x.com' || h.endsWith('.x.com') ||
        h === 'twitter.com' || h.endsWith('.twitter.com');
    }
    function activePillId() {
      var h = (location.hostname || '').toLowerCase();
      var path = (location.pathname || '').toLowerCase();
      if (h.indexOf('grok.com') >= 0) return 'web';
      if (h.indexOf('grokipedia.com') >= 0) return 'wiki';
      if (isXHost(h)) {
        if (path === '/i/chat' || path.indexOf('/i/chat/') === 0 ||
            path === '/messages' || path.indexOf('/messages/') === 0) return 'xchat';
        return 'xcom';
      }
      if (h === '127.0.0.1' || h === 'localhost') {
        if (path.indexOf('/search') === 0) return 'web';
        if (path.indexOf('/apps') === 0 || path === '/' || path.indexOf('/app') === 0) return 'build';
      }
      return FALLBACK_PILL || '';
    }
    function applyActivePill() {
      var bar = document.getElementById(BAR_ID);
      if (!bar) return;
      var id = activePillId();
      bar.querySelectorAll('.grok-pill[data-pill]').forEach(function (p) {
        p.classList.toggle('active', !!id && p.getAttribute('data-pill') === id);
      });
    }
    function hookHistory() {
      if (window.__xplorerGrokHistoryHooked) return;
      window.__xplorerGrokHistoryHooked = true;
      var push = history.pushState, replacement = history.replaceState;
      if (push) history.pushState = function () {
        var r = push.apply(this, arguments); applyActivePill(); return r;
      };
      if (replacement) history.replaceState = function () {
        var r = replacement.apply(this, arguments); applyActivePill(); return r;
      };
      window.addEventListener('popstate', applyActivePill);
    }
    function ensureStyle() {
      if (document.getElementById(STYLE_ID)) return;
      var style = document.createElement('style');
      style.id = STYLE_ID;
      style.textContent = CSS;
      document.documentElement.appendChild(style);
    }
    function clearOffset() {
      var s = document.documentElement.style;
      s.removeProperty('padding-top');
      s.removeProperty('scroll-padding-top');
      s.removeProperty('transform');
      s.removeProperty('transform-origin');
      if (document.body) document.body.style.removeProperty('padding-top');
    }
    function isHidden() {
      try { return localStorage.getItem(HIDE_KEY) === '1'; } catch (e) { return false; }
    }
    // Offset the page so the fixed bar never covers content: pad the root once.
    // Do NOT transform the root — the bar is a child of <html>, so a transform
    // moves the bar itself and breaks its fixed positioning, leaving a gap at
    // the top on scroll.
    function applyPadding(bar) {
      var s = document.documentElement.style;
      if (isHidden()) { clearOffset(); return; }
      var px = bar.getBoundingClientRect().height || 44;
      var pad = px + 'px';
      if (s.transform && s.transform !== 'none') {
        s.removeProperty('transform');
        s.removeProperty('transform-origin');
      }
      s.setProperty('padding-top', pad, 'important');
      s.setProperty('box-sizing', 'border-box', 'important');
      s.setProperty('scroll-padding-top', pad, 'important');
      if (document.body) document.body.style.removeProperty('padding-top');
    }
    function mountBar(bar) {
      var html = document.documentElement;
      if (bar.parentNode !== html) html.insertBefore(bar, html.firstChild);
      else if (html.firstChild !== bar) html.insertBefore(bar, html.firstChild);
    }
    // Canonical markup uses root-relative hrefs ("/search", "/switch-home?…").
    // On a third-party origin those must point at the loopback gateway. Rewrite
    // them in JS (replaces the old C++ base::ReplaceSubstringsAfterOffset) so it
    // is robust to quote/whitespace/attribute order, and re-run on every mount.
    function absolutizeHrefs(bar) {
      if (!GW) return;
      bar.querySelectorAll('a[href^="/"]').forEach(function (a) {
        a.setAttribute('href', GW + a.getAttribute('href'));
      });
    }
    function ensureBar() {
      ensureStyle();
      applyTheme();
      var bar = document.getElementById(BAR_ID);
      if (!bar) {
        bar = document.createElement('header');
        bar.id = BAR_ID;
        bar.className = 'grok-toolbar';
      }
      bar.innerHTML = BAR_HTML;
      absolutizeHrefs(bar);
      mountBar(bar);
      applyPadding(bar);
      applyActivePill();
      wirePillHandoffs(bar);
      wireHideToggle(bar, function () { applyPadding(bar); });
    }
    function barNeedsMount() {
      var bar = document.getElementById(BAR_ID);
      return !bar || bar.parentNode !== document.documentElement ||
        document.documentElement.firstChild !== bar;
    }
    function onRouteChange() {
      applyActivePill();
      var bar = document.getElementById(BAR_ID);
      if (bar) applyPadding(bar);  // SPA route changes can reset our offset
    }
    function pageQuery() {
      try {
        var q = new URLSearchParams(location.search).get('q') || '';
        if (q) return q;
        try { return localStorage.getItem('xplorer_search_query') || ''; } catch (e) { return ''; }
      } catch (e) { return ''; }
    }
    function pageSearchMode() {
      try { return localStorage.getItem('xplorer_search_mode') || ''; } catch (e) { return ''; }
    }
    function handoffQuery(q, mode, fallback) {
      var prompt = q;
      if (mode === 'imagine') prompt = 'Generate an image: ' + q;
      else if (mode === 'videos') prompt = 'Search for videos: ' + q;
      else if (mode === 'images') prompt = 'Search for images: ' + q;
      fetch(GW + '/api/page/grok-web', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ query: prompt })
      }).then(function (r) { return r.json().then(function (d) {
        if (!r.ok) throw new Error(d.error || 'handoff failed');
        var url = d.grok_url || fallback;
        if (mode === 'imagine' && url.indexOf('xplorer_grok=') >= 0) {
          url = url.replace(/^https:\/\/grok\.com\/?/, 'https://grok.com/imagine');
        }
        location.href = url;
      }); }).catch(function () { location.href = fallback; });
    }
    function wirePillHandoffs(bar) {
      if (!bar || bar.dataset.pillHandoff === '1') return;
      bar.dataset.pillHandoff = '1';
      bar.addEventListener('click', function (ev) {
        var imagineLink = ev.target && ev.target.closest ?
          ev.target.closest('a[href*="grok.com/imagine"]') : null;
        if (imagineLink) {
          var iq = pageQuery();
          if (!iq) return;
          ev.preventDefault();
          ev.stopPropagation();
          handoffQuery(iq, 'imagine', 'https://grok.com/imagine');
          return;
        }
        var pill = ev.target && ev.target.closest ? ev.target.closest('.grok-pill[data-pill]') : null;
        if (!pill || pill.getAttribute('data-pill') !== 'web') return;
        var host2 = (location.hostname || '').toLowerCase();
        if (host2.indexOf('grokipedia.com') < 0) return;
        var q = pageQuery();
        if (!q) return;
        ev.preventDefault();
        ev.stopPropagation();
        handoffQuery(q, pageSearchMode(), 'https://grok.com/');
      }, true);
    }

    ensureBar();
    hookHistory();
    if (!window.__xplorerGrokBarWatch) {
      window.__xplorerGrokBarWatch = true;
      window.addEventListener('popstate', onRouteChange);
      window.addEventListener('pageshow', onRouteChange);
      document.addEventListener('visibilitychange', function () {
        if (!document.hidden) onRouteChange();
      });
      var lastPath = location.pathname + location.search + location.hash;
      new MutationObserver(function () {
        if (barNeedsMount()) ensureBar();
        else {
          var p = location.pathname + location.search + location.hash;
          if (p !== lastPath) { lastPath = p; onRouteChange(); }
        }
      }).observe(document.documentElement, { childList: true, subtree: true });
      setInterval(function () {
        if (barNeedsMount()) ensureBar();
        else {
          var p = location.pathname + location.search + location.hash;
          if (p !== lastPath) { lastPath = p; onRouteChange(); }
          // Re-assert the offset: some sites strip our inline style on re-render.
          var bar = document.getElementById(BAR_ID);
          if (bar) applyPadding(bar);
        }
      }, 400);
    }
  }

  window.XplorerToolbar = {
    mountNative: mountNative,
    wireHideToggle: wireHideToggle,
    makeRevealDraggable: makeRevealDraggable,
  };
})();
