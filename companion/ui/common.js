/** Shared utilities for Grok companion UI (search + chat). */

const DEFAULT_MODEL = 'grok-composer-2.5-fast';
/** Web/video search needs grok-build (Composer has no web search tools). */
const SEARCH_DEFAULT_MODEL = 'grok-build';
const MODEL_STORAGE_KEY = 'grok_model';
const SEARCH_MODEL_STORAGE_KEY = 'grok_search_model';
const SEARCH_HOME_STORAGE_KEY = 'grok_search_home';
const SEARCH_HOME_BUILD = 'build';
const SEARCH_HOME_WEB = 'web';

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

/** Wire Grok Web / Grok Build segmented toggle. */
async function initSearchHomeToggle(container, { onSwitch } = {}) {
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
  setActive(currentHome);

  buttons.forEach((btn) => {
    btn.addEventListener('click', async () => {
      const mode = btn.dataset.home;
      if (!mode || mode === currentHome) return;
      try {
        const updated = await saveSettings({ search_home: mode });
        const saved = updated.search_home || mode;
        persistSearchHome(saved);
        setActive(saved);
        if (onSwitch) onSwitch(saved, updated);
        else if (saved === SEARCH_HOME_WEB) {
          window.location.href = updated.grok_web_url || 'https://grok.com/';
        } else {
          window.location.href = updated.grok_build_url || '/search';
        }
      } catch (e) {
        alert(`Could not save preference: ${e.message}`);
      }
    });
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

/** Apply browser theme from AgentGateway GET /theme */
async function syncBrowserTheme() {
  let scheme = 'system';
  try {
    const res = await fetch('/api/theme');
    if (res.ok) {
      const data = await res.json();
      scheme = data.color_scheme || 'system';
    }
  } catch { /* use system */ }

  if (scheme === 'dark') {
    document.documentElement.setAttribute('data-theme', 'dark');
  } else if (scheme === 'light') {
    document.documentElement.setAttribute('data-theme', 'light');
  } else {
    document.documentElement.removeAttribute('data-theme');
  }
}

function startThemeWatcher(intervalMs = 5000) {
  syncBrowserTheme();
  setInterval(syncBrowserTheme, intervalMs);
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

/** Lightweight markdown → HTML (escaped input). */
function renderMarkdown(raw) {
  if (!raw) return '';
  let s = escapeHtml(raw);

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