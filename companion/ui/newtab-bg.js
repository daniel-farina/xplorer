/* New-tab background: the landing-page landscape by default, auto-switching
   light/dark with the OS theme, plus a gear that lets the user set a custom
   image per mode (drag-drop or browse). Custom images persist in IndexedDB.
   Pure UI — no server upload; defaults are served from companion/ui. */
(function () {
  'use strict';

  var DEFAULTS = { light: '/newtab-bg-light.jpg', dark: '/newtab-bg-dark.jpg' };
  var bgEl = document.getElementById('nt-bg');
  if (!bgEl) return;

  // ---- IndexedDB (store image blobs keyed by 'light' / 'dark') ----
  var DB = 'xplorer-newtab', STORE = 'bg', dbp = null;
  function db() {
    if (dbp) return dbp;
    dbp = new Promise(function (resolve, reject) {
      var r = indexedDB.open(DB, 1);
      r.onupgradeneeded = function () {
        if (!r.result.objectStoreNames.contains(STORE)) r.result.createObjectStore(STORE);
      };
      r.onsuccess = function () { resolve(r.result); };
      r.onerror = function () { reject(r.error); };
    });
    return dbp;
  }
  function idb(mode, action, value) {
    return db().then(function (d) {
      return new Promise(function (resolve, reject) {
        var rw = action === 'get' ? 'readonly' : 'readwrite';
        var tx = d.transaction(STORE, rw), st = tx.objectStore(STORE), req;
        if (action === 'get') req = st.get(mode);
        else if (action === 'put') req = st.put(value, mode);
        else req = st.delete(mode);
        req.onsuccess = function () { resolve(req.result); };
        req.onerror = function () { reject(req.error); };
      });
    }).catch(function () { return null; });
  }

  function currentMode() {
    return document.documentElement.getAttribute('data-theme') === 'dark' ? 'dark' : 'light';
  }

  // ---- Apply the active background ----
  var liveUrl = null;
  function applyBackground() {
    var mode = currentMode();
    idb(mode, 'get').then(function (blob) {
      if (liveUrl) { URL.revokeObjectURL(liveUrl); liveUrl = null; }
      var url = DEFAULTS[mode];
      if (blob) { liveUrl = URL.createObjectURL(blob); url = liveUrl; }
      var img = new Image();
      img.onload = function () {
        bgEl.style.backgroundImage = 'url("' + url + '")';
        bgEl.classList.add('is-ready');
      };
      img.onerror = function () {
        bgEl.style.backgroundImage = 'url("' + DEFAULTS[mode] + '")';
        bgEl.classList.add('is-ready');
      };
      img.src = url;
    });
  }

  // ---- Picker UI (gear + panel) ----
  var GEAR = '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="3"/><path d="M19.4 15a1.6 1.6 0 0 0 .3 1.8l.1.1a2 2 0 1 1-2.8 2.8l-.1-.1a1.6 1.6 0 0 0-1.8-.3 1.6 1.6 0 0 0-1 1.5V21a2 2 0 1 1-4 0v-.1a1.6 1.6 0 0 0-1-1.5 1.6 1.6 0 0 0-1.8.3l-.1.1a2 2 0 1 1-2.8-2.8l.1-.1a1.6 1.6 0 0 0 .3-1.8 1.6 1.6 0 0 0-1.5-1H3a2 2 0 1 1 0-4h.1a1.6 1.6 0 0 0 1.5-1 1.6 1.6 0 0 0-.3-1.8l-.1-.1a2 2 0 1 1 2.8-2.8l.1.1a1.6 1.6 0 0 0 1.8.3H9a1.6 1.6 0 0 0 1-1.5V3a2 2 0 1 1 4 0v.1a1.6 1.6 0 0 0 1 1.5 1.6 1.6 0 0 0 1.8-.3l.1-.1a2 2 0 1 1 2.8 2.8l-.1.1a1.6 1.6 0 0 0-.3 1.8V9a1.6 1.6 0 0 0 1.5 1H21a2 2 0 1 1 0 4h-.1a1.6 1.6 0 0 0-1.5 1z"/></svg>';

  var thumbUrls = { light: null, dark: null };

  function thumbFor(mode) {
    return idb(mode, 'get').then(function (blob) {
      var card = panel.querySelector('.nt-drop[data-mode="' + mode + '"]');
      var thumb = card.querySelector('.nt-drop__thumb');
      var reset = card.parentElement.querySelector('.nt-mode__reset');
      if (thumbUrls[mode]) { URL.revokeObjectURL(thumbUrls[mode]); thumbUrls[mode] = null; }
      if (blob) {
        thumbUrls[mode] = URL.createObjectURL(blob);
        thumb.style.backgroundImage = 'url("' + thumbUrls[mode] + '")';
        reset.hidden = false;
      } else {
        thumb.style.backgroundImage = 'url("' + DEFAULTS[mode] + '")';
        reset.hidden = true;
      }
    });
  }

  function setImage(mode, file) {
    if (!file || !/^image\//.test(file.type)) return;
    idb(mode, 'put', file).then(function () {
      thumbFor(mode);
      if (mode === currentMode()) applyBackground();
    });
  }
  function resetImage(mode) {
    idb(mode, 'delete').then(function () {
      thumbFor(mode);
      if (mode === currentMode()) applyBackground();
    });
  }

  function modeCard(mode, label, swatch) {
    return '' +
      '<div class="nt-mode">' +
        '<div class="nt-mode__head">' +
          '<span class="nt-mode__label"><span class="swatch ' + swatch + '"></span>' + label + '</span>' +
          '<button type="button" class="nt-mode__reset" data-mode="' + mode + '" hidden>Reset</button>' +
        '</div>' +
        '<label class="nt-drop" data-mode="' + mode + '">' +
          '<span class="nt-drop__thumb"></span>' +
          '<span class="nt-drop__text"><b>Drag &amp; drop</b> an image<br>or click to browse</span>' +
          '<input type="file" accept="image/*" data-mode="' + mode + '">' +
        '</label>' +
      '</div>';
  }

  var gear = document.createElement('button');
  gear.type = 'button';
  gear.className = 'nt-bg-gear';
  gear.title = 'Customize new-tab background';
  gear.setAttribute('aria-label', 'Customize new-tab background');
  gear.innerHTML = GEAR;

  var panel = document.createElement('div');
  panel.className = 'nt-bg-panel';
  panel.setAttribute('role', 'dialog');
  panel.setAttribute('aria-label', 'New-tab background');
  panel.innerHTML =
    '<p class="nt-bg-panel__title">New-tab background</p>' +
    '<p class="nt-bg-panel__hint">Switches automatically with light / dark mode.</p>' +
    modeCard('light', 'Light mode', 'light') +
    modeCard('dark', 'Dark mode', 'dark');

  document.body.appendChild(gear);
  document.body.appendChild(panel);

  // wire each mode card
  ['light', 'dark'].forEach(function (mode) {
    var card = panel.querySelector('.nt-drop[data-mode="' + mode + '"]');
    var input = card.querySelector('input[type="file"]');
    input.addEventListener('change', function () {
      if (input.files && input.files[0]) setImage(mode, input.files[0]);
      input.value = '';
    });
    card.addEventListener('dragover', function (e) { e.preventDefault(); card.classList.add('dragover'); });
    card.addEventListener('dragleave', function () { card.classList.remove('dragover'); });
    card.addEventListener('drop', function (e) {
      e.preventDefault(); card.classList.remove('dragover');
      var f = e.dataTransfer && e.dataTransfer.files && e.dataTransfer.files[0];
      if (f) setImage(mode, f);
    });
    panel.querySelector('.nt-mode__reset[data-mode="' + mode + '"]')
      .addEventListener('click', function () { resetImage(mode); });
  });

  function openPanel() {
    thumbFor('light'); thumbFor('dark');
    panel.classList.add('is-open');
  }
  function closePanel() { panel.classList.remove('is-open'); }
  gear.addEventListener('click', function (e) {
    e.stopPropagation();
    panel.classList.contains('is-open') ? closePanel() : openPanel();
  });
  document.addEventListener('click', function (e) {
    if (panel.classList.contains('is-open') && !panel.contains(e.target) && e.target !== gear) closePanel();
  });
  document.addEventListener('keydown', function (e) { if (e.key === 'Escape') closePanel(); });

  // ---- React to OS theme changes ----
  if (window.matchMedia) {
    var mq = matchMedia('(prefers-color-scheme: dark)');
    var onChange = function (e) {
      document.documentElement.setAttribute('data-theme', e.matches ? 'dark' : 'light');
      applyBackground();
    };
    if (mq.addEventListener) mq.addEventListener('change', onChange);
    else if (mq.addListener) mq.addListener(onChange);
  }

  applyBackground();
})();
