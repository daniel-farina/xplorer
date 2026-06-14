const $ = (s) => document.querySelector(s);
const grid = $('#apps-grid');
const emptyEl = $('#apps-empty');
const createBtn = $('#create-btn');
const importBtn = $('#import-btn');

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

function renderApps(data) {
  const apps = data.apps || [];
  grid.innerHTML = '';
  emptyEl.classList.toggle('hidden', apps.length > 0);
  for (const app of apps) {
    const card = document.createElement('article');
    card.className = 'app-card' + (app.status === 'building' ? ' building' : '');
    card.innerHTML = `
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
      </div>
      ${app.last_error ? '<p class="app-error">' + escapeHtml(app.last_error) + '</p>' : ''}
      <div class="app-actions">
        <button type="button" class="apps-btn primary" data-open="${escapeHtml(app.id)}">Build</button>
        ${app.status === 'ready' && app.open_url
          ? `<button type="button" class="apps-btn" data-preview="${escapeHtml(app.open_url)}">Preview</button>`
          : ''}
        <button type="button" class="apps-btn" data-rename="${escapeHtml(app.id)}" data-name="${escapeHtml(app.name || 'App')}">Rename</button>
        <button type="button" class="apps-btn" data-duplicate="${escapeHtml(app.id)}">Duplicate</button>
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

initSearchHomeToggle($('#home-toggle'));
startThemeWatcher();
refresh();
setInterval(refresh, 2000);