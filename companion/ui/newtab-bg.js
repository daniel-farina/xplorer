/* New-tab background picker. Each theme mode (light / dark) gets its own
   background, chosen from a TYPE:
     default  — plain white in light mode, dark in dark mode (the out-of-box look)
     solid    — a custom solid color
     gradient — a two-color gradient + angle
     stars    — an animated, twinkling star field over deep space
     image    — the built-in landscape preset, or a custom uploaded image
   The choice auto-switches with the OS light/dark theme and persists in
   IndexedDB. Pure UI — no server upload; presets are served from companion/ui. */
(function () {
  'use strict';

  var DEFAULTS = { light: '/newtab-bg-light.jpg', dark: '/newtab-bg-dark.jpg' };
  var bgEl = document.getElementById('nt-bg');
  if (!bgEl) return;

  var TYPES = [
    { id: 'default',  label: 'Default' },
    { id: 'solid',    label: 'Color' },
    { id: 'gradient', label: 'Gradient' },
    { id: 'stars',    label: 'Stars' },
    { id: 'image',    label: 'Image' }
  ];
  var COLOR_PRESETS = ['#ffffff', '#0b0d12', '#0a1f44', '#0f3d2e', '#3a1f4d', '#5a1f1f', '#1f3a4d', '#f4ead2'];
  var GRAD_PRESETS = [
    { a: '#eaf2ff', b: '#cfe0ff', angle: 160 },
    { a: '#1c2740', b: '#070810', angle: 160 },
    { a: '#ff9a9e', b: '#fad0c4', angle: 160 },
    { a: '#a18cd1', b: '#fbc2eb', angle: 160 },
    { a: '#0f2027', b: '#2c5364', angle: 160 },
    { a: '#43cea2', b: '#185a9d', angle: 160 }
  ];
  var STARS_BASE = {
    dark:  'radial-gradient(130% 110% at 50% 14%, #16203a 0%, #0a0d18 55%, #05060c 100%)',
    light: 'radial-gradient(130% 110% at 50% 14%, #dce7ff 0%, #b7c7ec 60%, #9fb2dd 100%)'
  };

  function themeBg() {
    var c = getComputedStyle(document.documentElement).getPropertyValue('--bg').trim();
    return c || '#0b0d12';
  }
  function defaultColor(mode) { return mode === 'dark' ? themeBg() : '#ffffff'; }
  function gradCss(g) { return 'linear-gradient(' + g.angle + 'deg, ' + g.a + ', ' + g.b + ')'; }
  function defaultCfg(mode) {
    return {
      type: 'default',
      color: defaultColor(mode),
      grad: mode === 'dark' ? { a: '#1c2740', b: '#070810', angle: 160 }
                            : { a: '#eaf2ff', b: '#cfe0ff', angle: 160 },
      image: 'preset',  // 'preset' | 'custom'
      blob: null        // custom image File (only when image === 'custom')
    };
  }

  function currentMode() {
    return document.documentElement.getAttribute('data-theme') === 'dark' ? 'dark' : 'light';
  }

  // ---- IndexedDB (one config object per mode, keyed by 'light' / 'dark') ----
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
  function readCfg(mode) {
    return idb(mode, 'get').then(function (v) {
      if (!v) return defaultCfg(mode);
      // Legacy data: a bare image File stored directly → migrate to an image cfg.
      if (v instanceof Blob) {
        var m = defaultCfg(mode); m.type = 'image'; m.image = 'custom'; m.blob = v; return m;
      }
      return Object.assign(defaultCfg(mode), v);
    });
  }

  var state = { light: null, dark: null };
  function save(mode) {
    idb(mode, 'put', state[mode]);
    if (mode === currentMode()) applyBackground();
  }

  // ---- Animated star field ----
  var starsCanvas = document.createElement('canvas');
  starsCanvas.className = 'nt-stars';
  starsCanvas.setAttribute('aria-hidden', 'true');
  bgEl.insertAdjacentElement('afterend', starsCanvas);
  var starsRAF = null, starsResize = null, starsLoop = null;
  function stopStars() {
    if (starsRAF) { cancelAnimationFrame(starsRAF); starsRAF = null; }
    if (starsResize) { window.removeEventListener('resize', starsResize); starsResize = null; }
    starsLoop = null;
    starsCanvas.style.display = 'none';
  }
  function startStars(mode) {
    var ctx = starsCanvas.getContext('2d');
    var dpr = Math.min(window.devicePixelRatio || 1, 2);
    var stars = [];
    function resize() {
      var w = window.innerWidth, h = window.innerHeight;
      starsCanvas.width = Math.round(w * dpr);
      starsCanvas.height = Math.round(h * dpr);
      starsCanvas.style.width = w + 'px';
      starsCanvas.style.height = h + 'px';
      var count = Math.max(60, Math.min(420, Math.round(w * h / 7000)));
      stars = [];
      for (var i = 0; i < count; i++) {
        stars.push({
          x: Math.random() * starsCanvas.width,
          y: Math.random() * starsCanvas.height,
          r: (Math.random() * 1.2 + 0.3) * dpr,
          tw: Math.random() * Math.PI * 2,
          sp: Math.random() * 0.025 + 0.006,
          vx: (Math.random() - 0.5) * 0.05 * dpr,
          vy: (Math.random() - 0.5) * 0.05 * dpr
        });
      }
    }
    resize();
    starsResize = resize;
    window.addEventListener('resize', resize);
    var rgb = mode === 'dark' ? '255, 255, 255' : '30, 52, 110';
    starsCanvas.style.display = 'block';
    starsLoop = function () {
      var W = starsCanvas.width, H = starsCanvas.height;
      ctx.clearRect(0, 0, W, H);
      for (var i = 0; i < stars.length; i++) {
        var s = stars[i];
        s.tw += s.sp;
        s.x += s.vx; s.y += s.vy;
        if (s.x < 0) s.x += W; else if (s.x > W) s.x -= W;
        if (s.y < 0) s.y += H; else if (s.y > H) s.y -= H;
        var a = 0.35 + 0.65 * (0.5 + 0.5 * Math.sin(s.tw));
        ctx.beginPath();
        ctx.arc(s.x, s.y, s.r, 0, 6.2832);
        ctx.fillStyle = 'rgba(' + rgb + ', ' + a.toFixed(3) + ')';
        ctx.fill();
      }
      starsRAF = requestAnimationFrame(starsLoop);
    };
    starsRAF = requestAnimationFrame(starsLoop);
  }
  // Pause the loop when the tab is hidden; resume without re-seeding positions.
  document.addEventListener('visibilitychange', function () {
    if (document.hidden) {
      if (starsRAF) { cancelAnimationFrame(starsRAF); starsRAF = null; }
    } else if (starsLoop && !starsRAF) {
      starsRAF = requestAnimationFrame(starsLoop);
    }
  });

  // ---- Apply the active background ----
  var liveUrl = null;
  function clearBg() {
    bgEl.style.background = '';
    bgEl.style.backgroundImage = '';
    bgEl.style.backgroundColor = '';
  }
  function applyBackground() {
    var mode = currentMode(), cfg = state[mode];
    if (!cfg) return;
    if (liveUrl) { URL.revokeObjectURL(liveUrl); liveUrl = null; }
    stopStars();
    clearBg();
    document.documentElement.setAttribute('data-bg-type', cfg.type);

    if (cfg.type === 'default') {
      bgEl.style.background = defaultColor(mode);
      bgEl.classList.add('is-ready');
    } else if (cfg.type === 'solid') {
      bgEl.style.background = cfg.color;
      bgEl.classList.add('is-ready');
    } else if (cfg.type === 'gradient') {
      bgEl.style.background = gradCss(cfg.grad);
      bgEl.classList.add('is-ready');
    } else if (cfg.type === 'stars') {
      bgEl.style.background = STARS_BASE[mode];
      startStars(mode);
      bgEl.classList.add('is-ready');
    } else { // image
      var url = DEFAULTS[mode];
      if (cfg.image === 'custom' && cfg.blob) { liveUrl = URL.createObjectURL(cfg.blob); url = liveUrl; }
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
    }
  }

  // ---- Picker UI (gear + panel) ----
  var GEAR = '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="3"/><path d="M19.4 15a1.6 1.6 0 0 0 .3 1.8l.1.1a2 2 0 1 1-2.8 2.8l-.1-.1a1.6 1.6 0 0 0-1.8-.3 1.6 1.6 0 0 0-1 1.5V21a2 2 0 1 1-4 0v-.1a1.6 1.6 0 0 0-1-1.5 1.6 1.6 0 0 0-1.8.3l-.1.1a2 2 0 1 1-2.8-2.8l.1-.1a1.6 1.6 0 0 0 .3-1.8 1.6 1.6 0 0 0-1.5-1H3a2 2 0 1 1 0-4h.1a1.6 1.6 0 0 0 1.5-1 1.6 1.6 0 0 0-.3-1.8l-.1-.1a2 2 0 1 1 2.8-2.8l.1.1a1.6 1.6 0 0 0 1.8.3H9a1.6 1.6 0 0 0 1-1.5V3a2 2 0 1 1 4 0v.1a1.6 1.6 0 0 0 1 1.5 1.6 1.6 0 0 0 1.8-.3l.1-.1a2 2 0 1 1 2.8 2.8l-.1.1a1.6 1.6 0 0 0-.3 1.8V9a1.6 1.6 0 0 0 1.5 1H21a2 2 0 1 1 0 4h-.1a1.6 1.6 0 0 0-1.5 1z"/></svg>';

  var thumbUrls = { light: null, dark: null };

  function cardEl(mode) { return panel.querySelector('.nt-mode[data-mode="' + mode + '"]'); }
  function ctlEl(mode) { return panel.querySelector('.nt-ctl[data-mode="' + mode + '"]'); }

  function ctlHtml(mode) {
    var cfg = state[mode];
    if (cfg.type === 'default') {
      return '<p class="nt-ctl__note">Plain background that matches the theme — white in light mode, dark in dark mode.</p>';
    }
    if (cfg.type === 'solid') {
      return '<div class="nt-ctl__row">' +
          '<input type="color" class="nt-color" data-act="color" value="' + cfg.color + '">' +
          '<span class="nt-ctl__val" data-val="color">' + cfg.color + '</span>' +
        '</div>' +
        '<div class="nt-swatches">' + COLOR_PRESETS.map(function (c) {
          var on = c.toLowerCase() === String(cfg.color).toLowerCase();
          return '<button type="button" class="nt-swatch' + (on ? ' is-active' : '') +
            '" data-act="swatch" data-color="' + c + '" style="background:' + c + '" title="' + c + '"></button>';
        }).join('') + '</div>';
    }
    if (cfg.type === 'gradient') {
      var g = cfg.grad;
      return '<div class="nt-ctl__row nt-grad-inputs">' +
          '<input type="color" class="nt-color" data-act="gradA" value="' + g.a + '">' +
          '<input type="color" class="nt-color" data-act="gradB" value="' + g.b + '">' +
          '<input type="range" class="nt-angle" data-act="gradAngle" min="0" max="360" value="' + g.angle + '" title="Angle">' +
        '</div>' +
        '<div class="nt-grads">' + GRAD_PRESETS.map(function (p, i) {
          return '<button type="button" class="nt-grad" data-act="gradPreset" data-i="' + i +
            '" style="background:' + gradCss(p) + '" title="Gradient preset"></button>';
        }).join('') + '</div>';
    }
    if (cfg.type === 'stars') {
      return '<p class="nt-ctl__note">A gently drifting, twinkling star field over deep space.</p>';
    }
    // image
    return '<div class="nt-img-opts">' +
        '<button type="button" class="nt-img-opt' + (cfg.image !== 'custom' ? ' is-active' : '') +
          '" data-act="imgPreset" style="background-image:url(' + DEFAULTS[mode] + ')"><span>Preset</span></button>' +
        '<label class="nt-drop' + (cfg.image === 'custom' ? ' is-active' : '') + '" data-mode="' + mode + '">' +
          '<span class="nt-drop__thumb"></span>' +
          '<span class="nt-drop__text"><b>Upload</b><br>drag &amp; drop / browse</span>' +
          '<input type="file" accept="image/*" data-mode="' + mode + '">' +
        '</label>' +
      '</div>';
  }

  function renderChips(mode) {
    var chips = cardEl(mode).querySelectorAll('.nt-type');
    chips.forEach(function (c) { c.classList.toggle('is-active', c.dataset.type === state[mode].type); });
  }
  function renderCtl(mode) {
    if (thumbUrls[mode]) { URL.revokeObjectURL(thumbUrls[mode]); thumbUrls[mode] = null; }
    var ctl = ctlEl(mode);
    ctl.innerHTML = ctlHtml(mode);
    if (state[mode].type === 'image' && state[mode].image === 'custom' && state[mode].blob) {
      var thumb = ctl.querySelector('.nt-drop__thumb');
      thumbUrls[mode] = URL.createObjectURL(state[mode].blob);
      thumb.style.backgroundImage = 'url("' + thumbUrls[mode] + '")';
    }
  }

  function setImageFile(mode, file) {
    if (!file || !/^image\//.test(file.type)) return;
    var cfg = state[mode];
    cfg.type = 'image'; cfg.image = 'custom'; cfg.blob = file;
    save(mode); renderChips(mode); renderCtl(mode);
  }
  function resetMode(mode) {
    if (thumbUrls[mode]) { URL.revokeObjectURL(thumbUrls[mode]); thumbUrls[mode] = null; }
    state[mode] = defaultCfg(mode);
    idb(mode, 'delete');
    renderChips(mode); renderCtl(mode);
    if (mode === currentMode()) applyBackground();
  }

  function cardHtml(mode, label, swatch) {
    return '<div class="nt-mode" data-mode="' + mode + '">' +
        '<div class="nt-mode__head">' +
          '<span class="nt-mode__label"><span class="swatch ' + swatch + '"></span>' + label + '</span>' +
          '<button type="button" class="nt-mode__reset" data-act="reset">Reset</button>' +
        '</div>' +
        '<div class="nt-types">' + TYPES.map(function (t) {
          return '<button type="button" class="nt-type" data-act="type" data-type="' + t.id + '">' + t.label + '</button>';
        }).join('') + '</div>' +
        '<div class="nt-ctl" data-mode="' + mode + '"></div>' +
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

  document.body.appendChild(gear);
  document.body.appendChild(panel);

  function buildPicker() {
    panel.innerHTML =
      '<p class="nt-bg-panel__title">New-tab background</p>' +
      '<p class="nt-bg-panel__hint">Pick a style per mode — switches automatically with light / dark.</p>' +
      cardHtml('light', 'Light mode', 'light') +
      cardHtml('dark', 'Dark mode', 'dark');
    ['light', 'dark'].forEach(function (mode) { renderChips(mode); renderCtl(mode); });
  }

  // ---- Delegated wiring (survives innerHTML re-renders) ----
  panel.addEventListener('click', function (e) {
    var el = e.target.closest('[data-act]'), card = e.target.closest('.nt-mode');
    if (!el || !card) return;
    var mode = card.dataset.mode, act = el.dataset.act, cfg = state[mode];
    if (act === 'type') { cfg.type = el.dataset.type; save(mode); renderChips(mode); renderCtl(mode); }
    else if (act === 'reset') { resetMode(mode); }
    else if (act === 'swatch') { cfg.type = 'solid'; cfg.color = el.dataset.color; save(mode); renderChips(mode); renderCtl(mode); }
    else if (act === 'gradPreset') { cfg.type = 'gradient'; cfg.grad = Object.assign({}, GRAD_PRESETS[+el.dataset.i]); save(mode); renderChips(mode); renderCtl(mode); }
    else if (act === 'imgPreset') { cfg.type = 'image'; cfg.image = 'preset'; save(mode); renderChips(mode); renderCtl(mode); }
  });
  panel.addEventListener('input', function (e) {
    var el = e.target.closest('[data-act]'), card = e.target.closest('.nt-mode');
    if (!el || !card) return;
    var mode = card.dataset.mode, act = el.dataset.act, cfg = state[mode];
    if (act === 'color') {
      cfg.type = 'solid'; cfg.color = el.value;
      var v = ctlEl(mode).querySelector('[data-val="color"]'); if (v) v.textContent = el.value;
      renderChips(mode); save(mode);
    } else if (act === 'gradA') { cfg.type = 'gradient'; cfg.grad.a = el.value; renderChips(mode); save(mode); }
    else if (act === 'gradB') { cfg.type = 'gradient'; cfg.grad.b = el.value; renderChips(mode); save(mode); }
    else if (act === 'gradAngle') { cfg.type = 'gradient'; cfg.grad.angle = +el.value; renderChips(mode); save(mode); }
  });
  panel.addEventListener('change', function (e) {
    var input = e.target;
    if (!input || input.type !== 'file') return;
    var card = input.closest('.nt-mode'); if (!card) return;
    if (input.files && input.files[0]) setImageFile(card.dataset.mode, input.files[0]);
    input.value = '';
  });
  panel.addEventListener('dragover', function (e) {
    var d = e.target.closest('.nt-drop'); if (!d) return;
    e.preventDefault(); d.classList.add('dragover');
  });
  panel.addEventListener('dragleave', function (e) {
    var d = e.target.closest('.nt-drop'); if (d) d.classList.remove('dragover');
  });
  panel.addEventListener('drop', function (e) {
    var d = e.target.closest('.nt-drop'); if (!d) return;
    e.preventDefault(); d.classList.remove('dragover');
    var card = d.closest('.nt-mode');
    var f = e.dataTransfer && e.dataTransfer.files && e.dataTransfer.files[0];
    if (f && card) setImageFile(card.dataset.mode, f);
  });

  function openPanel() { panel.classList.add('is-open'); }
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

  // ---- Init: load both modes, build the picker, paint the active background ----
  Promise.all([readCfg('light'), readCfg('dark')]).then(function (r) {
    state.light = r[0]; state.dark = r[1];
    buildPicker();
    applyBackground();
  });
})();
