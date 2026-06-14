const $ = (s) => document.querySelector(s);
const form = $('#search-form');
const input = $('#q');
const submitBtn = form?.querySelector('button[type="submit"]');
const imageTools = $('#image-tools');
const imageFile = $('#image-file');
const imagePreview = $('#image-preview');
const previewImg = $('#preview-img');
const hero = $('#hero');
const resultsEl = $('#results');
const modesEl = $('#search-modes');
const mainEl = document.querySelector('.main');

const SEARCH_MODE_KEY = 'xplorer_search_mode';
let mode = 'web';
let attachedImage = null;

const MODE_LABELS = {
  web: 'Web',
  images: 'Images',
  videos: 'Videos',
  imagine: 'Imagine',
};

function syncUrl() {
  const q = input.value.trim();
  const params = new URLSearchParams();
  if (mode !== 'web') params.set('mode', mode);
  if (q) params.set('q', q);
  const qs = params.toString();
  const next = `${location.pathname}${qs ? `?${qs}` : ''}`;
  if (location.pathname + location.search !== next) {
    history.replaceState(null, '', next);
  }
}

function setMode(next) {
  mode = next;
  modesEl?.querySelectorAll('[data-mode]').forEach((btn) => {
    btn.classList.toggle('active', btn.dataset.mode === mode);
  });
  updateModeUi();
  syncUrl();
  try {
    localStorage.setItem(SEARCH_MODE_KEY, mode);
  } catch { /* ignore */ }
}

function updateModeUi() {
  const isImages = mode === 'images';
  imageTools?.classList.toggle('hidden', !isImages);
  input.placeholder =
    mode === 'imagine' ? 'Describe an image to generate…' :
    mode === 'videos' ? 'Search videos with Grok…' :
    mode === 'images' ? 'Search or attach an image…' :
    'Search with Grok…';
}

function formatGrokWebQuery(query) {
  const q = query.trim();
  if (mode === 'videos') return `Search for videos: ${q}`;
  if (mode === 'images') return `Search for images: ${q}`;
  if (mode === 'imagine') return `Generate an image: ${q}`;
  return q;
}

function usesNativeSearch() {
  return !!attachedImage || mode === 'imagine' || mode === 'videos';
}

async function openGrokWebQuery(query) {
  const prompt = formatGrokWebQuery(query);
  if (submitBtn) submitBtn.disabled = true;
  try {
    const res = await fetch('/api/page/grok-web', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ query: prompt }),
    });
    const data = await res.json();
    if (!res.ok) throw new Error(data.error || res.statusText);
    if (!data.grok_url) throw new Error('missing grok_url');
    window.open(data.grok_url, '_blank');
  } catch (err) {
    alert(`Grok Web failed: ${err.message}`);
  } finally {
    if (submitBtn) submitBtn.disabled = false;
  }
}

function showResultsLoading() {
  hero?.classList.add('hidden');
  mainEl?.classList.add('has-results');
  if (resultsEl) {
    resultsEl.classList.remove('hidden');
    resultsEl.innerHTML = '<div class="thinking">Grok is searching…</div>';
  }
}

function cardThumb(item) {
  if (item.thumbnail) return item.thumbnail;
  if (item.image) return item.image;
  return '';
}

function renderMediaGrid(items, kind) {
  if (!items?.length) return '';
  const label = kind === 'video' ? 'Videos' : kind === 'image' ? 'Images' : 'Results';
  const cards = items.map((item) => {
    const url = escapeHtml(item.url || '#');
    const title = escapeHtml(item.title || item.url || '');
    const thumb = cardThumb(item);
    const cls = kind === 'video' ? 'video-card' : kind === 'image' ? 'image-card' : 'link-card';
    const thumbHtml = thumb
      ? `<img class="card-thumb" src="${escapeHtml(thumb)}" alt="" loading="lazy">`
      : `<div class="card-thumb placeholder">${kind === 'video' ? '▶' : '◇'}</div>`;
    return `<a class="media-card ${cls}" href="${url}" target="_blank" rel="noopener noreferrer">
      ${thumbHtml}
      <div class="card-body"><span class="card-provider">${escapeHtml(item.provider || '')}</span>
      <h3 class="card-title">${title}</h3></div></a>`;
  }).join('');
  return `<div class="results-grid-panel">
    <div class="results-grid-label">${label}<span class="count">${items.length}</span></div>
    <div class="results-grid">${cards}</div></div>`;
}

function renderSearchResults(query, data) {
  if (!resultsEl) return;
  const answer = data.answer || data.text || '';
  let html = '';

  if (attachedImage?.previewUrl) {
    html += `<div class="query-image"><img src="${attachedImage.previewUrl}" alt="Query image"></div>`;
  }

  if (data.mode === 'imagine' && data.images?.length) {
    html += `<div class="result-card"><h3>Imagine: ${escapeHtml(query)}</h3>
      <div class="imagine-grid">${data.images.map((u) =>
        `<a href="${escapeHtml(u)}" target="_blank" rel="noopener"><img src="${escapeHtml(u)}" alt="generated"></a>`
      ).join('')}</div></div>`;
    resultsEl.innerHTML = html;
    return;
  }

  if (answer) {
    html += `<div class="result-card result-body markdown">${renderMarkdown(answer)}</div>`;
  }

  html += renderMediaGrid(data.videos, 'video');
  html += renderMediaGrid(data.images, 'image');
  html += renderMediaGrid(data.links, 'link');

  if (!html) {
    html = '<div class="result-card"><p>No results returned. Try rephrasing your query.</p></div>';
  }
  resultsEl.innerHTML = html;

  const grokBtn = document.createElement('button');
  grokBtn.type = 'button';
  grokBtn.className = 'open-grok-web';
  grokBtn.textContent = 'Continue in Grok Web →';
  grokBtn.onclick = () => openGrokWebQuery(query);
  resultsEl.appendChild(grokBtn);
}

function createSearchStreamUi() {
  if (!resultsEl) return null;
  resultsEl.innerHTML = '';
  const wrap = document.createElement('div');
  wrap.className = 'stream-block streaming';
  wrap.innerHTML = `
    <details class="thinking-panel" open>
      <summary>✦ Grok is searching</summary>
      <div class="thinking-text"></div>
    </details>
    <div class="answer-panel hidden">
      <div class="answer result-body markdown"></div>
    </div>`;
  resultsEl.appendChild(wrap);
  return {
    wrap,
    thinkingPanel: wrap.querySelector('.thinking-panel'),
    thinkingText: wrap.querySelector('.thinking-text'),
    answerPanel: wrap.querySelector('.answer-panel'),
    answerEl: wrap.querySelector('.answer'),
  };
}

async function runNativeSearch(query) {
  showResultsLoading();
  if (submitBtn) submitBtn.disabled = true;
  const body = { query, mode: attachedImage ? 'images' : mode, stream: true };
  if (attachedImage) {
    body.image = attachedImage.data;
    body.image_mime = attachedImage.mime || 'image/jpeg';
  }
  const ui = createSearchStreamUi();
  let thinkingText = '';
  let answerText = '';
  let resultData = null;
  try {
    const res = await fetch('/api/search/stream', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(body),
    });
    if (!res.ok || !res.body) {
      const err = await res.json().catch(() => ({}));
      throw new Error(err.error || res.statusText);
    }
    const reader = res.body.getReader();
    const decoder = new TextDecoder();
    let buffer = '';
    while (true) {
      const { done, value } = await reader.read();
      if (done) break;
      buffer += decoder.decode(value, { stream: true });
      let idx;
      while ((idx = buffer.indexOf('\n')) >= 0) {
        const line = buffer.slice(0, idx).trim();
        buffer = buffer.slice(idx + 1);
        const evt = parseStreamLine(line);
        if (!evt) continue;
        if (evt.type === 'thought') {
          thinkingText += evt.data || '';
          if (ui) ui.thinkingText.textContent = thinkingText;
        } else if (evt.type === 'text') {
          if (ui) ui.answerPanel.classList.remove('hidden');
          answerText += evt.data || '';
          if (ui) ui.answerEl.innerHTML = renderMarkdown(answerText);
        } else if (evt.type === 'result') {
          resultData = evt;
        } else if (evt.type === 'error') {
          throw new Error(evt.error || 'search failed');
        }
      }
    }
    if (resultData) {
      renderSearchResults(query, resultData);
    } else if (answerText) {
      renderSearchResults(query, { answer: answerText, mode: body.mode, text: answerText });
    } else {
      throw new Error('Search returned no results');
    }
  } catch (err) {
    if (resultsEl) {
      resultsEl.innerHTML = `<div class="result-card error"><p>${escapeHtml(err.message)}</p></div>`;
    }
  } finally {
    if (submitBtn) submitBtn.disabled = false;
  }
}

async function setAttachedImage(img, source) {
  attachedImage = img ? { ...img, source } : null;
  if (attachedImage?.previewUrl) {
    previewImg.src = attachedImage.previewUrl;
    imagePreview?.classList.remove('hidden');
  } else {
    imagePreview?.classList.add('hidden');
    previewImg.removeAttribute('src');
  }
}

$('#upload-image')?.addEventListener('click', () => imageFile?.click());

imageFile?.addEventListener('change', async () => {
  const file = imageFile.files?.[0];
  if (!file) return;
  try {
    const compressed = await compressImageForVision(file);
    await setAttachedImage(compressed, 'upload');
    if (mode !== 'images') setMode('images');
    if (!input.value.trim()) input.placeholder = 'Ask about this image…';
  } catch (e) {
    alert(`Could not load image: ${e.message}`);
  }
  imageFile.value = '';
});

$('#screenshot-page')?.addEventListener('click', async () => {
  const btn = $('#screenshot-page');
  if (btn) btn.disabled = true;
  try {
    const res = await fetch('/api/screenshot', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: '{}',
    });
    const data = await res.json();
    if (!res.ok) throw new Error(data.error || res.statusText);
    const blob = await (await fetch(`data:image/png;base64,${data.image}`)).blob();
    const compressed = await compressImageForVision(blob);
    await setAttachedImage(compressed, 'screenshot');
    if (mode !== 'images') setMode('images');
    if (data.title && !input.value.trim()) {
      input.placeholder = `Ask about: ${data.title}`;
    }
  } catch (e) {
    alert(`Screenshot failed: ${e.message}`);
  } finally {
    if (btn) btn.disabled = false;
  }
});

$('#clear-image')?.addEventListener('click', () => setAttachedImage(null));

document.addEventListener('paste', async (e) => {
  if (mode !== 'images') return;
  const items = e.clipboardData?.items;
  if (!items) return;
  for (const item of items) {
    if (!item.type.startsWith('image/')) continue;
    e.preventDefault();
    const file = item.getAsFile();
    if (!file) continue;
    const compressed = await compressImageForVision(file);
    await setAttachedImage(compressed, 'paste');
    break;
  }
});

form?.addEventListener('dragover', (e) => {
  if (mode === 'images') e.preventDefault();
});

form?.addEventListener('drop', async (e) => {
  if (mode !== 'images') return;
  const file = e.dataTransfer?.files?.[0];
  if (!file || !file.type.startsWith('image/')) return;
  e.preventDefault();
  const compressed = await compressImageForVision(file);
  await setAttachedImage(compressed, 'drop');
});

modesEl?.querySelectorAll('[data-mode]').forEach((btn) => {
  btn.addEventListener('click', () => setMode(btn.dataset.mode));
});

form?.addEventListener('submit', async (e) => {
  e.preventDefault();
  const q = input.value.trim();
  if (!q && !attachedImage) return;
  const prompt = q || 'Describe this image and find similar images';
  syncUrl();
  if (usesNativeSearch()) {
    await runNativeSearch(prompt);
    return;
  }
  await openGrokWebQuery(prompt);
});

input?.addEventListener('input', () => syncUrl());

initSearchHomeToggle($('#home-toggle'), {
  onSwitch: (saved, updated) => {
    const params = new URLSearchParams();
    const q = input.value.trim();
    if (q) params.set('q', q);
    if (mode !== 'web') params.set('mode', mode);
    const suffix = params.toString() ? `?${params.toString()}` : '';
    if (saved === SEARCH_HOME_BUILD) {
      window.location.href = `/search${suffix}`;
    } else if (saved === SEARCH_HOME_WIKI) {
      window.location.href = updated?.grok_wiki_url || 'https://grokipedia.com/';
    } else {
      window.location.href = updated?.grok_web_url || 'https://grok.com/';
    }
  },
});

startThemeWatcher();
updateModeUi();

const urlParams = new URLSearchParams(location.search);
const modeParam = urlParams.get('mode');
if (modeParam && ['web', 'images', 'videos', 'imagine'].includes(modeParam)) {
  setMode(modeParam);
} else {
  try {
    const saved = localStorage.getItem(SEARCH_MODE_KEY);
    if (saved && ['web', 'images', 'videos', 'imagine'].includes(saved)) {
      setMode(saved);
    }
  } catch { /* ignore */ }
}
if (urlParams.get('q')) {
  input.value = urlParams.get('q');
  form.requestSubmit();
}