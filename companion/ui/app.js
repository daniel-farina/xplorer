const $ = (s) => document.querySelector(s);
const messagesEl = $('#messages');
const convList = $('#conv-list');
const input = $('#input');
const sendBtn = $('#send');
const modelSelect = $('#model-select');

let conversations = [];
let activeId = null;
let busy = false;
let streamAbort = null;
let models = [];
let activeModel = getStoredModel();
let convFilterQuery = '';

const convFilterInput = document.getElementById('conv-filter');
const stopBtn = document.getElementById('stop');

function chatConversations() {
  return conversations.filter((c) => c.kind !== 'app');
}

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

async function deleteConversation(convId) {
  const r = await fetch(`/api/conversations/${encodeURIComponent(convId)}`, {
    method: 'DELETE',
  });
  const data = await r.json().catch(() => ({}));
  if (!r.ok) throw new Error(data.error || r.statusText);
}

async function renameConversation(conv, nextTitle) {
  const title = String(nextTitle || '').trim();
  if (!title || title === conv.title) return;
  const r = await fetch(`/api/conversations/${encodeURIComponent(conv.id)}/rename`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ title }),
  });
  const data = await r.json().catch(() => ({}));
  if (!r.ok) throw new Error(data.error || r.statusText);
  conv.title = data.title || title;
}

function filteredConversations() {
  const q = convFilterQuery.trim().toLowerCase();
  const list = chatConversations();
  if (!q) return list;
  return list.filter((c) => (c.title || 'Chat').toLowerCase().includes(q));
}

function renderConvList() {
  convList.innerHTML = '';
  const list = filteredConversations();
  if (!list.length && convFilterQuery.trim()) {
    const empty = document.createElement('li');
    empty.className = 'conv-empty';
    empty.textContent = 'No matching chats';
    convList.appendChild(empty);
    return;
  }
  for (const c of list) {
    const li = document.createElement('li');
    li.className = c.id === activeId ? 'active' : '';
    li.title = 'Double-click to rename';
    li.onclick = () => selectConv(c.id);

    const title = document.createElement('span');
    title.className = 'conv-title';
    title.textContent = c.title || 'Chat';
    li.appendChild(title);

    const del = document.createElement('button');
    del.type = 'button';
    del.className = 'conv-delete';
    del.title = 'Delete conversation';
    del.setAttribute('aria-label', 'Delete conversation');
    del.textContent = '×';
    del.onclick = async (e) => {
      e.stopPropagation();
      if (!confirm(`Delete "${c.title || 'Chat'}"?`)) return;
      try {
        await deleteConversation(c.id);
        conversations = conversations.filter((x) => x.id !== c.id);
        if (activeId === c.id) {
          const next = chatConversations()[0];
          activeId = next?.id || null;
          if (!activeId) {
            await newChat();
            return;
          }
        }
        renderConvList();
        selectConv(activeId);
      } catch (err) {
        alert(err.message);
      }
    };
    li.appendChild(del);

    li.ondblclick = async (e) => {
      e.preventDefault();
      e.stopPropagation();
      const next = window.prompt('Rename conversation', c.title || 'Chat');
      if (!next?.trim()) return;
      try {
        await renameConversation(c, next.trim());
        renderConvList();
      } catch (err) {
        alert(err.message);
      }
    };
    convList.appendChild(li);
  }
}

function selectConv(id) {
  activeId = id;
  activeModel = getConvModel(id);
  if (modelSelect) modelSelect.value = activeModel;
  const conv = conversations.find((c) => c.id === id);
  renderConvList();
  renderMessages(conv);
}

async function refresh() {
  const data = await api('/api/conversations');
  conversations = data.conversations || [];
  const chats = chatConversations();
  if (!activeId && chats.length) activeId = chats[0].id;
  if (activeId && !chats.some((c) => c.id === activeId)) {
    activeId = chats[0]?.id || null;
  }
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

function setStreamingUi(active) {
  busy = active;
  sendBtn.disabled = active;
  if (stopBtn) stopBtn.hidden = !active;
}

function appendStatusLine(container, text) {
  const line = document.createElement('div');
  line.className = 'stream-status-line';
  line.textContent = text;
  container.appendChild(line);
  while (container.children.length > 6) {
    container.firstChild.remove();
  }
  messagesEl.scrollTop = messagesEl.scrollHeight;
}

async function sendMessage(text, { retry = false } = {}) {
  if (!text.trim() || busy || !activeId) return;
  const conv = conversations.find((c) => c.id === activeId);
  if (!conv) return;

  const model = messageNeedsBrowserTools(text)
    ? modelForSearchMode('web', activeModel, models)
    : activeModel;

  if (!retry) {
    conv.messages = conv.messages || [];
    conv.messages.push({ role: 'user', content: text });
    renderMessages(conv);
    input.value = '';
  }

  setStreamingUi(true);
  streamAbort = new AbortController();

  const thinking = document.createElement('div');
  thinking.className = 'msg assistant thinking';
  const status = document.createElement('div');
  status.className = 'stream-status';
  status.textContent = 'Grok is thinking…';
  thinking.appendChild(status);
  messagesEl.appendChild(thinking);
  messagesEl.scrollTop = messagesEl.scrollHeight;

  try {
    const res = await fetch(`/api/conversations/${activeId}/message/stream`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ message: text, model }),
      signal: streamAbort.signal,
    });
    if (!res.ok) {
      const err = await res.json().catch(() => ({}));
      throw new Error(err.error || res.statusText);
    }

    const reader = res.body.getReader();
    const decoder = new TextDecoder();
    let buffer = '';
    let reply = '';
    let thoughtBuf = '';

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
          if (evt.model_label) {
            appendStatusLine(status, `Model: ${evt.model_label}`);
          }
        } else if (evt.type === 'thought') {
          thoughtBuf += evt.data || '';
          if (thoughtBuf.length > 120) thoughtBuf = thoughtBuf.slice(-120);
          status.textContent = thoughtBuf || 'Grok is thinking…';
        } else if (evt.type === 'tool' || evt.type === 'tool_use') {
          const name = evt.name || evt.tool || evt.data || 'tool';
          appendStatusLine(status, `Using ${name}…`);
        } else if (evt.type === 'text') {
          reply += evt.data || '';
          const div = messagesEl.querySelector('.msg.assistant.streaming') ||
            (() => {
              const el = document.createElement('div');
              el.className = 'msg assistant streaming markdown';
              messagesEl.insertBefore(el, thinking);
              return el;
            })();
          div.innerHTML = renderMarkdown(reply);
          wireCodeCopyButtons(div);
          messagesEl.scrollTop = messagesEl.scrollHeight;
        } else if (evt.type === 'result') {
          if (evt.reply) reply = evt.reply;
          if (evt.sessionId) conv.session_id = evt.sessionId;
        } else if (evt.type === 'error') {
          throw new Error(evt.error || 'chat failed');
        }
      }
    }

    thinking.remove();
    messagesEl.querySelector('.msg.assistant.streaming')?.remove();
    if (reply.trim()) {
      conv.messages.push({ role: 'assistant', content: reply });
      renderMessages(conv);
      renderConvList();
    } else {
      throw new Error('empty response from Grok');
    }
  } catch (e) {
    if (e.name === 'AbortError') {
      thinking.remove();
      messagesEl.querySelector('.msg.assistant.streaming')?.remove();
      return;
    }
    thinking.classList.add('error');
    thinking.innerHTML = '';
    const errText = document.createElement('div');
    errText.textContent = `Error: ${e.message}`;
    thinking.appendChild(errText);
    const retryBtn = document.createElement('button');
    retryBtn.type = 'button';
    retryBtn.className = 'retry-btn';
    retryBtn.textContent = 'Retry';
    retryBtn.onclick = () => {
      thinking.remove();
      sendMessage(text, { retry: true });
    };
    thinking.appendChild(retryBtn);
  } finally {
    streamAbort = null;
    setStreamingUi(false);
    input.focus();
  }
}

$('#composer').onsubmit = (e) => {
  e.preventDefault();
  sendMessage(input.value);
};

$('#new-chat').onclick = newChat;

stopBtn?.addEventListener('click', () => {
  streamAbort?.abort();
});

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
  if (activeId) persistConvModel(activeId, activeModel);
  try {
    await saveSettings({ model: activeModel });
  } catch { /* local preference still applies */ }
});

convFilterInput?.addEventListener('input', () => {
  convFilterQuery = convFilterInput.value;
  renderConvList();
});

document.addEventListener('keydown', (e) => {
  if (e.key !== '/' || e.metaKey || e.ctrlKey || e.altKey) return;
  const tag = (document.activeElement?.tagName || '').toLowerCase();
  if (tag === 'input' || tag === 'textarea' || tag === 'select') return;
  if (!convFilterInput) return;
  e.preventDefault();
  convFilterInput.focus();
  convFilterInput.select();
});

initSearchHomeToggle($('#home-toggle'));

startThemeWatcher();
const urlParams = new URLSearchParams(location.search);

initModels().then(() => refresh().then(() => {
  const convParam = urlParams.get('conv');
  if (convParam && conversations.some((c) => c.id === convParam)) {
    selectConv(convParam);
    input.focus();
  } else if (!chatConversations().length) {
    newChat();
  }
}));