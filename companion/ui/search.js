const $ = (s) => document.querySelector(s);
const form = $('#search-form');
const input = $('#q');
const submitBtn = form?.querySelector('button[type="submit"]');
const imageTools = $('#image-tools');
const imageFile = $('#image-file');
const imagePreview = $('#image-preview');
const previewImg = $('#preview-img');

let mode = 'web';
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

function formatGrokWebQuery(query) {
  const q = query.trim();
  if (mode === 'videos') return `Search for videos: ${q}`;
  if (mode === 'images') return `Search for images: ${q}`;
  if (mode === 'imagine') return `Generate an image: ${q}`;
  return q;
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

form?.addEventListener('submit', async (e) => {
  e.preventDefault();
  const q = input.value.trim();
  if (!q && !attachedImage) return;
  const prompt = q || 'Describe this image and find similar images';
  await openGrokWebQuery(prompt);
});

initSearchHomeToggle($('#home-toggle'), {
  pageHome: SEARCH_HOME_WEB,
});

startThemeWatcher();
updateModeUi();

const urlParams = new URLSearchParams(location.search);
const modeParam = urlParams.get('mode');
if (modeParam && ['web', 'images', 'videos', 'imagine'].includes(modeParam)) {
  mode = modeParam;
  updateModeUi();
}
if (urlParams.get('q')) {
  input.value = urlParams.get('q');
  form.requestSubmit();
}