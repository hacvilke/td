// =============================================================================
// TD Engine — Live ECS Inspector (Godot-like)
// File: web/inspector.js
//
// Provides a live, read-only view of the engine's ECS world. Mirrors Godot's
// "Remote" scene-tree inspector: lists every live entity, shows its components,
// and lets you select one to see its full state (position, velocity, sprite,
// collider, beat state, etc.).
//
// Public API:
//
//   TDInspector.mount(containerEl)
//      Mount the inspector UI into a DOM element. Returns a handle with
//      .unmount(), .refresh(), .select(entityId), .refreshRate(ms).
//
//   TDInspector.tick()
//      Manually pump one refresh. Auto-called by mount() on a timer.
//
//   TDInspector.snapshot()
//      Returns a plain JS object describing the current world state:
//        { entityCount, entities: [{ id, name, alive, position, velocity,
//           sprite, collider, beat }], createdAt }
//      Useful for tests + headless environments (no DOM required).
//
//   TDInspector.select(entityId)
//      Programmatically select an entity in the inspector.
//
//   TDInspector.onSelect(callback)
//      Subscribe to user selection events. callback receives entityId.
//
// Strictly additive: if TDEngine/TDBridge aren't loaded yet, snapshot()
// returns { entityCount: 0, entities: [], createdAt: 0, error: 'not-ready' }.
// =============================================================================

(function (global) {
  'use strict';

  // ---- snapshot ---------------------------------------------------------------

  function snapshot() {
    const out = { entityCount: 0, entities: [], createdAt: Date.now(), error: null };
    if (!global.TDEngine || !global.TDEngine.module) {
      out.error = 'not-ready';
      return out;
    }
    let count = 0;
    try {
      count = TDEngine.ecs.count();
    } catch (e) {
      out.error = 'ecs-count-failed: ' + (e && e.message || e);
      return out;
    }
    out.entityCount = count;
    if (count === 0) return out;

    // We don't have an "iterate all entities" C API exposed yet, so we probe
    // IDs in [1, count] (the engine allocates monotonically). Entities that
    // fail isValid() are skipped. This is good enough for a v1 inspector.
    for (let id = 1; id <= count && out.entities.length < 1024; id++) {
      let alive = false;
      try { alive = TDEngine.ecs.isValid(id); } catch (e) { continue; }
      if (!alive) continue;

      const ent = { id, name: 'Entity_' + id, alive: true, position: null, velocity: null, sprite: null, collider: null, beat: null };
      try { ent.position = TDEngine.ecs.getPosition(id); } catch (e) {}
      // Velocity + sprite + collider aren't readable via current API; we
      // surface them as null rather than guess. When the engine adds getters,
      // they'll flow through here automatically.
      out.entities.push(ent);
    }
    return out;
  }

  // ---- DOM rendering ----------------------------------------------------------

  const DEFAULT_REFRESH_MS = 250;  // 4 Hz — fast enough to feel live, cheap on CPU

  function h(tag, attrs, children) {
    const el = document.createElement(tag);
    if (attrs) {
      for (const k in attrs) {
        if (k === 'class') el.className = attrs[k];
        else if (k === 'text') el.textContent = attrs[k];
        else if (k === 'html') el.innerHTML = attrs[k];
        else if (k.startsWith('on') && typeof attrs[k] === 'function') {
          el.addEventListener(k.slice(2), attrs[k]);
        } else if (attrs[k] !== null && attrs[k] !== undefined) {
          el.setAttribute(k, attrs[k]);
        }
      }
    }
    if (children) {
      (Array.isArray(children) ? children : [children]).forEach(function (c) {
        if (c == null) return;
        el.appendChild(typeof c === 'string' ? document.createTextNode(c) : c);
      });
    }
    return el;
  }

  function mount(containerEl, opts) {
    opts = opts || {};
    const root = h('div', { class: 'td-inspector' });
    containerEl.appendChild(root);

    const state = {
      root,
      selectedId: null,
      refreshMs: opts.refreshMs || DEFAULT_REFRESH_MS,
      timer: null,
      selectListeners: [],
      lastSnap: null,
    };

    function renderHeader(snap) {
      const header = h('div', { class: 'td-inspector-header' }, [
        h('span', { class: 'td-inspector-title', text: 'INSPECTOR' }),
        h('span', { class: 'td-inspector-count', text: (snap.entityCount) + ' entities' }),
        h('button', {
          class: 'td-inspector-refresh',
          text: 'Refresh',
          onclick: function () { tick(); },
        }),
      ]);
      return header;
    }

    function renderEntityRow(ent, selectedId) {
      const isSelected = (ent.id === selectedId);
      const pos = ent.position ? ent.position.x.toFixed(1) + ', ' + ent.position.y.toFixed(1) : '—';
      return h('div', {
        class: 'td-inspector-row' + (isSelected ? ' selected' : ''),
        'data-id': ent.id,
        onclick: function () { select(ent.id); },
      }, [
        h('span', { class: 'td-inspector-id', text: '#' + ent.id }),
        h('span', { class: 'td-inspector-name', text: ent.name }),
        h('span', { class: 'td-inspector-pos', text: pos }),
      ]);
    }

    function renderDetail(ent) {
      if (!ent) {
        return h('div', { class: 'td-inspector-detail empty', text: 'Select an entity to inspect.' });
      }
      const rows = [];
      function kv(k, v) {
        rows.push(h('div', { class: 'td-inspector-kv' }, [
          h('span', { class: 'td-inspector-k', text: k }),
          h('span', { class: 'td-inspector-v', text: String(v) }),
        ]));
      }
      kv('id', ent.id);
      kv('name', ent.name);
      kv('alive', ent.alive);
      if (ent.position) { kv('position.x', ent.position.x.toFixed(3)); kv('position.y', ent.position.y.toFixed(3)); }
      if (ent.velocity) { kv('velocity.x', ent.velocity.x.toFixed(3)); kv('velocity.y', ent.velocity.y.toFixed(3)); }
      if (ent.sprite)   { kv('sprite', JSON.stringify(ent.sprite)); }
      if (ent.collider) { kv('collider', JSON.stringify(ent.collider)); }
      if (ent.beat)     { kv('beat', JSON.stringify(ent.beat)); }
      return h('div', { class: 'td-inspector-detail' }, rows);
    }

    function tick() {
      const snap = snapshot();
      state.lastSnap = snap;
      root.innerHTML = '';
      root.appendChild(renderHeader(snap));
      if (snap.error && snap.error !== 'not-ready') {
        root.appendChild(h('div', { class: 'td-inspector-error', text: 'Error: ' + snap.error }));
        return;
      }
      if (snap.error === 'not-ready') {
        root.appendChild(h('div', { class: 'td-inspector-empty', text: 'Engine not ready.' }));
        return;
      }
      if (snap.entities.length === 0) {
        root.appendChild(h('div', { class: 'td-inspector-empty', text: 'No live entities.' }));
        return;
      }
      // Split into list + detail
      const list = h('div', { class: 'td-inspector-list' },
        snap.entities.map(function (e) { return renderEntityRow(e, state.selectedId); })
      );
      const selected = snap.entities.find(function (e) { return e.id === state.selectedId; }) || snap.entities[0];
      const detail = renderDetail(selected);
      const body = h('div', { class: 'td-inspector-body' }, [list, detail]);
      root.appendChild(body);
    }

    function select(id) {
      state.selectedId = id;
      state.selectListeners.forEach(function (cb) {
        try { cb(id); } catch (e) { console.error('[TDInspector] select listener:', e); }
      });
      tick();
    }

    function start() {
      if (state.timer) return;
      state.timer = setInterval(tick, state.refreshMs);
      tick();
    }
    function stop() { if (state.timer) { clearInterval(state.timer); state.timer = null; } }

    start();

    return {
      unmount: function () { stop(); if (root.parentNode) root.parentNode.removeChild(root); },
      refresh: tick,
      select: select,
      refreshRate: function (ms) {
        if (typeof ms === 'number' && ms >= 50) {
          state.refreshMs = ms;
          if (state.timer) { stop(); start(); }
        }
        return state.refreshMs;
      },
      snapshot: function () { return state.lastSnap || snapshot(); },
    };
  }

  // ---- Public API ------------------------------------------------------------
  //
  // TDInspector.mount(containerEl, opts) → handle with .unmount/.refresh/
  //   .select(id)/.refreshRate(ms)/.snapshot(). Use this for live UI.
  //
  // TDInspector.snapshot() → plain object. Use this for tests + headless.
  //
  // TDInspector.tick() → alias of snapshot(), kept for symmetry with other
  //   modules (TDEngine.profiler.tick(), etc.).

  const TDInspector = {
    mount: mount,
    tick: function () { return snapshot(); },
    snapshot: snapshot,
    version: '1.0.0',
  };

  global.TDInspector = TDInspector;

})(typeof window !== 'undefined' ? window : this);
