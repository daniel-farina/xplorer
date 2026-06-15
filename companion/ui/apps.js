const $ = (s) => document.querySelector(s);
const grid = $('#apps-grid');
const emptyEl = $('#apps-empty');
const createBtn = $('#create-btn');
const importBtn = $('#import-btn');
const bulkBar = $('#apps-bulk-bar');
const selectAllCb = $('#apps-select-all');
const exportSelectedBtn = $('#export-selected-btn');
const deleteSelectedBtn = $('#delete-selected-btn');
const restartSelectedBtn = $('#restart-selected-btn');

let lastApps = [];
let lastRenderSig = '';
let statusFilter = 'all';
const filterBar = $('#apps-filter-bar');

// --- View routing: /apps shows the gallery; /apps/new shows only the form. ---
const galleryPanel = $('#apps-gallery-panel');
const createPanel = $('#apps-create-panel');
const newAppBtn = $('#new-app-btn');
const backToGalleryBtn = $('#back-to-gallery-btn');
const appsTitle = $('#apps-title');
const appsSubtitle = $('#apps-subtitle');

function isNewView() {
  return location.pathname.replace(/\/+$/, '') === '/apps/new';
}

// Toggle between gallery view (/apps) and the create-app form view (/apps/new).
function applyView() {
  const creating = isNewView();
  createPanel?.classList.toggle('hidden', !creating);
  galleryPanel?.classList.toggle('hidden', creating);
  newAppBtn?.classList.toggle('hidden', creating);
  backToGalleryBtn?.classList.toggle('hidden', !creating);
  if (appsTitle) appsTitle.textContent = creating ? 'Create new app' : 'Apps';
  if (appsSubtitle) {
    appsSubtitle.textContent = creating
      ? 'Describe an app for Grok to build, or import an existing app folder.'
      : 'Build, import, and manage apps with Grok Build agents. Each app lives in its own folder with a dedicated conversation.';
  }
  if (creating) $('#create-prompt')?.focus();
}

function goToView(path) {
  if (location.pathname !== path) history.pushState({}, '', path);
  applyView();
}

function showAppsToast(message, isError = false) {
  let el = document.getElementById('apps-toast');
  if (!el) {
    el = document.createElement('div');
    el.id = 'apps-toast';
    el.className = 'apps-toast hidden';
    document.body.appendChild(el);
  }
  el.textContent = message;
  el.classList.toggle('error', isError);
  el.classList.remove('hidden');
  clearTimeout(el._hideTimer);
  el._hideTimer = setTimeout(() => el.classList.add('hidden'), 4500);
}

async function fetchApps() {
  const r = await fetch('/api/apps');
  if (!r.ok) throw new Error('failed to load apps');
  return r.json();
}

function statusLabel(status) {
  if (status === 'building') return 'Building';
  if (status === 'ready') return 'Ready';
  if (status === 'error') return 'Error';
  return 'Idle';
}

function previewUrl(app) {
  return app.runtime_url || app.open_url || '';
}

async function downloadAppZip(app) {
  const res = await fetch(`/api/apps/${encodeURIComponent(app.id)}/export`);
  if (!res.ok) {
    const err = await res.json().catch(() => ({}));
    throw new Error(err.error || res.statusText || 'Export failed');
  }
  const blob = await res.blob();
  const cd = res.headers.get('Content-Disposition') || '';
  const match = cd.match(/filename="([^"]+)"/);
  const slug = (app.name || 'app').trim().replace(/[^\w.-]+/g, '_').slice(0, 48);
  const filename = match?.[1] || `${slug}.zip`;
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = filename;
  a.click();
  URL.revokeObjectURL(url);
  return filename;
}

function selectedAppIds() {
  return [...grid.querySelectorAll('.app-select-cb:checked')].map((cb) => cb.dataset.id);
}

function updateBulkBar(apps) {
  bulkBar?.classList.toggle('hidden', apps.length === 0);
  const checked = selectedAppIds().length;
  const exportableCount = apps.filter((a) => a.exportable).length;
  if (exportSelectedBtn) {
    exportSelectedBtn.disabled = checked === 0
      || !selectedAppIds().some((id) => lastApps.find((a) => a.id === id)?.exportable);
  }
  if (deleteSelectedBtn) deleteSelectedBtn.disabled = checked === 0;
  if (restartSelectedBtn) {
    const canRestart = selectedAppIds().some((id) => {
      const app = lastApps.find((a) => a.id === id);
      return app?.runtime_ready && !app?.runtime_alive;
    });
    restartSelectedBtn.disabled = checked === 0 || !canRestart;
  }
  if (selectAllCb) {
    selectAllCb.indeterminate = checked > 0 && checked < apps.length;
    selectAllCb.checked = apps.length > 0 && checked === apps.length;
  }
  if (exportSelectedBtn) {
    exportSelectedBtn.title = exportableCount
      ? 'Download zips for selected exportable apps'
      : 'No exportable apps';
  }
}

function appsForFilter(apps) {
  if (statusFilter === 'all') return apps;
  return apps.filter((a) => (a.status || 'idle') === statusFilter);
}

function statusCounts(apps) {
  const counts = { all: apps.length, ready: 0, building: 0, error: 0, idle: 0 };
  for (const app of apps) {
    const s = app.status || 'idle';
    if (s in counts) counts[s] += 1;
  }
  return counts;
}

const FILTER_LABELS = {
  all: 'All',
  ready: 'Ready',
  building: 'Building',
  idle: 'Idle',
  error: 'Error',
};

function updateFilterCounts(apps) {
  const counts = statusCounts(apps);
  filterBar?.querySelectorAll('[data-filter]').forEach((btn) => {
    const key = btn.dataset.filter || 'all';
    const n = counts[key] ?? 0;
    const label = FILTER_LABELS[key] || key;
    btn.textContent = n ? `${label} (${n})` : label;
  });
}

function renderApps(data) {
  const apps = data.apps || [];
  lastApps = apps;
  const visible = appsForFilter(apps);
  // NOTE: do NOT clear the grid here. Clearing before the signature guard
  // (below) would destroy the live <iframe> thumbnails every poll and make
  // grid.childElementCount 0, defeating the guard. The grid is only cleared
  // right before an actual rebuild (after the guard passes).
  emptyEl.classList.toggle('hidden', visible.length > 0);
  emptyEl.textContent = statusFilter === 'all'
    ? 'No apps yet. Click “Create new app” to build one.'
    : `No ${statusFilter} apps.`;
  filterBar?.querySelectorAll('[data-filter]').forEach((btn) => {
    btn.classList.toggle('active', btn.dataset.filter === statusFilter);
  });
  updateFilterCounts(apps);
  // Skip the grid rebuild when nothing visible changed — this preserves the
  // live <iframe> thumbnails (rebuilding innerHTML every 2s would reload them).
  // Never rebuild while a ⋯ menu is open (it would close it).
  // NOTE: runtime_alive is a live process-health check that legitimately flaps
  // False/True between polls (the per-app runtime spins up when the thumbnail
  // loads /run/<id>/, then reaps when idle). It must NOT be in the rebuild
  // signature or it recreates the <iframe> thumbnails every poll (the "preview
  // refreshes every second" bug). Its small UI bits are updated in place below.
  const sig = statusFilter + '|' + visible.map((a) =>
    `${a.id}:${a.status}:${a.runtime_ready ? 1 : 0}:${a.exportable ? 1 : 0}:${a.name || ''}`).join(',');
  if (grid.querySelector('.app-menu-pop:not(.hidden)')) return;
  if (sig === lastRenderSig && grid.childElementCount) return;
  lastRenderSig = sig;
  grid.innerHTML = '';
  for (const app of visible) {
    const card = document.createElement('article');
    card.className = 'app-card' + (app.status === 'building' ? ' building' : '');
    const pUrl = previewUrl(app);
    const canPreview = app.status === 'ready' && app.runtime_ready && pUrl;
    const thumbReady = app.runtime_ready && pUrl;
    card.innerHTML = `
      <label class="app-select"><input type="checkbox" class="app-select-cb" data-id="${escapeHtml(app.id)}"></label>
      <div class="app-thumb${thumbReady ? '' : ' is-placeholder'}" data-open="${escapeHtml(app.id)}" title="Open builder">
        ${thumbReady
          ? `<iframe class="app-thumb-frame" src="${escapeHtml(pUrl)}" tabindex="-1" aria-hidden="true" scrolling="no" sandbox="allow-scripts allow-same-origin"></iframe><span class="app-thumb-overlay"></span>`
          : `<img class="app-thumb-icon" src="/api/apps/${encodeURIComponent(app.id)}/icon" alt=""><span class="app-thumb-hint">${app.status === 'building' ? 'Building…' : 'Not built yet'}</span>`}
      </div>
      <div class="app-card-head">
        <img class="app-icon" src="/api/apps/${encodeURIComponent(app.id)}/icon" alt="">
        <div class="app-card-headtext">
          <h3>${escapeHtml(app.name || 'App')}</h3>
          <div class="app-status ${escapeHtml(app.status || 'idle')}">
            ${app.status === 'building' ? '<span class="pulse"></span>' : ''}
            ${escapeHtml(statusLabel(app.status))}
            ${app.runtime_alive ? '<span class="runtime-dot alive" title="Runtime server running">●</span>' : ''}
            ${app.runtime_ready && !app.runtime_alive ? '<span class="runtime-dot idle" title="Built but runtime stopped">○</span>' : ''}
          </div>
        </div>
      </div>
      ${app.status === 'error' && app.last_error ? '<p class="app-error">Last build failed: ' + escapeHtml(app.last_error) + '</p>' : ''}
      <div class="app-actions">
        <button type="button" class="apps-btn primary" data-open="${escapeHtml(app.id)}">Build</button>
        <button type="button" class="apps-btn" data-preview="${escapeHtml(pUrl || '')}"${canPreview ? '' : ' disabled title="Build the app first"'}>Preview</button>
        <div class="app-menu">
          <button type="button" class="apps-btn app-menu-btn" data-menu="${escapeHtml(app.id)}" aria-haspopup="true" aria-expanded="false" title="More actions">⋯</button>
          <div class="app-menu-pop hidden" role="menu">
            ${app.runtime_ready && !app.runtime_alive ? `<button type="button" role="menuitem" data-restart="${escapeHtml(app.id)}">Restart runtime</button>` : ''}
            <button type="button" role="menuitem" data-modify="${escapeHtml(app.id)}">Modify with Grok…</button>
            <button type="button" role="menuitem" data-rename="${escapeHtml(app.id)}" data-name="${escapeHtml(app.name || 'App')}">Rename…</button>
            <button type="button" role="menuitem" data-duplicate="${escapeHtml(app.id)}">Duplicate</button>
            ${app.exportable
              ? `<button type="button" role="menuitem" data-export="${escapeHtml(app.id)}" data-name="${escapeHtml(app.name || 'App')}">Export .zip</button>`
              : '<button type="button" role="menuitem" disabled title="App folder missing">Export .zip</button>'}
            <button type="button" role="menuitem" class="danger" data-delete="${escapeHtml(app.id)}" data-name="${escapeHtml(app.name || 'App')}">Delete</button>
          </div>
        </div>
      </div>`;
    grid.appendChild(card);
  }
  grid.querySelectorAll('[data-open]').forEach((btn) => {
    btn.onclick = () => {
      window.location.href = `/app?id=${encodeURIComponent(btn.dataset.open)}`;
    };
  });
  grid.querySelectorAll('[data-restart]').forEach((btn) => {
    btn.onclick = async () => {
      btn.disabled = true;
      try {
        const r = await fetch(`/api/apps/${encodeURIComponent(btn.dataset.restart)}/restart`, {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: '{}',
        });
        const data = await r.json().catch(() => ({}));
        if (!r.ok) throw new Error(data.error || r.statusText);
        showAppsToast('Runtime restarted');
        await refresh();
      } catch (e) {
        showAppsToast(e.message, true);
        btn.disabled = false;
      }
    };
  });
  grid.querySelectorAll('[data-preview]').forEach((btn) => {
    btn.onclick = () => {
      window.open(btn.dataset.preview, '_blank', 'noopener');
    };
  });
  grid.querySelectorAll('[data-rename]').forEach((btn) => {
    btn.onclick = async () => {
      const next = window.prompt('Rename app', btn.dataset.name || '');
      if (!next?.trim() || next.trim() === btn.dataset.name) return;
      btn.disabled = true;
      try {
        const r = await fetch(`/api/apps/${encodeURIComponent(btn.dataset.rename)}/rename`, {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ name: next.trim() }),
        });
        const data = await r.json().catch(() => ({}));
        if (!r.ok) throw new Error(data.error || r.statusText);
        await refresh();
      } catch (e) {
        alert(e.message);
        btn.disabled = false;
      }
    };
  });
  grid.querySelectorAll('[data-duplicate]').forEach((btn) => {
    btn.onclick = async () => {
      btn.disabled = true;
      try {
        const r = await fetch(`/api/apps/${encodeURIComponent(btn.dataset.duplicate)}/duplicate`, {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: '{}',
        });
        const data = await r.json().catch(() => ({}));
        if (!r.ok) throw new Error(data.error || r.statusText);
        await refresh();
      } catch (e) {
        alert(e.message);
        btn.disabled = false;
      }
    };
  });
  grid.querySelectorAll('[data-export]').forEach((btn) => {
    btn.onclick = async () => {
      btn.disabled = true;
      try {
        const app = lastApps.find((a) => a.id === btn.dataset.export);
        if (!app) throw new Error('App not found');
        const filename = await downloadAppZip(app);
        showAppsToast(`Exported ${filename}`);
      } catch (e) {
        showAppsToast(e.message, true);
      } finally {
        btn.disabled = false;
      }
    };
  });
  grid.querySelectorAll('.app-select-cb').forEach((cb) => {
    cb.onchange = () => updateBulkBar(visible);
  });
  updateBulkBar(visible);
  grid.querySelectorAll('[data-modify]').forEach((btn) => {
    btn.onclick = () => {
      const prompt = window.prompt('What should Grok change in this app?');
      if (!prompt?.trim()) return;
      openAppBuild(btn.dataset.modify, prompt.trim());
    };
  });
  grid.querySelectorAll('[data-delete]').forEach((btn) => {
    btn.onclick = async () => {
      const name = btn.dataset.name || 'this app';
      if (!confirm(`Delete "${name}"? This removes the app folder and cannot be undone.`)) return;
      btn.disabled = true;
      try {
        const r = await fetch(`/api/apps/${encodeURIComponent(btn.dataset.delete)}`, {
          method: 'DELETE',
        });
        const data = await r.json().catch(() => ({}));
        if (!r.ok) throw new Error(data.error || r.statusText);
        await refresh();
      } catch (e) {
        alert(e.message);
        btn.disabled = false;
      }
    };
  });
  // ⋯ overflow menu: open the clicked card's menu, close others.
  grid.querySelectorAll('[data-menu]').forEach((btn) => {
    btn.onclick = (e) => {
      e.stopPropagation();
      const pop = btn.parentElement.querySelector('.app-menu-pop');
      const willOpen = pop.classList.contains('hidden');
      grid.querySelectorAll('.app-menu-pop').forEach((p) => p.classList.add('hidden'));
      grid.querySelectorAll('.app-menu-btn').forEach((b) => b.setAttribute('aria-expanded', 'false'));
      if (willOpen) { pop.classList.remove('hidden'); btn.setAttribute('aria-expanded', 'true'); }
    };
  });
  grid.querySelectorAll('.app-menu-pop [role="menuitem"]').forEach((mi) => {
    mi.addEventListener('click', () => mi.closest('.app-menu-pop')?.classList.add('hidden'));
  });
}

async function refresh() {
  try {
    const data = await fetchApps();
    renderApps(data);
  } catch (e) {
    console.error(e);
  }
}

function openAppBuild(appId, prompt) {
  sessionStorage.setItem('xplorer_app_build', JSON.stringify({ id: appId, prompt }));
  window.location.href = `/app?id=${encodeURIComponent(appId)}&autobuild=1`;
}

$('#create-prompt')?.addEventListener('keydown', (e) => {
  if (e.key === 'Enter' && (e.metaKey || e.ctrlKey)) {
    e.preventDefault();
    createBtn.click();
  }
});

createBtn.onclick = async () => {
  const prompt = $('#create-prompt').value.trim();
  const name = $('#create-name').value.trim();
  if (!prompt) return;
  createBtn.disabled = true;
  try {
    const r = await fetch('/api/apps', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ prompt, name: name || undefined }),
    });
    const data = await r.json();
    if (!r.ok) throw new Error(data.error || r.statusText);
    $('#create-prompt').value = '';
    $('#create-name').value = '';
    if (data.app?.id) {
      openAppBuild(data.app.id, prompt);
      return;
    }
    goToView('/apps');
    await refresh();
  } catch (e) {
    alert(e.message);
  } finally {
    createBtn.disabled = false;
  }
};

importBtn.onclick = async () => {
  const path = $('#import-path').value.trim();
  const name = $('#import-name').value.trim();
  if (!path) return;
  importBtn.disabled = true;
  try {
    const r = await fetch('/api/apps/import', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ path, name: name || undefined }),
    });
    const data = await r.json();
    if (!r.ok) throw new Error(data.error || r.statusText);
    $('#import-path').value = '';
    $('#import-name').value = '';
    goToView('/apps');
    await refresh();
  } catch (e) {
    alert(e.message);
  } finally {
    importBtn.disabled = false;
  }
};

newAppBtn?.addEventListener('click', () => goToView('/apps/new'));
backToGalleryBtn?.addEventListener('click', () => goToView('/apps'));
window.addEventListener('popstate', applyView);

selectAllCb?.addEventListener('change', () => {
  const on = !!selectAllCb.checked;
  grid.querySelectorAll('.app-select-cb').forEach((cb) => { cb.checked = on; });
  updateBulkBar(lastApps);
});

restartSelectedBtn?.addEventListener('click', async () => {
  const ids = selectedAppIds().filter((id) => {
    const app = lastApps.find((a) => a.id === id);
    return app?.runtime_ready && !app?.runtime_alive;
  });
  if (!ids.length) return;
  restartSelectedBtn.disabled = true;
  try {
    const res = await fetch('/api/apps/restart-batch', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ ids }),
    });
    const data = await res.json().catch(() => ({}));
    if (!res.ok) throw new Error(data.error || res.statusText || 'Restart failed');
    const n = data.restarted ?? ids.length;
    showAppsToast(n === 1 ? 'Restarted 1 runtime' : `Restarted ${n} runtimes`);
    await refresh();
  } catch (e) {
    showAppsToast(e.message, true);
  } finally {
    restartSelectedBtn.disabled = false;
    updateBulkBar(lastApps);
  }
});

deleteSelectedBtn?.addEventListener('click', async () => {
  const ids = selectedAppIds();
  if (!ids.length) return;
  const names = ids.map((id) => lastApps.find((a) => a.id === id)?.name || id);
  if (!confirm(`Delete ${ids.length} app(s)?\n\n${names.join('\n')}\n\nThis removes app folders and cannot be undone.`)) {
    return;
  }
  deleteSelectedBtn.disabled = true;
  let ok = 0;
  try {
    for (const id of ids) {
      const r = await fetch(`/api/apps/${encodeURIComponent(id)}`, { method: 'DELETE' });
      const data = await r.json().catch(() => ({}));
      if (!r.ok) throw new Error(data.error || r.statusText || 'Delete failed');
      ok += 1;
    }
    showAppsToast(ok === 1 ? 'Deleted 1 app' : `Deleted ${ok} apps`);
    await refresh();
  } catch (e) {
    showAppsToast(e.message, true);
    await refresh();
  } finally {
    deleteSelectedBtn.disabled = false;
  }
});

exportSelectedBtn?.addEventListener('click', async () => {
  const ids = selectedAppIds().filter((id) => lastApps.find((a) => a.id === id)?.exportable);
  if (!ids.length) return;
  exportSelectedBtn.disabled = true;
  try {
    if (ids.length === 1) {
      const app = lastApps.find((a) => a.id === ids[0]);
      const filename = await downloadAppZip(app);
      showAppsToast(`Exported ${filename}`);
    } else {
      const res = await fetch('/api/apps/export-batch', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ ids }),
      });
      if (!res.ok) {
        const err = await res.json().catch(() => ({}));
        throw new Error(err.error || res.statusText || 'Batch export failed');
      }
      const blob = await res.blob();
      const url = URL.createObjectURL(blob);
      const a = document.createElement('a');
      a.href = url;
      a.download = 'xplorer-apps.zip';
      a.click();
      URL.revokeObjectURL(url);
      showAppsToast(`Exported ${ids.length} apps`);
    }
  } catch (e) {
    showAppsToast(e.message, true);
  } finally {
    exportSelectedBtn.disabled = false;
    updateBulkBar(lastApps);
  }
});

filterBar?.querySelectorAll('[data-filter]').forEach((btn) => {
  btn.addEventListener('click', () => {
    statusFilter = btn.dataset.filter || 'all';
    renderApps({ apps: lastApps });
  });
});

// Close any open ⋯ menu when clicking elsewhere (bound once).
document.addEventListener('click', (e) => {
  if (!e.target.closest('.app-menu')) {
    grid.querySelectorAll('.app-menu-pop').forEach((p) => p.classList.add('hidden'));
    grid.querySelectorAll('.app-menu-btn').forEach((b) => b.setAttribute('aria-expanded', 'false'));
  }
});

mountGrokToolbar({ pageHome: 'build' });
startThemeWatcher();
applyView();
refresh();
setInterval(refresh, 2000);