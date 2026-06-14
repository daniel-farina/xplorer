const $ = (s) => document.querySelector(s);
const messagesEl = $('#messages');
const convList = $('#conv-list');
const input = $('#input');
const sendBtn = $('#send');
const modelSelect = $('#model-select');

let conversations = [];
let activeId = null;
let busy = false;
let models = [];
let activeModel = getStoredModel();

async function api(path, opts = {}) {
  const r = await fetch(path, {
    headers: { 'Content-Type': 'application/json' },
    ...opts,
  });
  const data = await r.json().catch(() => ({}));
  if (!r.ok) throw new Error(data.error || r.statusText);
  return data;
}

function renderMessages(conv) {
  messagesEl.innerHTML = '';
  if (!conv) return;
  for (const m of conv.messages || []) {
    const div = document.createElement('div');
    div.className = `msg ${m.role}`;
    if (m.role === 'assistant') {
      div.innerHTML = renderMarkdown(m.content || '');
      div.classList.add('markdown');
      wireCodeCopyButtons(div);
    } else {
      div.textContent = m.content || '';
    }
    messagesEl.appendChild(div);
  }
  messagesEl.scrollTop = messagesEl.scrollHeight;
}

function renderConvList() {
  convList.innerHTML = '';
  for (const c of conversations) {
    const li = document.createElement('li');
    li.textContent = c.title || 'Chat';
    li.className = c.id === activeId ? 'active' : '';
    li.onclick = () => selectConv(c.id);
    convList.appendChild(li);
  }
}

function selectConv(id) {
  activeId = id;
  const conv = conversations.find((c) => c.id === id);
  renderConvList();
  renderMessages(conv);
}

async function refresh() {
  const data = await api('/api/conversations');
  conversations = data.conversations || [];
  if (!activeId && conversations.length) activeId = conversations[0].id;
  renderConvList();
  selectConv(activeId);
}

async function newChat() {
  const conv = await api('/api/conversations', { method: 'POST', body: '{}' });
  conversations.unshift(conv);
  activeId = conv.id;
  renderConvList();
  renderMessages(conv);
  input.focus();
}

async function sendMessage(text) {
  if (!text.trim() || busy || !activeId) return;
  busy = true;
  sendBtn.disabled = true;
  const conv = conversations.find((c) => c.id === activeId);
  conv.messages = conv.messages || [];
  conv.messages.push({ role: 'user', content: text });
  renderMessages(conv);
  input.value = '';

  const thinking = document.createElement('div');
  thinking.className = 'msg assistant thinking';
  thinking.textContent = 'Grok is thinking…';
  messagesEl.appendChild(thinking);
  messagesEl.scrollTop = messagesEl.scrollHeight;

  try {
    const res = await fetch(`/api/conversations/${activeId}/message/stream`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ message: text, model: activeModel }),
    });
    if (!res.ok) {
      const err = await res.json().catch(() => ({}));
      throw new Error(err.error || res.statusText);
    }

    const reader = res.body.getReader();
    const decoder = new TextDecoder();
    let buffer = '';
    let reply = '';
    let streamModelLabel = modelLabel(activeModel, models);

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
          if (evt.model_label) streamModelLabel = evt.model_label;
        } else if (evt.type === 'thought') {
          thinking.textContent = evt.data || 'Grok is thinking…';
        } else if (evt.type === 'text') {
          thinking.remove();
          reply += evt.data || '';
          const div = messagesEl.querySelector('.msg.assistant.streaming') ||
            (() => {
              const el = document.createElement('div');
              el.className = 'msg assistant streaming';
              messagesEl.appendChild(el);
              return el;
            })();
          div.innerHTML = renderMarkdown(reply);
          div.classList.add('markdown');
          wireCodeCopyButtons(div);
          messagesEl.scrollTop = messagesEl.scrollHeight;
        } else if (evt.type === 'result') {
          if (evt.reply) reply = evt.reply;
          if (evt.sessionId) conv.session_id = evt.sessionId;
          if (evt.model_label) streamModelLabel = evt.model_label;
        } else if (evt.type === 'error') {
          throw new Error(evt.error || 'chat failed');
        }
      }
    }

    thinking.remove();
    messagesEl.querySelector('.msg.assistant.streaming')?.remove();
    conv.messages.push({ role: 'assistant', content: reply });
    renderMessages(conv);
    await refresh();
  } catch (e) {
    thinking.textContent = `Error: ${e.message}`;
    thinking.classList.remove('thinking');
  } finally {
    busy = false;
    sendBtn.disabled = false;
    input.focus();
  }
}

$('#composer').onsubmit = (e) => {
  e.preventDefault();
  sendMessage(input.value);
};

$('#new-chat').onclick = newChat;

input.addEventListener('keydown', (e) => {
  if (e.key === 'Enter' && !e.shiftKey) {
    e.preventDefault();
    sendMessage(input.value);
  }
});

async function initModels() {
  try {
    const settings = await fetchSettings();
    if (settings.model) {
      activeModel = settings.model;
      persistModel(activeModel);
    }
  } catch { /* use localStorage fallback */ }
  models = await fetchModels();
  if (!models.some((m) => m.id === activeModel)) {
    activeModel = models[0]?.id || DEFAULT_MODEL;
  }
  populateModelSelect(modelSelect, models, activeModel);
}

modelSelect?.addEventListener('change', async () => {
  activeModel = modelSelect.value;
  persistModel(activeModel);
  try {
    await saveSettings({ model: activeModel });
  } catch { /* local preference still applies */ }
});

initSearchHomeToggle($('#home-toggle'));

startThemeWatcher();
const urlParams = new URLSearchParams(location.search);

initModels().then(() => refresh().then(() => {
  const convParam = urlParams.get('conv');
  if (convParam && conversations.some((c) => c.id === convParam)) {
    selectConv(convParam);
    input.focus();
  } else if (!conversations.length) {
    newChat();
  }
}));