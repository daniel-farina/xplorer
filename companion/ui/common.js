/** Shared utilities for Grok companion UI (search + chat). */

const DEFAULT_MODEL = 'grok-composer-2.5-fast';
/** Web/video search needs grok-build (Composer has no web search tools). */
const SEARCH_DEFAULT_MODEL = 'grok-build';
const MODEL_STORAGE_KEY = 'grok_model';
const SEARCH_MODEL_STORAGE_KEY = 'grok_search_model';

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