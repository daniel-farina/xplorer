/** Shared utilities for Grok companion UI (search + chat). */

const DEFAULT_MODEL = 'grok-composer-2.5-fast';
/** Web/video search needs grok-build (Composer has no web search tools). */
const SEARCH_DEFAULT_MODEL = 'grok-build';
const MODEL_STORAGE_KEY = 'grok_model';
const SEARCH_MODEL_STORAGE_KEY = 'grok_search_model';
const SEARCH_HOME_STORAGE_KEY = 'grok_search_home';
const SEARCH_QUERY_STORAGE_KEY = 'xplorer_search_query';
const SEARCH_MODE_STORAGE_KEY = 'xplorer_search_mode';
const SEARCH_HOME_BUILD = 'build';
const SEARCH_HOME_WEB = 'web';
const SEARCH_HOME_WIKI = 'wiki';

function getStoredModel() {
  try {
    return localStorage.getItem(MODEL_STORAGE_KEY) || DEFAULT_MODEL;
  } catch {
    return DEFAULT_MODEL;
  }
}

function persistModel(model) {
  try {
    localStorage.setItem(MODEL_STORAGE_KEY, model);
  } catch { /* ignore */ }
}

function getStoredSearchModel() {
  try {
    return localStorage.getItem(SEARCH_MODEL_STORAGE_KEY) || SEARCH_DEFAULT_MODEL;
  } catch {
    return SEARCH_DEFAULT_MODEL;
  }
}

function persistSearchModel(model) {
  try {
    localStorage.setItem(SEARCH_MODEL_STORAGE_KEY, model);
  } catch { /* ignore */ }
}

function getStoredSearchHome() {
  try {
    return localStorage.getItem(SEARCH_HOME_STORAGE_KEY) || SEARCH_HOME_WEB;
  } catch {
    return SEARCH_HOME_WEB;
  }
}

function persistSearchHome(mode) {
  try {
    localStorage.setItem(SEARCH_HOME_STORAGE_KEY, mode);
  } catch { /* ignore */ }
}

function getStoredSearchQuery() {
  try {
    return localStorage.getItem(SEARCH_QUERY_STORAGE_KEY) || '';
  } catch {
    return '';
  }
}

function persistSearchQuery(query) {
  try {
    const q = String(query || '').trim();
    if (q) localStorage.setItem(SEARCH_QUERY_STORAGE_KEY, q);
    else localStorage.removeItem(SEARCH_QUERY_STORAGE_KEY);
  } catch { /* ignore */ }
}

function getStoredSearchMode() {
  try {
    const m = localStorage.getItem(SEARCH_MODE_STORAGE_KEY) || 'web';
    return ['web', 'imagine'].includes(m) ? m : 'web';
  } catch {
    return 'web';
  }
}

function persistSearchMode(mode) {
  try {
    if (['web', 'imagine'].includes(mode)) {
      localStorage.setItem(SEARCH_MODE_STORAGE_KEY, mode);
    }
  } catch { /* ignore */ }
}

const CONV_MODEL_STORAGE_KEY = 'xplorer_conv_models';

function getConvModelsMap() {
  try {
    return JSON.parse(localStorage.getItem(CONV_MODEL_STORAGE_KEY) || '{}');
  } catch {
    return {};
  }
}

function getConvModel(convId) {
  if (!convId) return getStoredModel();
  const map = getConvModelsMap();
  return map[convId] || getStoredModel();
}

function persistConvModel(convId, model) {
  if (!convId || !model) return;
  try {
    const map = getConvModelsMap();
    map[convId] = model;
    localStorage.setItem(CONV_MODEL_STORAGE_KEY, JSON.stringify(map));
  } catch { /* ignore */ }
}

/** Append search query to Grokipedia (wiki home) URL. */
function wikiUrlForQuery(query, fallbackUrl = 'https://grokipedia.com/') {
  const q = String(query || '').trim();
  if (!q) return fallbackUrl;
  const sep = fallbackUrl.includes('?') ? '&' : '?';
  return `${fallbackUrl}${sep}q=${encodeURIComponent(q)}`;
}

/** Hand off imagine prompts to grok.com/imagine with xplorer_grok context. */
async function imagineUrlForQuery(query, fallbackUrl = 'https://grok.com/imagine') {
  const dest = await grokWebUrlForQuery(query, 'imagine', fallbackUrl);
  if (dest.includes('xplorer_grok=')) {
    return dest.replace(/^https:\/\/grok\.com\/?/, 'https://grok.com/imagine');
  }
  return dest;
}

/** Build a grok.com URL that carries a search query via xplorer_grok pending id. */
async function grokWebUrlForQuery(query, mode, fallbackUrl = 'https://grok.com/') {
  const q = String(query || '').trim();
  if (!q) return fallbackUrl;
  let prompt = q;
  if (mode === 'videos') prompt = `Search for videos: ${q}`;
  else if (mode === 'images') prompt = `Search for images: ${q}`;
  else if (mode === 'imagine') prompt = `Generate an image: ${q}`;
  try {
    const res = await fetch('/api/page/grok-web', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ query: prompt }),
    });
    const data = await res.json();
    if (res.ok && data.grok_url) return data.grok_url;
  } catch { /* fall through */ }
  return fallbackUrl;
}

async function fetchSettings() {
  const res = await fetch('/api/settings');
  if (!res.ok) throw new Error('settings unavailable');
  return res.json();
}

async function saveSettings(partial) {
  const res = await fetch('/api/settings', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(partial),
  });
  if (!res.ok) {
    const err = await res.json().catch(() => ({}));
    throw new Error(err.error || res.statusText);
  }
  return res.json();
}

/** Sub-route within Grok Build (conversations / apps / app builder). */
function companionBuildSubRoute(path) {
  if (path === '/' || path === '') return 'conversations';
  if (path.startsWith('/apps')) return 'apps';
  if (path.startsWith('/app')) return 'app';
  if (path.startsWith('/settings')) return 'settings';
  if (path.startsWith('/welcome')) return 'welcome';
  return '';
}

/** Inline SVG icons (16px, stroke=currentColor) used across the toolbar. */
const GROK_ICONS = {
  xplorer: '<svg class="gi" viewBox="0 0 32 32" fill="none" stroke="currentColor" stroke-width="3.4" stroke-linecap="round" stroke-linejoin="round"><path d="M9.5 9.5 L22.5 22.5 M22.5 9.5 L9.5 22.5"/></svg>',
  chat: '<svg class="gi" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M21 11.5a8.4 8.4 0 0 1-8.9 8.4 9 9 0 0 1-3.6-.7L3 21l1.8-4.5A8.4 8.4 0 0 1 12.6 3 8.4 8.4 0 0 1 21 11.5z"/></svg>',
  build: '<svg class="gi" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M14.7 6.3a4 4 0 0 0-5.4 5.3L3 18v3h3l6.4-6.3a4 4 0 0 0 5.3-5.4l-2.8 2.8-2-2 2.8-2.8z"/></svg>',
  web: '<svg class="gi" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="9"/><path d="M3 12h18M12 3a14 14 0 0 1 0 18 14 14 0 0 1 0-18z"/></svg>',
  groki: '<svg class="gi" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M4 5a2 2 0 0 1 2-2h13v18H6a2 2 0 0 1-2-2z"/><path d="M19 17H6a2 2 0 0 0-2 2"/></svg>',
  x: '<svg class="gi" viewBox="0 0 24 24" fill="currentColor" stroke="none"><path d="M18.2 2H21l-6.5 7.4L22 22h-6.2l-4.8-6.3L5.5 22H2.7l7-8L2 2h6.3l4.4 5.8zm-1 18h1.5L7.5 3.7H5.9z"/></svg>',
  settings: '<svg class="gi" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="3"/><path d="M19.4 15a1.6 1.6 0 0 0 .3 1.8l.1.1a2 2 0 1 1-2.8 2.8l-.1-.1a1.6 1.6 0 0 0-1.8-.3 1.6 1.6 0 0 0-1 1.5V21a2 2 0 1 1-4 0v-.1a1.6 1.6 0 0 0-1-1.5 1.6 1.6 0 0 0-1.8.3l-.1.1a2 2 0 1 1-2.8-2.8l.1-.1a1.6 1.6 0 0 0 .3-1.8 1.6 1.6 0 0 0-1.5-1H3a2 2 0 1 1 0-4h.1a1.6 1.6 0 0 0 1.5-1 1.6 1.6 0 0 0-.3-1.8l-.1-.1a2 2 0 1 1 2.8-2.8l.1.1a1.6 1.6 0 0 0 1.8.3H9a1.6 1.6 0 0 0 1-1.5V3a2 2 0 1 1 4 0v.1a1.6 1.6 0 0 0 1 1.5 1.6 1.6 0 0 0 1.8-.3l.1-.1a2 2 0 1 1 2.8 2.8l-.1.1a1.6 1.6 0 0 0-.3 1.8V9a1.6 1.6 0 0 0 1.5 1H21a2 2 0 1 1 0 4h-.1a1.6 1.6 0 0 0-1.5 1z"/></svg>',
  hide: '<svg class="gi" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="18 15 12 9 6 15"/></svg>',
  reveal: '<svg class="gi" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="6 9 12 15 18 9"/></svg>',
  logs: '<svg class="gi" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M4 5h16M4 10h16M4 15h10M4 20h7"/></svg>',
  grip: '<svg class="gi grok-reveal-grip" viewBox="0 0 24 24" fill="currentColor" stroke="none" aria-hidden="true"><circle cx="9" cy="6" r="1.6"/><circle cx="15" cy="6" r="1.6"/><circle cx="9" cy="12" r="1.6"/><circle cx="15" cy="12" r="1.6"/><circle cx="9" cy="18" r="1.6"/><circle cx="15" cy="18" r="1.6"/></svg>',
};

const TOOLBAR_HIDDEN_KEY = 'xplorer_toolbar_hidden';

/** Uniform Xplorer topbar — single source of truth for all companion pages. */
function grokToolbarHTML() {
  return `<header class="grok-toolbar">
    <a class="grok-logo" href="/search" title="Xplorer home" aria-label="Xplorer">${GROK_ICONS.xplorer}<span>plorer</span></a>
    <div class="grok-toolbar-spacer"></div>
    <div class="grok-toolbar-actions">
      <div class="grok-nav-pills" id="home-toggle" title="Xplorer navigation">
        <div class="grok-pill-wrap">
          <a class="grok-pill" data-pill="xchat" href="https://x.com/i/chat" target="_blank" rel="noopener noreferrer">${GROK_ICONS.chat}<span>X Chat</span></a>
          <div class="grok-pill-menu">
            <a href="https://x.com/i/chat" target="_blank" rel="noopener noreferrer">${GROK_ICONS.chat}<span>Open X Chat</span></a>
          </div>
        </div>
        <div class="grok-pill-wrap">
          <a class="grok-pill" data-home="build" data-pill="build" href="/switch-home?mode=build">${GROK_ICONS.build}<span>Grok Build</span></a>
          <div class="grok-pill-menu">
            <a href="/" data-route="conversations">${GROK_ICONS.chat}<span>Conversations</span></a>
            <a href="/apps" data-route="apps">${GROK_ICONS.build}<span>Apps</span></a>
            <a href="/logs" data-route="logs">${GROK_ICONS.logs}<span>Logs</span></a>
          </div>
        </div>
        <div class="grok-pill-wrap">
          <a class="grok-pill" data-home="web" data-pill="web" href="/switch-home?mode=web">${GROK_ICONS.web}<span>Grok Web</span></a>
          <div class="grok-pill-menu">
            <a href="/search" data-route="search">${GROK_ICONS.web}<span>Search</span></a>
            <a href="https://grok.com/imagine" target="_blank" rel="noopener noreferrer">${GROK_ICONS.xplorer}<span>Imagine</span></a>
          </div>
        </div>
        <div class="grok-pill-wrap">
          <a class="grok-pill" data-home="wiki" data-pill="wiki" href="/switch-home?mode=wiki">${GROK_ICONS.groki}<span>Groki</span></a>
          <div class="grok-pill-menu">
            <a href="https://grokipedia.com/" target="_blank" rel="noopener noreferrer">${GROK_ICONS.groki}<span>Grokipedia</span></a>
          </div>
        </div>
        <div class="grok-pill-wrap">
          <a class="grok-pill" data-pill="xcom" href="https://x.com/" target="_blank" rel="noopener noreferrer">${GROK_ICONS.x}<span>x.com</span></a>
          <div class="grok-pill-menu">
            <a href="https://x.com/" target="_blank" rel="noopener noreferrer">${GROK_ICONS.x}<span>Home</span></a>
          </div>
        </div>
      </div>
      <a href="/settings" class="grok-toolbar-btn grok-icon-btn grok-settings-btn" data-route="settings" title="Xplorer settings" aria-label="Settings">${GROK_ICONS.settings}</a>
      <button type="button" class="grok-toolbar-btn grok-icon-btn grok-toolbar-hide" title="Hide toolbar" aria-label="Hide toolbar">${GROK_ICONS.hide}</button>
    </div>
  </header>`;
}

async function mountGrokToolbar({ pageHome, onSwitch } = {}) {
  // Canonical markup lives in /toolbar.html (the SINGLE source shared with the
  // native overlay). Fetch it live; fall back to the inline grokToolbarHTML()
  // copy only if the partial can't be loaded so the bar is never blank.
  let html;
  try {
    const res = await fetch('/toolbar.html', { cache: 'no-store' });
    if (res.ok) {
      const inner = (await res.text()).trim();
      if (inner) html = `<header class="grok-toolbar">${inner}</header>`;
    }
  } catch {}
  if (!html) html = grokToolbarHTML();
  const mount = document.getElementById('grok-toolbar-mount');
  const legacy = document.querySelector('header.grok-toolbar');
  if (mount) {
    mount.outerHTML = html;
  } else if (legacy) {
    legacy.outerHTML = html;
  } else {
    document.body.insertAdjacentHTML('afterbegin', html);
  }
  syncCompanionToolbarPill();
  const path = (location.pathname || '').toLowerCase();
  const settingsBtn = document.querySelector('.grok-settings-btn');
  if (settingsBtn) {
    settingsBtn.classList.toggle('active', path.startsWith('/settings'));
  }
  initSearchHomeToggle(document.getElementById('home-toggle'), { pageHome, onSwitch });
  initToolbarHideToggle();
}

/** Hide/show the toolbar; persists state and drops a floating reveal handle. */
function initToolbarHideToggle() {
  const bar = document.querySelector('header.grok-toolbar, #xplorer-grok-bar.grok-toolbar');
  if (!bar) return;
  let reveal = document.getElementById('grok-toolbar-reveal');
  if (!reveal) {
    reveal = document.createElement('button');
    reveal.id = 'grok-toolbar-reveal';
    reveal.type = 'button';
    reveal.title = 'Show toolbar (drag the grip to move)';
    reveal.setAttribute('aria-label', 'Show toolbar');
    reveal.innerHTML = `${GROK_ICONS.grip}${GROK_ICONS.xplorer}${GROK_ICONS.reveal}`;
    document.body.appendChild(reveal);
  }
  makeRevealDraggable(reveal);
  const apply = (hidden) => {
    bar.classList.toggle('grok-toolbar-hidden', hidden);
    reveal.classList.toggle('show', hidden);
    try { localStorage.setItem(TOOLBAR_HIDDEN_KEY, hidden ? '1' : '0'); } catch {}
  };
  let stored = '0';
  try { stored = localStorage.getItem(TOOLBAR_HIDDEN_KEY) || '0'; } catch {}
  apply(stored === '1');
  const hideBtn = bar.querySelector('.grok-toolbar-hide');
  if (hideBtn && !hideBtn.dataset.wired) {
    hideBtn.dataset.wired = '1';
    hideBtn.addEventListener('click', () => apply(true));
  }
  if (!reveal.dataset.wired) {
    reveal.dataset.wired = '1';
    reveal.addEventListener('click', () => apply(false));
  }
}

const TOOLBAR_REVEAL_POS_KEY = 'xplorer_toolbar_reveal_pos';

/** Let the floating reveal pill be dragged out of the way; persists position.
 *  A real drag (>4px) repositions and suppresses the reveal click. */
function makeRevealDraggable(reveal) {
  if (reveal.dataset.dragWired) return;
  reveal.dataset.dragWired = '1';
  reveal.style.touchAction = 'none';
  const clamp = (v, max) => Math.max(0, Math.min(v, max));
  const positionAt = (x, y) => {
    const r = reveal.getBoundingClientRect();
    reveal.style.left = clamp(x, window.innerWidth - r.width) + 'px';
    reveal.style.top = clamp(y, window.innerHeight - r.height) + 'px';
    reveal.style.right = 'auto';
  };
  try {
    const p = JSON.parse(localStorage.getItem(TOOLBAR_REVEAL_POS_KEY) || 'null');
    if (p && typeof p.x === 'number') {
      // position once it has a measurable size
      requestAnimationFrame(() => positionAt(p.x, p.y));
    }
  } catch {}
  let start = null;
  let moved = false;
  reveal.addEventListener('pointerdown', (e) => {
    const r = reveal.getBoundingClientRect();
    start = { px: e.clientX, py: e.clientY, ox: r.left, oy: r.top };
    moved = false;
    try { reveal.setPointerCapture(e.pointerId); } catch {}
  });
  reveal.addEventListener('pointermove', (e) => {
    if (!start) return;
    const dx = e.clientX - start.px;
    const dy = e.clientY - start.py;
    if (!moved && Math.hypot(dx, dy) < 4) return;
    moved = true;
    reveal.classList.add('dragging');
    positionAt(start.ox + dx, start.oy + dy);
  });
  const end = (e) => {
    if (!start) return;
    try { reveal.releasePointerCapture(e.pointerId); } catch {}
    reveal.classList.remove('dragging');
    if (moved) {
      const r = reveal.getBoundingClientRect();
      try {
        localStorage.setItem(TOOLBAR_REVEAL_POS_KEY,
          JSON.stringify({ x: Math.round(r.left), y: Math.round(r.top) }));
      } catch {}
    }
    start = null;
  };
  reveal.addEventListener('pointerup', end);
  reveal.addEventListener('pointercancel', end);
  // Suppress the reveal click when the pointerup ended a drag.
  reveal.addEventListener('click', (e) => {
    if (moved) { e.stopImmediatePropagation(); e.preventDefault(); }
  }, true);
}

/** Highlight companion toolbar pill from current route (127.0.0.1 pages). */
function syncCompanionToolbarPill() {
  const path = (location.pathname || '').toLowerCase();
  let routeHome = SEARCH_HOME_BUILD;
  if (path.startsWith('/search')) routeHome = SEARCH_HOME_WEB;
  const subRoute = companionBuildSubRoute(path);
  const toggle = document.getElementById('home-toggle');
  if (!toggle) return;
  toggle.querySelectorAll('[data-home]').forEach((btn) => {
    btn.classList.toggle('active', btn.dataset.home === routeHome);
  });
  toggle.querySelectorAll('.grok-pill-menu a[data-route]').forEach((link) => {
    link.classList.toggle('active', !!subRoute && link.dataset.route === subRoute);
  });
  toggle.querySelectorAll('.grok-pill-menu a[data-route="search"]').forEach((link) => {
    link.classList.toggle('active', path.startsWith('/search'));
  });
  document.querySelectorAll('[data-route="settings"]').forEach((el) => {
    el.classList.toggle('active', path.startsWith('/settings'));
  });
}

if (typeof document !== 'undefined') {
  document.addEventListener('DOMContentLoaded', () => {
    if (document.body.dataset.grokToolbar === 'auto') {
      mountGrokToolbar();
    }
  });
}

/** Wire Grok Build / Grok Web / Groki home toggle. */
async function initSearchHomeToggle(container, { onSwitch, pageHome } = {}) {
  if (!container || container.dataset.grokToggleWired) return;
  container.dataset.grokToggleWired = '1';
  const buttons = container.querySelectorAll('[data-home]');
  let settings;
  try {
    settings = await fetchSettings();
  } catch {
    settings = { search_home: getStoredSearchHome() };
  }
  let currentHome = settings.search_home || SEARCH_HOME_WEB;
  persistSearchHome(currentHome);

  const setActive = (mode) => {
    currentHome = mode;
    buttons.forEach((btn) => {
      btn.classList.toggle('active', btn.dataset.home === mode);
    });
  };
  setActive(pageHome || currentHome);
  syncCompanionToolbarPill();
  buttons.forEach((btn) => {
    const label = (btn.textContent || btn.dataset.home || '').trim();
    if (label) btn.title = `${label} — Alt+←/→ switch home`;
  });

  buttons.forEach((btn) => {
    btn.addEventListener('click', async (ev) => {
      // Canonical markup renders these as <a href> (so they also work as
      // plain links on native pages); on companion pages we drive the home
      // switch via API instead of letting the browser navigate.
      if (ev) ev.preventDefault();
      const mode = btn.dataset.home;
      if (!mode || mode === currentHome) return;
      try {
        const updated = await saveSettings({ search_home: mode });
        const saved = updated.search_home || mode;
        persistSearchHome(saved);
        setActive(saved);
        if (onSwitch) {
          onSwitch(saved, updated);
        } else if (saved === SEARCH_HOME_WEB) {
          const params = new URLSearchParams(window.location.search);
          const q = params.get('q') || getStoredSearchQuery();
          const m = params.get('mode') || getStoredSearchMode();
          let url = `${window.location.origin}/search`;
          const qs = new URLSearchParams();
          if (q) qs.set('q', q);
          if (m && m !== 'web') qs.set('mode', m);
          const s = qs.toString();
          if (s) url += `?${s}`;
          window.location.href = url;
        } else if (saved === SEARCH_HOME_WIKI) {
          const params = new URLSearchParams(window.location.search);
          const q = params.get('q') || getStoredSearchQuery();
          window.location.href = wikiUrlForQuery(
            q,
            updated.grok_wiki_url || 'https://grokipedia.com/',
          );
        } else {
          window.location.href = `${window.location.origin}/switch-home?mode=${encodeURIComponent(saved)}`;
        }
      } catch (e) {
        alert(`Could not save preference: ${e.message}`);
      }
    });
  });
  initToolbarHomeHotkeys(container);
}

/** Alt+←/→ cycles Grok Build / Web / Wiki home pills. */
function initToolbarHomeHotkeys(container) {
  if (!container || window.__xplorerToolbarHomeHotkeys) return;
  window.__xplorerToolbarHomeHotkeys = true;
  document.addEventListener('keydown', (e) => {
    if (!e.altKey || e.metaKey || e.ctrlKey) return;
    if (e.key !== 'ArrowLeft' && e.key !== 'ArrowRight') return;
    const toggle = document.getElementById('home-toggle');
    if (!toggle) return;
    const buttons = [...toggle.querySelectorAll('[data-home]')];
    if (buttons.length < 2) return;
    const active = buttons.findIndex((b) => b.classList.contains('active'));
    if (active < 0) return;
    const delta = e.key === 'ArrowRight' ? 1 : -1;
    const next = buttons[(active + delta + buttons.length) % buttons.length];
    e.preventDefault();
    next.click();
  });
}

/** Browser/tab/bookmark tasks need grok-build + native MCP tools. */
function messageNeedsBrowserTools(message) {
  const lower = String(message || '').toLowerCase();
  const keywords = [
    'tab', 'tabs', 'bookmark', 'bookmarks', 'organize', 'organise', 'group',
    'browser', 'chrome', 'navigate', 'close tab', 'split tab', 'history',
    'xplorer',
  ];
  return keywords.some((kw) => lower.includes(kw));
}

/** Pick the right model for a search mode (web/videos → grok-build). */
function modelForSearchMode(mode, selectedModel, models) {
  const needsWeb = mode === 'web' || mode === 'videos';
  if (needsWeb && selectedModel === DEFAULT_MODEL) {
    const grok = (models || []).find((m) => m.id === SEARCH_DEFAULT_MODEL);
    return grok ? grok.id : SEARCH_DEFAULT_MODEL;
  }
  return selectedModel;
}

function modelLabel(id, models) {
  const opt = (models || []).find((o) => o.id === id);
  return opt ? opt.label : id;
}

function updateModelBadge(el, modelId, modelLabelText) {
  if (!el) return;
  el.textContent = modelLabelText || modelId;
  el.classList.add('active');
  el.title = `Active model: ${modelLabelText || modelId}`;
}

/** Fetch models from backend; fall back to a minimal list. */
async function fetchModels() {
  try {
    const res = await fetch('/api/models');
    if (!res.ok) throw new Error('models unavailable');
    const data = await res.json();
    const list = data.models || [];
    if (list.length) return list;
  } catch { /* fall through */ }
  return [
    { id: 'grok-composer-2.5-fast', label: 'Composer 2.5' },
    { id: 'grok-build', label: 'Grok Build' },
  ];
}

function populateModelSelect(select, models, selectedId) {
  if (!select) return;
  select.innerHTML = '';
  for (const m of models) {
    const opt = document.createElement('option');
    opt.value = m.id;
    opt.textContent = m.label || m.id;
    if (m.id === selectedId) opt.selected = true;
    select.appendChild(opt);
  }
}

/** Follow macOS / system light-dark via prefers-color-scheme. */
function applySystemTheme() {
  const dark = window.matchMedia('(prefers-color-scheme: dark)').matches;
  document.documentElement.setAttribute('data-theme', dark ? 'dark' : 'light');
}

function startThemeWatcher() {
  applySystemTheme();
  if (window.__grokThemeMqBound) return;
  window.__grokThemeMqBound = true;
  const mq = window.matchMedia('(prefers-color-scheme: dark)');
  const onChange = () => applySystemTheme();
  if (mq.addEventListener) mq.addEventListener('change', onChange);
  else if (mq.addListener) mq.addListener(onChange);
}

function escapeHtml(s) {
  return String(s)
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;');
}

/** Remove terminal ANSI color codes (e.g. \x1b[2m) from grok stderr leakage. */
function stripAnsi(s) {
  return String(s).replace(/\x1b\[[0-9;]*m/g, '');
}

/** Parse one NDJSON stream line; returns null for noise/non-JSON. */
function parseStreamLine(line) {
  const cleaned = stripAnsi(line).trim();
  if (!cleaned || cleaned[0] !== '{') return null;
  try {
    return JSON.parse(cleaned);
  } catch {
    return null;
  }
}

/** Safe http(s) URLs only. */
function safeUrl(url) {
  try {
    const u = new URL(url);
    return u.protocol === 'http:' || u.protocol === 'https:' ? u.href : '';
  } catch {
    return '';
  }
}

/** Detect video/content provider from URL. */
function detectProvider(url) {
  try {
    const h = new URL(url).hostname.replace(/^www\./, '');
    if (h === 'youtu.be' || h.endsWith('youtube.com')) return 'youtube';
    if (h.endsWith('vimeo.com')) return 'vimeo';
    if (h.endsWith('dailymotion.com')) return 'dailymotion';
    if (h.endsWith('twitch.tv')) return 'twitch';
  } catch { /* ignore */ }
  return '';
}

function youtubeVideoId(url) {
  try {
    const u = new URL(url);
    if (u.hostname.includes('youtu.be')) return u.pathname.slice(1).split('/')[0];
    return u.searchParams.get('v') || '';
  } catch {
    return '';
  }
}

/** Best-effort thumbnail for video URLs (YouTube supported natively). */
function mediaThumbnail(item) {
  if (item.thumbnail) return safeUrl(item.thumbnail);
  const url = safeUrl(item.url);
  if (!url) return '';
  const provider = item.provider || detectProvider(url);
  if (provider === 'youtube') {
    const id = youtubeVideoId(url);
    if (id) return `https://img.youtube.com/vi/${id}/mqdefault.jpg`;
  }
  return '';
}

function domainFromUrl(url) {
  try {
    return new URL(url).hostname.replace(/^www\./, '');
  } catch {
    return url;
  }
}

function normalizeResultItem(raw, kind) {
  const url = safeUrl(typeof raw === 'string' ? raw : raw?.url);
  if (!url) return null;
  const title = typeof raw === 'string'
    ? url
    : (raw.title || raw.name || domainFromUrl(url));
  return {
    kind: raw?.kind || kind,
    url,
    title,
    snippet: raw?.snippet || raw?.description || '',
    thumbnail: mediaThumbnail({ ...raw, url }),
    provider: raw?.provider || detectProvider(url),
    source: raw?.source || domainFromUrl(url),
  };
}

/** Merge result items, dedupe by URL. */
function mergeResultItems(existing, incoming, kind) {
  const seen = new Set(existing.map((i) => i.url));
  for (const raw of incoming) {
    const item = normalizeResultItem(raw, kind);
    if (!item || seen.has(item.url)) continue;
    seen.add(item.url);
    existing.push(item);
  }
  return existing;
}

/** Extract [[n]](url) and [title](url) citations from grok web-search text. */
function extractMarkdownLinks(text) {
  const links = [];
  const seen = new Set();
  const patterns = [
    /\[\[(\d+)\]\]\(([^)]+)\)/g,
    /\[([^\]]+)\]\((https?:\/\/[^)]+)\)/g,
  ];
  for (const re of patterns) {
    let m;
    while ((m = re.exec(text)) !== null) {
      const href = safeUrl(m[2]);
      if (!href || seen.has(href)) continue;
      seen.add(href);
      const title = m[1] && !/^\d+$/.test(m[1]) ? m[1] : href;
      links.push({ title, url: href, snippet: '' });
    }
  }
  return links;
}

/** Pull structured items from streaming answer text (URLs + markdown titles). */
function extractResultsFromText(text, mode) {
  const kind = mode === 'videos' ? 'video' : mode === 'images' ? 'image' : 'link';
  const items = [];
  const patterns = [
    /\*\*([^*]+)\*\*\s*[—–-]\s*(https?:\/\/[^\s<>"')]+)/g,
    /\[([^\]]+)\]\((https?:\/\/[^)]+)\)/g,
    /(https?:\/\/[^\s<>"')]+)/g,
  ];
  const seen = new Set();
  for (const re of patterns) {
    let m;
    while ((m = re.exec(text)) !== null) {
      const title = m.length > 2 ? m[1] : '';
      const url = m[m.length - 1];
      const item = normalizeResultItem({ title, url }, kind);
      if (!item || seen.has(item.url)) continue;
      seen.add(item.url);
      items.push(item);
    }
  }
  return items;
}

/** Minimal syntax colors for fenced code (no external deps). */
function highlightCodeLight(code, lang) {
  let s = escapeHtml(code);
  const langKw = {
    python: /\b(def|class|import|from|return|if|elif|else|for|while|with|as|try|except|raise|pass|lambda|yield|True|False|None)\b/g,
    rust: /\b(fn|let|mut|const|struct|enum|impl|trait|pub|use|mod|return|if|else|match|for|while|loop|async|await|true|false|Some|None|Ok|Err)\b/g,
    go: /\b(func|package|import|return|if|else|for|range|switch|case|default|var|const|type|struct|interface|map|chan|go|defer|true|false|nil)\b/g,
    bash: /\b(if|then|else|elif|fi|for|do|done|case|esac|function|return|export|local)\b/g,
  };
  const baseKw = /\b(const|let|var|function|return|if|else|for|while|class|import|export|from|async|await|new|try|catch|throw|typeof|interface|type|enum|def|self|print)\b/g;
  const kw = langKw[lang] || baseKw;
  s = s.replace(kw, '<span class="hl-kw">$1</span>');
  s = s.replace(/(#.*$|\/\/[^\n]*)/gm, '<span class="hl-cmt">$1</span>');
  s = s.replace(/('(?:\\.|[^'\\])*'|"(?:\\.|[^"\\])*"|`(?:\\.|[^`\\])*`)/g, '<span class="hl-str">$1</span>');
  s = s.replace(/\b(\d+(?:\.\d+)?)\b/g, '<span class="hl-num">$1</span>');
  return s;
}

/** Lightweight markdown → HTML (escaped input). */
function renderMarkdown(raw) {
  if (!raw) return '';
  const codeBlocks = [];
  let s = String(raw).replace(/```(\w*)\n?([\s\S]*?)```/g, (_, lang, code) => {
    const idx = codeBlocks.length;
    codeBlocks.push({ lang: lang || '', code });
    return `\x00CODE${idx}\x00`;
  });
  s = escapeHtml(s);
  s = s.replace(/\x00CODE(\d+)\x00/g, (_, n) => {
    const block = codeBlocks[Number(n)];
    if (!block) return '';
    const lang = block.lang ? ` language-${escapeHtml(block.lang)}` : '';
    const body = highlightCodeLight(block.code.trim(), block.lang);
    return `<pre class="code-block"><button type="button" class="code-copy-btn" title="Copy code (⌘⇧C)">Copy</button><code class="${lang.trim()}">${body}</code></pre>`;
  });

  s = s.replace(
    /\[\[(\d+)\]\]\((https?:\/\/[^)\s]+)\)/g,
    (_, n, url) => {
      const href = safeUrl(url);
      return href
        ? `<a href="${href}" target="_blank" rel="noopener" class="cite">[${n}]</a>`
        : `[${n}](${url})`;
    },
  );

  s = s.replace(
    /\[([^\]]+)\]\((https?:\/\/[^)\s]+)\)/g,
    (_, label, url) => {
      const href = safeUrl(url);
      return href
        ? `<a href="${href}" target="_blank" rel="noopener">${label}</a>`
        : `[${label}](${url})`;
    },
  );

  s = s.replace(/^### (.+)$/gm, '<h3>$1</h3>');
  s = s.replace(/^## (.+)$/gm, '<h2>$1</h2>');
  s = s.replace(/\*\*([^*\n]+)\*\*/g, '<strong>$1</strong>');
  s = s.replace(/`([^`\n]+)`/g, '<code>$1</code>');
  s = s.replace(/^---$/gm, '<hr>');
  s = s.replace(/^- (.+)$/gm, '<li>$1</li>');
  s = s.replace(/(?:<li>[\s\S]*?<\/li>\n?)+/g, (block) => `<ul>${block}</ul>`);

  const parts = s.split(/\n{2,}/);
  s = parts
    .map((p) => {
      const t = p.trim();
      if (!t) return '';
      if (/^<(h[23]|ul|hr)/.test(t)) return t;
      return `<p>${t.replace(/\n/g, '<br>')}</p>`;
    })
    .filter(Boolean)
    .join('\n');

  return s;
}

function copyCodeBlockText(pre) {
  const text = pre?.querySelector('code')?.textContent || '';
  if (!text) return Promise.reject(new Error('no code'));
  return navigator.clipboard.writeText(text);
}

/** Cmd+Shift+C copies the hovered or focused code block. */
function initCodeCopyHotkey() {
  if (window.__xplorerCodeCopyHotkey) return;
  window.__xplorerCodeCopyHotkey = true;
  let hoveredPre = null;
  document.addEventListener('mouseover', (e) => {
    const pre = e.target.closest?.('pre.code-block');
    hoveredPre = pre || null;
  });
  document.addEventListener('keydown', (e) => {
    if (!(e.metaKey || e.ctrlKey) || !e.shiftKey || e.key.toLowerCase() !== 'c') return;
    const active = document.activeElement?.closest?.('pre.code-block');
    const sel = window.getSelection?.();
    const selPre = sel?.anchorNode
      ? (sel.anchorNode.nodeType === 1 ? sel.anchorNode : sel.anchorNode.parentElement)
          ?.closest?.('pre.code-block')
      : null;
    const pre = active || selPre || hoveredPre;
    if (!pre) return;
    e.preventDefault();
    const btn = pre.querySelector('.code-copy-btn');
    copyCodeBlockText(pre).then(() => {
      if (btn) {
        const prev = btn.textContent;
        btn.textContent = 'Copied';
        setTimeout(() => { btn.textContent = prev; }, 1500);
      }
    }).catch(() => {
      const text = pre.querySelector('code')?.textContent || '';
      prompt('Copy code:', text);
    });
  });
}

/** Wire copy buttons injected by renderMarkdown into code fences. */
function wireCodeCopyButtons(root) {
  if (!root) return;
  root.querySelectorAll('pre.code-block .code-copy-btn').forEach((btn) => {
    if (btn.dataset.wired) return;
    btn.dataset.wired = '1';
    btn.addEventListener('click', (e) => {
      e.preventDefault();
      e.stopPropagation();
      const pre = btn.closest('pre.code-block');
      copyCodeBlockText(pre).then(() => {
        btn.textContent = 'Copied';
        setTimeout(() => { btn.textContent = 'Copy'; }, 1500);
      }).catch(() => {
        const text = pre?.querySelector('code')?.textContent || '';
        prompt('Copy code:', text);
      });
    });
  });
}

initCodeCopyHotkey();

/** Resize/compress image for vision API (keeps CLI args under limits). */
async function compressImageForVision(fileOrBlob, maxDim = 1280, quality = 0.82) {
  const blob = fileOrBlob instanceof Blob ? fileOrBlob : fileOrBlob;
  const bitmap = await createImageBitmap(blob);
  const scale = Math.min(1, maxDim / Math.max(bitmap.width, bitmap.height));
  const w = Math.max(1, Math.round(bitmap.width * scale));
  const h = Math.max(1, Math.round(bitmap.height * scale));
  const canvas = document.createElement('canvas');
  canvas.width = w;
  canvas.height = h;
  const ctx = canvas.getContext('2d');
  ctx.drawImage(bitmap, 0, 0, w, h);
  bitmap.close();
  const out = await new Promise((resolve) => {
    canvas.toBlob(resolve, 'image/jpeg', quality);
  });
  const buf = await out.arrayBuffer();
  const bytes = new Uint8Array(buf);
  let binary = '';
  for (let i = 0; i < bytes.length; i++) binary += String.fromCharCode(bytes[i]);
  return {
    data: btoa(binary),
    mime: 'image/jpeg',
    previewUrl: canvas.toDataURL('image/jpeg', quality),
  };
}

function fileToBase64(file) {
  return new Promise((resolve, reject) => {
    const reader = new FileReader();
    reader.onload = () => {
      const dataUrl = reader.result;
      const comma = String(dataUrl).indexOf(',');
      resolve({
        data: String(dataUrl).slice(comma + 1),
        mime: file.type || 'image/png',
        previewUrl: dataUrl,
      });
    };
    reader.onerror = reject;
    reader.readAsDataURL(file);
  });
}