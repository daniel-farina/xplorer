const $ = (s) => document.querySelector(s);
const grid = $('#apps-grid');
const emptyEl = $('#apps-empty');
const createBtn = $('#create-btn');
const importBtn = $('#import-btn');

const activeStreams = new Map();

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
      <div class="app-actions">
        <button type="button" class="apps-btn primary" data-open="${escapeHtml(app.id)}">Open</button>
        <button type="button" class="apps-btn" data-modify="${escapeHtml(app.id)}">Modify</button>
      </div>`;
    grid.appendChild(card);
  }
  grid.querySelectorAll('[data-open]').forEach((btn) => {
    btn.onclick = () => {
      window.location.href = `/app?id=${encodeURIComponent(btn.dataset.open)}`;
    };
  });
  grid.querySelectorAll('[data-modify]').forEach((btn) => {
    btn.onclick = async () => {
      const prompt = window.prompt('What should Grok change in this app?');
      if (!prompt) return;
      await startBuildStream(btn.dataset.modify, prompt);
      refresh();
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

async function startBuildStream(appId, prompt) {
  if (activeStreams.has(appId)) return;
  activeStreams.set(appId, true);
  try {
    const r = await fetch(`/api/apps/${encodeURIComponent(appId)}/build/stream`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ prompt }),
    });
    if (!r.ok || !r.body) return;
    const reader = r.body.getReader();
    const decoder = new TextDecoder();
    let buf = '';
    while (true) {
      const { done, value } = await reader.read();
      if (done) break;
      buf += decoder.decode(value, { stream: true });
      const lines = buf.split('\n');
      buf = lines.pop() || '';
      for (const line of lines) {
        if (!line.trim()) continue;
        try {
          const evt = JSON.parse(line);
          if (evt.type === 'error') console.warn('[apps]', evt.error);
        } catch { /* ignore */ }
      }
    }
  } catch (e) {
    console.error('[apps] stream', e);
  } finally {
    activeStreams.delete(appId);
    refresh();
  }
}

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
    await refresh();
    if (data.app?.id) {
      startBuildStream(data.app.id, prompt);
    }
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