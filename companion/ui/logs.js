// Logs page — polls GET /api/logs and renders a filterable table.
(function () {
  const body = document.getElementById('logs-body');
  const empty = document.getElementById('logs-empty');
  const sourceSel = document.getElementById('log-source');
  const autoChk = document.getElementById('log-auto');
  const refreshBtn = document.getElementById('log-refresh');

  const expanded = new Set(); // event keys with their detail row open
  let timer = null;

  function esc(s) {
    return String(s == null ? '' : s).replace(/[&<>"']/g, (c) => ({
      '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;',
    }[c]));
  }

  // ts_us = microseconds since the Windows epoch (1601). Convert to Unix ms.
  const WIN_EPOCH_UNIX_MS = 11644473600000;
  function fmtTime(tsUs) {
    if (!tsUs) return '';
    const unixMs = tsUs / 1000 - WIN_EPOCH_UNIX_MS;
    const d = new Date(unixMs);
    if (isNaN(d.getTime())) return '';
    return d.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit' });
  }

  function keyOf(e, i) {
    return `${e.ts_us || ''}:${e.source || ''}:${e.event || ''}:${i}`;
  }

  function render(events) {
    body.innerHTML = '';
    if (!events.length) {
      empty.hidden = false;
      return;
    }
    empty.hidden = true;
    events.forEach((e, i) => {
      const key = keyOf(e, i);
      const detail = e.detail || '';
      const tr = document.createElement('tr');
      tr.className = 'logs-row' + (detail ? ' has-detail' : '');
      const level = (e.level || 'info').toLowerCase();
      const exit = (e.exit_code !== undefined && e.exit_code !== null) ? ` (exit ${e.exit_code})` : '';
      const appCell = e.app_id
        ? `<a href="/app?id=${esc(e.app_id)}" title="Open app">${esc(String(e.app_id).slice(0, 8))}</a>`
        : '';
      tr.innerHTML =
        `<td class="col-time">${esc(fmtTime(e.ts_ms))}</td>` +
        `<td class="col-level"><span class="log-level ${esc(level)}">${esc(level)}</span></td>` +
        `<td class="col-source">${esc(e.source || '')}</td>` +
        `<td class="col-app">${appCell}</td>` +
        `<td class="col-event">${esc(e.event || '')}</td>` +
        `<td class="col-msg">${esc(e.message || '')}${exit}${detail ? ' <span aria-hidden="true">›</span>' : ''}</td>`;
      if (detail) {
        tr.addEventListener('click', () => {
          if (expanded.has(key)) expanded.delete(key); else expanded.add(key);
          refresh();
        });
      }
      body.appendChild(tr);
      if (detail && expanded.has(key)) {
        const dr = document.createElement('tr');
        dr.className = 'log-detail-row';
        dr.innerHTML = `<td colspan="6"><pre>${esc(detail)}</pre></td>`;
        body.appendChild(dr);
      }
    });
  }

  async function refresh() {
    const params = new URLSearchParams();
    if (sourceSel.value) params.set('source', sourceSel.value);
    const qs = params.toString();
    try {
      const r = await fetch(`/api/logs${qs ? '?' + qs : ''}`, { cache: 'no-store' });
      const data = await r.json();
      render(Array.isArray(data.events) ? data.events : []);
    } catch (e) {
      // leave the last good render in place on transient errors
    }
  }

  function startTimer() {
    stopTimer();
    if (autoChk.checked) timer = setInterval(refresh, 2000);
  }
  function stopTimer() {
    if (timer) { clearInterval(timer); timer = null; }
  }

  sourceSel.addEventListener('change', refresh);
  refreshBtn.addEventListener('click', refresh);
  autoChk.addEventListener('change', startTimer);
  document.addEventListener('visibilitychange', () => {
    if (document.hidden) stopTimer(); else { refresh(); startTimer(); }
  });

  refresh();
  startTimer();
})();
