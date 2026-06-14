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
    return localStorage.getItem(SEARCH_HOME_STORAGE_KEY) || SEARCH_HOME_BUILD;
  } catch {
    return SEARCH_HOME_BUILD;
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
    return ['web', 'images', 'videos', 'imagine'].includes(m) ? m : 'web';
  } catch {
    return 'web';
  }
}

function persistSearchMode(mode) {
  try {
    if (['web', 'images', 'videos', 'imagine'].includes(mode)) {
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
  return '';
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
}

/** Wire Grok Build / Grok Web / Groki home toggle. */
async function initSearchHomeToggle(container, { onSwitch, pageHome } = {}) {
  if (!container) return;
  const buttons = container.querySelectorAll('[data-home]');
  let settings;
  try {
    settings = await fetchSettings();
  } catch {
    settings = { search_home: getStoredSearchHome() };
  }
  let currentHome = settings.search_home || SEARCH_HOME_BUILD;
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
    btn.addEventListener('click', async () => {
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
          const dest = await grokWebUrlForQuery(q, m, updated.grok_web_url || 'https://grok.com/');
          window.location.href = dest;
        } else if (saved === SEARCH_HOME_WIKI) {
          const params = new URLSearchParams(window.location.search);
          const q = params.get('q') || getStoredSearchQuery();
          window.location.href = wikiUrlForQuery(
            q,
            updated.grok_wiki_url || 'https://grokipedia.com/',
          );
        } else {
          const params = new URLSearchParams(window.location.search);
          let url = `${window.location.origin}/switch-home?mode=${encodeURIComponent(saved)}`;
          const q = params.get('q') || getStoredSearchQuery();
          const m = params.get('mode') || getStoredSearchMode();
          if (q) url += `&q=${encodeURIComponent(q)}`;
          if (m && m !== 'web') url += `&m=${encodeURIComponent(m)}`;
          window.location.href = url;
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