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
let messageQueue = [];
let queueSeq = 0;
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
      appendMsgMeta(div, m);
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
      // window.confirm/alert/prompt are suppressed in the side-panel WebContents,
      // so the old confirm() gate silently returned false and delete never ran.
      // Inline two-click confirm instead: first click arms (×→✓), second deletes.
      if (del.dataset.armed !== '1') {
        del.dataset.armed = '1';
        del.textContent = '✓';
        del.classList.add('confirm');
        del.title = 'Click again to delete';
        setTimeout(() => {
          if (del.dataset.armed === '1') {
            del.dataset.armed = '0';
            del.textContent = '×';
            del.classList.remove('confirm');
            del.title = 'Delete conversation';
          }
        }, 2500);
        return;
      }
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
        del.dataset.armed = '0';
        del.textContent = '×';
        del.classList.remove('confirm');
        console.error('delete failed:', err);
      }
    };
    li.appendChild(del);

    li.ondblclick = (e) => {
      e.preventDefault();
      e.stopPropagation();
      // window.prompt is suppressed in the side panel too — edit the title
      // inline instead (Enter saves, Esc cancels, blur saves).
      title.contentEditable = 'true';
      title.classList.add('editing');
      title.focus();
      const range = document.createRange();
      range.selectNodeContents(title);
      const sel = window.getSelection();
      sel.removeAllRanges();
      sel.addRange(range);
      let done = false;
      const finish = async (save) => {
        if (done) return;
        done = true;
        title.contentEditable = 'false';
        title.classList.remove('editing');
        const next = title.textContent.trim();
        if (save && next && next !== (c.title || 'Chat')) {
          try { await renameConversation(c, next); } catch (err) { console.error('rename failed:', err); }
        }
        renderConvList();
      };
      title.onkeydown = (ev) => {
        if (ev.key === 'Enter') { ev.preventDefault(); finish(true); }
        else if (ev.key === 'Escape') { ev.preventDefault(); finish(false); }
      };
      title.onblur = () => finish(true);
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
  sendBtn.disabled = false;  // stays enabled so a 2nd message queues
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
  if (!text.trim() || !activeId) return;
  if (busy) { enqueueMessage(text); input.value = ''; return; }
  const conv = conversations.find((c) => c.id === activeId);
  if (!conv) return;
  if (!(await ensureGrokReady())) return;  // Grok Build required to build/modify

  // Use the model the user picked — EVERY model can call the browser MCP tools
  // (they come from the global `xplorer` MCP server, not a specific model/agent).
  // No auto-upgrade: that silently turned Composer chats into grok-build.
  const model = activeModel;
  if (activeId) persistConvModel(activeId, model);  // record the chat's real model

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

  let reply = '';
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
          // The error event carries its text in `message` (the gateway) or
          // `error` (the grok process) — read both so detection sees the real
          // error, not the literal "chat failed" fallback.
          throw new Error(evt.message || evt.error || 'chat failed');
        }
      }
    }

    thinking.remove();
    messagesEl.querySelector('.msg.assistant.streaming')?.remove();
    if (reply.trim()) {
      conv.messages.push({ role: 'assistant', content: reply, model });
      renderMessages(conv);
      renderConvList();
    } else {
      throw new Error('empty response from Grok');
    }
  } catch (e) {
    if (e.name === 'AbortError') {
      thinking.remove();
      // Keep whatever Grok generated before Stop instead of discarding it.
      const partial = messagesEl.querySelector('.msg.assistant.streaming');
      if (reply.trim()) {
        if (partial) partial.remove();
        conv.messages.push({ role: 'assistant', content: reply.trim() + '\n\n_(stopped)_', model });
        renderMessages(conv);
        renderConvList();
      } else if (partial) {
        partial.remove();
      }
      return;
    }
    thinking.classList.add('error');
    thinking.innerHTML = '';
    const msg = e.message || '';
    // Grok isn't authenticated → show a clear "sign in" prompt instead of a
    // cryptic error. Covers BOTH cases: an expired token (upstream 401) and a
    // logged-out state (no token → grok can't fetch models → "unknown model id"
    // / "Couldn't set model").
    const needsLogin = /Unauthorized \(401\)|expired credentials|no auth context|grok login|PermissionDenied|not logged in|unknown model id|Couldn't set model|Run 'grok models'/i.test(msg);
    const errText = document.createElement('div');
    if (needsLogin) {
      errText.className = 'auth-expired';
      errText.innerHTML = '🔑 <b>Connect to Grok to continue.</b><br>' +
        'In a terminal, make sure Grok Build is installed, then run ' +
        '<code>grok login</code> to sign in — and Retry.';
    } else {
      errText.textContent = `Error: ${msg}`;
    }
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
    drainQueue();
    input.focus();
  }
}

// ---- Xplorer: message queue + per-chat info panel -------------------------
function enqueueMessage(text) {
  const t = (text || '').trim();
  if (!t) return;
  messageQueue.push({ id: ++queueSeq, text: t });
  renderQueue();
}

function drainQueue() {
  if (busy || !messageQueue.length) return;
  const next = messageQueue.shift();
  renderQueue();
  if (next) sendMessage(next.text);
}

function cancelQueued(id) {
  messageQueue = messageQueue.filter((m) => m.id !== id);
  renderQueue();
}

function interruptWith(id) {
  const item = messageQueue.find((m) => m.id === id);
  if (!item) return;
  messageQueue = messageQueue.filter((m) => m.id !== id);
  messageQueue.unshift(item);        // jump to the front of the queue
  renderQueue();
  if (busy) streamAbort?.abort();    // finally -> drainQueue sends it next
  else drainQueue();
}

function renderQueue() {
  const el = document.getElementById('msg-queue');
  if (!el) return;
  el.innerHTML = '';
  if (!messageQueue.length) { el.hidden = true; return; }
  el.hidden = false;
  for (const item of messageQueue) {
    const chip = document.createElement('div');
    chip.className = 'queue-chip';
    chip.title = item.text;
    const txt = document.createElement('span');
    txt.className = 'queue-text';
    txt.textContent = item.text;
    const send = document.createElement('button');
    send.type = 'button';
    send.className = 'queue-send';
    send.title = 'Send now (interrupt current)';
    send.textContent = '↩';
    send.addEventListener('click', () => interruptWith(item.id));
    const del = document.createElement('button');
    del.type = 'button';
    del.className = 'queue-del';
    del.title = 'Remove from queue';
    del.textContent = '×';
    del.addEventListener('click', () => cancelQueued(item.id));
    chip.append(txt, send, del);
    el.appendChild(chip);
  }
}

function appendMsgMeta(div, m) {
  const meta = document.createElement('div');
  meta.className = 'msg-meta';
  if (m.model) {
    const badge = document.createElement('span');
    badge.className = 'msg-model';
    badge.textContent = modelLabel(m.model, models);
    meta.appendChild(badge);
  }
  const copy = document.createElement('button');
  copy.type = 'button';
  copy.className = 'msg-copy';
  copy.textContent = 'Copy';
  copy.title = 'Copy response';
  copy.addEventListener('click', () => {
    navigator.clipboard?.writeText(m.content || '');
    copy.textContent = 'Copied ✓';
    setTimeout(() => { copy.textContent = 'Copy'; }, 1200);
  });
  meta.appendChild(copy);
  div.appendChild(meta);
}

function renderChatInfo() {
  const el = document.getElementById('chat-info');
  if (!el) return;
  el.innerHTML = '';
  const conv = conversations.find((c) => c.id === activeId);
  if (!conv) {
    const e = document.createElement('div');
    e.className = 'info-empty';
    e.textContent = 'No active chat.';
    el.appendChild(e);
    return;
  }
  const sid = conv.session_id || '';
  const rows = [
    ['Model', modelLabel(getConvModel(conv.id), models)],
    ['Messages', String((conv.messages || []).length)],
    ['Conversation ID', conv.id],
    ['Grok session', sid || '— (send a message first)'],
  ];
  const dl = document.createElement('dl');
  dl.className = 'info-grid';
  for (const [k, v] of rows) {
    const dt = document.createElement('dt'); dt.textContent = k;
    const dd = document.createElement('dd'); dd.textContent = v;
    dl.append(dt, dd);
  }
  el.appendChild(dl);
  if (sid) {
    const cmd = `grok -r ${sid}`;
    const actions = document.createElement('div');
    actions.className = 'info-actions';
    const btn = document.createElement('button');
    btn.type = 'button';
    btn.className = 'info-copy';
    btn.textContent = 'Copy CLI resume';
    btn.addEventListener('click', () => {
      navigator.clipboard?.writeText(cmd);
      btn.textContent = 'Copied ✓';
      setTimeout(() => { btn.textContent = 'Copy CLI resume'; }, 1500);
    });
    const code = document.createElement('code');
    code.className = 'info-cmd';
    code.textContent = cmd;
    actions.append(btn, code);
    el.appendChild(actions);
  }
  const settingsLink = document.createElement('a');
  settingsLink.className = 'info-settings-link';
  settingsLink.href = '/settings';
  settingsLink.target = '_blank';
  settingsLink.rel = 'noopener';
  settingsLink.textContent = 'Open Grok settings (models, max-turns, toolbar) →';
  el.appendChild(settingsLink);
}

(function wireChatInfo() {
  const btn = document.getElementById('chat-info-btn');
  const panel = document.getElementById('chat-info');
  if (!btn || !panel) return;
  btn.addEventListener('click', () => {
    const show = panel.hidden;
    if (show) renderChatInfo();
    panel.hidden = !show;
    btn.setAttribute('aria-expanded', show ? 'true' : 'false');
  });
})();
// ---------------------------------------------------------------------------

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
  const next = modelSelect.value;
  if (next === activeModel) return;
  activeModel = next;
  persistModel(activeModel);
  try {
    await saveSettings({ model: activeModel });
  } catch { /* local preference still applies */ }
  // A grok conversation is locked to one model/agent — switching mid-chat is
  // rejected (MODEL_SWITCH_INCOMPATIBLE_AGENT). If the current chat already has
  // messages, start a fresh one on the newly chosen model; otherwise just apply
  // it to the (still empty) current chat.
  const cur = conversations.find((c) => c.id === activeId);
  if (cur && (cur.messages?.length || 0) > 0) {
    await newChat();
  }
  if (activeId) persistConvModel(activeId, activeModel);
});

convFilterInput?.addEventListener('input', () => {
  convFilterQuery = convFilterInput.value;
  renderConvList();
});

convFilterInput?.addEventListener('keydown', (e) => {
  if (e.key === 'Escape') {
    convFilterQuery = '';
    if (convFilterInput) convFilterInput.value = '';
    renderConvList();
  }
});

document.addEventListener('keydown', (e) => {
  if (e.key === 'Escape') {
    if (convFilterQuery && convFilterInput) {
      convFilterQuery = '';
      convFilterInput.value = '';
      renderConvList();
      return;
    }
  }
  if (e.key !== '/' || e.metaKey || e.ctrlKey || e.altKey) return;
  const tag = (document.activeElement?.tagName || '').toLowerCase();
  if (tag === 'input' || tag === 'textarea' || tag === 'select') return;
  if (!convFilterInput) return;
  e.preventDefault();
  convFilterInput.focus();
  convFilterInput.select();
});

mountGrokToolbar({ pageHome: 'build' });

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

// The side panel keeps its WebContents alive across close/open, so a stale
// error / "thinking" bubble from a previous failed send lingers when reopened.
// On reopen, re-render the active conversation from its saved messages (errors
// aren't persisted, so they clear) — keeping the last conversation but clean.
document.addEventListener('visibilitychange', () => {
  if (document.visibilityState !== 'visible' || streamAbort) return;
  if (!activeId) return;
  const conv = conversations.find((c) => c.id === activeId);
  if (conv) renderMessages(conv);
});