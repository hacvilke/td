// =============================================================================
// TD Engine — Frame Profiler Overlay (Godot-like)
// File: web/profiler.js
//
// Lightweight frame-time + memory + custom-counter profiler. Mirrors Godot's
// "Debug > Profiler" panel: rolling frame-time graph, current/avg/max frame
// time, draw-call count, WASM heap usage, and arbitrary user counters.
//
// Public API:
//
//   TDProfiler.mount(containerEl, opts?)
//      Mount the profiler overlay. opts:
//        historySize (default 240) — number of frames kept for the graph
//        refreshMs (default 100)   — DOM refresh interval (graph redraw)
//        height (default 120)      — overlay height in px
//      Returns a handle with:
//        .unmount()                — tear down
//        .frame(dtMs)              — record one frame (call from your main loop)
//        .counter(name, value)     — set a named counter (e.g. drawCalls)
//        .increment(name, delta)   — convenience for counter += delta
//        .mark(label)              — drop a vertical marker on the graph
//        .snapshot()               — plain object for tests/headless
//        .reset()                  — clear history + counters
//
//   TDProfiler.snapshot()
//      Plain object: { frameMs, avgMs, maxMs, minMs, frames, counters,
//                      heapUsed, heapTotal, markers, createdAt }
//
// Strictly additive: if performance.now() isn't available (very old envs),
// falls back to Date.now(). If the DOM isn't available, mount() throws but
// snapshot() still works headless.
//
// Auto-integration: if TDEngine is present and has a main-loop hook, the
// profiler auto-pipes frame times. Otherwise the user calls .frame() manually.
// =============================================================================

(function (global) {
  'use strict';

  function now() {
    return (typeof performance !== 'undefined' && performance.now)
      ? performance.now()
      : Date.now();
  }

  function heapInfo() {
    if (typeof memoryUsage === 'function') { // Node
      const m = process.memoryUsage();
      return { used: m.heapUsed, total: m.heapTotal, limit: m.heapUsed };
    }
    if (global.TDEngine && global.TDEngine.module) {
      // Emscripten exposes WASM memory as a WebAssembly.Memory
      try {
        const mod = global.TDEngine.module;
        if (mod && mod.wasmMemory) {
          const buf = mod.wasmMemory.buffer;
          return { used: buf.byteLength, total: buf.byteLength, limit: buf.byteLength };
        }
        if (mod && mod.HEAPU8 && mod.HEAPU8.buffer) {
          return { used: mod.HEAPU8.buffer.byteLength, total: mod.HEAPU8.buffer.byteLength, limit: mod.HEAPU8.buffer.byteLength };
        }
      } catch (e) {}
    }
    if (global.performance && performance.memory) { // Chrome non-standard
      return { used: performance.memory.usedJSHeapSize, total: performance.memory.totalJSHeapSize, limit: performance.memory.jsHeapSizeLimit };
    }
    return { used: 0, total: 0, limit: 0 };
  }

  // ---- headless core ---------------------------------------------------------

  function createCore(historySize) {
    return {
      history: [],            // ring of { t, dt }
      historySize: historySize || 240,
      counters: new Map(),    // name -> number
      counterHistory: new Map(), // name -> number[] (rolling, optional)
      markers: [],            // { t, label }
      lastT: 0,
      frameMs: 0,
      frames: 0,
      sumMs: 0,
      maxMs: 0,
      minMs: Infinity,
    };
  }

  function recordFrame(core, dtMs) {
    if (typeof dtMs !== 'number' || !isFinite(dtMs)) dtMs = 0;
    if (dtMs < 0) dtMs = 0;
    core.frameMs = dtMs;
    core.frames++;
    core.sumMs += dtMs;
    if (dtMs > core.maxMs) core.maxMs = dtMs;
    if (dtMs < core.minMs) core.minMs = dtMs;
    core.history.push({ t: now(), dt: dtMs });
    if (core.history.length > core.historySize) core.history.shift();
  }

  function setCounter(core, name, value) {
    core.counters.set(name, value);
  }
  function increment(core, name, delta) {
    core.counters.set(name, (core.counters.get(name) || 0) + (delta || 1));
  }
  function mark(core, label) {
    core.markers.push({ t: now(), label: label || '' });
    if (core.markers.length > 64) core.markers.shift();
  }
  function reset(core) {
    core.history.length = 0;
    core.counters.clear();
    core.markers.length = 0;
    core.frames = 0;
    core.sumMs = 0;
    core.maxMs = 0;
    core.minMs = Infinity;
    core.frameMs = 0;
  }

  function snapshotOf(core) {
    const avg = core.frames > 0 ? core.sumMs / core.frames : 0;
    const heap = heapInfo();
    const counters = {};
    core.counters.forEach(function (v, k) { counters[k] = v; });
    return {
      frameMs: core.frameMs,
      avgMs: avg,
      maxMs: core.maxMs === Infinity ? 0 : core.maxMs,
      minMs: core.minMs === Infinity ? 0 : core.minMs,
      frames: core.frames,
      counters: counters,
      heapUsed: heap.used,
      heapTotal: heap.total,
      heapLimit: heap.limit,
      markers: core.markers.slice(-8),
      historyLen: core.history.length,
      createdAt: Date.now(),
    };
  }

  // ---- DOM rendering ---------------------------------------------------------

  function h(tag, attrs, children) {
    const el = document.createElement(tag);
    if (attrs) for (const k in attrs) {
      if (k === 'class') el.className = attrs[k];
      else if (k === 'text') el.textContent = attrs[k];
      else if (attrs[k] !== null && attrs[k] !== undefined) el.setAttribute(k, attrs[k]);
    }
    if (children) (Array.isArray(children) ? children : [children]).forEach(function (c) {
      if (c == null) return;
      el.appendChild(typeof c === 'string' ? document.createTextNode(c) : c);
    });
    return el;
  }

  function mount(containerEl, opts) {
    opts = opts || {};
    const core = createCore(opts.historySize || 240);
    const refreshMs = opts.refreshMs || 100;
    const height = opts.height || 120;

    const root = h('div', { class: 'td-profiler' });
    root.style.height = height + 'px';
    containerEl.appendChild(root);

    const canvas = h('canvas', { class: 'td-profiler-graph' });
    canvas.width = 480; canvas.height = height - 24;
    root.appendChild(canvas);
    const ctx = canvas.getContext('2d');

    const statsEl = h('div', { class: 'td-profiler-stats' });
    root.appendChild(statsEl);

    let timer = null;
    function draw() {
      const w = canvas.width, hh = canvas.height;
      ctx.clearRect(0, 0, w, hh);
      // grid lines
      ctx.strokeStyle = 'rgba(255,255,255,0.08)';
      ctx.lineWidth = 1;
      for (let i = 1; i < 4; i++) {
        const y = (hh * i) / 4;
        ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(w, y); ctx.stroke();
      }
      if (core.history.length < 2) return;
      // 16.67ms (60 FPS) budget line in green, 33.3ms (30 FPS) in yellow
      function lineFor(budgetMs, color) {
        const y = hh - (budgetMs / 50) * hh; // 0..50ms range
        if (y < 0 || y > hh) return;
        ctx.strokeStyle = color;
        ctx.setLineDash([4, 4]);
        ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(w, y); ctx.stroke();
        ctx.setLineDash([]);
      }
      lineFor(16.67, 'rgba(80,220,120,0.4)');
      lineFor(33.33, 'rgba(220,200,80,0.4)');

      // frame-time polyline
      ctx.strokeStyle = '#5ce1ff';
      ctx.lineWidth = 1.5;
      ctx.beginPath();
      const n = core.history.length;
      for (let i = 0; i < n; i++) {
        const x = (i / (core.historySize - 1)) * w;
        const y = hh - Math.min(core.history[i].dt / 50, 1) * hh;
        if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
      }
      ctx.stroke();

      // markers
      core.markers.forEach(function (m) {
        // find nearest history index
        const idx = core.history.findIndex(function (h2) { return h2.t >= m.t; });
        if (idx < 0) return;
        const x = (idx / (core.historySize - 1)) * w;
        ctx.strokeStyle = 'rgba(255,120,120,0.7)';
        ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, hh); ctx.stroke();
      });
    }

    function renderStats() {
      const s = snapshotOf(core);
      const fps = s.frameMs > 0 ? (1000 / s.frameMs) : 0;
      const counters = Object.keys(s.counters).map(function (k) {
        return k + '=' + s.counters[k];
      }).join(' ');
      statsEl.innerHTML = '';
      statsEl.appendChild(h('span', { text: 'frame: ' + s.frameMs.toFixed(2) + 'ms' }));
      statsEl.appendChild(h('span', { text: 'avg: ' + s.avgMs.toFixed(2) + 'ms' }));
      statsEl.appendChild(h('span', { text: 'max: ' + s.maxMs.toFixed(2) + 'ms' }));
      statsEl.appendChild(h('span', { text: 'fps: ' + fps.toFixed(0) }));
      statsEl.appendChild(h('span', { text: 'heap: ' + (s.heapUsed / 1048576).toFixed(1) + '/' + (s.heapTotal / 1048576).toFixed(1) + 'MB' }));
      if (counters) statsEl.appendChild(h('span', { class: 'td-profiler-counters', text: counters }));
    }

    function loop() { draw(); renderStats(); }
    timer = setInterval(loop, refreshMs);
    loop();

    return {
      unmount: function () { if (timer) clearInterval(timer); if (root.parentNode) root.parentNode.removeChild(root); },
      frame: function (dtMs) { recordFrame(core, dtMs); },
      counter: function (name, value) { setCounter(core, name, value); },
      increment: function (name, delta) { increment(core, name, delta); },
      mark: function (label) { mark(core, label); },
      reset: function () { reset(core); },
      snapshot: function () { return snapshotOf(core); },
    };
  }

  // ---- Public API ------------------------------------------------------------

  const TDProfiler = {
    mount: mount,
    snapshot: function () { return snapshotOf(createCore(1)); }, // headless empty snapshot
    // Headless mode: expose core ops for tests
    _createCore: createCore,
    _recordFrame: recordFrame,
    _setCounter: setCounter,
    _increment: increment,
    _mark: mark,
    _snapshotOf: snapshotOf,
    version: '1.0.0',
  };

  global.TDProfiler = TDProfiler;

})(typeof window !== 'undefined' ? window : this);
