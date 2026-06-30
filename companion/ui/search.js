const $ = (s) => document.querySelector(s);
const form = $('#search-form');
const input = $('#q');
const submitBtn = form?.querySelector('button[type="submit"]');
const hero = $('#hero');
const modesEl = $('#search-modes');

let mode = 'web';

const MODE_LABELS = { web: 'Web', x: 'X Live', imagine: 'Imagine' };

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
  if (!['web', 'x', 'imagine'].includes(next)) next = 'web';
  mode = next;
  modesEl?.querySelectorAll('[data-mode]').forEach((btn) => {
    btn.classList.toggle('active', btn.dataset.mode === mode);
  });
  input.placeholder = mode === 'imagine'
    ? 'Describe an image to generate…'
    : mode === 'x'
    ? 'See what 𝕏 is saying about…'
    : 'Search with Grok…';
  if (mode !== 'x') { const xp = document.getElementById('x-panel'); if (xp) xp.hidden = true; }
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
  if (mode === 'x') { await renderXSnapshot(q); return; }
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

async function initSearchModelFromSettings() {
  try {
    const settings = await fetchSettings();
    if (settings && settings.search_model) {
      persistSearchModel(settings.search_model);
    }
  } catch {
    // fall back to localStorage / default
  }
}

initSearchModelFromSettings().catch(() => {});

const urlParams = new URLSearchParams(location.search);
const modeParam = urlParams.get('mode');
if (modeParam && ['web', 'x', 'imagine'].includes(modeParam)) {
  setMode(modeParam);
} else {
  setMode(getStoredSearchMode() === 'imagine' ? 'imagine' : 'web');
}
// A fresh new tab opens a blank Grok home — only honor an explicit ?q= in the
// URL; do NOT auto-restore the last search (that made every new tab reopen the
// previous query, e.g. /search?q=xcnn).
const urlQuery = urlParams.get('q');
if (urlQuery) {
  input.value = urlQuery;
  syncUrl();
}
if (urlQuery) {
  if (mode === 'x') renderXSnapshot(urlQuery); else openGrokWebQuery(urlQuery);
}

// --- X Live: the "On 𝕏 right now" module ------------------------------------
// Streams /api/search/stream?mode=x (Grok + the X MCP) and renders a live
// snapshot inline instead of navigating away. Works against the mock X server
// today; switches to live X data automatically when xapi is configured.
function xEscape(s) {
  return String(s == null ? '' : s).replace(/[&<>"']/g, (c) => (
    { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
}

async function renderXSnapshot(query) {
  const panel = document.getElementById('x-panel');
  if (!panel) return;
  const statusEl = document.getElementById('x-panel-status');
  const answerEl = document.getElementById('x-answer');
  const postsEl = document.getElementById('x-posts');
  const trendsEl = document.getElementById('x-trends');
  panel.hidden = false;
  answerEl.innerHTML = '';
  postsEl.innerHTML = '';
  trendsEl.innerHTML = '';
  statusEl.textContent = 'Asking Grok + 𝕏…';
  if (submitBtn) submitBtn.disabled = true;
  let reply = '';
  try {
    const url = `/api/search/stream?q=${encodeURIComponent(query)}&mode=x&model=grok-build`;
    const res = await fetch(url);
    if (!res.ok) throw new Error(res.statusText || ('HTTP ' + res.status));
    const reader = res.body.getReader();
    const decoder = new TextDecoder();
    let buf = '';
    for (;;) {
      const { done, value } = await reader.read();
      if (done) break;
      buf += decoder.decode(value, { stream: true });
      let nl;
      while ((nl = buf.indexOf('\n')) >= 0) {
        const line = buf.slice(0, nl); buf = buf.slice(nl + 1);
        const evt = parseStreamLine(line);
        if (!evt) continue;
        if (evt.type === 'tool' || evt.type === 'tool_use') {
          statusEl.textContent = 'Using ' + (evt.name || evt.tool || evt.data || 'X tools') + '…';
        } else if (evt.type === 'thought') {
          statusEl.textContent = 'Reading 𝕏…';
        } else if (evt.type === 'text') {
          reply += evt.data || '';
        } else if (evt.type === 'result') {
          if (evt.reply) reply = evt.reply;
        } else if (evt.type === 'error') {
          throw new Error(evt.message || evt.error || 'X snapshot failed');
        }
      }
    }
    statusEl.textContent = '';
    renderXResult(reply, { answerEl, postsEl, trendsEl });
  } catch (err) {
    statusEl.textContent = '';
    answerEl.textContent = '𝕏 snapshot failed: ' + ((err && err.message) || err);
  } finally {
    if (submitBtn) submitBtn.disabled = false;
  }
}

function renderXResult(reply, els) {
  let data = null;
  const m = reply.match(/```json\s*([\s\S]*?)```/);
  try { data = JSON.parse(m ? m[1] : reply); } catch { data = null; }
  const answer = (data && data.answer) || reply.replace(/```json[\s\S]*?```/g, '').trim();
  els.answerEl.innerHTML = (typeof renderMarkdown === 'function')
    ? renderMarkdown(answer) : xEscape(answer).replace(/\n/g, '<br>');
  if (data && Array.isArray(data.posts) && data.posts.length) {
    els.postsEl.innerHTML = data.posts.map((p) => {
      const u = (typeof safeUrl === 'function' ? safeUrl(p.url) : p.url) || '#';
      return `<a class="x-post" href="${u}" target="_blank" rel="noopener">`
        + `<div class="x-post-head"><span class="x-post-author">${xEscape(p.author || '')}</span>`
        + `<span class="x-post-name">${xEscape(p.name || '')}</span></div>`
        + `<div class="x-post-text">${xEscape(p.text || '')}</div>`
        + `<div class="x-post-meta">♥ ${Number(p.likes) || 0} · ⇄ ${Number(p.reposts) || 0}</div></a>`;
    }).join('');
  }
  if (data && Array.isArray(data.trends) && data.trends.length) {
    els.trendsEl.innerHTML = '<span class="x-trends-label">Trending</span>'
      + data.trends.map((t) => `<span class="x-trend">${xEscape(t)}</span>`).join('');
  }
}