const $ = (s) => document.querySelector(s);
const grid = $('#apps-grid');
const emptyEl = $('#apps-empty');
const createBtn = $('#create-btn');
const importBtn = $('#import-btn');
const bulkBar = $('#apps-bulk-bar');
const selectAllCb = $('#apps-select-all');
const exportSelectedBtn = $('#export-selected-btn');
const deleteSelectedBtn = $('#delete-selected-btn');

let lastApps = [];

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

function renderApps(data) {
  const apps = data.apps || [];
  lastApps = apps;
  grid.innerHTML = '';
  emptyEl.classList.toggle('hidden', apps.length > 0);
  for (const app of apps) {
    const card = document.createElement('article');
    card.className = 'app-card' + (app.status === 'building' ? ' building' : '');
    card.innerHTML = `
      <label class="app-select"><input type="checkbox" class="app-select-cb" data-id="${escapeHtml(app.id)}"></label>
      <div class="app-card-head">
        <img class="app-icon" src="/api/apps/${encodeURIComponent(app.id)}/icon" alt="">
        <div>
          <h3>${escapeHtml(app.name || 'App')}</h3>
          <p class="app-meta">${escapeHtml(app.path || '')}</p>
        </div>
      </div>
      <div class="app-status ${escapeHtml(app.status || 'idle')}">
        ${app.status === 'building' ? '<span class="pulse"></span>' : ''}
        ${escapeHtml(statusLabel(app.status))}
        ${app.runtime_alive ? '<span class="runtime-dot alive" title="Runtime server running">●</span>' : ''}
        ${app.runtime_ready && !app.runtime_alive ? '<span class="runtime-dot idle" title="Built but runtime stopped">○</span>' : ''}
      </div>
      ${app.last_error ? '<p class="app-error">' + escapeHtml(app.last_error) + '</p>' : ''}
      <div class="app-actions">
        <button type="button" class="apps-btn primary" data-open="${escapeHtml(app.id)}">Build</button>
        ${app.status === 'ready' && app.open_url
          ? `<button type="button" class="apps-btn" data-preview="${escapeHtml(app.open_url)}">Preview</button>`
          : ''}
        <button type="button" class="apps-btn" data-rename="${escapeHtml(app.id)}" data-name="${escapeHtml(app.name || 'App')}">Rename</button>
        <button type="button" class="apps-btn" data-duplicate="${escapeHtml(app.id)}">Duplicate</button>
        ${app.exportable
          ? `<button type="button" class="apps-btn" data-export="${escapeHtml(app.id)}" data-name="${escapeHtml(app.name || 'App')}">Export</button>`
          : '<button type="button" class="apps-btn" disabled title="App folder missing">Export</button>'}
        <button type="button" class="apps-btn" data-modify="${escapeHtml(app.id)}">Modify</button>
        <button type="button" class="apps-btn danger" data-delete="${escapeHtml(app.id)}" data-name="${escapeHtml(app.name || 'App')}">Delete</button>
      </div>`;
    grid.appendChild(card);
  }
  grid.querySelectorAll('[data-open]').forEach((btn) => {
    btn.onclick = () => {
      window.location.href = `/app?id=${encodeURIComponent(btn.dataset.open)}`;
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
    cb.onchange = () => updateBulkBar(apps);
  });
  updateBulkBar(apps);
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
    await refresh();
  } catch (e) {
    alert(e.message);
  } finally {
    importBtn.disabled = false;
  }
};

selectAllCb?.addEventListener('change', () => {
  const on = !!selectAllCb.checked;
  grid.querySelectorAll('.app-select-cb').forEach((cb) => { cb.checked = on; });
  updateBulkBar(lastApps);
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
  const ids = selectedAppIds();
  if (!ids.length) return;
  exportSelectedBtn.disabled = true;
  let ok = 0;
  try {
    for (const id of ids) {
      const app = lastApps.find((a) => a.id === id);
      if (!app?.exportable) continue;
      await downloadAppZip(app);
      ok += 1;
    }
    showAppsToast(ok === 1 ? 'Exported 1 app' : `Exported ${ok} apps`);
  } catch (e) {
    showAppsToast(e.message, true);
  } finally {
    exportSelectedBtn.disabled = false;
    updateBulkBar(lastApps);
  }
});

initSearchHomeToggle($('#home-toggle'));
startThemeWatcher();
refresh();
setInterval(refresh, 2000);