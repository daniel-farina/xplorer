const $ = (s) => document.querySelector(s);
const messagesEl = $('#messages');
const convList = $('#conv-list');
const input = $('#input');
const sendBtn = $('#send');
const modelSelect = $('#model-select');

let conversations = [];
let activeId = null;
const streams = {};   // convId -> { aborter, running, reply, status, error }
const queues = {};    // convId -> [ {id, text} ]
let models = [];
let activeModel = getStoredModel();
let queueSeq = 0;
let convFilterQuery = '';

const convFilterInput = document.getElementById('conv-filter');
const stopBtn = document.getElementById('stop');

function chatConversations() {
  return conversations;  // include app-build conversations in the sidebar
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
      if (m.image) {
        const img = document.createElement('img');
        img.src = m.image;
        img.alt = 'captured tab';
        img.style.cssText = 'display:block;max-width:220px;max-height:160px;margin-top:6px;border-radius:8px;border:1px solid rgba(127,127,127,.3);';
        div.appendChild(img);
      }
    }
    messagesEl.appendChild(div);
  }
  appendTail(conv.id);
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
    if (c.kind === 'app') li.classList.add('conv-app');
    li.appendChild(title);
    if (c.kind === 'app') {
      const tag = document.createElement('span');
      tag.className = 'conv-app-tag';
      tag.textContent = '\u{1F527}';
      tag.title = 'App build';
      li.appendChild(tag);
    }

    if (isRunning(c.id)) {
      const dot = document.createElement('button');
      dot.type = 'button';
      dot.className = 'conv-running';
      dot.title = 'Agent running — click to stop';
      dot.textContent = '●';
      dot.onclick = (e) => { e.stopPropagation(); stopChat(c.id); };
      li.appendChild(dot);
    } else if (queues[c.id] && queues[c.id].length) {
      const q = document.createElement('span');
      q.className = 'conv-queued';
      q.title = queues[c.id].length + ' queued';
      q.textContent = '⋯';
      li.appendChild(q);
    }

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

/** convId -> { id, label } for scheduled-task conversations. */
let scheduleByConv = {};
let scheduleJobsCache = null;
let scheduleJobsCacheAt = 0;
const SCHEDULE_CACHE_MS = 5000;

function rebuildScheduleByConv(jobs) {
  scheduleByConv = {};
  for (const job of jobs) {
    if (!job?.id) continue;
    const info = { id: job.id, label: job.label || job.id };
    const convIds = new Set();
    if (job.run?.target_conv_id) convIds.add(job.run.target_conv_id);
    if (Array.isArray(job.history)) {
      for (const h of job.history) {
        if (h?.conv_id) convIds.add(h.conv_id);
      }
    }
    for (const cid of convIds) scheduleByConv[cid] = info;
  }
}

async function fetchScheduleJobs() {
  const now = Date.now();
  if (scheduleJobsCache && now - scheduleJobsCacheAt < SCHEDULE_CACHE_MS) {
    return scheduleJobsCache;
  }
  try {
    const res = await fetch('/api/schedules');
    if (!res.ok) return scheduleJobsCache || [];
    const data = await res.json();
    scheduleJobsCache = data.jobs || [];
    scheduleJobsCacheAt = now;
    rebuildScheduleByConv(scheduleJobsCache);
    return scheduleJobsCache;
  } catch {
    return scheduleJobsCache || [];
  }
}

function getScheduleForConv(convId) {
  return convId ? scheduleByConv[convId] || null : null;
}

function syncScheduleUrlParam(info) {
  const params = new URLSearchParams(location.search);
  if (info?.id) {
    params.set('schedule', info.id);
    if (activeId) params.set('conv', activeId);
  } else {
    params.delete('schedule');
  }
  const qs = params.toString();
  history.replaceState(null, '', qs ? `?${qs}` : location.pathname);
}

function updateScheduleLink() {
  const link = document.getElementById('chat-schedule-link');
  if (!link) return;
  const info = getScheduleForConv(activeId);
  if (!info) {
    link.hidden = true;
    syncScheduleUrlParam(null);
    return;
  }
  link.hidden = false;
  link.href = '/schedules?id=' + encodeURIComponent(info.id);
  const label = (info.label || '').trim();
  link.textContent = label ? `${label} · Settings` : 'Task settings';
  link.title = 'Open scheduled task settings';
  syncScheduleUrlParam(info);
}

async function resolveScheduleForConv(convId, hintJobId) {
  if (!convId) return null;
  if (hintJobId) {
    scheduleByConv[convId] = scheduleByConv[convId] || { id: hintJobId, label: '' };
  }
  const jobs = await fetchScheduleJobs();
  if (hintJobId) {
    const job = jobs.find((j) => j.id === hintJobId);
    if (job) scheduleByConv[convId] = { id: job.id, label: job.label || job.id };
  }
  return getScheduleForConv(convId);
}

function selectConv(id) {
  activeId = id;
  activeModel = getConvModel(id);
  if (modelSelect) modelSelect.value = activeModel;
  const conv = conversations.find((c) => c.id === id);
  renderConvList();
  renderMessages(conv);
  setComposerRunning();
  renderQueue();
  updateActiveBadge();
  updateScheduleLink();
  const hint = new URLSearchParams(location.search).get('schedule');
  resolveScheduleForConv(id, hint).then((info) => {
    if (activeId !== id) return;
    if (info) updateScheduleLink();
    const panel = document.getElementById('chat-info');
    if (panel && !panel.hidden) renderChatInfo();
  });
}

async function refresh() {
  const prev = conversations.slice();
  const data = await api('/api/conversations');
  conversations = data.conversations || [];
  syncRemoteRuns(prev);
  const chats = chatConversations();
  if (!activeId && chats.length) activeId = chats[0].id;
  if (activeId && !chats.some((c) => c.id === activeId)) {
    activeId = chats[0]?.id || null;
  }
  if (activeId) {
    const conv = conversations.find((c) => c.id === activeId);
    renderConvList();
    renderMessages(conv);
    setComposerRunning();
    updateActiveBadge();
    updateScheduleLink();
    fetchScheduleJobs().then(() => {
      if (conversations.some((c) => c.id === activeId)) updateScheduleLink();
    });
    startRemotePoll();
  } else {
    renderConvList();
    selectConv(activeId);
    startRemotePoll();
  }
}

// Targeted title-sync: pull fresh conversation metadata and copy over any
// changed titles in place (e.g. the server's async LLM-generated topic title).
// Deliberately does NOT replace the `conversations` array — that would disturb
// live message streams — it only patches the `title` field of existing entries.
async function syncConvTitles() {
  try {
    const data = await api('/api/conversations');
    const byId = new Map((data.conversations || []).map((c) => [c.id, c]));
    let changed = false;
    for (const c of conversations) {
      const f = byId.get(c.id);
      if (f && f.title && f.title !== c.title) { c.title = f.title; changed = true; }
    }
    if (changed) renderConvList();
  } catch {}
}

let remotePollTimer = null;

function anyRemoteRun() {
  return conversations.some((c) => c.running);
}

function stopRemotePoll() {
  if (remotePollTimer) {
    clearInterval(remotePollTimer);
    remotePollTimer = null;
  }
}

function startRemotePoll() {
  stopRemotePoll();
  if (!anyRemoteRun()) return;
  remotePollTimer = setInterval(() => {
    refresh().catch((e) => console.error('remote poll failed:', e));
  }, 2000);
}

async function newChat() {
  const conv = await api('/api/conversations', { method: 'POST', body: '{}' });
  conversations.unshift(conv);
  activeId = conv.id;
  renderConvList();
  renderMessages(conv);
  input.focus();
}

// (setStreamingUi replaced by setComposerRunning, defined below)

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

const currentConv = () => conversations.find((c) => c.id === activeId);
function isRunning(convId) {
  if (streams[convId] && streams[convId].running) return true;
  const conv = conversations.find((c) => c.id === convId);
  return !!(conv && conv.running);
}
function activeRunCount() {
  const ids = new Set();
  for (const c of conversations) {
    if (c.running) ids.add(c.id);
  }
  for (const [id, st] of Object.entries(streams)) {
    if (st.running) ids.add(id);
  }
  return ids.size;
}

function syncRemoteRuns(prevConvs) {
  const prevById = new Map((prevConvs || []).map((c) => [c.id, c]));
  for (const c of conversations) {
    const wasRunning = prevById.get(c.id)?.running;
    if (c.running && !streams[c.id]?.running) {
      streams[c.id] = {
        running: true,
        remote: true,
        reply: '',
        status: 'Agent is working…',
      };
    } else if (!c.running && streams[c.id]?.remote) {
      delete streams[c.id];
    } else if (c.running && streams[c.id]?.remote && wasRunning) {
      // Still running — keep tail visible.
    }
  }
}

function updateActiveBadge() {
  const el = document.getElementById('active-count');
  if (!el) return;
  const n = activeRunCount();
  el.textContent = n ? String(n) : '';
  el.hidden = !n;
  el.title = n ? (n + ' chat' + (n > 1 ? 's' : '') + ' running') : '';
}

// Send stays enabled (a 2nd message queues); Stop shows when the ACTIVE chat runs.
function setComposerRunning() {
  if (sendBtn) sendBtn.disabled = false;
  if (stopBtn) stopBtn.hidden = !isRunning(activeId);
}

// Kill an agent: abort the UI stream AND terminate the grok process on the gateway.
async function stopChat(convId) {
  const st = streams[convId];
  if (st && st.aborter) st.aborter.abort();
  try { await api('/api/conversations/' + convId + '/stop', { method: 'POST' }); } catch (e) { /* best effort */ }
  if (st?.remote) delete streams[convId];
  await refresh();
  startRemotePoll();
}

function appendError(convId) {
  const st = streams[convId];
  if (!st || !st.error) return;
  const box = document.createElement('div');
  box.className = 'msg assistant thinking error';
  const errText = document.createElement('div');
  if (st.error.needsLogin) {
    errText.className = 'auth-expired';
    errText.innerHTML = '\U0001F511 <b>Connect to Grok to continue.</b><br>' +
      'In a terminal, make sure Grok Build is installed, then run ' +
      '<code>grok login</code> to sign in — and Retry.';
  } else {
    errText.textContent = 'Error: ' + (st.error.msg || '');
  }
  box.appendChild(errText);
  const retryBtn = document.createElement('button');
  retryBtn.type = 'button';
  retryBtn.className = 'retry-btn';
  retryBtn.textContent = 'Retry';
  const text = st.error.text;
  retryBtn.onclick = () => { delete streams[convId]; renderMessages(currentConv()); sendMessage(text, { retry: true, convId }); };
  box.appendChild(retryBtn);
  messagesEl.appendChild(box);
}

// Streaming/error tail for a conversation (only painted when it is active).
function appendTail(convId) {
  const st = streams[convId];
  if (!st) return;
  if (st.running) {
    const live = document.createElement('div');
    live.className = 'msg assistant streaming markdown';
    if (st.reply) { live.innerHTML = renderMarkdown(st.reply); wireCodeCopyButtons(live); } else { live.hidden = true; }
    messagesEl.appendChild(live);
    const think = document.createElement('div');
    think.className = 'msg assistant thinking';
    const panel = document.createElement('details');
    panel.className = 'thinking-panel';
    panel.open = true;
    const sum = document.createElement('summary');
    sum.textContent = '✦ Thinking';
    panel.appendChild(sum);
    const tt = document.createElement('div');
    tt.className = 'thinking-text';
    tt.textContent = st.thinking || '';
    panel.appendChild(tt);
    if (!st.thinking) panel.hidden = true;
    think.appendChild(panel);
    const s = document.createElement('div');
    s.className = 'stream-status';
    s.textContent = st.status || 'Grok is thinking…';
    think.appendChild(s);
    messagesEl.appendChild(think);
  } else if (st.error) {
    appendError(convId);
  }
}

// Incremental update of the active tail during streaming (no full re-render).
function updateTail(convId) {
  if (convId !== activeId) return;
  const st = streams[convId];
  if (!st || !st.running) return;
  const live = messagesEl.querySelector('.msg.assistant.streaming');
  const think = messagesEl.querySelector('.msg.assistant.thinking');
  if (!live || !think) { renderMessages(currentConv()); return; }
  if (st.reply) { live.hidden = false; live.innerHTML = renderMarkdown(st.reply); wireCodeCopyButtons(live); }
  const panel = think.querySelector('.thinking-panel');
  const tt = think.querySelector('.thinking-text');
  if (panel && tt) {
    if (st.thinking) { panel.hidden = false; tt.textContent = st.thinking; tt.scrollTop = tt.scrollHeight; }
    else { panel.hidden = true; }
  }
  const s = think.querySelector('.stream-status');
  if (s) s.textContent = st.status || 'Grok is thinking…';
  messagesEl.scrollTop = messagesEl.scrollHeight;
}

// Each conversation streams independently — switching/closing never interrupts it.
async function sendMessage(text, { retry = false, convId = activeId } = {}) {
  text = (text || '').trim();
  if (!text || !convId) return;
  const conv = conversations.find((c) => c.id === convId);
  if (!conv) return;
  if (isRunning(convId)) { enqueueMessage(convId, text); if (convId === activeId) input.value = ''; return; }
  if (!(await ensureGrokReady())) return;

  const model = getConvModel(convId) || activeModel;
  persistConvModel(convId, model);

  if (!retry) {
    conv.messages = conv.messages || [];
    conv.messages.push({ role: 'user', content: text });
    if (convId === activeId) input.value = '';
  }

  const st = { aborter: new AbortController(), running: true, reply: '', status: 'Grok is thinking…', thinking: '', error: null, model };
  streams[convId] = st;
  if (convId === activeId) { renderMessages(conv); setComposerRunning(); }
  updateActiveBadge();
  renderConvList();

  let reply = '';
  try {
    const appBuild = conv.kind === 'app' && conv.app_id;
    const url = appBuild
      ? '/api/apps/' + conv.app_id + '/build/stream'
      : '/api/conversations/' + convId + '/message/stream';
    const res = await fetch(url, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ message: text, model }),
      signal: st.aborter.signal,
    });
    if (!res.ok) { const err = await res.json().catch(() => ({})); throw new Error(err.error || res.statusText); }
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
        if (evt.type === 'thought') {
          st.thinking += evt.data || '';
          updateTail(convId);
        } else if (evt.type === 'tool' || evt.type === 'tool_use') {
          const name = evt.name || evt.tool || evt.data || 'tool';
          st.status = 'Using ' + name + '…';
          updateTail(convId);
        } else if (evt.type === 'text') {
          reply += evt.data || '';
          st.reply = reply;
          updateTail(convId);
        } else if (evt.type === 'result') {
          if (evt.reply) { reply = evt.reply; st.reply = reply; }
          if (evt.sessionId) conv.session_id = evt.sessionId;
        } else if (evt.type === 'error') {
          throw new Error(evt.message || evt.error || 'chat failed');
        }
      }
    }
    if (reply.trim()) { conv.messages.push({ role: 'assistant', content: reply, model }); delete streams[convId]; }
    else { throw new Error('empty response from Grok'); }
  } catch (e) {
    if (e.name === 'AbortError') {
      if (reply.trim()) conv.messages.push({ role: 'assistant', content: reply.trim() + '\n\n_(stopped)_', model });
      delete streams[convId];
    } else {
      const msg = e.message || '';
      const needsLogin = /Unauthorized \(401\)|expired credentials|no auth context|grok login|PermissionDenied|not logged in|unknown model id|Couldn't set model|Run 'grok models'/i.test(msg);
      st.error = { msg, needsLogin, text };
    }
  } finally {
    if (streams[convId]) streams[convId].running = false;
    updateActiveBadge();
    renderConvList();
    if (convId === activeId) { renderMessages(currentConv()); setComposerRunning(); input.focus(); }
    // For a brand-new chat, the server upgrades the title asynchronously (an
    // LLM-generated topic). Poll a few times to pick it up without disturbing
    // the live `conversations` array / streams.
    if ((conv.title || '') === 'New chat' || (conv.messages?.length || 0) <= 2) {
      syncConvTitles();
      setTimeout(syncConvTitles, 4000);
      setTimeout(syncConvTitles, 12000);
    }
    drainQueue(convId);
  }
}

// Strip the trailing ```json {...} block the mode=images system prompt appends —
// the prose answer above it is what we show; the raw JSON only bloats + (as one
// long line in a code block) blows out the panel width.
function stripJsonBlock(s) {
  const cut = (s || '').split(/```json/i)[0].trim();
  return cut || (s || '').trim();
}

// Persist an image-search message server-side so the chat survives a
// refresh/remote-poll — /api/search/stream doesn't write to the conversation.
async function persistImageMsg(convId, role, content, image, model) {
  const body = { role, content };
  if (image) body.image = image;
  if (model) body.model = model;
  const payload = JSON.stringify(body);
  // Retry a couple of times — under a burst (several image searches at once) a
  // connection can be dropped, and a lost append means the chat vanishes on the
  // next refresh.
  for (let attempt = 0; attempt < 3; attempt++) {
    try {
      await api('/api/conversations/' + convId + '/append', { method: 'POST', body: payload });
      return;
    } catch (e) {
      await new Promise((r) => setTimeout(r, 250 * (attempt + 1)));
    }
  }
}

// Shrink a captured image to a small thumbnail (persisted with the user message
// so the chat doesn't carry megabytes of base64 in the session store).
function downscaleDataUrl(dataUrl, max = 240) {
  return new Promise((resolve) => {
    try {
      const img = new Image();
      img.onload = () => {
        const s = Math.min(1, max / Math.max(img.width, img.height));
        const w = Math.max(1, Math.round(img.width * s));
        const h = Math.max(1, Math.round(img.height * s));
        const c = document.createElement('canvas'); c.width = w; c.height = h;
        c.getContext('2d').drawImage(img, 0, 0, w, h);
        try { resolve(c.toDataURL('image/jpeg', 0.7)); } catch { resolve(dataUrl); }
      };
      img.onerror = () => resolve(dataUrl);
      img.src = dataUrl;
    } catch { resolve(dataUrl); }
  });
}

// ---- Grok image search: capture the current tab, stream a Grok vision answer.
// No rebuild — reuses POST /api/screenshot + POST /api/search/stream (mode=images)
// forced onto grok-composer-2.5-fast (the only vision-capable model). The query is
// phrased to answer directly from the image, sidestepping the mode=images system
// prompt's "web search for similar images" instruction that grok-composer can't do.
async function runImageSearch() {
  if (!(await ensureGrokReady())) return;
  // Image search always opens a FRESH chat — never hijack the current one.
  const conv = await api('/api/conversations', { method: 'POST', body: '{}' });
  conversations.unshift(conv);
  const convId = conv.id;
  activeId = convId;
  conv.messages = conv.messages || [];

  const st = { aborter: new AbortController(), running: true, reply: '', status: 'Capturing this tab…', thinking: '', error: null, model: 'grok-composer-2.5-fast' };
  streams[convId] = st;
  renderConvList();
  if (convId === activeId) { renderMessages(conv); setComposerRunning(); }
  updateActiveBadge();

  let shot, region = false;
  try {
    // Prefer a region the native Lens menu drag-selected (one-shot); otherwise
    // capture the whole tab (the sidebar-button path).
    const pending = await api('/api/pending-image').catch(() => null);
    if (pending && pending.image) { shot = pending; region = true; }
    else { shot = await api('/api/screenshot', { method: 'POST', body: '{}' }); }
    if (!shot || !shot.image) throw new Error(shot?.error || 'capture failed');
  } catch (e) {
    st.running = false;
    st.error = { msg: 'Could not capture the tab: ' + (e.message || ''), text: '' };
    if (convId === activeId) renderMessages(conv);
    renderConvList(); updateActiveBadge();
    return;
  }

  const userContent = region
    ? '\u{1F5BC}\u{FE0F} Search this selection with Grok'
    : '\u{1F5BC}\u{FE0F} Search this tab’s image with Grok' + (shot.title ? ' — ' + shot.title : '');
  const thumb = await downscaleDataUrl(
    'data:' + (shot.mime_type || 'image/png') + ';base64,' + shot.image);
  conv.messages.push({ role: 'user', content: userContent, image: thumb });
  persistImageMsg(convId, 'user', userContent, thumb);  // persist so it survives refresh
  st.status = 'Grok is looking at the image…';
  if (convId === activeId) renderMessages(conv);

  let reply = '';
  try {
    const res = await fetch('/api/search/stream', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        mode: 'images',
        image: shot.image,
        image_mime: shot.mime_type || 'image/png',
        model: 'grok-composer-2.5-fast',
        query: 'Describe exactly what is visible in this image: identify the main subject or content and any notable text or UI, then add any useful context. Answer directly from the image; do not call any tools or web search. Reply in plain prose — do NOT output JSON.',
      }),
      signal: st.aborter.signal,
    });
    if (!res.ok) { const err = await res.json().catch(() => ({})); throw new Error(err.error || res.statusText); }
    const reader = res.body.getReader();
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
        if (evt.type === 'thought') { st.thinking += evt.data || ''; updateTail(convId); }
        else if (evt.type === 'tool' || evt.type === 'tool_use') { st.status = 'Using ' + (evt.name || evt.tool || 'tool') + '…'; updateTail(convId); }
        else if (evt.type === 'text') { reply += evt.data || ''; st.reply = reply; updateTail(convId); }
        else if (evt.type === 'result') { if (evt.reply) { reply = evt.reply; st.reply = reply; } }
        else if (evt.type === 'error') { throw new Error(evt.message || evt.error || 'image search failed'); }
      }
    }
    const clean = stripJsonBlock(reply);
    if (clean.trim()) {
      conv.messages.push({ role: 'assistant', content: clean, model: st.model });
      persistImageMsg(convId, 'assistant', clean, null, st.model);
      delete streams[convId];
    } else { throw new Error('empty response from Grok'); }
  } catch (e) {
    if (e.name === 'AbortError') {
      if (reply.trim()) conv.messages.push({ role: 'assistant', content: stripJsonBlock(reply) + '\n\n_(stopped)_', model: st.model });
      delete streams[convId];
    } else {
      st.error = { msg: e.message || 'image search failed', text: '' };
    }
  } finally {
    if (streams[convId]) streams[convId].running = false;
    updateActiveBadge(); renderConvList();
    if (convId === activeId) { renderMessages(currentConv()); setComposerRunning(); }
  }
}

// ---- Xplorer: message queue + per-chat info panel -------------------------
function enqueueMessage(convId, text) {
  const t = (text || '').trim();
  if (!t) return;
  (queues[convId] = queues[convId] || []).push({ id: ++queueSeq, text: t });
  if (convId === activeId) renderQueue();
  renderConvList();
}

function drainQueue(convId) {
  if (isRunning(convId)) return;
  const q = queues[convId];
  if (!q || !q.length) return;
  const next = q.shift();
  if (convId === activeId) renderQueue();
  renderConvList();
  if (next) sendMessage(next.text, { convId });
}

function cancelQueued(id) {
  const q = queues[activeId];
  if (!q) return;
  queues[activeId] = q.filter((m) => m.id !== id);
  renderQueue();
  renderConvList();
}

function interruptWith(id) {
  const q = queues[activeId];
  if (!q) return;
  const item = q.find((m) => m.id === id);
  if (!item) return;
  queues[activeId] = q.filter((m) => m.id !== id);
  queues[activeId].unshift(item);
  renderQueue();
  if (isRunning(activeId)) stopChat(activeId);  // finally -> drainQueue sends it next
  else drainQueue(activeId);
}

function renderQueue() {
  const el = document.getElementById('msg-queue');
  if (!el) return;
  el.innerHTML = '';
  const q = queues[activeId] || [];
  if (!q.length) { el.hidden = true; return; }
  el.hidden = false;
  for (const item of q) {
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
  const sched = getScheduleForConv(conv.id);
  const rows = [
    ['Model', modelLabel(getConvModel(conv.id), models)],
    ['Messages', String((conv.messages || []).length)],
    ['Conversation ID', conv.id],
    ['Grok session', sid || '— (send a message first)'],
  ];
  if (sched) {
    rows.splice(1, 0, ['Scheduled task', sched.label || sched.id]);
  }
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
  if (sched) {
    const taskLink = document.createElement('a');
    taskLink.className = 'info-settings-link';
    taskLink.href = '/schedules?id=' + encodeURIComponent(sched.id);
    taskLink.textContent = 'Open task settings (schedule, prompt, history) →';
    el.appendChild(taskLink);
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
document.getElementById('img-search')?.addEventListener('click', () => {
  // Trigger the same native region drag-select as the Lens menu — the user drags
  // an area on the page; the selection is written to a pending image and the side
  // panel re-opens (?imagesearch=1) to run Grok vision on it. Avoids the
  // whole-tab /api/screenshot path (which could hang).
  api('/api/region-search', { method: 'POST', body: '{}' }).catch(() => {});
});

stopBtn?.addEventListener('click', () => {
  if (activeId) stopChat(activeId);
});

input.addEventListener('keydown', (e) => {
  if (e.key === 'Enter' && !e.shiftKey) {
    e.preventDefault();
    sendMessage(input.value);
  }
});

// Response-settings gear: effort + max turns, persisted via /api/settings.
(function setupEffortGear() {
  const gear = $('#effort-gear');
  const pop = $('#effort-popover');
  const effortSel = $('#effort-select');
  const maxturns = $('#maxturns-input');
  if (!gear || !pop) return;
  gear.addEventListener('click', (e) => { e.stopPropagation(); pop.hidden = !pop.hidden; });
  document.addEventListener('click', (e) => {
    if (!pop.hidden && !pop.contains(e.target) && !gear.contains(e.target)) pop.hidden = true;
  });
  const save = async () => {
    const body = {};
    if (effortSel) body.effort = effortSel.value;
    if (maxturns && maxturns.value) body.max_turns = parseInt(maxturns.value, 10);
    try { await saveSettings(body); } catch { /* local-only */ }
  };
  effortSel?.addEventListener('change', save);
  maxturns?.addEventListener('change', save);
  fetchSettings().then((s) => {
    if (s && s.effort && effortSel) effortSel.value = s.effort;
    if (s && s.max_turns && maxturns) maxturns.value = s.max_turns;
  }).catch(() => {});
})();

async function initModels() {
  try {
    const settings = await fetchSettings();
    if (settings.model) {
      activeModel = settings.model;
      persistModel(activeModel);
    }
  } catch { /* use localStorage fallback */ }
  models = await fetchModels();
  // Never clobber a remembered choice: only re-pick when the list is non-empty
  // and the saved model truly isn't in it — and even then prefer the stored
  // model over models[0] (which is composer-fast and would silently reset it).
  if (models.length && !models.some((m) => m.id === activeModel)) {
    const stored = getStoredModel();
    activeModel = models.some((m) => m.id === stored) ? stored : models[0].id;
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
  startRemotePoll();
  consumePendingApp();
  if (urlParams.get('imagesearch')) {
    // Native "Search this tab with Image Search" (Lens) routed here by
    // grok_companion::GrokImageSearchForTab — capture the active tab + Grok vision.
    history.replaceState(null, '', location.pathname);  // consume the one-shot trigger
    runImageSearch();
  }
}));

// Cross-platform "update available" banner (no-op on macOS, where Sparkle's
// native dialog handles it). The gateway only checks upstream every ~6h, so a
// mount on load plus a recheck on visibilitychange is plenty.
mountUpdateBanner();

// The side panel keeps its WebContents alive across close/open, so a stale
// error / "thinking" bubble from a previous failed send lingers when reopened.
// On reopen, re-render the active conversation from its saved messages (errors
// aren't persisted, so they clear) — keeping the last conversation but clean.
document.addEventListener('visibilitychange', () => {
  if (document.visibilityState !== 'visible') {
    stopRemotePoll();
    return;
  }
  refresh().then(() => startRemotePoll()).catch(() => {});
  mountUpdateBanner();  // recheck for a newer release on reopen
  if (isRunning(activeId)) return;
  if (!activeId) return;
  const conv = conversations.find((c) => c.id === activeId);
  if (conv) renderMessages(conv);
});

// ---- App handoff: auto-select + auto-send a freshly created app conversation ----
// /apps (same gateway origin, separate persistent WebContents) writes
//   localStorage['xplorer_pending_app'] = { conv, prompt, ts }
// The side panel WebContents persists and loads '/' only once, so we pick it up
// via localStorage + 'storage'/visibilitychange, not a per-open URL param.
const PENDING_APP_KEY = 'xplorer_pending_app';
let consumingPendingApp = false;
async function consumePendingApp() {
  if (consumingPendingApp) return;
  let pend;
  try { pend = JSON.parse(localStorage.getItem(PENDING_APP_KEY) || 'null'); }
  catch { pend = null; }
  if (!pend || !pend.conv || !pend.prompt) return;
  consumingPendingApp = true;
  try {
    if (!conversations.some((c) => c.id === pend.conv)) {
      await refresh();
    }
    if (!conversations.some((c) => c.id === pend.conv)) return;
    localStorage.removeItem(PENDING_APP_KEY);  // claim BEFORE sending -> dedupe
    selectConv(pend.conv);
    input.focus();
    sendMessage(pend.prompt, { convId: pend.conv });  // kind=app -> build/stream
  } finally {
    consumingPendingApp = false;
  }
}
window.addEventListener('storage', (e) => {
  if (e.key && e.key !== PENDING_APP_KEY) return;
  if (e.newValue == null) return;
  consumePendingApp();
});
document.addEventListener('visibilitychange', () => {
  if (document.visibilityState === 'visible') consumePendingApp();
});
