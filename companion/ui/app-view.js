const $ = (s) => document.querySelector(s);
const params = new URLSearchParams(location.search);
const appId = params.get('id');

const CHAT_OPEN_KEY = 'grok_app_chat_open';

let app = null;
let busy = false;

function appPreviewUrl() {
  if (!app) return '';
  return app.runtime_url || app.open_url ||
    `/api/apps/${encodeURIComponent(appId)}/preview/index.html`;
}

function updateExportButton() {
  const btn = $('#export-app');
  if (!btn) return;
  const canExport = !!app?.exportable;
  btn.hidden = !canExport;
  btn.disabled = !canExport;
  btn.title = canExport ? 'Download app folder as zip' : 'App folder missing';
}

async function exportAppZip() {
  if (!app?.exportable || !appId) return;
  const btn = $('#export-app');
  if (btn) btn.disabled = true;
  try {
    const res = await fetch(`/api/apps/${encodeURIComponent(appId)}/export`);
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
  } catch (err) {
    alert(err.message);
  } finally {
    if (btn) btn.disabled = false;
  }
}

function updateOpenTabLink() {
  const link = $('#open-app-tab');
  const url = appPreviewUrl();
  if (!link) return;
  if (!url) {
    link.classList.add('disabled');
    link.href = '#';
    link.title = 'App not ready yet';
    return;
  }
  link.classList.remove('disabled');
  link.href = url;
  const port = app?.runtime_port;
  link.title = port
    ? `Open on localhost:${port}`
    : 'Open app on localhost';
}

function setChatOpen(open) {
  const workspace = $('#app-workspace');
  const toggle = $('#chat-toggle');
  if (!workspace || !toggle) return;
  workspace.classList.toggle('chat-collapsed', !open);
  toggle.textContent = open ? '‹' : '›';
  toggle.setAttribute('aria-expanded', open ? 'true' : 'false');
  try {
    localStorage.setItem(CHAT_OPEN_KEY, open ? '1' : '0');
  } catch { /* ignore */ }
}

function initChatToggle() {
  const toggle = $('#chat-toggle');
  if (!toggle) return;
  let open = true;
  try {
    open = localStorage.getItem(CHAT_OPEN_KEY) !== '0';
  } catch { /* ignore */ }
  setChatOpen(open);
  toggle.onclick = () => {
    setChatOpen($('#app-workspace').classList.contains('chat-collapsed'));
  };
}

function renderMessageContent(el, role, content) {
  const text = stripAnsi(content || '');
  if (role === 'assistant' && text) {
    el.classList.add('markdown');
    el.innerHTML = renderMarkdown(text);
    wireCodeCopyButtons(el);
  } else {
    el.classList.remove('markdown');
    el.textContent = text;
  }
}

function dedupeMessages(messages) {
  const out = [];
  for (const m of messages || []) {
    const prev = out[out.length - 1];
    if (prev && prev.role === m.role && prev.content === m.content) continue;
    out.push(m);
  }
  return out;
}

function appendUserBubbleIfNew(box, text) {
  const trimmed = text.trim();
  const lastUser = [...box.querySelectorAll('.msg.user')].pop();
  if (lastUser && lastUser.textContent.trim() === trimmed) return;
  const userDiv = document.createElement('div');
  userDiv.className = 'msg user';
  userDiv.textContent = text;
  box.appendChild(userDiv);
}

function renderMessages(messages) {
  const box = $('#chat-messages');
  box.innerHTML = '';
  for (const m of dedupeMessages(messages)) {
    const div = document.createElement('div');
    div.className = `msg ${m.role}`;
    renderMessageContent(div, m.role, m.content);
    box.appendChild(div);
  }
  box.scrollTop = box.scrollHeight;
}

function createStreamBubble(box) {
  const wrap = document.createElement('div');
  wrap.className = 'stream-block streaming';
  wrap.innerHTML = `
    <details class="thinking-panel" open>
      <summary>✦ Thinking</summary>
      <div class="thinking-text">Grok is working…</div>
    </details>
    <div class="answer-panel hidden">
      <div class="answer msg assistant markdown"></div>
    </div>`;
  box.appendChild(wrap);
  box.scrollTop = box.scrollHeight;
  return {
    wrap,
    thinkingPanel: wrap.querySelector('.thinking-panel'),
    thinkingText: wrap.querySelector('.thinking-text'),
    answerPanel: wrap.querySelector('.answer-panel'),
    answerEl: wrap.querySelector('.answer'),
  };
}

function scrollChat(box) {
  box.scrollTop = box.scrollHeight;
}

async function loadConversation() {
  const convId = app?.conversation_id;
  if (!convId) return [];
  const r = await fetch('/api/conversations');
  const data = await r.json();
  const conv = (data.conversations || []).find((c) => c.id === convId);
  renderMessages(conv?.messages || []);
  return conv?.messages || [];
}

async function loadApp() {
  if (!appId) {
    location.href = '/apps';
    return;
  }
  const r = await fetch(`/api/apps/${encodeURIComponent(appId)}`);
  const data = await r.json();
  if (!r.ok) throw new Error(data.error || r.statusText);
  app = data.app;
  document.title = `${app.name || 'App'} — Grok Build`;
  $('#app-title').textContent = app.name || 'App';
  const pathEl = $('#app-path');
  pathEl.textContent = app.path || '';
  pathEl.title = app.path || '';
  $('#app-icon').src = `/api/apps/${encodeURIComponent(appId)}/icon`;
  updateStatus(app.status);
  if (app.runtime_port) {
    pathEl.textContent = `localhost:${app.runtime_port}`;
    pathEl.title = app.open_url || app.runtime_url || app.path || '';
  }
  updateOpenTabLink();
  updateExportButton();
  $('#export-app').onclick = exportAppZip;
  if (!window.__xplorerExportHotkey) {
    window.__xplorerExportHotkey = true;
    document.addEventListener('keydown', (e) => {
      if ((e.metaKey || e.ctrlKey) && e.shiftKey && e.key.toLowerCase() === 'e') {
        e.preventDefault();
        exportAppZip();
      }
    });
  }
  $('#copy-cli').onclick = () => {
    const cmd = app.cli_command || `grok --cwd ${app.path || '.'}`;
    navigator.clipboard.writeText(cmd).then(() => {
      $('#copy-cli').textContent = 'Copied!';
      setTimeout(() => { $('#copy-cli').textContent = 'Copy CLI command'; }, 1500);
    }).catch(() => prompt('Copy CLI command:', cmd));
  };
  const refreshBtn = $('#refresh-preview');
  if (refreshBtn) {
    refreshBtn.onclick = async () => {
      refreshBtn.classList.add('spinning');
      refreshBtn.disabled = true;
      try { await refreshApp(); await loadPreview(); }
      finally { refreshBtn.classList.remove('spinning'); refreshBtn.disabled = false; }
    };
  }
  await loadConversation();
  await loadPreview();

  if (params.get('autobuild') === '1') {
    const raw = sessionStorage.getItem('xplorer_app_build');
    sessionStorage.removeItem('xplorer_app_build');
    if (raw) {
      try {
        const pending = JSON.parse(raw);
        if (pending.id === appId && pending.prompt) {
          history.replaceState(null, '', `/app?id=${encodeURIComponent(appId)}`);
          await runAgentStream({
            text: pending.prompt,
            mode: 'build',
          });
          return;
        }
      } catch { /* ignore */ }
    }
  }

  if (app.status === 'building' && !busy) {
    pollWhileBuilding();
  }
}

function updateStatus(status) {
  const el = $('#app-status');
  el.className = `app-status ${status || 'idle'}`;
  const labels = {
    building: 'Building…',
    ready: 'Ready',
    error: 'Error',
    idle: 'Idle',
  };
  el.textContent = labels[status] || status || 'Idle';
}

async function probeIndexExists() {
  const probes = [
    app?.runtime_url && `${app.runtime_url}index.html`,
    `/run/${encodeURIComponent(appId)}/index.html`,
    `/api/apps/${encodeURIComponent(appId)}/preview/index.html`,
  ].filter(Boolean);
  for (const url of probes) {
    try {
      const r = await fetch(url, { cache: 'no-store' });
      if (r.ok) return true;
    } catch { /* try next */ }
  }
  return false;
}

async function loadPreview() {
  const preview = $('#app-preview');
  let placeholder = $('#preview-placeholder');
  updateOpenTabLink();
  const previewUrl = appPreviewUrl();
  if (!(await probeIndexExists())) {
    if (!placeholder) {
      placeholder = document.createElement('div');
      placeholder.id = 'preview-placeholder';
      placeholder.className = 'app-preview-placeholder';
      preview.appendChild(placeholder);
    }
    placeholder.textContent =
      'No index.html yet. Grok will build the app here — watch the chat for live progress.';
    preview.querySelectorAll('iframe').forEach((f) => f.remove());
    return;
  }
  const iframeUrl = `${previewUrl}${previewUrl.includes('?') ? '&' : '?'}t=${Date.now()}`;
  if (placeholder) placeholder.remove();
  if (!preview.querySelector('iframe')) {
    const iframe = document.createElement('iframe');
    iframe.title = app.name || 'App preview';
    hookPreviewScrollbars(iframe);
    iframe.src = iframeUrl;
    preview.appendChild(iframe);
  } else {
    const iframe = preview.querySelector('iframe');
    hookPreviewScrollbars(iframe);
    iframe.src = iframeUrl;
  }
}

/** Attach a one-time load hook that themes the preview's own scrollbars. */
function hookPreviewScrollbars(iframe) {
  if (iframe.dataset.sbHook) return;
  iframe.dataset.sbHook = '1';
  iframe.addEventListener('load', () => styleIframeScrollbars(iframe));
}

/** Inject theme-aware scrollbar CSS into the (same-origin) preview document. */
function styleIframeScrollbars(iframe) {
  try {
    const doc = iframe.contentDocument;
    if (!doc) return; // cross-origin or not ready
    const cs = getComputedStyle(document.documentElement);
    const v = (n, f) => (cs.getPropertyValue(n) || f).trim();
    const thumb = v('--border', '#555');
    const thumbHover = v('--text-secondary', '#888');
    const ID = 'xplorer-scrollbar-style';
    let st = doc.getElementById(ID);
    if (!st) {
      st = doc.createElement('style');
      st.id = ID;
      (doc.head || doc.documentElement).appendChild(st);
    }
    st.textContent =
      'html{scrollbar-width:thin;scrollbar-color:' + thumb + ' transparent}' +
      '::-webkit-scrollbar{width:12px;height:12px}' +
      '::-webkit-scrollbar-track{background:transparent}' +
      '::-webkit-scrollbar-thumb{background:' + thumb + ';border-radius:10px;' +
      'background-clip:padding-box;border:3px solid transparent}' +
      '::-webkit-scrollbar-thumb:hover{background:' + thumbHover + ';' +
      'background-clip:padding-box;border:3px solid transparent}' +
      '::-webkit-scrollbar-corner{background:transparent}';
  } catch (e) {
    /* cross-origin guard */
  }
}

async function refreshApp() {
  const r = await fetch(`/api/apps/${encodeURIComponent(appId)}`);
  const data = await r.json();
  if (r.ok && data.app) {
    app = data.app;
    updateStatus(app.status);
    if (app.runtime_port) {
      const pathEl = $('#app-path');
      pathEl.textContent = `localhost:${app.runtime_port}`;
      pathEl.title = app.open_url || app.runtime_url || app.path || '';
    }
    updateOpenTabLink();
    updateExportButton();
  }
}

async function runAgentStream({ text, mode = 'message' }) {
  if (busy || !app || !text) return;
  if (!(await ensureGrokReady())) return;  // Grok Build required to build apps
  const endpoint = mode === 'build' ? 'build/stream' : 'message/stream';
  busy = true;
  $('#chat-send').disabled = true;
  updateStatus('building');

  const box = $('#chat-messages');
  box.querySelector('.building-notice')?.remove();
  appendUserBubbleIfNew(box, text);

  const ui = createStreamBubble(box);
  let thinkingText = '';
  let answerText = '';
  let sawThought = false;
  let sawAnswer = false;

  try {
    const r = await fetch(`/api/apps/${encodeURIComponent(appId)}/${endpoint}`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(mode === 'build' ? { prompt: text } : { message: text }),
    });
    if (!r.ok || !r.body) {
      const err = await r.json().catch(() => ({}));
      throw new Error(err.error || 'stream failed');
    }

    const reader = r.body.getReader();
    const decoder = new TextDecoder();
    let buffer = '';

    while (true) {
      const { done, value } = await reader.read();
      if (done) break;
      buffer += decoder.decode(value, { stream: true });
      let idx;
      while ((idx = buffer.indexOf('\n')) >= 0) {
        const line = buffer.slice(0, idx).trim();
        buffer = buffer.slice(idx + 1);
        if (!line) continue;
        const evt = parseStreamLine(line);
        if (!evt) continue;

        if (evt.type === 'meta') {
          /* model metadata — reserved for future label */
        } else if (evt.type === 'thought') {
          sawThought = true;
          thinkingText += evt.data || '';
          ui.thinkingText.textContent = thinkingText;
          scrollChat(box);
        } else if (evt.type === 'text') {
          if (!sawAnswer) {
            ui.answerPanel.classList.remove('hidden');
            if (sawThought) ui.thinkingPanel.removeAttribute('open');
          }
          sawAnswer = true;
          answerText += evt.data || '';
          ui.answerEl.innerHTML = renderMarkdown(answerText);
          wireCodeCopyButtons(ui.answerEl);
          scrollChat(box);
        } else if (evt.type === 'result') {
          if (evt.reply) answerText = stripAnsi(evt.reply);
          else if (evt.text) answerText = stripAnsi(evt.text);
          if (answerText) {
            sawAnswer = true;
            ui.answerPanel.classList.remove('hidden');
            ui.answerEl.innerHTML = renderMarkdown(answerText);
            wireCodeCopyButtons(ui.answerEl);
            if (sawThought) ui.thinkingPanel.removeAttribute('open');
          }
        } else if (evt.type === 'max_turns_reached') {
          ui.thinkingText.textContent = (thinkingText || '') +
            (thinkingText ? '\n\n' : '') + '— Reached max turns (still working in background)';
        } else if (evt.type === 'error') {
          throw new Error(evt.error || 'Grok build failed');
        }
      }
    }

    ui.wrap.classList.remove('streaming');
    ui.thinkingPanel.classList.add('finished');
    if (sawThought && !sawAnswer) {
      ui.thinkingPanel.setAttribute('open', '');
      ui.thinkingPanel.querySelector('summary').textContent = '✦ Thought process';
    } else if (sawThought) {
      ui.thinkingPanel.querySelector('summary').textContent = '✦ Thought process';
    } else if (!sawAnswer) {
      ui.wrap.remove();
    }

    await refreshApp();
    await loadPreview();
  } catch (err) {
    ui.wrap.classList.remove('streaming');
    ui.wrap.classList.add('error');
    ui.answerPanel.classList.remove('hidden');
    ui.answerEl.classList.remove('markdown');
    ui.answerEl.textContent = err.message || String(err);
    updateStatus('error');
    await refreshApp();
  } finally {
    busy = false;
    $('#chat-send').disabled = false;
  }
}

async function pollWhileBuilding() {
  const box = $('#chat-messages');
  let notice = box.querySelector('.building-notice');
  if (!notice) {
    notice = document.createElement('div');
    notice.className = 'msg assistant building-notice';
    notice.textContent = 'Grok is building… (stream running elsewhere — refreshing chat)';
    box.appendChild(notice);
  }
  while (app?.status === 'building' && !busy) {
    await new Promise((r) => setTimeout(r, 2000));
    await loadConversation();
    await refreshApp();
    if (app.status !== 'building') {
      notice.remove();
      await loadPreview();
      return;
    }
  }
  notice?.remove();
}

$('#chat-form').onsubmit = async (e) => {
  e.preventDefault();
  if (busy || !app) return;
  const input = $('#chat-input');
  const text = input.value.trim();
  if (!text) return;
  input.value = '';
  await runAgentStream({ text, mode: 'message' });
};

$('#chat-input')?.addEventListener('keydown', (e) => {
  if (e.key === 'Enter' && !e.shiftKey) {
    e.preventDefault();
    $('#chat-form')?.requestSubmit();
  }
});

$('#delete-app')?.addEventListener('click', async () => {
  if (!app || busy) return;
  const name = app.name || 'this app';
  if (!confirm(`Delete "${name}"? This removes the app folder and cannot be undone.`)) return;
  const btn = $('#delete-app');
  if (btn) btn.disabled = true;
  try {
    const r = await fetch(`/api/apps/${encodeURIComponent(appId)}`, { method: 'DELETE' });
    const data = await r.json().catch(() => ({}));
    if (!r.ok) throw new Error(data.error || r.statusText);
    location.href = '/apps';
  } catch (err) {
    alert(err.message);
    if (btn) btn.disabled = false;
  }
});

function initOverflowMenu() {
  const wrap = $('#app-overflow');
  const btn = $('#app-overflow-btn');
  const menu = $('#app-overflow-menu');
  if (!wrap || !btn || !menu) return;
  const close = () => { menu.hidden = true; btn.setAttribute('aria-expanded', 'false'); };
  const open = () => { menu.hidden = false; btn.setAttribute('aria-expanded', 'true'); };
  btn.onclick = (e) => { e.stopPropagation(); menu.hidden ? open() : close(); };
  document.addEventListener('click', (e) => { if (!wrap.contains(e.target)) close(); });
  document.addEventListener('keydown', (e) => { if (e.key === 'Escape') close(); });
  menu.querySelectorAll('.app-menu-item').forEach((el) =>
    el.addEventListener('click', () => setTimeout(close, 0)));
}

mountGrokToolbar({ pageHome: 'build' });
initChatToggle();
initOverflowMenu();
startThemeWatcher();
loadApp().catch((e) => {
  alert(e.message);
  location.href = '/apps';
});

setInterval(async () => {
  if (!appId || busy) return;
  try {
    await refreshApp();
  } catch { /* ignore */ }
}, 3000);