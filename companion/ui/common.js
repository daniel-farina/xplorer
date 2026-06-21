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

/** Canonical toolbar markup lives in /toolbar.html (fetched live) and the
 *  shared behavior in /toolbar.js (window.XplorerToolbar). This is a minimal
 *  emergency fallback, used only if the live /toolbar.html fetch fails. */
const GROK_TOOLBAR_FALLBACK =
  '<header class="grok-toolbar">' +
  '<a class="grok-logo" href="/search" title="Xplorer home" aria-label="Xplorer">' +
  '<svg class="gi" viewBox="0 0 32 32" fill="none" stroke="currentColor" stroke-width="3.4" stroke-linecap="round" stroke-linejoin="round"><path d="M9.5 9.5 L22.5 22.5 M22.5 9.5 L9.5 22.5"/></svg>' +
  '<span>plorer</span></a>' +
  '<div class="grok-toolbar-spacer"></div>' +
  '</header>';

async function mountGrokToolbar({ pageHome, onSwitch } = {}) {
  // XPLORER: the toolbar is now a NATIVE browser-chrome bar (XplorerToolbarView)
  // present on every tab, so suppress the in-page web bar entirely — companion
  // pages (/search, /apps, ...) must not render a duplicate. No-op; callers and
  // helpers stay defined. Remove this early return to restore the web bar.
  return;
  // eslint-disable-next-line no-unreachable
  let html;
  try {
    const res = await fetch('/toolbar.html', { cache: 'no-store' });
    if (res.ok) {
      const inner = (await res.text()).trim();
      if (inner) html = `<header class="grok-toolbar">${inner}</header>`;
    }
  } catch {}
  if (!html) html = GROK_TOOLBAR_FALLBACK;
  const mount = document.getElementById('grok-toolbar-mount');
  const legacy = document.querySelector('header.grok-toolbar');
  if (mount) {
    mount.outerHTML = html;
  } else if (legacy) {
    legacy.outerHTML = html;
  } else {
    document.body.insertAdjacentHTML('afterbegin', html);
  }
  // Apply config-driven customization BEFORE wiring. applyToolbarConfig may
  // REBUILD the .grok-nav-pills DOM from the stored ordered array, so the pill
  // wirings below (syncCompanionToolbarPill / initSearchHomeToggle /
  // wireHideToggle) must target the rebuilt DOM. Companion origin is
  // same-origin, so hrefs need no gateway prefix (''). If the fetch fails or
  // there is no config, the static toolbar.html markup is left as-is (the
  // default 5 pills — current behavior, no regression).
  try {
    const barEl = document.querySelector('header.grok-toolbar');
    if (barEl && window.XplorerToolbar?.applyToolbarConfig) {
      const settings = await fetchSettings();
      if (settings?.toolbar) {
        XplorerToolbar.applyToolbarConfig(barEl, settings.toolbar, '');
      }
    }
  } catch { /* fetch failed → leave static markup, wire it as-is */ }
  syncCompanionToolbarPill();
  const path = (location.pathname || '').toLowerCase();
  const settingsBtn = document.querySelector('.grok-settings-btn');
  if (settingsBtn) {
    settingsBtn.classList.toggle('active', path.startsWith('/settings'));
  }
  initSearchHomeToggle(document.getElementById('home-toggle'), { pageHome, onSwitch });
  // Shared hide/show + draggable reveal handle (single source: toolbar.js).
  if (window.XplorerToolbar) {
    XplorerToolbar.wireHideToggle(
      document.querySelector('header.grok-toolbar, #xplorer-grok-bar.grok-toolbar'));
  }
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

// The native browser-chrome toolbar (xplorer::XplorerToolbarView) is now the
// single toolbar surface and covers companion pages too, so the in-page
// auto-mount is suppressed to avoid a double bar. mountGrokToolbar remains
// defined for explicit callers; we just no longer auto-call it on load.
// (Previously: DOMContentLoaded → mountGrokToolbar() when
//  document.body.dataset.grokToolbar === 'auto'.)

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
  // Pull fenced code blocks out first so their contents aren't reformatted.
  const codeBlocks = [];
  let s = String(raw).replace(/```(\w*)\n?([\s\S]*?)```/g, (_, lang, code) => {
    codeBlocks.push({ lang: lang || '', code });
    return `\x00CODE${codeBlocks.length - 1}\x00`;
  });
  s = escapeHtml(s);

  // Inline spans: citations, links, inline code, bold, italic, strikethrough.
  const inline = (t) => t
    .replace(/\[\[(\d+)\]\]\((https?:\/\/[^)\s]+)\)/g, (m, n, url) => {
      const h = safeUrl(url);
      return h ? `<a href="${h}" target="_blank" rel="noopener" class="cite">[${n}]</a>` : m;
    })
    .replace(/\[([^\]]+)\]\((https?:\/\/[^)\s]+)\)/g, (m, label, url) => {
      const h = safeUrl(url);
      return h ? `<a href="${h}" target="_blank" rel="noopener">${label}</a>` : m;
    })
    .replace(/`([^`\n]+)`/g, '<code>$1</code>')
    .replace(/\*\*([^*\n]+)\*\*/g, '<strong>$1</strong>')
    .replace(/(^|[^*\w])\*([^*\n]+?)\*(?!\w)/g, '$1<em>$2</em>')
    .replace(/(^|\s)_([^_\n]+?)_(?=\s|$|[.,!?;:])/g, '$1<em>$2</em>')
    .replace(/~~([^~\n]+)~~/g, '<del>$1</del>');

  // Block-level: parse line by line (headings, rules, quotes, lists, paragraphs).
  const out = [];
  let list = null;
  let para = [];
  const flushPara = () => { if (para.length) { out.push(`<p>${inline(para.join(' '))}</p>`); para = []; } };
  const closeList = () => { if (list) { out.push(`</${list}>`); list = null; } };
  const splitRow = (row) =>
    row.replace(/^\s*\|/, '').replace(/\|\s*$/, '').split('|').map((cell) => inline(cell.trim()));
  const lines = s.split('\n');
  for (let i = 0; i < lines.length; i++) {
    const t = lines[i].trim();
    let m;
    if (!t) { flushPara(); closeList(); continue; }
    if (/^\x00CODE\d+\x00$/.test(t)) { flushPara(); closeList(); out.push(t); continue; }
    // Table: a "| ... |" header row immediately followed by a "|---|---|" rule.
    if (/^\|.*\|$/.test(t) && i + 1 < lines.length &&
        /^\|(\s*:?-+:?\s*\|)+$/.test(lines[i + 1].trim())) {
      flushPara(); closeList();
      const head = splitRow(t).map((c) => `<th>${c}</th>`).join('');
      i++; // consume the separator row
      const rows = [];
      while (i + 1 < lines.length && /^\|.*\|$/.test(lines[i + 1].trim())) {
        i++;
        rows.push(`<tr>${splitRow(lines[i].trim()).map((c) => `<td>${c}</td>`).join('')}</tr>`);
      }
      out.push(`<table><thead><tr>${head}</tr></thead><tbody>${rows.join('')}</tbody></table>`);
      continue;
    }
    if ((m = t.match(/^(#{1,6})\s+(.+)$/))) { flushPara(); closeList(); out.push(`<h${m[1].length}>${inline(m[2])}</h${m[1].length}>`); continue; }
    if (/^(-{3,}|\*{3,}|_{3,})$/.test(t)) { flushPara(); closeList(); out.push('<hr>'); continue; }
    if ((m = t.match(/^&gt;\s?(.*)$/))) { flushPara(); closeList(); out.push(`<blockquote>${inline(m[1])}</blockquote>`); continue; }
    if ((m = t.match(/^[-*+]\s+(.+)$/))) { flushPara(); if (list !== 'ul') { closeList(); out.push('<ul>'); list = 'ul'; } out.push(`<li>${inline(m[1])}</li>`); continue; }
    if ((m = t.match(/^\d+[.)]\s+(.+)$/))) { flushPara(); if (list !== 'ol') { closeList(); out.push('<ol>'); list = 'ol'; } out.push(`<li>${inline(m[1])}</li>`); continue; }
    para.push(t);
  }
  flushPara(); closeList();
  s = out.join('\n');

  // Restore fenced code blocks (highlighted, with copy button).
  s = s.replace(/\x00CODE(\d+)\x00/g, (_, n) => {
    const block = codeBlocks[Number(n)];
    if (!block) return '';
    const lang = block.lang ? ` language-${escapeHtml(block.lang)}` : '';
    const body = highlightCodeLight(block.code.trim(), block.lang);
    return `<pre class="code-block"><button type="button" class="code-copy-btn" title="Copy code (⌘⇧C)">Copy</button><code class="${lang.trim()}">${body}</code></pre>`;
  });
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

// --- Grok Build install check + splash -------------------------------------
// Building apps shells out to the `grok` CLI ("Grok Build"). If it isn't
// installed, guide the user to set it up instead of failing mid-build.
async function fetchGrokStatus() {
  try {
    const r = await fetch('/api/grok/status', { cache: 'no-store' });
    if (!r.ok) return { installed: false };
    return await r.json();
  } catch {
    return { installed: false };
  }
}

function ensureGrokSplashStyles() {
  if (document.getElementById('grok-splash-styles')) return;
  const s = document.createElement('style');
  s.id = 'grok-splash-styles';
  s.textContent = `
  .grok-splash-overlay{position:fixed;inset:0;z-index:2147483600;display:flex;
    align-items:center;justify-content:center;padding:24px;
    background:rgba(0,0,0,.55);backdrop-filter:blur(4px);}
  .grok-splash-overlay.hidden{display:none;}
  .grok-splash{max-width:520px;width:100%;background:var(--surface,#111);
    color:var(--text,#fff);border:1px solid var(--border,#2f2f2f);border-radius:16px;
    padding:28px 28px 24px;box-shadow:0 24px 60px var(--shadow,rgba(0,0,0,.5));
    font:14px/1.5 -apple-system,system-ui,'Segoe UI',sans-serif;}
  .grok-splash__icon{width:48px;height:48px;border-radius:12px;
    background:var(--accent-soft,rgba(255,255,255,.1));display:flex;align-items:center;
    justify-content:center;font-size:24px;margin-bottom:16px;}
  .grok-splash h2{margin:0 0 8px;font-size:20px;font-weight:650;}
  .grok-splash p{margin:0 0 16px;color:var(--text-secondary,#a3a3a3);}
  .grok-splash ol{margin:0 0 18px;padding-left:20px;}
  .grok-splash ol li{margin:0 0 10px;}
  .grok-splash code{background:var(--elevated,#1a1a1a);border:1px solid var(--border,#2f2f2f);
    border-radius:6px;padding:2px 7px;font:12.5px ui-monospace,Menlo,Consolas,monospace;user-select:all;}
  .grok-splash__row{display:flex;gap:10px;align-items:center;flex-wrap:wrap;margin-top:4px;}
  .grok-splash__btn{appearance:none;cursor:pointer;border-radius:9px;padding:9px 16px;
    font-size:13.5px;font-weight:600;border:1px solid var(--border,#2f2f2f);text-decoration:none;}
  .grok-splash__btn.primary{background:var(--button-bg,#fff);color:var(--button-text,#000);border-color:transparent;}
  .grok-splash__btn.ghost{background:transparent;color:var(--text,#fff);}
  .grok-splash__note{margin-top:16px;font-size:12px;color:var(--text-secondary,#a3a3a3);}
  .grok-splash__status{margin-top:12px;font-size:12.5px;min-height:16px;}
  .grok-splash__status.err{color:#ff7a7a;}
  `;
  document.head.appendChild(s);
}

// Show the "install Grok Build" splash. Recheck re-runs the check and hides on
// success (firing a 'grok-ready' event on the overlay).
function showGrokInstallSplash() {
  ensureGrokSplashStyles();
  let overlay = document.getElementById('grok-splash');
  if (!overlay) {
    overlay = document.createElement('div');
    overlay.id = 'grok-splash';
    overlay.className = 'grok-splash-overlay';
    overlay.innerHTML = `
      <div class="grok-splash" role="dialog" aria-modal="true" aria-labelledby="grok-splash-title">
        <div class="grok-splash__icon">✦</div>
        <h2 id="grok-splash-title">Set up Grok Build</h2>
        <p>Building apps runs <strong>Grok Build</strong> — the <code>grok</code> command-line tool — which isn't installed or on your PATH yet.</p>
        <ol>
          <li>Install the <strong>Grok CLI</strong> and make sure <code>grok</code> runs in your terminal.</li>
          <li>Sign in: <code>grok login</code></li>
          <li>Come back and click <strong>Recheck</strong>.</li>
        </ol>
        <div class="grok-splash__row">
          <button type="button" class="grok-splash__btn primary" data-grok-recheck>Recheck</button>
          <a class="grok-splash__btn ghost" href="https://grok.com/" target="_blank" rel="noopener">Get Grok</a>
          <button type="button" class="grok-splash__btn ghost" data-grok-dismiss>Not now</button>
        </div>
        <div class="grok-splash__status" data-grok-status></div>
        <p class="grok-splash__note">Already installed elsewhere? Set its full path as <code>grok_bin</code> in <code>~/.xplorer/companion.json</code>, or the <code>GROK_BIN</code> environment variable.</p>
      </div>`;
    document.body.appendChild(overlay);
    overlay.querySelector('[data-grok-dismiss]').onclick = () => overlay.classList.add('hidden');
    overlay.querySelector('[data-grok-recheck]').onclick = async () => {
      const statusEl = overlay.querySelector('[data-grok-status]');
      const btn = overlay.querySelector('[data-grok-recheck]');
      btn.disabled = true;
      statusEl.className = 'grok-splash__status';
      statusEl.textContent = 'Checking…';
      const s = await fetchGrokStatus();
      btn.disabled = false;
      if (s.installed) {
        statusEl.textContent = 'Grok Build detected ✓';
        overlay.dispatchEvent(new CustomEvent('grok-ready'));
        setTimeout(() => overlay.classList.add('hidden'), 700);
      } else {
        statusEl.className = 'grok-splash__status err';
        statusEl.textContent = 'Still not detected — make sure `grok` runs in a terminal, then retry.';
      }
    };
  }
  overlay.classList.remove('hidden');
  return overlay;
}

// Gate app-building actions: resolves true if grok is installed; otherwise
// shows the install splash and resolves false.
async function ensureGrokReady() {
  const status = await fetchGrokStatus();
  if (status.installed) return true;
  showGrokInstallSplash();
  return false;
}