const statusEl = document.getElementById('settings-status');
const searchHome = document.getElementById('search-home');
const chatModel = document.getElementById('chat-model');
const searchModel = document.getElementById('search-model');
const browserTheme = document.getElementById('browser-theme');

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

init().catch((e) => setStatus(e.message, 'err'));