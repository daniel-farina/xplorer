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
  // ICON LIBRARY. id -> inline SVG string (24x24, class="gi", currentColor).
  // The first six (globe/book/chat/xmark/wrench + image/sparkle) are copied
  // verbatim from toolbar.html so config-rendered built-ins are pixel-identical
  // to the static markup. The rest match the same stroke style (width 2, round
  // caps/joins) so custom/catalog pills look native.
  var S = 'viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"';
  var ICONS = {
    // --- copied verbatim from toolbar.html ---
    chat: '<svg class="gi" ' + S + '><path d="M21 11.5a8.4 8.4 0 0 1-8.9 8.4 9 9 0 0 1-3.6-.7L3 21l1.8-4.5A8.4 8.4 0 0 1 12.6 3 8.4 8.4 0 0 1 21 11.5z"/></svg>',
    wrench: '<svg class="gi" ' + S + '><path d="M14.7 6.3a4 4 0 0 0-5.4 5.3L3 18v3h3l6.4-6.3a4 4 0 0 0 5.3-5.4l-2.8 2.8-2-2 2.8-2.8z"/></svg>',
    globe: '<svg class="gi" ' + S + '><circle cx="12" cy="12" r="9"/><path d="M3 12h18M12 3a14 14 0 0 1 0 18 14 14 0 0 1 0-18z"/></svg>',
    book: '<svg class="gi" ' + S + '><path d="M4 5a2 2 0 0 1 2-2h13v18H6a2 2 0 0 1-2-2z"/><path d="M19 17H6a2 2 0 0 0-2 2"/></svg>',
    xmark: '<svg class="gi" viewBox="0 0 24 24" fill="currentColor" stroke="none"><path d="M18.2 2H21l-6.5 7.4L22 22h-6.2l-4.8-6.3L5.5 22H2.7l7-8L2 2h6.3l4.4 5.8zm-1 18h1.5L7.5 3.7H5.9z"/></svg>',
    image: '<svg class="gi" ' + S + '><rect x="3" y="3" width="18" height="18" rx="2"/><circle cx="8.5" cy="8.5" r="1.5"/><path d="m21 15-5-5L5 21"/></svg>',
    sparkle: '<svg class="gi" ' + S + '><path d="M12 3l1.8 5.2L19 10l-5.2 1.8L12 17l-1.8-5.2L5 10l5.2-1.8z"/></svg>',
    // --- curated glyphs (matching style) ---
    users: '<svg class="gi" ' + S + '><path d="M16 21v-2a4 4 0 0 0-4-4H6a4 4 0 0 0-4 4v2"/><circle cx="9" cy="7" r="4"/><path d="M22 21v-2a4 4 0 0 0-3-3.9M16 3.1a4 4 0 0 1 0 7.8"/></svg>',
    dollar: '<svg class="gi" ' + S + '><path d="M12 2v20M17 5.5A4 4 0 0 0 13 4h-2a3.5 3.5 0 0 0 0 7h2a3.5 3.5 0 0 1 0 7h-2a4 4 0 0 1-4-1.5"/></svg>',
    chart: '<svg class="gi" ' + S + '><path d="M3 3v18h18"/><path d="M7 15l3-4 3 3 4-6"/></svg>',
    columns: '<svg class="gi" ' + S + '><rect x="3" y="4" width="18" height="16" rx="2"/><path d="M9 4v16M15 4v16"/></svg>',
    mic: '<svg class="gi" ' + S + '><rect x="9" y="2" width="6" height="11" rx="3"/><path d="M5 11a7 7 0 0 0 14 0M12 18v3"/></svg>',
    terminal: '<svg class="gi" ' + S + '><path d="M4 17l6-5-6-5M12 19h8"/></svg>',
    code: '<svg class="gi" ' + S + '><path d="M16 18l6-6-6-6M8 6l-6 6 6 6"/></svg>',
    rocket: '<svg class="gi" ' + S + '><path d="M5 13c-1.5 1.3-2 5-2 5s3.7-.5 5-2"/><path d="M9 15l-3-3a14 14 0 0 1 9-9c2.8 0 5 2.2 5 5a14 14 0 0 1-9 9z"/><circle cx="14.5" cy="9.5" r="1.5"/></svg>',
    briefcase: '<svg class="gi" ' + S + '><rect x="2" y="7" width="20" height="14" rx="2"/><path d="M8 7V5a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2"/></svg>',
    home: '<svg class="gi" ' + S + '><path d="M3 11l9-8 9 8M5 10v10h5v-6h4v6h5V10"/></svg>',
    bell: '<svg class="gi" ' + S + '><path d="M18 8a6 6 0 0 0-12 0c0 7-3 9-3 9h18s-3-2-3-9"/><path d="M10.3 21a1.9 1.9 0 0 0 3.4 0"/></svg>',
    star: '<svg class="gi" ' + S + '><path d="M12 3l2.9 5.9 6.5.9-4.7 4.6 1.1 6.5L12 18l-5.8 3 1.1-6.5L2.6 9.8l6.5-.9z"/></svg>',
    bolt: '<svg class="gi" ' + S + '><path d="M13 2L4 14h7l-1 8 9-12h-7z"/></svg>',
    brain: '<svg class="gi" ' + S + '><path d="M9.5 3a2.5 2.5 0 0 0-2.5 2.5A2.5 2.5 0 0 0 5 8c0 1 .5 1.8 1.2 2.3A2.5 2.5 0 0 0 5 12.5 2.5 2.5 0 0 0 7 15a2.5 2.5 0 0 0 2.5 2.5c1 0 1.5-.5 1.5-.5V4s-.5-1-1.5-1zM14.5 3a2.5 2.5 0 0 1 2.5 2.5A2.5 2.5 0 0 1 19 8c0 1-.5 1.8-1.2 2.3A2.5 2.5 0 0 1 19 12.5 2.5 2.5 0 0 1 17 15a2.5 2.5 0 0 1-2.5 2.5c-1 0-1.5-.5-1.5-.5V4s.5-1 1.5-1z"/></svg>',
    search: '<svg class="gi" ' + S + '><circle cx="11" cy="11" r="7"/><path d="M21 21l-4.3-4.3"/></svg>',
    gear: '<svg class="gi" ' + S + '><circle cx="12" cy="12" r="3"/><path d="M19.4 15a1.6 1.6 0 0 0 .3 1.8l.1.1a2 2 0 1 1-2.8 2.8l-.1-.1a1.6 1.6 0 0 0-1.8-.3 1.6 1.6 0 0 0-1 1.5V21a2 2 0 1 1-4 0v-.1a1.6 1.6 0 0 0-1-1.5 1.6 1.6 0 0 0-1.8.3l-.1.1a2 2 0 1 1-2.8-2.8l.1-.1a1.6 1.6 0 0 0 .3-1.8 1.6 1.6 0 0 0-1.5-1H3a2 2 0 1 1 0-4h.1a1.6 1.6 0 0 0 1.5-1 1.6 1.6 0 0 0-.3-1.8l-.1-.1a2 2 0 1 1 2.8-2.8l.1.1a1.6 1.6 0 0 0 1.8.3H9a1.6 1.6 0 0 0 1-1.5V3a2 2 0 1 1 4 0v.1a1.6 1.6 0 0 0 1 1.5 1.6 1.6 0 0 0 1.8-.3l.1-.1a2 2 0 1 1 2.8 2.8l-.1.1a1.6 1.6 0 0 0-.3 1.8V9a1.6 1.6 0 0 0 1.5 1H21a2 2 0 1 1 0 4h-.1a1.6 1.6 0 0 0-1.5 1z"/></svg>',
    video: '<svg class="gi" ' + S + '><rect x="2" y="6" width="14" height="12" rx="2"/><path d="M16 10l6-3v10l-6-3z"/></svg>',
    bookmark: '<svg class="gi" ' + S + '><path d="M6 3h12a1 1 0 0 1 1 1v17l-7-4-7 4V4a1 1 0 0 1 1-1z"/></svg>',
    compass: '<svg class="gi" ' + S + '><circle cx="12" cy="12" r="9"/><path d="M16 8l-2.5 5.5L8 16l2.5-5.5z"/></svg>',
    link: '<svg class="gi" ' + S + '><path d="M10 13a5 5 0 0 0 7 0l3-3a5 5 0 0 0-7-7l-1.5 1.5"/><path d="M14 11a5 5 0 0 0-7 0l-3 3a5 5 0 0 0 7 7l1.5-1.5"/></svg>',
    plus: '<svg class="gi" ' + S + '><path d="M12 5v14M5 12h14"/></svg>',
    news: '<svg class="gi" ' + S + '><path d="M4 4h13a1 1 0 0 1 1 1v14a2 2 0 0 0 2-2V8M4 4a1 1 0 0 0-1 1v13a1 1 0 0 0 1 1h14"/><path d="M7 8h7M7 12h7M7 16h4"/></svg>',
    grid: '<svg class="gi" ' + S + '><rect x="3" y="3" width="7" height="7" rx="1"/><rect x="14" y="3" width="7" height="7" rx="1"/><rect x="3" y="14" width="7" height="7" rx="1"/><rect x="14" y="14" width="7" height="7" rx="1"/></svg>',
  };

  // DEFAULT_PILLS — ordered, reproduces the static toolbar.html EXACTLY.
  // isHome pills carry data-home (=== id here) which drives /switch-home.
  var DEFAULT_PILLS = [
    { id: 'xchat', label: 'X Chat', icon: 'chat', href: 'https://x.com/i/chat', enabled: true },
    { id: 'build', label: 'Grok Build', icon: 'wrench', href: '/apps', enabled: true },
    { id: 'web', label: 'Grok Web', icon: 'globe', href: '/switch-home?mode=web', enabled: true, isHome: true },
    { id: 'imagine', label: 'Imagine', icon: 'image', href: 'https://grok.com/imagine', enabled: true },
    { id: 'wiki', label: 'Groki', icon: 'book', href: '/switch-home?mode=wiki', enabled: true, isHome: true },
    { id: 'xcom', label: 'x.com', icon: 'xmark', href: 'https://x.com/', enabled: true },
    { id: 'xgrok', label: 'Grok on X', icon: 'sparkle', href: 'https://x.com/i/grok', enabled: true },
  ];

  // PILL_CATALOG — quick-add seed services for the settings picker. The five
  // built-ins first, then extra xAI/X services (verified URLs).
  var PILL_CATALOG = DEFAULT_PILLS.concat([
    { id: 'console', label: 'Console', icon: 'terminal', href: 'https://console.x.ai' },
    { id: 'docs', label: 'Docs', icon: 'code', href: 'https://docs.x.ai' },
    { id: 'xai', label: 'xAI', icon: 'rocket', href: 'https://x.ai' },
    { id: 'business', label: 'Business', icon: 'briefcase', href: 'https://x.ai/grok/business' },
    { id: 'plans', label: 'Plans', icon: 'dollar', href: 'https://grok.com/plans' },
    { id: 'companions', label: 'Companions', icon: 'users', href: 'https://grok.com/companions' },
    { id: 'xpremium', label: 'X Premium', icon: 'sparkle', href: 'https://x.com/i/premium_sign_up' },
    { id: 'xanalytics', label: 'Analytics', icon: 'chart', href: 'https://x.com/i/account_analytics' },
    { id: 'xpro', label: 'X Pro', icon: 'columns', href: 'https://pro.x.com' },
    { id: 'xspaces', label: 'Spaces', icon: 'mic', href: 'https://x.com/i/spaces/start' },
    { id: 'xcommunities', label: 'Communities', icon: 'users', href: 'https://x.com/communities' },
  ]);

  var DEFAULT_BY_ID = {};
  DEFAULT_PILLS.forEach(function (p) { DEFAULT_BY_ID[p.id] = p; });

  // --------------------------------------------------------------------------
  // CONFIG-DRIVEN customization (shared by both surfaces). |config| is read
  // from grok_settings.json under the "toolbar" key as an ORDERED ARRAY:
  //   { pills: [ {id,label,href,icon,enabled,isHome?,children?:[{label,href}]} ] }
  // Semantics:
  //   - no config / pills not a non-empty array → leave the static toolbar.html
  //     markup untouched (the default 5 pills; current behavior — no regression)
  //   - otherwise REBUILD .grok-nav-pills from the array, in order, skipping
  //     entries with enabled === false. Built-in ids are merged with their
  //     DEFAULT_PILLS entry so a stored {id:'web',enabled:true} still gets its
  //     icon/home/children.
  // gatewayOrigin (when set) is prefixed onto root-relative hrefs ("/…") so the
  // bar works on third-party origins (native overlay) just like baked-in markup.
  function applyToolbarConfig(barEl, config, gatewayOrigin) {
    if (!barEl || !config || !Array.isArray(config.pills) || !config.pills.length) {
      return;
    }
    var container = barEl.querySelector('.grok-nav-pills');
    if (!container) return;
    var gw = gatewayOrigin || '';

    var resolveHref = function (href) {
      href = href || '';
      if (gw && href.charAt(0) === '/') return gw + href;
      return href;
    };
    var isExternal = function (href) {
      return /^https?:\/\//i.test(href || '');
    };

    container.innerHTML = '';
    config.pills.forEach(function (raw) {
      if (!raw || !raw.id) return;
      var base = DEFAULT_BY_ID[raw.id] || {};
      // Merge stored fields over the built-in defaults so a sparse stored entry
      // still renders the built-in's icon/home/children.
      var pill = {
        id: raw.id,
        label: raw.label != null ? raw.label : base.label,
        href: raw.href != null ? raw.href : base.href,
        icon: raw.icon != null ? raw.icon : base.icon,
        enabled: raw.enabled,
        isHome: raw.isHome != null ? raw.isHome : base.isHome,
        children: Array.isArray(raw.children) ? raw.children
          : (Array.isArray(base.children) ? base.children : null),
      };
      if (pill.enabled === false) return;

      var wrap = document.createElement('div');
      wrap.className = 'grok-pill-wrap';

      var a = document.createElement('a');
      a.className = 'grok-pill';
      a.setAttribute('data-pill', pill.id);
      // isHome pills drive /switch-home; data-home value === mode (=== id for
      // the build/web/wiki built-ins). The switch logic keys off this value.
      if (pill.isHome) a.setAttribute('data-home', pill.id);
      var href = resolveHref(pill.href);
      a.setAttribute('href', href);
      if (isExternal(pill.href)) a.setAttribute('rel', 'noopener noreferrer');
      a.innerHTML = (ICONS[pill.icon] || ICONS.link) +
        '<span>' + escapeText(pill.label || '') + '</span>';
      wrap.appendChild(a);

      if (Array.isArray(pill.children) && pill.children.length) {
        var menu = document.createElement('div');
        menu.className = 'grok-pill-menu';
        pill.children.forEach(function (child) {
          if (!child) return;
          var ca = document.createElement('a');
          ca.textContent = child.label || '';
          var chref = resolveHref(child.href || '');
          ca.setAttribute('href', chref);
          if (isExternal(child.href)) ca.setAttribute('rel', 'noopener noreferrer');
          menu.appendChild(ca);
        });
        wrap.appendChild(menu);
      }
      container.appendChild(wrap);
    });
  }

  // Minimal HTML-text escape for labels injected via innerHTML alongside SVG.
  function escapeText(s) {
    return String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
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
    var TOOLBAR_CONFIG = host.toolbarConfig || null;

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
      // Config is applied after absolutize so config children with absolute
      // hrefs are untouched; root-relative ("/…") children are gateway-prefixed
      // here (GW is passed in). mountNative re-renders innerHTML on each
      // ensureBar, so re-applying the config every time is correct.
      applyToolbarConfig(bar, TOOLBAR_CONFIG, GW);
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
    applyToolbarConfig: applyToolbarConfig,
    ICONS: ICONS,
    DEFAULT_PILLS: DEFAULT_PILLS,
    PILL_CATALOG: PILL_CATALOG,
  };
})();
