const $ = (s) => document.querySelector(s);
const form = $('#search-form');
const input = $('#q');
const results = $('#results');
const hero = $('#hero');
const modes = $('#modes');
const modelSelect = $('#model-select');
const modelBadge = $('#model-badge');
const submitBtn = form?.querySelector('button[type="submit"]');
const imageTools = $('#image-tools');
const imageFile = $('#image-file');
const imagePreview = $('#image-preview');
const previewImg = $('#preview-img');

let mode = 'web';
let models = [];
let activeModel = getStoredSearchModel();
let attachedImage = null; // { data, mime, previewUrl, source }

function updateModeUi() {
  const isImages = mode === 'images';
  imageTools?.classList.toggle('hidden', !isImages);
  input.placeholder =
    mode === 'imagine' ? 'Describe an image to generate…' :
    mode === 'videos' ? 'Search videos with Grok…' :
    mode === 'images' ? 'Search with Image Search' :
    'Search with Grok…';
}

function applyModeModel() {
  const next = modelForSearchMode(mode, activeModel, models);
  if (next !== activeModel) {
    activeModel = next;
    populateModelSelect(modelSelect, models, activeModel);
    updateModelBadge(modelBadge, activeModel, modelLabel(activeModel, models));
  }
}

modes?.querySelectorAll('.mode').forEach((btn) => {
  btn.onclick = () => {
    modes.querySelectorAll('.mode').forEach((b) => b.classList.remove('active'));
    btn.classList.add('active');
    mode = btn.dataset.mode;
    updateModeUi();
    applyModeModel();
  };
});

$('#open-sidebar')?.addEventListener('click', () => { window.location.href = '/'; });

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
    if (mode === 'images' && !input.value.trim()) {
      input.placeholder = 'Ask about this image…';
    }
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

async function initModels() {
  models = await fetchModels();
  activeModel = modelForSearchMode(mode, activeModel, models);
  if (!models.some((m) => m.id === activeModel)) {
    activeModel = models.find((m) => m.id === SEARCH_DEFAULT_MODEL)?.id
      || models[0]?.id || SEARCH_DEFAULT_MODEL;
  }
  populateModelSelect(modelSelect, models, activeModel);
  updateModelBadge(modelBadge, activeModel, modelLabel(activeModel, models));
}

modelSelect?.addEventListener('change', async () => {
  activeModel = modelSelect.value;
  persistSearchModel(activeModel);
  updateModelBadge(modelBadge, activeModel, modelLabel(activeModel, models));
  try {
    await fetch('/api/settings', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ model: activeModel }),
    });
  } catch { /* local preference still applies */ }
});

form?.addEventListener('submit', async (e) => {
  e.preventDefault();
  const q = input.value.trim();
  if (!q && !attachedImage) return;
  hero.classList.add('hidden');
  results.classList.remove('hidden');
  await streamSearch(q, mode);
});

function buildStreamingShell(hasImage) {
  const preview = hasImage && attachedImage?.previewUrl
    ? `<div class="query-image"><img src="${attachedImage.previewUrl}" alt="Query image"></div>`
    : '';
  results.innerHTML = `
    ${preview}
    <div class="result-card streaming">
      <details class="thinking-panel" open>
        <summary>✦ Thinking</summary>
        <div class="thinking-text">${hasImage ? 'Grok is analyzing the image…' : 'Grok is searching…'}</div>
      </details>
      <div class="answer-panel hidden">
        <div class="answer-label">Answer</div>
        <div class="answer markdown"></div>
      </div>
      <div class="sources-panel hidden">
        <div class="sources-label">Sources</div>
        <div class="sources-list"></div>
      </div>
    </div>`;
  return {
    thinkingPanel: results.querySelector('.thinking-panel'),
    thinkingText: results.querySelector('.thinking-text'),
    answerPanel: results.querySelector('.answer-panel'),
    answerEl: results.querySelector('.answer'),
    sourcesPanel: results.querySelector('.sources-panel'),
    sourcesList: results.querySelector('.sources-list'),
  };
}

function renderSourceLinks(container, links) {
  if (!container || !links?.length) return;
  container.innerHTML = links.map((l) =>
    `<a class="source-link" href="${escapeHtml(l.url)}" target="_blank" rel="noopener">
      <span class="source-title">${escapeHtml(l.title || l.url)}</span>
      <span class="source-url">${escapeHtml(l.url)}</span>
    </a>`,
  ).join('');
}

async function streamSearch(query, searchMode) {
  const hasImage = !!attachedImage?.data;
  const ui = buildStreamingShell(hasImage);
  let thinkingText = '';
  let answerText = '';
  let links = [];
  let images = [];
  const searchModel = modelForSearchMode(searchMode, activeModel, models);
  let streamModel = searchModel;
  let streamModelLabel = modelLabel(searchModel, models);
  let sawThought = false;

  if (submitBtn) submitBtn.disabled = true;
  const body = {
    query: query || (hasImage ? 'Describe this image and find similar images' : ''),
    mode: searchMode,
    model: searchModel,
  };
  if (hasImage) {
    body.image = attachedImage.data;
    body.image_mime = attachedImage.mime || 'image/jpeg';
    body.image_source = attachedImage.source || 'upload';
  }

  try {
    const res = await fetch('/api/search/stream', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(body),
    });
    if (!res.ok) {
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
        if (!line) continue;

        const evt = parseStreamLine(line);
        if (!evt) continue;
        if (evt.type === 'meta') {
          if (evt.model) streamModel = evt.model;
          if (evt.model_label) streamModelLabel = evt.model_label;
          updateModelBadge(modelBadge, streamModel, streamModelLabel);
        } else if (evt.type === 'thought') {
          sawThought = true;
          thinkingText += evt.data || '';
          ui.thinkingText.textContent = thinkingText;
        } else if (evt.type === 'text') {
          if (!answerText) {
            ui.answerPanel.classList.remove('hidden');
            if (sawThought) ui.thinkingPanel.removeAttribute('open');
          }
          answerText += evt.data || '';
          ui.answerEl.innerHTML = renderMarkdown(answerText);
          const inlineLinks = extractMarkdownLinks(answerText);
          if (inlineLinks.length) {
            links = inlineLinks;
            ui.sourcesPanel?.classList.remove('hidden');
            renderSourceLinks(ui.sourcesList, links);
          }
        } else if (evt.type === 'result') {
          if (evt.answer) answerText = evt.answer;
          if (evt.text && !answerText) answerText = evt.text;
          if (evt.links) links = evt.links;
          if (evt.images) images = evt.images;
          if (evt.model) streamModel = evt.model;
          if (evt.model_label) streamModelLabel = evt.model_label;
        } else if (evt.type === 'error') {
          throw new Error(evt.error || 'search failed');
        }
      }
    }

    renderResults(query, {
      mode: searchMode,
      answer: answerText,
      text: answerText,
      thinking: thinkingText,
      links,
      images,
      model: streamModel,
      model_label: streamModelLabel,
      queryImage: hasImage ? attachedImage?.previewUrl : null,
    });
  } catch (err) {
    results.innerHTML = `<div class="result-card"><p>Error: ${escapeHtml(err.message)}</p></div>`;
  } finally {
    if (submitBtn) submitBtn.disabled = false;
  }
}

function renderImageGrid(images) {
  if (!images?.length) return '';
  const items = images.map((img) => {
    const url = typeof img === 'string' ? img : img.url;
    const title = typeof img === 'string' ? '' : (img.title || img.description || '');
    if (!url) return '';
    return `<a class="similar-image" href="${escapeHtml(url)}" target="_blank" rel="noopener">
      <img src="${escapeHtml(url)}" alt="${escapeHtml(title || 'similar image')}" loading="lazy">
      ${title ? `<span>${escapeHtml(title)}</span>` : ''}
    </a>`;
  }).join('');
  return items
    ? `<div class="similar-section"><h3>Similar images</h3><div class="similar-grid">${items}</div></div>`
    : '';
}

function renderResults(query, data) {
  const queryImage = data.queryImage
    ? `<div class="query-image"><img src="${data.queryImage}" alt="Query image"></div>`
    : '';

  if (data.mode === 'imagine' && data.images?.length) {
    results.innerHTML = queryImage + `<div class="result-card"><h3>Imagine: ${escapeHtml(query)}</h3>
      <div class="imagine-grid">${data.images.map((u) => {
        const url = typeof u === 'string' ? u : u.url;
        return url ? `<img src="${escapeHtml(url)}" alt="generated">` : '';
      }).join('')}</div>
      ${data.model_label ? `<div class="result-meta">Model: ${escapeHtml(data.model_label)}</div>` : ''}
      </div>`;
    return;
  }

  const text = data.answer || data.text || '';
  const thinking = data.thinking || '';
  const thinkingBlock = thinking.trim()
    ? `<details class="thinking-panel finished">
         <summary>✦ Thinking</summary>
         <div class="thinking-text">${escapeHtml(thinking)}</div>
       </details>`
    : '';

  const allLinks = (data.links?.length ? data.links : extractMarkdownLinks(text));
  const sourcesBlock = allLinks.length
    ? `<div class="result-card sources-card"><h3>Sources</h3><div class="sources-list">${allLinks.map((l) =>
        `<a class="source-link" href="${escapeHtml(l.url)}" target="_blank" rel="noopener">
          <span class="source-title">${escapeHtml(l.title || l.url)}</span>
          <span class="source-url">${escapeHtml(l.url)}</span>
        </a>`).join('')}</div></div>`
    : '';

  const meta = data.model_label
    ? `<div class="result-meta">Model: ${escapeHtml(data.model_label)}</div>`
    : '';

  results.innerHTML =
    queryImage +
    thinkingBlock +
    sourcesBlock +
    `<div class="result-card result-body"><div class="markdown">${renderMarkdown(text)}</div>${meta}</div>` +
    renderImageGrid(data.images);
}

startThemeWatcher();
initModels();
updateModeUi();

const params = new URLSearchParams(location.search);
if (params.get('q')) {
  input.value = params.get('q');
  form.requestSubmit();
}