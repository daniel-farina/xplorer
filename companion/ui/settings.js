const statusEl = document.getElementById('settings-status');
const searchHome = document.getElementById('search-home');
const chatModel = document.getElementById('chat-model');
const searchModel = document.getElementById('search-model');
const browserTheme = document.getElementById('browser-theme');
const maxTurns = document.getElementById('max-turns');

function setStatus(msg, kind = '') {
  if (!statusEl) return;
  statusEl.textContent = msg;
  statusEl.className = 'settings-status' + (kind ? ` ${kind}` : '');
}

function fillSelect(select, models, selected) {
  if (!select) return;
  select.innerHTML = '';
  for (const m of models) {
    const opt = document.createElement('option');
    opt.value = m.id;
    opt.textContent = m.label || m.id;
    if (m.id === selected) opt.selected = true;
    select.appendChild(opt);
  }
}

async function loadTheme() {
  try {
    const res = await fetch('/api/theme');
    if (!res.ok) return;
    const data = await res.json();
    if (browserTheme && data.color_scheme) {
      browserTheme.value = data.color_scheme;
    }
  } catch { /* ignore */ }
}

async function init() {
  startThemeWatcher();
  const settings = await fetchSettings();
  const models = settings.models || await fetchModels();

  if (searchHome) searchHome.value = settings.search_home || SEARCH_HOME_BUILD;
  fillSelect(chatModel, models, settings.model || DEFAULT_MODEL);
  fillSelect(searchModel, models, settings.search_model || SEARCH_DEFAULT_MODEL);
  if (maxTurns) maxTurns.value = settings.max_turns || 50;

  document.getElementById('info-companion')?.replaceChildren(
    document.createTextNode(settings.companion_url || location.origin),
  );
  document.getElementById('info-gateway')?.replaceChildren(
    document.createTextNode(settings.gateway_url || '—'),
  );
  document.getElementById('info-grok')?.replaceChildren(
    document.createTextNode(settings.grok_bin || '—'),
  );
  document.getElementById('info-cdp')?.replaceChildren(
    document.createTextNode(settings.cdp_url || '—'),
  );

  await loadTheme();
}

async function persist(partial) {
  setStatus('Saving…');
  try {
    await saveSettings(partial);
    setStatus('Saved', 'ok');
    setTimeout(() => setStatus(''), 2000);
  } catch (e) {
    setStatus(e.message, 'err');
  }
}

searchHome?.addEventListener('change', () => {
  persistSearchHome(searchHome.value);
  persist({ search_home: searchHome.value });
});

chatModel?.addEventListener('change', () => {
  persistModel(chatModel.value);
  persist({ model: chatModel.value });
});

searchModel?.addEventListener('change', () => {
  persistSearchModel(searchModel.value);
  persist({ search_model: searchModel.value });
});

maxTurns?.addEventListener('change', () => {
  let n = parseInt(maxTurns.value, 10);
  if (!Number.isFinite(n)) n = 50;
  n = Math.min(200, Math.max(1, n));
  maxTurns.value = String(n);
  persist({ max_turns: n });
});

browserTheme?.addEventListener('change', async () => {
  setStatus('Saving theme…');
  try {
    const res = await fetch('/api/theme', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ color_scheme: browserTheme.value }),
    });
    const data = await res.json();
    if (!res.ok) throw new Error(data.error || res.statusText);
    setStatus('Theme updated', 'ok');
    setTimeout(() => setStatus(''), 2000);
  } catch (e) {
    setStatus(e.message, 'err');
  }
});

document.getElementById('replay-welcome')?.addEventListener('click', () => {
  window.location.href = '/welcome';
});

// --------------------------------------------------------------------------
// Sidebar pane switching (General / Bookmarks). Simple class/hidden toggle.
// --------------------------------------------------------------------------
const navButtons = document.querySelectorAll('.settings-nav-btn');
const panes = document.querySelectorAll('.settings-pane');
function showPane(name) {
  navButtons.forEach((b) => b.classList.toggle('active', b.dataset.pane === name));
  panes.forEach((p) => {
    const on = p.dataset.pane === name;
    p.classList.toggle('active', on);
    p.hidden = !on;
  });
}
navButtons.forEach((b) => {
  b.addEventListener('click', () => showPane(b.dataset.pane));
});

// --------------------------------------------------------------------------
// Bookmarks editor. Config contract (stored top-level under "bookmarks") is an
// ORDERED ARRAY: [ {id, label, url} ]. Order in the array == tab order in the
// native "Bookmarks" group. The string "id" is parsed to an int64 by the C++
// seeder and stamped on the tab as TabOwnership::bookmark_node_id, so it must
// be a stable, unique, positive integer. Existing ids are preserved; a newly
// added row gets max(existing numeric ids)+1. Empty/invalid-url rows are
// dropped server-side. Empty list → built-in defaults re-seeded next launch.
// --------------------------------------------------------------------------
const bookmarksEditor = document.getElementById('bookmarks-editor');
const bookmarksStatus = document.getElementById('bookmarks-status');
const bookmarksAdd = document.getElementById('bookmarks-add');

function setBookmarksStatus(msg, kind = '') {
  if (!bookmarksStatus) return;
  bookmarksStatus.textContent = msg;
  bookmarksStatus.className = 'settings-status' + (kind ? ` ${kind}` : '');
}

/** Render one bookmark row: ▲▼ reorder + label + url + remove. */
function makeBookmarkRow(bm = {}) {
  const card = document.createElement('div');
  card.className = 'tb-pill';
  // Stash the id on the row so collect can preserve it (blank for new rows).
  card.dataset.bmId = bm.id != null ? String(bm.id) : '';

  const head = document.createElement('div');
  head.className = 'tb-pill-head';

  const order = document.createElement('div');
  order.className = 'tb-order';
  const up = document.createElement('button');
  up.type = 'button';
  up.className = 'tb-order-btn tb-up';
  up.title = 'Move up';
  up.textContent = '▲';
  up.addEventListener('click', () => {
    const prev = card.previousElementSibling;
    if (prev) card.parentNode.insertBefore(card, prev);
  });
  const down = document.createElement('button');
  down.type = 'button';
  down.className = 'tb-order-btn tb-down';
  down.title = 'Move down';
  down.textContent = '▼';
  down.addEventListener('click', () => {
    const next = card.nextElementSibling;
    if (next) card.parentNode.insertBefore(next, card);
  });
  order.append(up, down);

  const fields = document.createElement('div');
  fields.className = 'tb-fields';
  const label = document.createElement('input');
  label.type = 'text';
  label.className = 'tb-label';
  label.placeholder = 'Label';
  label.value = bm.label || '';
  const url = document.createElement('input');
  url.type = 'text';
  url.className = 'tb-href';
  url.placeholder = 'https://…';
  url.value = bm.url || '';
  fields.append(label, url);

  const remove = document.createElement('button');
  remove.type = 'button';
  remove.className = 'tb-pill-remove';
  remove.title = 'Remove bookmark';
  remove.textContent = '×';
  remove.addEventListener('click', () => card.remove());

  head.append(order, fields, remove);
  card.appendChild(head);
  return card;
}

/** Render the editor from a list of {id,label,url} objects. */
function renderBookmarksEditor(bookmarks) {
  if (!bookmarksEditor) return;
  bookmarksEditor.innerHTML = '';
  for (const bm of bookmarks) bookmarksEditor.appendChild(makeBookmarkRow(bm));
}

/** Largest numeric id currently in the editor (0 if none) — for new-row ids. */
function maxBookmarkId() {
  let max = 0;
  bookmarksEditor?.querySelectorAll('.tb-pill').forEach((card) => {
    const n = parseInt(card.dataset.bmId, 10);
    if (Number.isFinite(n) && n > max) max = n;
  });
  return max;
}

/** Collect [{id,label,url}], preserving ids and assigning ids to new rows. */
function collectBookmarks() {
  const out = [];
  if (!bookmarksEditor) return out;
  let nextId = maxBookmarkId() + 1;
  bookmarksEditor.querySelectorAll('.tb-pill').forEach((card) => {
    const label = card.querySelector('.tb-label')?.value.trim() || '';
    const url = card.querySelector('.tb-href')?.value.trim() || '';
    let id = card.dataset.bmId;
    if (!id) {
      id = String(nextId);
      nextId += 1;
      card.dataset.bmId = id;
    }
    out.push({ id, label, url });
  });
  return out;
}

async function loadBookmarksEditor() {
  let bookmarks = [];
  try {
    const settings = await fetchSettings();
    const stored = settings.bookmarks;
    if (Array.isArray(stored)) {
      bookmarks = stored.map((b) => ({
        id: b.id != null ? String(b.id) : '',
        label: b.label || '',
        url: b.url || '',
      }));
    }
  } catch (e) {
    setBookmarksStatus(e.message, 'err');
  }
  renderBookmarksEditor(bookmarks);
}

bookmarksAdd?.addEventListener('click', () => {
  const card = makeBookmarkRow({ id: String(maxBookmarkId() + 1) });
  bookmarksEditor.appendChild(card);
  card.querySelector('.tb-label')?.focus();
});

document.getElementById('bookmarks-save')?.addEventListener('click', async () => {
  setBookmarksStatus('Saving…');
  try {
    await saveSettings({ bookmarks: collectBookmarks() });
    setBookmarksStatus('Saved — changes apply immediately in Xplorer', 'ok');
    setTimeout(() => setBookmarksStatus(''), 3000);
  } catch (e) {
    setBookmarksStatus(e.message, 'err');
  }
});

loadBookmarksEditor();

init().catch((e) => setStatus(e.message, 'err'));