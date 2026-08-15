// =============================================================================
// TD Engine — Error Boundary + Crash Reporter
// File: web/error_boundary.js
//
// Wraps the engine boot + main loop in a try/catch that produces a friendly,
// recoverable error UI instead of a blank screen. Mirrors Godot's "Script
// Editor error popup" + crash reporter flow.
//
// Public API:
//
//   TDErrorBoundary.install(opts?)
//      Install global handlers: window.onerror, unhandledrejection, and
//      (if TDBridge exists) TDBridge boot error wrapping. opts:
//        container (default: document.body) — where to mount the error card
//        onSubmit (function(payload))       — called when user clicks "Submit"
//        submitEndpoint (string)            — if set, POST the payload as JSON
//        showStackTrace (default: true)     — render <details> with stack
//        maxStoredReports (default: 10)     — keep last N in localStorage
//      Returns a handle with .uninstall() and .report(err, ctx).
//
//   TDErrorBoundary.report(err, ctx?)
//      Manually report an error. ctx is an optional object merged into the
//      payload. Returns the payload (so tests can inspect it).
//
//   TDErrorBoundary.listReports()
//      Returns an array of stored reports from localStorage (or [] in
//      headless mode).
//
//   TDErrorBoundary.clearReports()
//      Wipe stored reports.
//
//   TDErrorBoundary.snapshot()
//      Returns { installed, reportCount, lastReport } — for tests.
//
// Strictly additive: if window/localStorage aren't available, falls back to
// in-memory storage. If document isn't available, no UI is rendered but
// .report() still works and listeners still fire.
// =============================================================================

(function (global) {
  'use strict';

  // ---- in-memory + localStorage backed storage -------------------------------

  const memoryStore = [];
  let maxStored = 10;
  const listeners = [];

  function lsGet() {
    try {
      if (typeof localStorage !== 'undefined' && localStorage.getItem) {
        const raw = localStorage.getItem('td-error-reports');
        return raw ? JSON.parse(raw) : [];
      }
    } catch (e) {}
    return memoryStore.slice();
  }
  function lsSet(arr) {
    try {
      if (typeof localStorage !== 'undefined' && localStorage.setItem) {
        localStorage.setItem('td-error-reports', JSON.stringify(arr));
        return;
      }
    } catch (e) {}
    memoryStore.length = 0;
    arr.forEach(function (x) { memoryStore.push(x); });
  }

  function storeReport(report) {
    const all = lsGet();
    all.push(report);
    while (all.length > maxStored) all.shift();
    lsSet(all);
    listeners.forEach(function (cb) {
      try { cb(report); } catch (e) { /* swallow */ }
    });
  }

  // ---- payload builder -------------------------------------------------------

  function buildPayload(err, ctx) {
    const stack = (err && err.stack) ? String(err.stack) : '';
    const message = (err && err.message) ? String(err.message) : String(err);
    const ua = (typeof navigator !== 'undefined' && navigator.userAgent) ? navigator.userAgent : 'unknown';
    const url = (typeof location !== 'undefined' && location.href) ? location.href : '';
    const engineVersion = (global.TDEngine && typeof global.TDEngine.lifecycle === 'object')
      ? (function () { try { return global.TDEngine.lifecycle.getVersion(); } catch (e) { return 'unknown'; } })()
      : 'not-loaded';
    return {
      id: 'err_' + Date.now() + '_' + Math.random().toString(36).slice(2, 8),
      timestamp: Date.now(),
      message: message,
      name: (err && err.name) ? err.name : 'Error',
      stack: stack,
      context: ctx || {},
      ua: ua,
      url: url,
      engineVersion: engineVersion,
    };
  }

  // ---- DOM rendering ---------------------------------------------------------

  function h(tag, attrs, children) {
    const el = document.createElement(tag);
    if (attrs) for (const k in attrs) {
      if (k === 'class') el.className = attrs[k];
      else if (k === 'text') el.textContent = attrs[k];
      else if (k.startsWith('on') && typeof attrs[k] === 'function') el.addEventListener(k.slice(2), attrs[k]);
      else if (attrs[k] !== null && attrs[k] !== undefined) el.setAttribute(k, attrs[k]);
    }
    if (children) (Array.isArray(children) ? children : [children]).forEach(function (c) {
      if (c == null) return;
      el.appendChild(typeof c === 'string' ? document.createTextNode(c) : c);
    });
    return el;
  }

  function renderCard(payload, opts) {
    const card = h('div', { class: 'td-error-card' }, [
      h('div', { class: 'td-error-header' }, [
        h('span', { class: 'td-error-icon', text: '!' }),
        h('span', { class: 'td-error-title', text: 'TD Engine encountered an error' }),
        h('button', { class: 'td-error-close', text: '\u00d7', onclick: function () {
          if (card.parentNode) card.parentNode.removeChild(card);
        } }),
      ]),
      h('div', { class: 'td-error-message', text: payload.message }),
      h('div', { class: 'td-error-meta', text: payload.name + ' @ ' + new Date(payload.timestamp).toLocaleTimeString() }),
    ]);

    if (opts.showStackTrace && payload.stack) {
      const details = h('details', { class: 'td-error-stack' }, [
        h('summary', { text: 'Stack trace' }),
        h('pre', { text: payload.stack }),
      ]);
      card.appendChild(details);
    }

    const actions = h('div', { class: 'td-error-actions' });
    if (opts.onSubmit || opts.submitEndpoint) {
      actions.appendChild(h('button', {
        class: 'td-error-submit',
        text: 'Submit Report',
        onclick: function () {
          if (opts.submitEndpoint) {
            try {
              fetch(opts.submitEndpoint, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(payload),
              }).catch(function () {});
            } catch (e) {}
          }
          if (typeof opts.onSubmit === 'function') {
            try { opts.onSubmit(payload); } catch (e) {}
          }
        },
      }));
    }
    actions.appendChild(h('button', {
      class: 'td-error-reload',
      text: 'Reload',
      onclick: function () { if (typeof location !== 'undefined' && location.reload) location.reload(); },
    }));
    actions.appendChild(h('button', {
      class: 'td-error-dismiss',
      text: 'Dismiss',
      onclick: function () { if (card.parentNode) card.parentNode.removeChild(card); },
    }));
    card.appendChild(actions);
    return card;
  }

  // ---- install ---------------------------------------------------------------

  let _installed = false;
  let _opts = null;
  let _prevOnError = null;
  let _prevRejection = null;

  function install(opts) {
    if (_installed) return { uninstall: function () {} };
    opts = opts || {};
    opts.showStackTrace = opts.showStackTrace !== false;
    maxStored = opts.maxStoredReports || 10;
    _opts = opts;

    function show(err, ctx) {
      const payload = report(err, ctx);
      if (typeof document !== 'undefined' && opts.container !== null) {
        const container = opts.container || document.body;
        if (container) {
          const card = renderCard(payload, opts);
          container.appendChild(card);
        }
      }
      // Also log to console for devs
      if (typeof console !== 'undefined' && console.error) {
        console.error('[TDErrorBoundary]', err);
      }
      return payload;
    }

    _prevOnError = global.onerror;
    global.onerror = function (message, source, lineno, colno, err) {
      show(err || new Error(message + ' @ ' + source + ':' + lineno + ':' + colno), { source: source, lineno: lineno, colno: colno });
      if (typeof _prevOnError === 'function') return _prevOnError.apply(this, arguments);
      return true; // prevent default
    };

    _prevRejection = global.onunhandledrejection;
    global.onunhandledrejection = function (ev) {
      const reason = (ev && ev.reason) ? ev.reason : new Error('Unhandled rejection');
      show(reason, { kind: 'unhandledrejection' });
      if (typeof _prevRejection === 'function') return _prevRejection.apply(this, arguments);
    };

    _installed = true;

    return {
      uninstall: function () {
        if (!_installed) return;
        global.onerror = _prevOnError;
        global.onunhandledrejection = _prevRejection;
        _installed = false;
        _opts = null;
      },
      report: show,
    };
  }

  function report(err, ctx) {
    const payload = buildPayload(err, ctx);
    storeReport(payload);
    return payload;
  }

  function listReports() { return lsGet(); }
  function clearReports() { lsSet([]); }
  function onReport(cb) { if (!listeners.includes(cb)) listeners.push(cb); }
  function snapshot() {
    const all = lsGet();
    return {
      installed: _installed,
      reportCount: all.length,
      lastReport: all.length > 0 ? all[all.length - 1] : null,
    };
  }

  // ---- Public API ------------------------------------------------------------

  const TDErrorBoundary = {
    install: install,
    report: report,
    listReports: listReports,
    clearReports: clearReports,
    onReport: onReport,
    snapshot: snapshot,
    // Test escape hatch (no localStorage dependency)
    _buildPayload: buildPayload,
    _storeReport: storeReport,
    _memoryStore: memoryStore,
    version: '1.0.0',
  };

  global.TDErrorBoundary = TDErrorBoundary;

})(typeof window !== 'undefined' ? window : this);
