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
  persistSearchMode(mode);
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
  return !!attachedImage || mode === 'imagine' || mode === 'videos' || mode === 'web';
}

function enrichSearchResults(data) {
  const out = { ...data, mode: data.mode || mode };
  const text = out.answer || out.text || '';
  if (!text) return out;
  if (out.mode === 'videos' && !out.videos?.length)
    out.videos = extractResultsFromText(text, 'videos');
  if (out.mode === 'images' && !out.images?.length)
    out.images = extractResultsFromText(text, 'images');
  if (out.mode === 'web' && !out.links?.length)
    out.links = extractResultsFromText(text, 'web');
  return out;
}

function renderEmptyState(kind) {
  const msg = kind === 'video'
    ? 'No videos with thumbnails yet. Try broader keywords or continue in Grok Web.'
    : kind === 'image'
      ? 'No images returned. Try rephrasing or attach an image for vision search.'
      : 'No links extracted yet. The answer above may still help — open Grok Web for more sources.';
  return `<div class="result-card empty-state"><p>${msg}</p></div>`;
}

async function openGrokWebQuery(query) {
  if (submitBtn) submitBtn.disabled = true;
  try {
    let dest;
    if (mode === 'imagine') {
      dest = await imagineUrlForQuery(query, 'https://grok.com/imagine');
    } else {
      const prompt = formatGrokWebQuery(query);
      const res = await fetch('/api/page/grok-web', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ query: prompt }),
      });
      const data = await res.json();
      if (!res.ok) throw new Error(data.error || res.statusText);
      if (!data.grok_url) throw new Error('missing grok_url');
      dest = data.grok_url;
    }
    window.open(dest, '_blank');
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
  data = enrichSearchResults(data);
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

  const videoGrid = renderMediaGrid(data.videos, 'video');
  const imageGrid = renderMediaGrid(data.images, 'image');
  const linkGrid = renderMediaGrid(data.links, 'link');
  html += videoGrid;
  html += imageGrid;
  html += linkGrid;

  const activeMode = data.mode || mode;
  if (activeMode === 'videos' && !data.videos?.length && !videoGrid) {
    html += renderEmptyState('video');
  } else if (activeMode === 'images' && !data.images?.length && !imageGrid) {
    html += renderEmptyState('image');
  } else if (activeMode === 'web' && !data.links?.length && !linkGrid && !answer) {
    html += renderEmptyState('link');
  }

  if (!html) {
    html = '<div class="result-card"><p>No results returned. Try rephrasing your query.</p></div>';
  }
  resultsEl.innerHTML = html;
  wireCodeCopyButtons(resultsEl);

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
          if (ui) {
            ui.answerEl.innerHTML = renderMarkdown(answerText);
            wireCodeCopyButtons(ui.answerEl);
          }
        } else if (evt.type === 'result') {
          resultData = evt;
        } else if (evt.type === 'error') {
          throw new Error(evt.error || 'search failed');
        }
      }
    }
    if (resultData) {
      renderSearchResults(query, { ...resultData, mode: body.mode });
    } else if (answerText) {
      renderSearchResults(query, { answer: answerText, mode: body.mode, text: answerText });
    } else {
      throw new Error('Search returned no results');
    }
  } catch (err) {
    if (resultsEl) {
      resultsEl.innerHTML = `<div class="result-card error"><p>${escapeHtml(err.message)}</p>
        <button type="button" class="open-grok-web">Try Grok Web instead →</button></div>`;
      resultsEl.querySelector('.open-grok-web')?.addEventListener('click', () => {
        openGrokWebQuery(query);
      });
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
  persistSearchQuery(q);
  syncUrl();
  if (usesNativeSearch()) {
    await runNativeSearch(prompt);
    return;
  }
  await openGrokWebQuery(prompt);
});

input?.addEventListener('input', () => {
  persistSearchQuery(input.value.trim());
  syncUrl();
});

initSearchHomeToggle($('#home-toggle'), {
  onSwitch: async (saved, updated) => {
    const params = new URLSearchParams();
    const q = input.value.trim();
    if (q) params.set('q', q);
    if (mode !== 'web') params.set('mode', mode);
    const suffix = params.toString() ? `?${params.toString()}` : '';
    if (saved === SEARCH_HOME_BUILD) {
      window.location.href = `/search${suffix}`;
    } else if (saved === SEARCH_HOME_WIKI) {
      window.location.href = wikiUrlForQuery(
        q || getStoredSearchQuery(),
        updated?.grok_wiki_url || 'https://grokipedia.com/',
      );
    } else {
      const query = q || getStoredSearchQuery();
      const dest = mode === 'imagine'
        ? await imagineUrlForQuery(query, 'https://grok.com/imagine')
        : await grokWebUrlForQuery(
          query,
          mode,
          updated?.grok_web_url || 'https://grok.com/',
        );
      window.location.href = dest;
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
  setMode(getStoredSearchMode());
}
const urlQuery = urlParams.get('q');
const initialQuery = urlQuery || getStoredSearchQuery();
if (initialQuery) {
  input.value = initialQuery;
  syncUrl();
}
if (urlQuery) {
  form.requestSubmit();
}