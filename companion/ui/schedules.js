// Scheduled-tasks management view (/schedules). Lives inside the side panel.
//
// Data model (GET /api/schedules -> {jobs:[...]}):
//   job = {
//     id, label, enabled,
//     trigger: { cron, interval_sec, once_at_us },   // once_at_us is a STRING
//     next_fire_us, last_fire_us,                     // STRINGS of Win-epoch us
//     last_status,                                    // ok/failed/running/...
//     run: { message, model, cwd, target_conv_id, app_id },
//     max_concurrent_tabs,
//     history: [ { fired_us, status, conv_id } ],     // most recent first
//   }
//
// All *_us fields are STRINGS of Windows-epoch microseconds:
//   unix_seconds = int(us)/1e6 - 11644473600
//
// window.confirm/alert/prompt are suppressed in the side panel, so deletes go
// through the in-page #sched-confirm overlay and edits are inline inputs.

const WIN_EPOCH_OFFSET_S = 11644473600; // seconds between 1601 and 1970 epochs

let jobs = [];
let selectedId = null;
let models = [];

// ---------------------------------------------------------------------------
// Time helpers
// ---------------------------------------------------------------------------

/** Windows-epoch microseconds (STRING) -> JS Date, or null if empty/zero. */
function usToDate(us) {
  if (us === null || us === undefined || us === '' || us === '0') return null;
  const n = Number(us);
  if (!Number.isFinite(n) || n <= 0) return null;
  const unixMs = (n / 1e6 - WIN_EPOCH_OFFSET_S) * 1000;
  if (!Number.isFinite(unixMs)) return null;
  return new Date(unixMs);
}

/** A JS Date -> the datetime-local input value (local wall-clock, no tz). */
function dateToLocalInput(d) {
  if (!d) return '';
  const pad = (x) => String(x).padStart(2, '0');
  return `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())}` +
    `T${pad(d.getHours())}:${pad(d.getMinutes())}`;
}

/** A datetime-local input value -> Windows-epoch microseconds STRING. */
function localInputToUs(value) {
  if (!value) return '';
  const d = new Date(value); // interpreted as local time
  if (isNaN(d.getTime())) return '';
  const unixS = d.getTime() / 1000;
  const winUs = Math.round((unixS + WIN_EPOCH_OFFSET_S) * 1e6);
  return String(winUs);
}

/** Friendly relative + absolute time for a *_us string. */
function humanizeUs(us, { future = false } = {}) {
  const d = usToDate(us);
  if (!d) return future ? 'not scheduled' : 'never';
  const now = Date.now();
  const diffMs = d.getTime() - now;
  const absS = Math.abs(diffMs) / 1000;
  const rel = relTime(absS);
  const abs = d.toLocaleString([], {
    month: 'short', day: 'numeric',
    hour: 'numeric', minute: '2-digit',
  });
  if (diffMs >= 0) {
    return absS < 30 ? `now (${abs})` : `in ${rel} · ${abs}`;
  }
  return absS < 30 ? `just now (${abs})` : `${rel} ago · ${abs}`;
}

function relTime(seconds) {
  if (seconds < 60) return `${Math.round(seconds)}s`;
  const m = seconds / 60;
  if (m < 60) return `${Math.round(m)}m`;
  const h = m / 60;
  if (h < 24) return `${Math.round(h)}h`;
  const days = h / 24;
  if (days < 14) return `${Math.round(days)}d`;
  return `${Math.round(days / 7)}w`;
}

// ---------------------------------------------------------------------------
// API
// ---------------------------------------------------------------------------

async function apiGetJobs() {
  const res = await fetch('/api/schedules');
  if (!res.ok) throw new Error('Could not load scheduled tasks');
  const data = await res.json();
  return Array.isArray(data.jobs) ? data.jobs : [];
}

/** POST the full job dict (with id) to create/update. Returns stored job. */
async function apiSaveJob(job) {
  const res = await fetch('/api/schedules', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(job),
  });
  const data = await res.json().catch(() => ({}));
  if (!res.ok) throw new Error(data.error || res.statusText);
  return data;
}

async function apiDeleteJob(id) {
  const res = await fetch('/api/schedules/' + encodeURIComponent(id), {
    method: 'DELETE',
  });
  const data = await res.json().catch(() => ({}));
  if (!res.ok) throw new Error(data.error || res.statusText);
  return data;
}

/** Natural-language interpret -> updated job dict (or {error}). ~10s grok call. */
async function apiInterpret(id, text) {
  const res = await fetch('/api/schedules/' + encodeURIComponent(id) + '/interpret', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ text }),
  });
  const data = await res.json().catch(() => ({}));
  if (!res.ok) throw new Error(data.error || res.statusText);
  if (data && data.error) throw new Error(data.error);
  return data;
}

async function apiRunNow(id) {
  const res = await fetch('/api/schedules/' + encodeURIComponent(id) + '/run', {
    method: 'POST',
  });
  const data = await res.json().catch(() => ({}));
  if (!res.ok) throw new Error(data.error || res.statusText);
  return data;
}

async function apiStopConversation(convId) {
  const res = await fetch('/api/conversations/' + encodeURIComponent(convId) + '/stop', {
    method: 'POST',
  });
  if (!res.ok) {
    const data = await res.json().catch(() => ({}));
    throw new Error(data.error || res.statusText);
  }
  return res.json().catch(() => ({}));
}

// ---------------------------------------------------------------------------
// Job dict helpers
// ---------------------------------------------------------------------------

function getJob(id) {
  return jobs.find((j) => j.id === id) || null;
}

/** Effective status pill text — show "paused" for disabled tasks. */
function effectiveStatus(job) {
  if (job.enabled === false) return 'paused';
  return job.last_status || 'idle';
}

/** A short, unique-ish id for a brand-new task. */
function newJobId() {
  return 'job-' + Date.now().toString(36) + '-' +
    Math.random().toString(36).slice(2, 6);
}

/** A blank job skeleton matching the server's dict shape. */
function blankJob() {
  return {
    id: newJobId(),
    label: 'New task',
    enabled: true,
    trigger: { cron: '', interval_sec: null, once_at_us: '' },
    next_fire_us: '0',
    last_fire_us: '0',
    last_status: '',
    run: { message: '', model: '', cwd: '', target_conv_id: '', app_id: '' },
    max_concurrent_tabs: 1,
    history: [],
  };
}

/** Which trigger type is active for a job: 'cron' | 'interval' | 'once'. */
function triggerType(job) {
  const t = job.trigger || {};
  if (t.interval_sec) return 'interval';
  if (t.once_at_us && t.once_at_us !== '0' && t.once_at_us !== '') return 'once';
  return 'cron';
}

// ---------------------------------------------------------------------------
// Left list
// ---------------------------------------------------------------------------

const listEl = document.getElementById('sched-list');
const listEmptyEl = document.getElementById('sched-list-empty');

function pill(status) {
  const span = document.createElement('span');
  const cls = String(status || 'idle').toLowerCase();
  span.className = 'sched-pill ' + cls;
  span.textContent = cls;
  return span;
}

function renderList() {
  listEl.innerHTML = '';
  const has = jobs.length > 0;
  listEmptyEl.hidden = has;
  listEl.hidden = !has;
  for (const job of jobs) {
    const li = document.createElement('li');
    li.className = 'sched-list-item';
    li.dataset.id = job.id;
    if (job.id === selectedId) li.classList.add('active');
    if (job.enabled === false) li.classList.add('disabled');

    const top = document.createElement('div');
    top.className = 'sched-li-top';
    const label = document.createElement('span');
    label.className = 'sched-li-label';
    label.textContent = job.label || '(untitled)';
    top.append(label, pill(effectiveStatus(job)));

    const next = document.createElement('div');
    next.className = 'sched-li-next';
    if (job.enabled === false) {
      next.textContent = 'Paused';
    } else {
      next.textContent = 'Next: ' + humanizeUs(job.next_fire_us, { future: true });
    }

    li.append(top, next);
    li.addEventListener('click', () => selectJob(job.id));
    listEl.appendChild(li);
  }
}

// ---------------------------------------------------------------------------
// Right detail
// ---------------------------------------------------------------------------

const placeholderEl = document.getElementById('sched-placeholder');
const detailEl = document.getElementById('sched-detail');

function selectJob(id) {
  selectedId = id;
  renderList();
  renderDetail();
}

function showPlaceholder() {
  selectedId = null;
  placeholderEl.hidden = false;
  detailEl.hidden = true;
  detailEl.innerHTML = '';
  renderList();
}

/** Build a labelled field wrapper. */
function field(labelText, control, hintText) {
  const label = document.createElement('label');
  label.className = 'settings-field';
  const span = document.createElement('span');
  span.textContent = labelText;
  label.append(span, control);
  if (hintText) {
    const small = document.createElement('small');
    small.textContent = hintText;
    label.appendChild(small);
  }
  return label;
}

function card(titleText, hintText) {
  const section = document.createElement('section');
  section.className = 'settings-card';
  const h2 = document.createElement('h2');
  h2.textContent = titleText;
  if (hintText) {
    const hint = document.createElement('span');
    hint.className = 'sched-card-hint';
    hint.textContent = hintText;
    h2.appendChild(hint);
  }
  section.appendChild(h2);
  return section;
}

function makeModelSelect(selected) {
  const sel = document.createElement('select');
  // Free-form "(default)" option first, then known models.
  const def = document.createElement('option');
  def.value = '';
  def.textContent = '(account default)';
  sel.appendChild(def);
  let matched = !selected;
  for (const m of models) {
    const opt = document.createElement('option');
    opt.value = m.id;
    opt.textContent = m.label || m.id;
    if (m.id === selected) { opt.selected = true; matched = true; }
    sel.appendChild(opt);
  }
  // If the job carries a model we don't have in the list, keep it selectable.
  if (!matched && selected) {
    const opt = document.createElement('option');
    opt.value = selected;
    opt.textContent = selected;
    opt.selected = true;
    sel.appendChild(opt);
  }
  return sel;
}

function renderDetail() {
  const job = getJob(selectedId);
  if (!job) { showPlaceholder(); return; }
  placeholderEl.hidden = true;
  detailEl.hidden = false;
  detailEl.innerHTML = '';

  // --- 1. Header: editable label + status pill --------------------------
  const head = document.createElement('div');
  head.className = 'sched-detail-head';
  const labelInput = document.createElement('input');
  labelInput.type = 'text';
  labelInput.className = 'sched-label-input';
  labelInput.value = job.label || '';
  labelInput.placeholder = 'Task name';
  head.append(labelInput, pill(effectiveStatus(job)));
  detailEl.appendChild(head);

  // --- 2. "Talk to it" — primary friendly path --------------------------
  const talkCard = card('Talk to it', 'Plain English — the schedule, the prompt, anything.');
  const talkRow = document.createElement('div');
  talkRow.className = 'sched-talk-row';
  const talkBox = document.createElement('textarea');
  talkBox.placeholder = 'e.g. "every weekday at 8am", "pause until Monday", "run it now"';
  talkBox.rows = 2;
  const talkSend = document.createElement('button');
  talkSend.type = 'button';
  talkSend.className = 'settings-btn settings-btn-primary sched-talk-send';
  talkSend.textContent = 'Send';
  talkRow.append(talkBox, talkSend);
  const talkStatus = document.createElement('p');
  talkStatus.className = 'sched-talk-status';
  talkCard.append(talkRow, talkStatus);
  detailEl.appendChild(talkCard);

  const runInterpret = async () => {
    const text = talkBox.value.trim();
    if (!text) return;
    talkSend.disabled = true;
    talkBox.disabled = true;
    talkStatus.className = 'sched-talk-status';
    talkStatus.innerHTML = '';
    const spinner = document.createElement('span');
    spinner.className = 'sched-spinner';
    talkStatus.append(spinner, document.createTextNode('Thinking… (this can take ~10s)'));
    try {
      const updated = await apiInterpret(job.id, text);
      upsertJob(updated);
      selectedId = updated.id || job.id;
      renderList();
      renderDetail(); // full re-render reflects the interpreted change
    } catch (e) {
      talkSend.disabled = false;
      talkBox.disabled = false;
      talkStatus.className = 'sched-talk-status err';
      talkStatus.textContent = e.message;
    }
  };
  talkSend.addEventListener('click', runInterpret);
  talkBox.addEventListener('keydown', (e) => {
    if (e.key === 'Enter' && (e.metaKey || e.ctrlKey)) {
      e.preventDefault();
      runInterpret();
    }
  });

  // --- 3. Schedule form (structured fallback) ---------------------------
  const formCard = card('Schedule', 'Or set it exactly.');

  // Trigger-type selector
  const trigType = triggerType(job);
  const typeRow = document.createElement('div');
  typeRow.className = 'sched-trigtype';
  const types = [
    { id: 'cron', label: 'Cron' },
    { id: 'interval', label: 'Every N minutes' },
    { id: 'once', label: 'Once at' },
  ];
  const typeBtns = {};
  for (const t of types) {
    const b = document.createElement('button');
    b.type = 'button';
    b.className = 'sched-trigtype-btn' + (t.id === trigType ? ' active' : '');
    b.textContent = t.label;
    b.dataset.type = t.id;
    typeRow.appendChild(b);
    typeBtns[t.id] = b;
  }
  formCard.appendChild(typeRow);

  // Cron input
  const cronInput = document.createElement('input');
  cronInput.type = 'text';
  cronInput.className = 'sched-mono';
  cronInput.placeholder = '0 8 * * 1-5';
  cronInput.value = (job.trigger && job.trigger.cron) || '';
  const cronField = field('Cron expression', cronInput,
    'Standard 5-field cron: minute hour day-of-month month day-of-week.');
  cronField.classList.add('sched-trig-input');
  cronField.dataset.type = 'cron';

  // Interval input (minutes in the UI; stored as interval_sec)
  const intervalInput = document.createElement('input');
  intervalInput.type = 'number';
  intervalInput.min = '1';
  intervalInput.step = '1';
  intervalInput.placeholder = '30';
  if (job.trigger && job.trigger.interval_sec) {
    intervalInput.value = String(Math.max(1, Math.round(job.trigger.interval_sec / 60)));
  }
  const intervalField = field('Every N minutes', intervalInput,
    'Repeats this many minutes apart.');
  intervalField.classList.add('sched-trig-input');
  intervalField.dataset.type = 'interval';

  // Once-at input
  const onceInput = document.createElement('input');
  onceInput.type = 'datetime-local';
  const onceDate = usToDate(job.trigger && job.trigger.once_at_us);
  if (onceDate) onceInput.value = dateToLocalInput(onceDate);
  const onceField = field('Run once at', onceInput, 'Local time. Fires a single time.');
  onceField.classList.add('sched-trig-input');
  onceField.dataset.type = 'once';

  formCard.append(cronField, intervalField, onceField);

  let activeTrigType = trigType;
  const applyTrigTypeVisibility = () => {
    for (const f of [cronField, intervalField, onceField]) {
      f.hidden = f.dataset.type !== activeTrigType;
    }
    for (const id of Object.keys(typeBtns)) {
      typeBtns[id].classList.toggle('active', id === activeTrigType);
    }
  };
  for (const id of Object.keys(typeBtns)) {
    typeBtns[id].addEventListener('click', () => {
      activeTrigType = id;
      applyTrigTypeVisibility();
    });
  }
  applyTrigTypeVisibility();

  // Run config
  const messageInput = document.createElement('textarea');
  messageInput.rows = 4;
  messageInput.placeholder = 'What should Grok do when this fires?';
  messageInput.value = (job.run && job.run.message) || '';
  formCard.appendChild(field('Prompt', messageInput,
    'The message sent to the agent on each run.'));

  const modelSelect = makeModelSelect((job.run && job.run.model) || '');
  formCard.appendChild(field('Model', modelSelect));

  const cwdInput = document.createElement('input');
  cwdInput.type = 'text';
  cwdInput.className = 'sched-mono';
  cwdInput.placeholder = '/path/to/app (optional)';
  cwdInput.value = (job.run && job.run.cwd) || '';
  formCard.appendChild(field('Working directory', cwdInput,
    'For app-build tasks. Leave blank for a plain chat run.'));

  const tabsInput = document.createElement('input');
  tabsInput.type = 'number';
  tabsInput.min = '1';
  tabsInput.step = '1';
  tabsInput.value = String(job.max_concurrent_tabs || 1);
  formCard.appendChild(field('Max concurrent tabs', tabsInput,
    'How many browser tabs the run may open at once.'));

  const saveRow = document.createElement('div');
  saveRow.className = 'settings-actions';
  const saveBtn = document.createElement('button');
  saveBtn.type = 'button';
  saveBtn.className = 'settings-btn settings-btn-primary';
  saveBtn.textContent = 'Save';
  saveRow.appendChild(saveBtn);
  const saveStatus = document.createElement('p');
  saveStatus.className = 'settings-status';
  formCard.append(saveRow, saveStatus);
  detailEl.appendChild(formCard);

  const collectAndSave = async () => {
    // Merge the edits into a deep copy of the current job so we preserve id,
    // history, target_conv_id, app_id, and any fields we don't surface.
    const merged = JSON.parse(JSON.stringify(job));
    merged.label = labelInput.value.trim() || 'Untitled task';
    merged.trigger = merged.trigger || {};
    if (activeTrigType === 'cron') {
      merged.trigger.cron = cronInput.value.trim();
      merged.trigger.interval_sec = null;
      merged.trigger.once_at_us = '';
    } else if (activeTrigType === 'interval') {
      let mins = parseInt(intervalInput.value, 10);
      if (!Number.isFinite(mins) || mins < 1) mins = 1;
      merged.trigger.cron = '';
      merged.trigger.interval_sec = mins * 60;
      merged.trigger.once_at_us = '';
    } else { // once
      merged.trigger.cron = '';
      merged.trigger.interval_sec = null;
      merged.trigger.once_at_us = localInputToUs(onceInput.value);
    }
    merged.run = merged.run || {};
    merged.run.message = messageInput.value;
    merged.run.model = modelSelect.value;
    merged.run.cwd = cwdInput.value.trim();
    let tabs = parseInt(tabsInput.value, 10);
    if (!Number.isFinite(tabs) || tabs < 1) tabs = 1;
    merged.max_concurrent_tabs = tabs;

    saveBtn.disabled = true;
    saveStatus.className = 'settings-status';
    saveStatus.textContent = 'Saving…';
    try {
      const stored = await apiSaveJob(merged);
      upsertJob(stored);
      selectedId = stored.id || merged.id;
      renderList();
      renderDetail();
    } catch (e) {
      saveBtn.disabled = false;
      saveStatus.className = 'settings-status err';
      saveStatus.textContent = e.message;
    }
  };
  saveBtn.addEventListener('click', collectAndSave);

  // Saving the label on blur/Enter also goes through the same path so it
  // persists without needing the Save button.
  labelInput.addEventListener('keydown', (e) => {
    if (e.key === 'Enter') { e.preventDefault(); labelInput.blur(); }
  });

  // --- 4. Run history ---------------------------------------------------
  const histCard = card('Run history');
  const history = historyForDisplay(job);
  if (!history.length) {
    const empty = document.createElement('p');
    empty.className = 'sched-history-empty';
    empty.textContent = 'No runs yet.';
    histCard.appendChild(empty);
  } else {
    const list = document.createElement('div');
    list.className = 'sched-history';
    // Most recent first; tolerate either order from the server by sorting on
    // fired_us descending.
    const sorted = history.slice().sort(
      (a, b) => Number(b.fired_us || 0) - Number(a.fired_us || 0));
    for (const h of sorted) {
      const row = document.createElement('div');
      row.className = 'sched-history-row';
      const time = document.createElement('span');
      time.className = 'sched-history-time';
      time.textContent = humanizeUs(h.fired_us);
      row.append(pill(h.status), time);
      if (h.conv_id) {
        const link = document.createElement('button');
        link.type = 'button';
        link.className = 'sched-history-link';
        link.textContent = h.status === 'running' ? 'watch live' : 'open reply';
        link.addEventListener('click', () => openConversation(h.conv_id, job.id));
        row.appendChild(link);
      }
      list.appendChild(row);
    }
    histCard.appendChild(list);
  }
  detailEl.appendChild(histCard);

  // --- 5. Actions row ---------------------------------------------------
  const actionsCard = card('Actions');
  const actions = document.createElement('div');
  actions.className = 'settings-actions';

  // Pause / Resume
  const pauseBtn = document.createElement('button');
  pauseBtn.type = 'button';
  pauseBtn.className = 'settings-btn';
  pauseBtn.textContent = job.enabled === false ? 'Resume' : 'Pause';
  pauseBtn.addEventListener('click', async () => {
    const merged = JSON.parse(JSON.stringify(job));
    // Flip enabled: a task is "on" unless enabled === false.
    merged.enabled = job.enabled === false;
    pauseBtn.disabled = true;
    setActionStatus(actionStatus, 'Saving…');
    try {
      const stored = await apiSaveJob(merged);
      upsertJob(stored);
      renderList();
      renderDetail();
    } catch (e) {
      pauseBtn.disabled = false;
      setActionStatus(actionStatus, e.message, 'err');
    }
  });
  actions.appendChild(pauseBtn);

  // Run now
  const runBtn = document.createElement('button');
  runBtn.type = 'button';
  runBtn.className = 'settings-btn';
  runBtn.textContent = 'Run now';
  runBtn.addEventListener('click', async () => {
    runBtn.disabled = true;
    setActionStatus(actionStatus, 'Firing…');
    try {
      await apiRunNow(job.id);
      setActionStatus(actionStatus, 'Started — refreshing…', 'ok');
      await reload();
      renderDetail();
    } catch (e) {
      runBtn.disabled = false;
      setActionStatus(actionStatus, e.message, 'err');
    }
  });
  actions.appendChild(runBtn);

  // Cancel running — only when last_status == 'running'
  if (job.last_status === 'running') {
    const latestConv = latestRunningConvId(job);
    if (latestConv) {
      const liveBtn = document.createElement('button');
      liveBtn.type = 'button';
      liveBtn.className = 'settings-btn settings-btn-primary';
      liveBtn.textContent = 'Watch live';
      liveBtn.addEventListener('click', () => openConversation(latestConv, job.id));
      actions.appendChild(liveBtn);
    }
    const cancelBtn = document.createElement('button');
    cancelBtn.type = 'button';
    cancelBtn.className = 'settings-btn sched-btn-danger';
    cancelBtn.textContent = 'Cancel running';
    cancelBtn.disabled = !latestConv;
    cancelBtn.title = latestConv ? '' : 'No conversation id to cancel';
    cancelBtn.addEventListener('click', async () => {
      if (!latestConv) return;
      cancelBtn.disabled = true;
      setActionStatus(actionStatus, 'Stopping…');
      try {
        await apiStopConversation(latestConv);
        setActionStatus(actionStatus, 'Stopped — refreshing…', 'ok');
        await reload();
        renderDetail();
      } catch (e) {
        cancelBtn.disabled = false;
        setActionStatus(actionStatus, e.message, 'err');
      }
    });
    actions.appendChild(cancelBtn);
  }

  // Delete
  const deleteBtn = document.createElement('button');
  deleteBtn.type = 'button';
  deleteBtn.className = 'settings-btn sched-btn-danger';
  deleteBtn.textContent = 'Delete';
  deleteBtn.addEventListener('click', () => {
    confirmDialog(
      `Delete “${job.label || 'this task'}”? This can’t be undone.`,
      async () => {
        setActionStatus(actionStatus, 'Deleting…');
        try {
          await apiDeleteJob(job.id);
          jobs = jobs.filter((j) => j.id !== job.id);
          const next = jobs[0];
          selectedId = next ? next.id : null;
          renderList();
          if (selectedId) renderDetail(); else showPlaceholder();
        } catch (e) {
          setActionStatus(actionStatus, e.message, 'err');
        }
      });
  });
  actions.appendChild(deleteBtn);

  const actionStatus = document.createElement('p');
  actionStatus.className = 'settings-status';
  actionsCard.append(actions, actionStatus);
  detailEl.appendChild(actionsCard);
}

function setActionStatus(el, msg, kind = '') {
  if (!el) return;
  el.className = 'settings-status' + (kind ? ` ${kind}` : '');
  el.textContent = msg;
}

/** Conversation id for the in-flight run, if any. */
function latestRunningConvId(job) {
  const history = Array.isArray(job.history) ? job.history : [];
  const running = history.find((h) => h.status === 'running' && h.conv_id);
  if (running) return running.conv_id;
  if (job.last_status === 'running' && job.run && job.run.target_conv_id) {
    return job.run.target_conv_id;
  }
  const sorted = history.slice().sort(
    (a, b) => Number(b.fired_us || 0) - Number(a.fired_us || 0));
  const withConv = sorted.find((h) => h.conv_id);
  if (withConv) return withConv.conv_id;
  return (job.run && job.run.target_conv_id) || '';
}

/** History rows for display, including an in-flight row when last_status=running. */
function historyForDisplay(job) {
  const history = Array.isArray(job.history) ? job.history.slice() : [];
  if (job.last_status === 'running' &&
      !history.some((h) => h.status === 'running')) {
    history.unshift({
      fired_us: job.last_fire_us || '0',
      status: 'running',
      conv_id: latestRunningConvId(job),
    });
  }
  return history;
}

function openConversation(convId, jobId) {
  if (!convId) return;
  let url = '/?conv=' + encodeURIComponent(convId);
  if (jobId) url += '&schedule=' + encodeURIComponent(jobId);
  location.href = url;
}

// ---------------------------------------------------------------------------
// State sync
// ---------------------------------------------------------------------------

/** Replace (or insert) a job in the local list by id. */
function upsertJob(job) {
  if (!job || !job.id) return;
  const idx = jobs.findIndex((j) => j.id === job.id);
  if (idx >= 0) jobs[idx] = job;
  else jobs.push(job);
}

async function reload() {
  jobs = await apiGetJobs();
  // Keep the selection if it still exists.
  if (selectedId && !getJob(selectedId)) selectedId = null;
}

function anyJobRunning() {
  return jobs.some((j) => j.enabled !== false && j.last_status === 'running');
}

function jobSnapshotChanged(prev, next) {
  if (!prev || !next) return true;
  return prev.last_status !== next.last_status ||
    prev.last_fire_us !== next.last_fire_us ||
    prev.next_fire_us !== next.next_fire_us ||
    (Array.isArray(prev.history) ? prev.history.length : 0) !==
      (Array.isArray(next.history) ? next.history.length : 0);
}

let pollTimer = null;

function stopPollTimer() {
  if (pollTimer) {
    clearInterval(pollTimer);
    pollTimer = null;
  }
}

function startPollTimer() {
  stopPollTimer();
  const ms = anyJobRunning() ? 2000 : 10000;
  pollTimer = setInterval(pollJobs, ms);
}

async function pollJobs() {
  const prev = jobs.slice();
  try {
    await reload();
  } catch (e) {
    console.error('schedules poll failed:', e);
    return;
  }
  renderList();
  if (selectedId) {
    const job = getJob(selectedId);
    const prevJob = prev.find((j) => j.id === selectedId);
    if (!job) {
      showPlaceholder();
    } else if (jobSnapshotChanged(prevJob, job)) {
      renderDetail();
    }
  }
  startPollTimer();
}

// ---------------------------------------------------------------------------
// In-page confirm dialog (window.confirm is suppressed in the side panel)
// ---------------------------------------------------------------------------

const confirmOverlay = document.getElementById('sched-confirm');
const confirmMsg = document.getElementById('sched-confirm-msg');
const confirmOk = document.getElementById('sched-confirm-ok');
const confirmCancel = document.getElementById('sched-confirm-cancel');
let confirmCb = null;

function confirmDialog(message, onConfirm) {
  confirmMsg.textContent = message;
  confirmCb = onConfirm;
  confirmOverlay.hidden = false;
}

function closeConfirm() {
  confirmOverlay.hidden = true;
  confirmCb = null;
}

confirmOk?.addEventListener('click', () => {
  const cb = confirmCb;
  closeConfirm();
  if (cb) cb();
});
confirmCancel?.addEventListener('click', closeConfirm);
confirmOverlay?.addEventListener('click', (e) => {
  if (e.target === confirmOverlay) closeConfirm();
});
document.addEventListener('keydown', (e) => {
  if (e.key === 'Escape' && !confirmOverlay.hidden) closeConfirm();
});

// ---------------------------------------------------------------------------
// Top-level controls
// ---------------------------------------------------------------------------

document.getElementById('sched-back')?.addEventListener('click', () => {
  location.href = '/';
});

async function createNewTask() {
  // Create locally, then persist so the server assigns/stores it; on failure
  // we still keep the local draft so the user can fix + Save.
  const draft = blankJob();
  try {
    const stored = await apiSaveJob(draft);
    upsertJob(stored);
    selectedId = stored.id || draft.id;
  } catch {
    upsertJob(draft);
    selectedId = draft.id;
  }
  renderList();
  renderDetail();
}

document.getElementById('sched-new')?.addEventListener('click', createNewTask);
document.getElementById('sched-empty-new')?.addEventListener('click', createNewTask);

// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------

async function init() {
  startThemeWatcher();
  // Open a specific task if /schedules?id=... is passed.
  const params = new URLSearchParams(location.search);
  const wantId = params.get('id');
  try {
    models = await fetchModels();
  } catch { models = []; }
  try {
    jobs = await apiGetJobs();
  } catch (e) {
    jobs = [];
    console.error('schedules load failed:', e);
  }
  if (wantId && getJob(wantId)) {
    selectedId = wantId;
  } else if (jobs.length === 1) {
    selectedId = jobs[0].id;
  }
  renderList();
  if (selectedId) renderDetail(); else showPlaceholder();
  startPollTimer();
}

document.addEventListener('visibilitychange', () => {
  if (document.hidden) {
    stopPollTimer();
  } else {
    pollJobs();
  }
});

init();
