const $ = (s) => document.querySelector(s);
const form = $('#search-form');
const input = $('#q');
const submitBtn = form?.querySelector('button[type="submit"]');
const hero = $('#hero');
const modesEl = $('#search-modes');

let mode = 'web';

const MODE_LABELS = { web: 'Web', imagine: 'Imagine' };

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
  if (!['web', 'imagine'].includes(next)) next = 'web';
  mode = next;
  modesEl?.querySelectorAll('[data-mode]').forEach((btn) => {
    btn.classList.toggle('active', btn.dataset.mode === mode);
  });
  input.placeholder = mode === 'imagine'
    ? 'Describe an image to generate…'
    : 'Search with Grok…';
  syncUrl();
  persistSearchMode(mode);
}

async function openGrokWebQuery(query) {
  if (submitBtn) submitBtn.disabled = true;
  try {
    let dest;
    if (mode === 'imagine') {
      dest = await imagineUrlForQuery(query, 'https://grok.com/imagine');
    } else {
      const res = await fetch('/api/page/grok-web', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ query: query.trim() }),
      });
      const data = await res.json();
      if (!res.ok) throw new Error(data.error || res.statusText);
      if (!data.grok_url) throw new Error('missing grok_url');
      dest = data.grok_url;
    }
    window.location.href = dest;
  } catch (err) {
    alert(`Grok Web failed: ${err.message}`);
  } finally {
    if (submitBtn) submitBtn.disabled = false;
  }
}

modesEl?.querySelectorAll('[data-mode]').forEach((btn) => {
  btn.addEventListener('click', () => setMode(btn.dataset.mode));
});

form?.addEventListener('submit', async (e) => {
  e.preventDefault();
  const q = input.value.trim();
  if (!q) return;
  persistSearchQuery(q);
  syncUrl();
  await openGrokWebQuery(q);
});

input?.addEventListener('input', () => {
  persistSearchQuery(input.value.trim());
  syncUrl();
});

mountGrokToolbar({
  pageHome: SEARCH_HOME_WEB,
  onSwitch: async (saved, updated) => {
    const params = new URLSearchParams();
    const q = input.value.trim();
    if (q) params.set('q', q);
    if (mode !== 'web') params.set('mode', mode);
    const suffix = params.toString() ? `?${params.toString()}` : '';
    if (saved === SEARCH_HOME_BUILD) {
      window.location.href = `/${suffix}`;
    } else if (saved === SEARCH_HOME_WIKI) {
      window.location.href = wikiUrlForQuery(
        q || getStoredSearchQuery(),
        updated?.grok_wiki_url || 'https://grokipedia.com/',
      );
    } else {
      window.location.href = `/search${suffix}`;
    }
  },
});

startThemeWatcher();

const urlParams = new URLSearchParams(location.search);
const modeParam = urlParams.get('mode');
if (modeParam && ['web', 'imagine'].includes(modeParam)) {
  setMode(modeParam);
} else {
  setMode(getStoredSearchMode() === 'imagine' ? 'imagine' : 'web');
}
const urlQuery = urlParams.get('q');
const initialQuery = urlQuery || getStoredSearchQuery();
if (initialQuery) {
  input.value = initialQuery;
  syncUrl();
}
if (urlQuery) {
  openGrokWebQuery(urlQuery);
}