// =============================================================================
// TD Engine - Sample Game: SCRIPT ARENA
// File: web/examples/script_arena.js
//
// A live showcase of two Wave 1/2 modules:
//
//   1. tdscript VM (Tier 1) — the engine's custom Lua-like bytecode VM.
//      A tdscript source string is compiled and loaded into the engine at
//      startup. The JS side calls named script functions via td_script_call()
//      to spawn particles, change gravity, switch palettes, etc. The script
//      itself uses td.create_entity / td.set_position / td.set_velocity /
//      td.set_sprite — the same ECS primitives the C++ side uses, but driven
//      from a sandboxed bytecode VM.
//
//   2. i18n / Localization (Tier 4) — the engine's translation table.
//      On boot we load JSON translation tables for 5 locales (en, es, fr,
//      zh, ar). Pressing L cycles through them. Arabic flips the HUD layout
//      to RTL via td_i18n_is_rtl(). All HUD strings come from td_i18n_t().
//
// Controls:
//   1 / Space (hold)  - Spawn particle bursts (calls script's spawn_burst)
//   2                 - Toggle gravity (script state)
//   3                 - Cycle palette (script state)
//   4                 - Clear all particles (JS iterates + destroys)
//   L                 - Cycle locale (en -> es -> fr -> zh -> ar -> en)
//   R                 - Reset
//   Esc               - Engine pause overlay
//
// All particles are real ECS entities in the C++ engine's World. The engine's
// WASM main loop renders every frame; this JS file drives game rules + the
// script VM bridge.
// =============================================================================

(function () {
  'use strict';

  const VK = {
    ONE:   0x31,
    TWO:   0x32,
    THREE: 0x33,
    FOUR:  0x34,
    SPACE: 0x20,
    L:     0x4C,
    R:     0x52,
    ESC:   0x1B,
  };

  const WIDTH = 800, HEIGHT = 600;

  // ---- tdscript source ------------------------------------------------------
  // Compiled + loaded into the engine's tdscript VM at startup. JS then calls
  // these functions by name via td_script_call(handle, name, argsJson).
  // The script uses td.* engine primitives — same as JS, but running inside
  // a sandboxed bytecode interpreter inside the WASM module.
  //
  // Available td.* functions (registered in script_vm.cpp registerTdLib):
  //   td.create_entity(name) -> id
  //   td.destroy_entity(id)
  //   td.set_position(id, x, y)
  //   td.get_position(id) -> x, y
  //   td.set_velocity(id, vx, vy)
  //   td.set_sprite(id, w, h, r, g, b, a)
  //   td.is_key_down(vk) -> bool
  //   td.find_by_name(name) -> id
  //   td.log(msg)
  //   (beat_*, connect/emit, etc.)
  //
  // Available math.* functions: floor, ceil, abs, sqrt, sin, cos, tan, atan,
  //   atan2, exp, log, pow, max, min, random, pi, huge.
  //
  // Available table.* functions: insert, remove, len, etc.
  const TDSCRIPT_SRC = [
    '-- tdscript: SCRIPT ARENA particle controller',
    '-- Demonstrates: functions, tables, math, td.* engine API.',
    '',
    'local gravity_on = false',
    'local palette_idx = 1',
    'local palettes = {',
    '  { r = 0.40, g = 0.91, b = 0.98 },  -- cyan',
    '  { r = 1.00, g = 0.42, b = 0.42 },  -- red',
    '  { r = 0.60, g = 1.00, b = 0.50 },  -- green',
    '  { r = 1.00, g = 0.82, b = 0.29 },  -- yellow',
    '  { r = 0.70, g = 0.50, b = 1.00 },  -- purple',
    '}',
    '',
    '-- spawn_burst(count, cx, cy)',
    '-- Creates `count` particles radiating from (cx, cy) at the current',
    '-- palette color. Returns a comma-separated string of new entity IDs',
    '-- so the JS caller can track them for physics + culling.',
    'function spawn_burst(count, cx, cy)',
    '  local p = palettes[palette_idx]',
    '  local ids = ""',
    '  local i = 0',
    '  while i < count do',
    '    local e = td.create_entity("particle")',
    '    if e >= 0 then',
    '      local angle = i * (6.28318 / count)',
    '      local speed = 80.0 + (i % 5) * 25.0',
    '      td.set_position(e, cx, cy)',
    '      td.set_velocity(e,',
    '        math.cos(angle) * speed,',
    '        math.sin(angle) * speed)',
    '      td.set_sprite(e, 8.0, 8.0, p.r, p.g, p.b, 1.0)',
    '      if ids == "" then',
    '        ids = tostring(e)',
    '      else',
    '        ids = ids .. "," .. tostring(e)',
    '      end',
    '    end',
    '    i = i + 1',
    '  end',
    '  return ids',
    'end',
    '',
    'function toggle_gravity()',
    '  gravity_on = not gravity_on',
    '  return gravity_on',
    'end',
    '',
    'function is_gravity_on()',
    '  return gravity_on',
    'end',
    '',
    'function next_palette()',
    '  palette_idx = palette_idx + 1',
    '  if palette_idx > #palettes then',
    '    palette_idx = 1',
    '  end',
    '  return palette_idx',
    'end',
    '',
    'function get_palette_idx()',
    '  return palette_idx',
    'end',
    '',
    'function set_palette_idx(idx)',
    '  palette_idx = idx',
    '  return palette_idx',
    'end',
    '',
    '-- clear_all(ids_str)',
    '-- Destroys every id in the comma-separated string. JS passes the list',
    '-- of tracked particle IDs so we exercise td.destroy_entity from script.',
    'function clear_all(ids_str)',
    '  if ids_str == nil or ids_str == "" then return 0 end',
    '  local n = 0',
    '  local rest = ids_str',
    '  while rest ~= "" do',
    '    local sep = string.find(rest, ",")',
    '    local token',
    '    if sep then',
    '      token = string.sub(rest, 1, sep - 1)',
    '      rest = string.sub(rest, sep + 1)',
    '    else',
    '      token = rest',
    '      rest = ""',
    '    end',
    '    local e = tonumber(token)',
    '    if e then',
    '      td.destroy_entity(e)',
    '      n = n + 1',
    '    end',
    '  end',
    '  return n',
    'end',
  ].join('\n');

  // ---- i18n tables ----------------------------------------------------------
  const LOCALES = [
    { code: 'en', name: 'English',  json: {
      "title": "SCRIPT ARENA",
      "subtitle": "tdscript VM + i18n showcase",
      "hint_spawn": "1 / Space to spawn",
      "hint_gravity": "2 gravity",
      "hint_palette": "3 palette",
      "hint_clear":   "4 clear",
      "hint_locale":  "L locale",
      "label_particles": "Particles",
      "label_gravity":   "Gravity",
      "label_palette":   "Palette",
      "label_locale":    "Locale",
      "value_on":  "ON",
      "value_off": "OFF",
      "palette_names": ["Cyan","Red","Green","Yellow","Purple"]
    }},
    { code: 'es', name: 'Español',  json: {
      "title": "ARENA DE SCRIPTS",
      "subtitle": "VM tdscript + i18n",
      "hint_spawn": "1 / Espacio: spawn",
      "hint_gravity": "2 gravedad",
      "hint_palette": "3 paleta",
      "hint_clear":   "4 limpiar",
      "hint_locale":  "L idioma",
      "label_particles": "Partículas",
      "label_gravity":   "Gravedad",
      "label_palette":   "Paleta",
      "label_locale":    "Idioma",
      "value_on":  "SÍ",
      "value_off": "NO",
      "palette_names": ["Cian","Rojo","Verde","Amarillo","Púrpura"]
    }},
    { code: 'fr', name: 'Français', json: {
      "title": "ARÈNE DE SCRIPTS",
      "subtitle": "VM tdscript + i18n",
      "hint_spawn": "1 / Espace pour générer",
      "hint_gravity": "2 gravité",
      "hint_palette": "3 palette",
      "hint_clear":   "4 effacer",
      "hint_locale":  "L langue",
      "label_particles": "Particules",
      "label_gravity":   "Gravité",
      "label_palette":   "Palette",
      "label_locale":    "Langue",
      "value_on":  "OUI",
      "value_off": "NON",
      "palette_names": ["Cyan","Rouge","Vert","Jaune","Violet"]
    }},
    { code: 'zh', name: '中文',     json: {
      "title": "脚本竞技场",
      "subtitle": "tdscript 虚拟机 + 国际化",
      "hint_spawn": "1 / 空格 生成",
      "hint_gravity": "2 重力",
      "hint_palette": "3 调色板",
      "hint_clear":   "4 清空",
      "hint_locale":  "L 语言",
      "label_particles": "粒子数",
      "label_gravity":   "重力",
      "label_palette":   "调色板",
      "label_locale":    "语言",
      "value_on":  "开",
      "value_off": "关",
      "palette_names": ["青","红","绿","黄","紫"]
    }},
    { code: 'ar', name: 'العربية',  json: {
      "title": "ساحة البرامج النصية",
      "subtitle": "آلة tdscript + الترجمة",
      "hint_spawn": "1 / مسافة لإطلاق",
      "hint_gravity": "2 جاذبية",
      "hint_palette": "3 لوحة ألوان",
      "hint_clear":   "4 مسح",
      "hint_locale":  "L لغة",
      "label_particles": "الجسيمات",
      "label_gravity":   "الجاذبية",
      "label_palette":   "اللوحة",
      "label_locale":    "اللغة",
      "value_on":  "نعم",
      "value_off": "لا",
      "palette_names": ["سماوي","أحمر","أخضر","أصفر","بنفسجي"]
    }},
  ];

  // ---- Game state -----------------------------------------------------------
  let scriptHandle = -1;
  let localeIdx = 0;
  let particles = [];          // [{id, x, y, vx, vy, life}]
  let lastSpawnTime = 0;
  let lastTime = performance.now();
  let gameState = 'menu';
  let initialized = false;
  let hudOverlay = null;

  // JS-side mirror of the script's palette_idx (1-based) so the HUD shows
  // the right value without round-tripping through the VM every frame.
  let paletteIdxLocal = 1;

  // Cached cwrap handles.
  let api = null;

  // ---- Public entry points --------------------------------------------------
  window.startTDExample = function () {
    if (!initialized) {
      initialized = true;
      cacheApi();
      setupGame();
    }
    gameState = 'playing';
  };

  window.restartTDExample = function () {
    if (!initialized) {
      initialized = true;
      cacheApi();
      setupGame();
    }
    clearAllParticles();
    gameState = 'playing';
  };

  // ---- API cache ------------------------------------------------------------
  function cacheApi() {
    const M = TDBridge.wasmExports;
    api = {
      createEntity:     M.cwrap('td_create_entity',        'number', ['string']),
      setPos:           M.cwrap('td_entity_set_position',  null,     ['number','number','number']),
      getPos:           M.cwrap('td_entity_get_position',  null,     ['number','number','number']),
      setVel:           M.cwrap('td_entity_set_velocity',  null,     ['number','number','number']),
      setSprite:        M.cwrap('td_entity_set_sprite',    null,
                                ['number','number','number','number','number','number','number']),
      destroy:          M.cwrap('td_entity_destroy',       null,     ['number']),
      isValid:          M.cwrap('td_entity_is_valid',      'number', ['number']),
      isKeyDown:        M.cwrap('td_is_key_down',          'boolean',['number']),
      scriptLoad:       M.cwrap('td_script_load',          'number', ['string','string']),
      scriptCall:       M.cwrap('td_script_call',          'string', ['number','string','string']),
      scriptUnload:     M.cwrap('td_script_unload',        null,     ['number']),
      i18nLoad:         M.cwrap('td_i18n_load',            null,     ['string','string']),
      i18nSetLocale:    M.cwrap('td_i18n_set_locale',      null,     ['string']),
      i18nT:            M.cwrap('td_i18n_t',               'string', ['string']),
      i18nIsRtl:        M.cwrap('td_i18n_is_rtl',          'number', []),
    };
  }

  // ---- Setup ----------------------------------------------------------------
  function setupGame() {
    // 1. Load all locale tables into the engine.
    for (const loc of LOCALES) {
      api.i18nLoad(loc.code, JSON.stringify(loc.json));
    }
    api.i18nSetLocale(LOCALES[localeIdx].code);

    // 2. Compile + load the tdscript source.
    scriptHandle = api.scriptLoad(TDSCRIPT_SRC, 'script_arena');
    if (scriptHandle < 0) {
      console.error('[script_arena] Failed to load tdscript source');
    } else {
      console.log('[script_arena] tdscript loaded, handle =', scriptHandle);
    }

    // 3. Build HUD (rebuilt on locale change).
    buildHud();

    // 4. Per-frame loop.
    requestAnimationFrame(loop);
  }

  // ---- HUD overlay ----------------------------------------------------------
  function buildHud() {
    if (hudOverlay && hudOverlay.parentNode) {
      hudOverlay.parentNode.removeChild(hudOverlay);
    }
    hudOverlay = document.createElement('div');
    hudOverlay.id = 'script-arena-hud';
    Object.assign(hudOverlay.style, {
      position: 'fixed',
      top: '48px',
      left: '0',
      right: '0',
      zIndex: '40',
      pointerEvents: 'none',
      fontFamily: 'ui-monospace, SF Mono, Menlo, Consolas, monospace',
      color: '#e2e8f0',
      fontSize: '13px',
      padding: '8px 12px',
      display: 'flex',
      gap: '14px',
      flexWrap: 'wrap',
      background: 'linear-gradient(180deg, rgba(10,14,20,0.92) 0%, rgba(17,22,30,0.55) 100%)',
      borderBottom: '1px solid #1f2937',
    });

    if (api.i18nIsRtl()) {
      hudOverlay.style.direction = 'rtl';
      hudOverlay.style.textAlign = 'right';
    } else {
      hudOverlay.style.direction = 'ltr';
      hudOverlay.style.textAlign = 'left';
    }

    function chip(labelKey, valueId) {
      const c = document.createElement('span');
      c.style.cssText = 'padding:2px 8px;border-radius:5px;background:rgba(31,41,55,0.7);border:1px solid #334155;';
      const valueEl = document.createElement('span');
      valueEl.id = valueId;
      valueEl.style.cssText = 'color:#67e8f9;font-weight:700;margin-left:6px;';
      c.textContent = api.i18nT(labelKey) + ':';
      c.appendChild(valueEl);
      return c;
    }

    hudOverlay.appendChild(chip('label_particles', 'hud-particles'));
    hudOverlay.appendChild(chip('label_gravity',   'hud-gravity'));
    hudOverlay.appendChild(chip('label_palette',   'hud-palette'));
    hudOverlay.appendChild(chip('label_locale',    'hud-locale'));

    const hints = document.createElement('div');
    hints.style.cssText = 'flex-basis:100%;font-size:11px;color:#94a3b8;margin-top:2px;';
    hints.textContent = [
      api.i18nT('hint_spawn'),
      api.i18nT('hint_gravity'),
      api.i18nT('hint_palette'),
      api.i18nT('hint_clear'),
      api.i18nT('hint_locale'),
    ].join('  •  ');
    hudOverlay.appendChild(hints);

    document.body.appendChild(hudOverlay);
    updateHudValues();
  }

  function updateHudValues() {
    if (!hudOverlay) return;
    const set = (id, txt) => {
      const el = document.getElementById(id);
      if (el) el.textContent = txt;
    };
    set('hud-particles', String(particles.length));
    const gravOn = (scriptHandle >= 0)
      ? (api.scriptCall(scriptHandle, 'is_gravity_on', '[]') === 'true')
      : false;
    set('hud-gravity', gravOn ? api.i18nT('value_on') : api.i18nT('value_off'));
    set('hud-palette', (LOCALES[localeIdx].json.palette_names[paletteIdxLocal - 1] || '?'));
    set('hud-locale',  LOCALES[localeIdx].name);
  }

  // ---- Per-frame loop -------------------------------------------------------
  function loop(now) {
    const dt = Math.min(0.05, (now - lastTime) / 1000);
    lastTime = now;

    if (gameState === 'playing') {
      handleInput(now);
      stepParticles(dt);
      cullParticles();
      updateHudValues();
    }

    requestAnimationFrame(loop);
  }

  // ---- Input ----------------------------------------------------------------
  function handleInput(now) {
    // 1 / Space -> spawn burst (rate-limited).
    if (api.isKeyDown(VK.ONE) || api.isKeyDown(VK.SPACE)) {
      if (now - lastSpawnTime > 80) {
        spawnBurstAt(WIDTH / 2, HEIGHT / 2, 12);
        lastSpawnTime = now;
      }
    }
    if (keyTriggered(VK.TWO)) {
      const r = api.scriptCall(scriptHandle, 'toggle_gravity', '[]');
      console.log('[script_arena] gravity ->', r);
    }
    if (keyTriggered(VK.THREE)) {
      api.scriptCall(scriptHandle, 'next_palette', '[]');
      paletteIdxLocal++;
      if (paletteIdxLocal > 5) paletteIdxLocal = 1;
    }
    if (keyTriggered(VK.FOUR)) {
      clearAllParticles();
    }
    if (keyTriggered(VK.L)) {
      localeIdx = (localeIdx + 1) % LOCALES.length;
      api.i18nSetLocale(LOCALES[localeIdx].code);
      buildHud();
    }
  }

  const prevKeys = {};
  function keyTriggered(vk) {
    const now = api.isKeyDown(vk);
    const was = prevKeys[vk] || false;
    prevKeys[vk] = now;
    return now && !was;
  }

  // ---- Script bridge --------------------------------------------------------
  // Calls script's spawn_burst(count, cx, cy). The script returns a
  // comma-separated string of newly-created entity IDs, which we parse to
  // track them for JS-side physics + culling.
  function spawnBurstAt(cx, cy, count) {
    if (scriptHandle < 0) return;
    const args = '[' + count + ',' + cx + ',' + cy + ']';
    const idList = api.scriptCall(scriptHandle, 'spawn_burst', args);
    if (!idList || idList === 'nil' || idList === '') return;

    // Parse the comma-separated list of new IDs.
    const ids = idList.split(',');
    for (const idStr of ids) {
      const id = parseInt(idStr, 10);
      if (!isFinite(id) || id < 0) continue;

      // Read position to sync JS-side state.
      const M = TDBridge.wasmExports;
      const buf = M._malloc(8);
      api.getPos(id, buf, buf + 4);
      const px = M.HEAPF32[buf >> 2];
      const py = M.HEAPF32[(buf + 4) >> 2];
      M._free(buf);

      // Re-derive velocity from the script's pattern (i % count for angle).
      const idx = particles.length;
      const angle = idx * (Math.PI * 2 / 12);
      const speed = 80 + (idx % 5) * 25;

      particles.push({
        id: id,
        x: px,
        y: py,
        vx: Math.cos(angle) * speed,
        vy: Math.sin(angle) * speed,
        life: 4.0,
      });
      if (particles.length >= 600) break;  // soft cap
    }
  }

  function clearAllParticles() {
    // Build a comma-separated ID list and pass it to the script's clear_all
    // function, which calls td.destroy_entity for each one.
    if (scriptHandle >= 0 && particles.length > 0) {
      const idsStr = particles.map(p => String(p.id)).join(',');
      // argsJson is a JSON array containing one string. The string itself
      // needs proper JSON escaping (quotes).
      const argsJson = '["' + idsStr + '"]';
      api.scriptCall(scriptHandle, 'clear_all', argsJson);
    }
    particles.length = 0;
  }

  // ---- Particle integration -------------------------------------------------
  function stepParticles(dt) {
    const gravOn = (scriptHandle >= 0)
      ? (api.scriptCall(scriptHandle, 'is_gravity_on', '[]') === 'true')
      : false;

    for (let i = 0; i < particles.length; i++) {
      const p = particles[i];
      if (gravOn) p.vy += 220 * dt;
      p.vx *= 0.995;
      p.vy *= 0.995;
      p.x += p.vx * dt;
      p.y += p.vy * dt;
      p.life -= dt;
      // Bounce off walls.
      if (p.x < 4)          { p.x = 4;          p.vx = -p.vx * 0.7; }
      if (p.x > WIDTH - 4)  { p.x = WIDTH - 4;  p.vx = -p.vx * 0.7; }
      if (p.y < 4)          { p.y = 4;          p.vy = -p.vy * 0.7; }
      if (p.y > HEIGHT - 4) { p.y = HEIGHT - 4; p.vy = -p.vy * 0.7; }
      // Push back to engine.
      api.setPos(p.id, p.x, p.y);
    }
  }

  function cullParticles() {
    let i = 0;
    while (i < particles.length) {
      const p = particles[i];
      if (p.life <= 0 || !api.isValid(p.id)) {
        if (api.isValid(p.id)) api.destroy(p.id);
        particles.splice(i, 1);
      } else {
        i++;
      }
    }
  }

  // Clean up HUD when the demo is unloaded (e.g. user picks another demo).
  window.addEventListener('beforeunload', function () {
    if (hudOverlay && hudOverlay.parentNode) {
      hudOverlay.parentNode.removeChild(hudOverlay);
    }
  });
})();
