// =============================================================================
// TD Engine — Deprecated API Tracker + Console Filter Tabs
// File: web/deprecated_tracker.js
//
// Provides a Godot-like deprecated-API warning system + filterable on-page
// console. Three main capabilities:
//
//   1. TDDeprecated.warn(apiName, replacement, sinceVersion)
//      Call this from anywhere in JS code when a deprecated API is used.
//      It logs a structured warning to the on-page console AND increments
//      a per-API hit counter so users can see which deprecated calls are
//      most active in their game.
//
//   2. TDFilterConsole
//      Wraps the existing #console-output div with filter tabs:
//        All | Info | Warning | Error | Deprecated
//      Each tab shows only messages of that level. A badge on each tab
//      shows the count of messages at that level since page load.
//      A search box filters messages by substring.
//
//   3. Integration with TDBridge.onLog
//      If the page calls TDBridge.onLog with a function, the deprecated
//      tracker intercepts log entries that contain "[DEPRECATED]" and
//      routes them through TDDeprecated.warn() so they show up in the
//      Deprecated tab + the per-API counter.
//
// This module is purely additive — if it fails to load, the existing
// console output still works (just without filter tabs).
// =============================================================================

(function (global) {
  'use strict';

  // ---- 1. Deprecated API registry -------------------------------------------

  const registry = new Map();  // apiName -> { hits, replacement, sinceVersion, lastSeen }
  const listeners = [];        // external subscribers (e.g. for analytics)

  function warn(apiName, replacement, sinceVersion) {
    if (!apiName) return;
    let entry = registry.get(apiName);
    if (!entry) {
      entry = {
        hits: 0,
        replacement: replacement || '',
        sinceVersion: sinceVersion || '',
        lastSeen: 0,
      };
      registry.set(apiName, entry);
    }
    entry.hits++;
    entry.lastSeen = Date.now();

    const msg = '[DEPRECATED] ' + apiName +
                (sinceVersion ? ' (since v' + sinceVersion + ')' : '') +
                (replacement ? ' — use ' + replacement + ' instead' : '') +
                ' [' + entry.hits + 'x]';

    // Forward to the on-page console if available
    if (global.TDBridge && typeof global.TDBridge._emitLog === 'function') {
      global.TDBridge._emitLog('deprecated', msg);
    } else if (global.console) {
      console.warn(msg);
    }

    // Notify external subscribers
    for (let i = 0; i < listeners.length; i++) {
      try { listeners[i](apiName, entry); } catch (e) {}
    }
  }

  function getRegistry() {
    // Return a plain object snapshot (sorted by hits desc)
    const out = [];
    registry.forEach(function (entry, name) {
      out.push({ name: name, hits: entry.hits, replacement: entry.replacement,
                 sinceVersion: entry.sinceVersion, lastSeen: entry.lastSeen });
    });
    out.sort(function (a, b) { return b.hits - a.hits; });
    return out;
  }

  function clearRegistry() {
    registry.clear();
  }

  function subscribe(cb) {
    if (typeof cb === 'function') listeners.push(cb);
    return function unsubscribe() {
      const i = listeners.indexOf(cb);
      if (i !== -1) listeners.splice(i, 1);
    };
  }

  const TDDeprecated = {
    warn: warn,
    getRegistry: getRegistry,
    clearRegistry: clearRegistry,
    subscribe: subscribe,
  };

  global.TDDeprecated = TDDeprecated;

  // ---- 2. Filter console with tabs -----------------------------------------

  function buildFilterConsole() {
    if (document.getElementById('td-filter-tabs')) return;  // already built

    const consoleOutput = document.getElementById('console-output');
    if (!consoleOutput) {
      // Page doesn't have a console-output element; nothing to enhance.
      return;
    }

    // Find the parent #engine-console container
    const engineConsole = document.getElementById('engine-console') || consoleOutput.parentElement;
    if (!engineConsole) return;

    // Build the tab bar — inserted as the first child of #engine-console,
    // above #console-output.
    const tabBar = document.createElement('div');
    tabBar.id = 'td-filter-tabs';
    tabBar.style.cssText = [
      'display:flex',
      'align-items:center',
      'gap:0',
      'padding:0 8px',
      'background:#0a0e14',
      'border-bottom:1px solid #1f2937',
      'font-family:ui-monospace,Menlo,Consolas,monospace',
      'font-size:12px',
      'color:#94a3b8',
      'flex-shrink:0',
    ].join(';');

    const TABS = [
      { key: 'all',        label: 'All',         color: '#cbd5e1' },
      { key: 'info',       label: 'Info',        color: '#67e8f9' },
      { key: 'warn',       label: 'Warning',     color: '#fbbf24' },
      { key: 'error',      label: 'Error',       color: '#f87171' },
      { key: 'deprecated', label: 'Deprecated',  color: '#c084fc' },
    ];

    const counts = { all: 0, info: 0, warn: 0, error: 0, deprecated: 0 };
    let activeFilter = 'all';
    let searchQuery = '';

    const tabButtons = {};
    TABS.forEach(function (tab) {
      const btn = document.createElement('button');
      btn.dataset.filter = tab.key;
      btn.style.cssText = [
        'background:transparent',
        'border:none',
        'border-bottom:2px solid transparent',
        'color:' + (tab.key === 'all' ? '#e2e8f0' : tab.color),
        'padding:8px 12px',
        'cursor:pointer',
        'font-family:inherit',
        'font-size:12px',
        'font-weight:' + (tab.key === 'all' ? '600' : '400'),
      ].join(';');
      btn.innerHTML = tab.label + ' <span class="td-tab-count" style="margin-left:4px;padding:1px 6px;background:' + tab.color + '22;color:' + tab.color + ';border-radius:8px;font-size:11px">0</span>';
      btn.addEventListener('click', function () {
        activeFilter = tab.key;
        Object.keys(tabButtons).forEach(function (k) {
          const b = tabButtons[k];
          b.style.fontWeight = (k === activeFilter) ? '600' : '400';
          b.style.color = (k === activeFilter) ? '#e2e8f0' : '#94a3b8';
          b.style.borderBottomColor = (k === activeFilter) ? '#67e8f9' : 'transparent';
        });
        applyFilter();
      });
      tabBar.appendChild(btn);
      tabButtons[tab.key] = btn;
    });
    // Default: 'all' tab active
    tabButtons.all.style.borderBottomColor = '#67e8f9';

    // Search box at the right end of the tab bar
    const searchBox = document.createElement('input');
    searchBox.type = 'text';
    searchBox.placeholder = 'Filter...';
    searchBox.style.cssText = [
      'margin-left:auto',
      'margin-right:8px',
      'padding:4px 8px',
      'background:#11161e',
      'color:#e2e8f0',
      'border:1px solid #1f2937',
      'border-radius:4px',
      'font-family:inherit',
      'font-size:12px',
      'width:140px',
    ].join(';');
    searchBox.addEventListener('input', function () {
      searchQuery = searchBox.value.toLowerCase();
      applyFilter();
    });
    tabBar.appendChild(searchBox);

    // Clear button
    const clearBtn = document.createElement('button');
    clearBtn.textContent = 'Clear';
    clearBtn.title = 'Clear all console output';
    clearBtn.style.cssText = [
      'background:transparent',
      'border:1px solid #334155',
      'color:#94a3b8',
      'padding:4px 10px',
      'cursor:pointer',
      'font-family:inherit',
      'font-size:12px',
      'border-radius:4px',
    ].join(';');
    clearBtn.addEventListener('click', function () {
      consoleOutput.innerHTML = '';
      Object.keys(counts).forEach(function (k) { counts[k] = 0; });
      updateBadges();
    });
    tabBar.appendChild(clearBtn);

    // Insert tab bar at the top of the engine console
    engineConsole.insertBefore(tabBar, consoleOutput);

    function levelForEntry(el) {
      // Map CSS class -> filter level
      if (el.classList.contains('log-error')) return 'error';
      if (el.classList.contains('log-warn')) return 'warn';
      if (el.classList.contains('log-deprecated')) return 'deprecated';
      return 'info';
    }

    function updateBadges() {
      Object.keys(tabButtons).forEach(function (k) {
        const span = tabButtons[k].querySelector('.td-tab-count');
        if (span) span.textContent = String(counts[k] || 0);
      });
    }

    function applyFilter() {
      const children = consoleOutput.children;
      for (let i = 0; i < children.length; i++) {
        const child = children[i];
        const lvl = levelForEntry(child);
        const levelOk = (activeFilter === 'all') || (lvl === activeFilter);
        const text = child.textContent || '';
        const searchOk = !searchQuery || text.toLowerCase().indexOf(searchQuery) !== -1;
        child.style.display = (levelOk && searchOk) ? '' : 'none';
      }
    }

    // Observe new log entries being appended to #console-output
    const observer = new MutationObserver(function (mutations) {
      mutations.forEach(function (m) {
        for (let i = 0; i < m.addedNodes.length; i++) {
          const node = m.addedNodes[i];
          if (node.nodeType !== 1) continue;  // text nodes ignored
          const lvl = levelForEntry(node);
          counts.all++;
          counts[lvl]++;
          // Apply current filter to the new entry
          const levelOk = (activeFilter === 'all') || (lvl === activeFilter);
          const text = node.textContent || '';
          const searchOk = !searchQuery || text.toLowerCase().indexOf(searchQuery) !== -1;
          node.style.display = (levelOk && searchOk) ? '' : 'none';
        }
        updateBadges();
      });
    });
    observer.observe(consoleOutput, { childList: true });
    updateBadges();
  }

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', buildFilterConsole);
  } else {
    buildFilterConsole();
  }

  // ---- 3. Hook into TDBridge.onLog to detect [DEPRECATED] messages ---------
  // The C++ engine can emit "[DEPRECATED] foo()" via TD_LOG_WARN. We intercept
  // those entries so they get classified as 'deprecated' (purple) rather than
  // 'warn' (yellow), and feed them through the registry for hit-counting.
  function classifyDeprecated(message) {
    if (!message) return null;
    const m = String(message).match(/^\[DEPRECATED\]\s+(\S+)(?:\s*\(since v([0-9.]+)\))?(?:\s*—\s*use\s+(\S+)\s*instead)?/);
    if (!m) return null;
    return { apiName: m[1], sinceVersion: m[2] || '', replacement: m[3] || '' };
  }

  // Export the classifier for testing + external use
  TDDeprecated.classifyDeprecated = classifyDeprecated;

  // Self-register: when the page calls TDBridge.onLog(cb), wrap cb so we
  // can intercept deprecated messages. We do this lazily because TDBridge
  // may not be defined yet at script-load time.
  function hookIntoBridge() {
    if (!global.TDBridge || !global.TDBridge.onLog) {
      // Try again in 200ms; TDBridge loads asynchronously
      setTimeout(hookIntoBridge, 200);
      return;
    }
    const origOnLog = global.TDBridge.onLog;
    global.TDBridge.onLog = function (cb) {
      return origOnLog.call(global.TDBridge, function (entry) {
        try {
          const dep = classifyDeprecated(entry && entry.message);
          if (dep) {
            // Re-classify the entry's level so the filter console puts it
            // in the Deprecated tab.
            entry = Object.assign({}, entry, { level: 'deprecated' });
            // Feed into registry for hit-counting (suppresses the automatic
            // _emitLog call inside warn() since we're already in the log
            // pipeline — we just want the count).
            const entry2 = registry.get(dep.apiName);
            if (entry2) { entry2.hits++; entry2.lastSeen = Date.now(); }
            else {
              registry.set(dep.apiName, {
                hits: 1,
                replacement: dep.replacement,
                sinceVersion: dep.sinceVersion,
                lastSeen: Date.now(),
              });
            }
          }
        } catch (e) {}
        cb(entry);
      });
    };
  }
  hookIntoBridge();

})(typeof window !== 'undefined' ? window : this);
