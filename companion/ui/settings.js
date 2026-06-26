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
// Sidebar pane switching (General / Toolbar). Simple class/hidden toggle.
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
// Toolbar editor. Config contract (stored under "toolbar" in settings) is an
// ORDERED ARRAY:
//   { pills: [ {id,label,href,icon,enabled,isHome?,children?:[{label,href}]} ] }
// Order in the array == order in the bar. enabled:false hides a pill but keeps
// it in the list. Built-in ids (xchat/build/web/wiki/xcom) merge their
// icon/home/children from DEFAULT_PILLS at render time in toolbar.js. isHome
// pills (build/web/wiki) carry data-home and drive /switch-home. Empty array
// → static toolbar.html default.
// --------------------------------------------------------------------------
const TB = window.XplorerToolbar || {};
const TB_ICONS = TB.ICONS || {};
const TB_DEFAULT_PILLS = TB.DEFAULT_PILLS || [];
const TB_CATALOG = TB.PILL_CATALOG || [];
const TB_ICON_IDS = Object.keys(TB_ICONS);
const TB_BUILTIN_IDS = new Set(TB_DEFAULT_PILLS.map((p) => p.id));

const toolbarEditor = document.getElementById('toolbar-editor');
const toolbarStatus = document.getElementById('toolbar-status');
const toolbarAddCustom = document.getElementById('toolbar-add-custom');
const toolbarCatalog = document.getElementById('toolbar-catalog');

let customPillCounter = 0;

function setToolbarStatus(msg, kind = '') {
  if (!toolbarStatus) return;
  toolbarStatus.textContent = msg;
  toolbarStatus.className = 'settings-status' + (kind ? ` ${kind}` : '');
}

/** Deep-clone the built-in defaults (so editing rows never mutates them). */
function cloneDefaultPills() {
  return TB_DEFAULT_PILLS.map((p) => ({
    id: p.id,
    label: p.label,
    href: p.href,
    icon: p.icon,
    enabled: p.enabled !== false,
    isHome: !!p.isHome,
    children: Array.isArray(p.children)
      ? p.children.map((c) => ({ label: c.label, href: c.href }))
      : null,
  }));
}

function makeChildRow(child = {}) {
  const row = document.createElement('div');
  row.className = 'tb-child-row';
  const label = document.createElement('input');
  label.type = 'text';
  label.placeholder = 'Label';
  label.className = 'tb-child-label';
  label.value = child.label || '';
  const href = document.createElement('input');
  href.type = 'text';
  href.placeholder = 'https://… or /path';
  href.className = 'tb-child-href';
  href.value = child.href || '';
  const remove = document.createElement('button');
  remove.type = 'button';
  remove.className = 'tb-child-remove';
  remove.title = 'Remove item';
  remove.textContent = '×';
  remove.addEventListener('click', () => row.remove());
  row.append(label, href, remove);
  return row;
}

/** Close any open icon-picker popovers (only one open at a time). */
function closeIconPickers(except) {
  toolbarEditor?.querySelectorAll('.tb-icon-picker.open').forEach((p) => {
    if (p !== except) p.classList.remove('open');
  });
}

document.addEventListener('click', (e) => {
  if (!e.target.closest('.tb-icon-field')) closeIconPickers();
});

/** Build the icon picker: a button showing the current icon + a popover grid. */
function makeIconField(pill, currentIcon) {
  const field = document.createElement('div');
  field.className = 'tb-icon-field';
  let selected = TB_ICONS[currentIcon] ? currentIcon : (currentIcon || 'link');

  const btn = document.createElement('button');
  btn.type = 'button';
  btn.className = 'tb-icon-btn';
  btn.title = 'Choose icon';
  btn.innerHTML = TB_ICONS[selected] || TB_ICONS.link || '';

  const picker = document.createElement('div');
  picker.className = 'tb-icon-picker';
  TB_ICON_IDS.forEach((iconId) => {
    const opt = document.createElement('button');
    opt.type = 'button';
    opt.className = 'tb-icon-opt';
    opt.title = iconId;
    opt.dataset.icon = iconId;
    opt.innerHTML = TB_ICONS[iconId];
    if (iconId === selected) opt.classList.add('selected');
    opt.addEventListener('click', () => {
      selected = iconId;
      field.dataset.icon = iconId;
      btn.innerHTML = TB_ICONS[iconId];
      picker.querySelectorAll('.tb-icon-opt.selected')
        .forEach((o) => o.classList.remove('selected'));
      opt.classList.add('selected');
      picker.classList.remove('open');
    });
    picker.appendChild(opt);
  });

  btn.addEventListener('click', () => {
    const willOpen = !picker.classList.contains('open');
    closeIconPickers(picker);
    picker.classList.toggle('open', willOpen);
  });

  field.dataset.icon = selected;
  field.append(btn, picker);
  return field;
}

/** Render one pill row. |pill| is a normalized object (see cloneDefaultPills). */
function makePillRow(pill) {
  const isBuiltin = TB_BUILTIN_IDS.has(pill.id);
  const card = document.createElement('div');
  card.className = 'tb-pill';
  card.dataset.pill = pill.id;
  if (pill.isHome) card.dataset.home = '1';
  if (pill.enabled === false) card.classList.add('tb-disabled');

  // Head: reorder buttons + icon + label + href + enable + remove.
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

  const iconField = makeIconField(pill, pill.icon);

  const fields = document.createElement('div');
  fields.className = 'tb-fields';
  const label = document.createElement('input');
  label.type = 'text';
  label.className = 'tb-label';
  label.placeholder = 'Label';
  label.value = pill.label || '';
  const href = document.createElement('input');
  href.type = 'text';
  href.className = 'tb-href';
  href.placeholder = 'https://… or /path';
  href.value = pill.href || '';
  fields.append(label, href);

  const toggle = document.createElement('label');
  toggle.className = 'tb-toggle';
  const check = document.createElement('input');
  check.type = 'checkbox';
  check.className = 'tb-enabled';
  check.checked = pill.enabled !== false;
  const toggleText = document.createElement('span');
  toggleText.textContent = 'On';
  toggle.append(check, toggleText);
  check.addEventListener('change', () => {
    card.classList.toggle('tb-disabled', !check.checked);
  });

  const remove = document.createElement('button');
  remove.type = 'button';
  remove.className = 'tb-pill-remove';
  remove.title = 'Remove pill';
  remove.textContent = '×';
  remove.addEventListener('click', () => {
    card.remove();
    refreshCatalogOptions();
  });

  head.append(order, iconField, fields, toggle, remove);
  card.appendChild(head);

  // Children sub-editor (any pill may have dropdown items).
  const childrenWrap = document.createElement('div');
  childrenWrap.className = 'tb-children';
  const childLabel = document.createElement('div');
  childLabel.className = 'tb-children-label';
  childLabel.textContent = 'Dropdown items';
  childrenWrap.appendChild(childLabel);
  const list = document.createElement('div');
  list.className = 'tb-child-list';
  childrenWrap.appendChild(list);
  if (Array.isArray(pill.children)) {
    for (const child of pill.children) list.appendChild(makeChildRow(child));
  }
  const add = document.createElement('button');
  add.type = 'button';
  add.className = 'tb-child-add';
  add.textContent = '+ Add item';
  add.addEventListener('click', () => list.appendChild(makeChildRow()));
  childrenWrap.appendChild(add);
  card.appendChild(childrenWrap);

  // Remember whether this id is a built-in home pill so collect can re-emit
  // isHome even if the source object came from the catalog.
  card.dataset.isHome = pill.isHome ? '1' : '';
  return card;
}

/** Render the editor from a list of normalized pill objects. */
function renderToolbarEditor(pills) {
  if (!toolbarEditor) return;
  toolbarEditor.innerHTML = '';
  for (const pill of pills) toolbarEditor.appendChild(makePillRow(pill));
  refreshCatalogOptions();
}

function applyToolbarAppearanceFields(toolbar = {}) {
  const placementEl = document.getElementById('toolbar-placement');
  const visibleEl = document.getElementById('toolbar-visible');
  if (placementEl) {
    placementEl.value = toolbar.placement === 'top' ? 'top' : 'sidebar';
  }
  if (visibleEl) {
    visibleEl.checked = toolbar.visible !== false;
  }
}

/** Collect pills plus placement/visible from the toolbar pane. */
function collectToolbarConfig() {
  const pills = [];
  if (!toolbarEditor) {
    return {
      pills,
      placement: document.getElementById('toolbar-placement')?.value || 'sidebar',
      visible: document.getElementById('toolbar-visible')?.checked !== false,
    };
  }
  toolbarEditor.querySelectorAll('.tb-pill').forEach((card) => {
    const id = card.dataset.pill;
    const label = card.querySelector('.tb-label')?.value.trim() || '';
    const href = card.querySelector('.tb-href')?.value.trim() || '';
    const icon = card.querySelector('.tb-icon-field')?.dataset.icon || 'link';
    const enabled = card.querySelector('.tb-enabled')?.checked !== false;
    const entry = { id, label, href, icon, enabled };
    if (card.dataset.isHome === '1') entry.isHome = true;
    const children = [];
    card.querySelectorAll('.tb-child-row').forEach((row) => {
      const cl = row.querySelector('.tb-child-label')?.value.trim() || '';
      const ch = row.querySelector('.tb-child-href')?.value.trim() || '';
      if (cl || ch) children.push({ label: cl, href: ch });
    });
    if (children.length) entry.children = children;
    pills.push(entry);
  });
  const placement = document.getElementById('toolbar-placement')?.value || 'sidebar';
  const visible = document.getElementById('toolbar-visible')?.checked !== false;
  return { pills, placement, visible };
}

/** Current set of pill ids present in the editor (to dedupe catalog adds). */
function currentPillIds() {
  const ids = new Set();
  toolbarEditor?.querySelectorAll('.tb-pill').forEach((c) => ids.add(c.dataset.pill));
  return ids;
}

/** Rebuild the catalog dropdown to only offer entries not already present. */
function refreshCatalogOptions() {
  if (!toolbarCatalog) return;
  const present = currentPillIds();
  toolbarCatalog.innerHTML = '';
  const placeholder = document.createElement('option');
  placeholder.value = '';
  placeholder.textContent = 'Add from catalog…';
  toolbarCatalog.appendChild(placeholder);
  TB_CATALOG.forEach((item) => {
    if (present.has(item.id)) return;
    const opt = document.createElement('option');
    opt.value = item.id;
    opt.textContent = item.label;
    toolbarCatalog.appendChild(opt);
  });
}

async function loadToolbarEditor() {
  let pills;
  let toolbarMeta = {};
  try {
    const settings = await fetchSettings();
    toolbarMeta = settings.toolbar || {};
    applyToolbarAppearanceFields(toolbarMeta);
    const stored = settings.toolbar && settings.toolbar.pills;
    if (Array.isArray(stored) && stored.length) {
      pills = stored.map((p) => ({
        id: p.id,
        label: p.label || '',
        href: p.href || '',
        icon: p.icon || 'link',
        enabled: p.enabled !== false,
        isHome: !!p.isHome,
        children: Array.isArray(p.children) ? p.children : null,
      }));
    } else {
      pills = cloneDefaultPills();
    }
  } catch (e) {
    pills = cloneDefaultPills();
    setToolbarStatus(e.message, 'err');
  }
  renderToolbarEditor(pills);
}

toolbarAddCustom?.addEventListener('click', () => {
  customPillCounter += 1;
  const pill = {
    id: 'custom-' + customPillCounter,
    label: '',
    href: '',
    icon: 'link',
    enabled: true,
    isHome: false,
    children: null,
  };
  const card = makePillRow(pill);
  toolbarEditor.appendChild(card);
  card.querySelector('.tb-label')?.focus();
  refreshCatalogOptions();
});

toolbarCatalog?.addEventListener('change', () => {
  const id = toolbarCatalog.value;
  if (!id) return;
  const item = TB_CATALOG.find((c) => c.id === id);
  toolbarCatalog.value = '';
  if (!item || currentPillIds().has(id)) return;
  const pill = {
    id: item.id,
    label: item.label,
    href: item.href,
    icon: item.icon || 'link',
    enabled: true,
    isHome: !!item.isHome,
    children: Array.isArray(item.children)
      ? item.children.map((c) => ({ label: c.label, href: c.href }))
      : null,
  };
  toolbarEditor.appendChild(makePillRow(pill));
  refreshCatalogOptions();
});

document.getElementById('toolbar-save')?.addEventListener('click', async () => {
  setToolbarStatus('Saving…');
  try {
    await saveSettings({ toolbar: collectToolbarConfig() });
    setToolbarStatus('Saved — changes apply immediately in Xplorer', 'ok');
    setTimeout(() => setToolbarStatus(''), 3000);
  } catch (e) {
    setToolbarStatus(e.message, 'err');
  }
});

document.getElementById('toolbar-reset')?.addEventListener('click', async () => {
  setToolbarStatus('Resetting…');
  try {
    // Empty array → static toolbar.html default on every surface.
    const toolbar = collectToolbarConfig();
    await saveSettings({ toolbar: { pills: [], placement: toolbar.placement, visible: toolbar.visible } });
    renderToolbarEditor(cloneDefaultPills());
    setToolbarStatus('Reset to default', 'ok');
    setTimeout(() => setToolbarStatus(''), 3000);
  } catch (e) {
    setToolbarStatus(e.message, 'err');
  }
});

loadToolbarEditor();

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