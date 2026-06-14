const $ = (s) => document.querySelector(s);
const params = new URLSearchParams(location.search);
const appId = params.get('id');

let app = null;
let busy = false;

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
  $('#copy-cli').onclick = () => {
    const cmd = app.cli_command || `grok --cwd ${app.path || '.'}`;
    navigator.clipboard.writeText(cmd).then(() => {
      $('#copy-cli').textContent = 'Copied!';
      setTimeout(() => { $('#copy-cli').textContent = 'Copy CLI'; }, 1500);
    }).catch(() => prompt('Copy CLI command:', cmd));
  };
  await loadConversation();
  await loadPreview();
}

function updateStatus(status) {
  const el = $('#app-status');
  el.className = `app-status ${status || 'idle'}`;
  el.textContent = status === 'building' ? 'Building…' : (status || 'idle');
}

async function loadConversation() {
  const convId = app.conversation_id;
  if (!convId) return;
  const r = await fetch('/api/conversations');
  const data = await r.json();
  const conv = (data.conversations || []).find((c) => c.id === convId);
  const box = $('#chat-messages');
  box.innerHTML = '';
  for (const m of conv?.messages || []) {
    const div = document.createElement('div');
    div.className = `msg ${m.role}`;
    div.textContent = m.content;
    box.appendChild(div);
  }
  box.scrollTop = box.scrollHeight;
}

async function loadPreview() {
  const preview = $('#app-preview');
  const placeholder = $('#preview-placeholder');
  const previewUrl = `/api/apps/${encodeURIComponent(appId)}/preview/index.html`;
  const probe = await fetch(previewUrl);
  if (!probe.ok) {
    placeholder.textContent =
      'No index.html yet. Chat with Grok to build the app, or open the folder path in your editor.';
    return;
  }
  placeholder.remove();
  const iframe = document.createElement('iframe');
  iframe.src = previewUrl;
  iframe.title = app.name || 'App preview';
  preview.appendChild(iframe);
}

$('#chat-form').onsubmit = async (e) => {
  e.preventDefault();
  if (busy || !app) return;
  const input = $('#chat-input');
  const text = input.value.trim();
  if (!text) return;
  busy = true;
  $('#chat-send').disabled = true;
  updateStatus('building');
  const userDiv = document.createElement('div');
  userDiv.className = 'msg user';
  userDiv.textContent = text;
  $('#chat-messages').appendChild(userDiv);
  input.value = '';
  try {
    const r = await fetch(`/api/apps/${encodeURIComponent(appId)}/message/stream`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ message: text }),
    });
    if (!r.ok || !r.body) throw new Error('stream failed');
    const reader = r.body.getReader();
    const decoder = new TextDecoder();
    let buf = '';
    let assistant = '';
    let assistantEl = null;
    while (true) {
      const { done, value } = await reader.read();
      if (done) break;
      buf += decoder.decode(value, { stream: true });
      const lines = buf.split('\n');
      buf = lines.pop() || '';
      for (const line of lines) {
        if (!line.trim() || line[0] !== '{') continue;
        try {
          const evt = JSON.parse(line);
          if (evt.type === 'text' && evt.data) {
            assistant += evt.data;
            if (!assistantEl) {
              assistantEl = document.createElement('div');
              assistantEl.className = 'msg assistant';
              $('#chat-messages').appendChild(assistantEl);
            }
            assistantEl.textContent = assistant;
            $('#chat-messages').scrollTop = $('#chat-messages').scrollHeight;
          } else if (evt.type === 'result' && evt.text) {
            assistant = evt.text;
            if (!assistantEl) {
              assistantEl = document.createElement('div');
              assistantEl.className = 'msg assistant';
              $('#chat-messages').appendChild(assistantEl);
            }
            assistantEl.textContent = assistant;
          } else if (evt.type === 'error') {
            throw new Error(evt.error || 'edit failed');
          }
        } catch (parseErr) {
          if (parseErr.message && parseErr.message !== 'Unexpected end of JSON input')
            throw parseErr;
        }
      }
    }
    const refreshed = await fetch(`/api/apps/${encodeURIComponent(appId)}`);
    const refreshedData = await refreshed.json();
    if (refreshed.ok) {
      app = refreshedData.app;
      updateStatus(app.status);
    }
    await loadPreview();
  } catch (err) {
    alert(err.message || err);
    updateStatus('error');
  } finally {
    busy = false;
    $('#chat-send').disabled = false;
  }
};

initSearchHomeToggle($('#home-toggle'));
startThemeWatcher();
loadApp().catch((e) => {
  alert(e.message);
  location.href = '/apps';
});
setInterval(async () => {
  if (!appId || busy) return;
  try {
    const r = await fetch(`/api/apps/${encodeURIComponent(appId)}`);
    const data = await r.json();
    if (r.ok && data.app) {
      app = data.app;
      updateStatus(app.status);
    }
  } catch { /* ignore */ }
}, 2000);