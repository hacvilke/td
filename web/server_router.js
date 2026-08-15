// =============================================================================
// TD Engine — Server URL Router
// File: web/server_router.js
//
// Lets users self-host the engine on their own server / VPN / CDN.
//
// Usage in the URL:
//   https://hacvilke.github.io/td/?server=https://my-vpn.example.com/td/
//
// All engine assets (td-engine.js, td-engine.wasm, js_bridge.js, td_api.js,
// net_websocket.js, etc.) will be loaded from the specified server URL instead
// of the local origin.
//
// The chosen server URL is persisted to localStorage so the user only has to
// set it once. A "Settings" gear button in the top bar opens a panel where
// the URL can be entered, validated, saved, or cleared.
//
// Security note: the server URL must serve with HTTPS (or be localhost) and
// must send appropriate CORS headers (Access-Control-Allow-Origin: *) so the
// browser allows cross-origin WASM + script loading. We surface a clear error
// if CORS blocks the load.
// =============================================================================

(function (global) {
  'use strict';

  const STORAGE_KEY = 'td_engine_server_url';
  const ALLOWED_PROTOCOLS = ['https:', 'http:'];

  function getCurrentServerUrl() {
    // 1. ?server=... in the URL takes priority (per-visit override)
    const params = new URLSearchParams(global.location.search);
    const fromUrl = params.get('server');
    if (fromUrl) return normalizeUrl(fromUrl);

    // 2. Fall back to localStorage (saved from a previous visit)
    try {
      const fromStorage = global.localStorage.getItem(STORAGE_KEY);
      if (fromStorage) return normalizeUrl(fromStorage);
    } catch (e) {
      // localStorage may be unavailable (private mode, etc.) — non-fatal
    }

    // 3. Default: same origin
    return '';
  }

  function normalizeUrl(input) {
    if (!input) return '';
    let s = String(input).trim();
    if (!s) return '';
    // Allow bare "example.com" -> treat as https://
    if (!/^[a-z]+:\/\//i.test(s) && !s.startsWith('//')) {
      s = 'https://' + s;
    }
    let u;
    try {
      u = new URL(s);
    } catch (e) {
      return '';  // invalid — caller will fall back to same-origin
    }
    if (ALLOWED_PROTOCOLS.indexOf(u.protocol) === -1) return '';
    // Ensure trailing slash so relative URL resolution works
    let base = u.origin + u.pathname;
    if (!base.endsWith('/')) base += '/';
    return base;
  }

  function saveServerUrl(input) {
    const normalized = normalizeUrl(input);
    try {
      if (normalized) {
        global.localStorage.setItem(STORAGE_KEY, normalized);
      } else {
        global.localStorage.removeItem(STORAGE_KEY);
      }
    } catch (e) {
      // non-fatal
    }
    return normalized;
  }

  function clearServerUrl() {
    try {
      global.localStorage.removeItem(STORAGE_KEY);
    } catch (e) {}
  }

  // Resolve a relative asset path (e.g. 'td-engine.js' or 'td_api.js')
  // against the chosen server URL. Returns the absolute URL to fetch.
  function resolveAsset(relativePath) {
    const base = getCurrentServerUrl();
    if (!base) return relativePath;  // same-origin
    // Strip leading slash from relativePath so URL resolution treats it as relative
    const clean = relativePath.replace(/^\/+/, '');
    return base + clean;
  }

  // Test whether a URL is reachable + CORS-permitted by doing a HEAD fetch.
  // Returns { ok: bool, status: number, error: string|null }.
  async function probeServer(url) {
    try {
      const resp = await fetch(url, { method: 'HEAD', mode: 'cors', cache: 'no-store' });
      return { ok: resp.ok, status: resp.status, error: resp.ok ? null : 'HTTP ' + resp.status };
    } catch (e) {
      return { ok: false, status: 0, error: (e && e.message) ? e.message : 'Network/CORS error' };
    }
  }

  const ServerRouter = {
    getCurrentServerUrl,
    normalizeUrl,
    saveServerUrl,
    clearServerUrl,
    resolveAsset,
    probeServer,
    STORAGE_KEY,
  };

  global.TDServerRouter = ServerRouter;

  // ===========================================================================
  // Settings panel UI — injected into the page on DOMContentLoaded.
  // Adds a gear button to the top bar that toggles a modal where users can
  // enter / test / save a server URL.
  // ===========================================================================
  function buildSettingsPanel() {
    if (document.getElementById('td-settings-panel')) return;  // already built

    // Gear button — appended to the existing #game-selector .meta row
    const meta = document.querySelector('#game-selector .meta');
    if (meta) {
      const sep = document.createTextNode(' | ');
      const link = document.createElement('a');
      link.href = '#';
      link.id = 'td-settings-link';
      link.title = 'Server settings — host the engine on your own VPN/server';
      link.textContent = 'Server';
      link.addEventListener('click', function (e) {
        e.preventDefault();
        const panel = document.getElementById('td-settings-panel');
        if (panel) panel.style.display = (panel.style.display === 'none' || !panel.style.display) ? 'flex' : 'none';
      });
      meta.appendChild(sep);
      meta.appendChild(link);
    }

    // Panel — modal overlay
    const panel = document.createElement('div');
    panel.id = 'td-settings-panel';
    panel.style.cssText = [
      'display:none',
      'position:fixed',
      'top:0', 'left:0', 'right:0', 'bottom:0',
      'background:rgba(0,0,0,0.7)',
      'z-index:1000',
      'align-items:center',
      'justify-content:center',
      'font-family:ui-monospace,Menlo,Consolas,monospace',
    ].join(';');

    panel.innerHTML = `
      <div style="background:#11161e;color:#cbd5e1;border:1px solid #1f2937;border-radius:8px;padding:24px;max-width:560px;width:90%;box-shadow:0 12px 40px rgba(0,0,0,0.6)">
        <div style="display:flex;justify-content:space-between;align-items:baseline;margin-bottom:16px">
          <h2 style="margin:0;color:#67e8f9;font-size:18px">Server Settings</h2>
          <button id="td-settings-close" style="background:transparent;border:none;color:#94a3b8;cursor:pointer;font-size:20px">×</button>
        </div>
        <p style="margin:0 0 12px;font-size:13px;line-height:1.5;color:#94a3b8">
          Host the TD Engine on your own server, VPN, or CDN. Enter the base URL
          where <code style="color:#67e8f9">td-engine.js</code>,
          <code style="color:#67e8f9">td-engine.wasm</code>, and the engine JS
          modules live. The page will load all engine assets from there instead
          of this site.
        </p>
        <label style="display:block;font-size:12px;color:#94a3b8;margin-bottom:6px">Server base URL</label>
        <input id="td-settings-input" type="text" placeholder="https://my-server.example.com/td/"
          style="width:100%;box-sizing:border-box;padding:10px 12px;background:#0a0e14;color:#e2e8f0;border:1px solid #1f2937;border-radius:4px;font-family:inherit;font-size:14px" />
        <div id="td-settings-status" style="margin-top:10px;font-size:12px;min-height:18px"></div>
        <div style="display:flex;gap:8px;margin-top:16px;flex-wrap:wrap">
          <button id="td-settings-test" style="flex:1;min-width:120px;padding:10px;background:#1e293b;color:#e2e8f0;border:1px solid #334155;border-radius:4px;cursor:pointer;font-family:inherit">Test connection</button>
          <button id="td-settings-save" style="flex:1;min-width:120px;padding:10px;background:#0e7490;color:#fff;border:none;border-radius:4px;cursor:pointer;font-family:inherit;font-weight:600">Save & reload</button>
          <button id="td-settings-clear" style="flex:1;min-width:120px;padding:10px;background:transparent;color:#94a3b8;border:1px solid #334155;border-radius:4px;cursor:pointer;font-family:inherit">Clear</button>
        </div>
        <p style="margin:14px 0 0;font-size:11px;line-height:1.5;color:#64748b">
          The server must serve over HTTPS (or be localhost) and send
          <code>Access-Control-Allow-Origin: *</code> for cross-origin WASM + script loading.
          Pass <code>?server=URL</code> in the page URL to override per-visit without saving.
        </p>
      </div>
    `;
    document.body.appendChild(panel);

    const input = document.getElementById('td-settings-input');
    const status = document.getElementById('td-settings-status');
    const current = getCurrentServerUrl();
    input.value = current || '';

    function setStatus(msg, kind) {
      status.textContent = msg || '';
      status.style.color = kind === 'error' ? '#f87171'
                         : kind === 'ok'    ? '#4ade80'
                         : '#94a3b8';
    }

    document.getElementById('td-settings-close').addEventListener('click', function () {
      panel.style.display = 'none';
    });
    panel.addEventListener('click', function (e) {
      if (e.target === panel) panel.style.display = 'none';
    });

    document.getElementById('td-settings-test').addEventListener('click', async function () {
      const raw = input.value.trim();
      if (!raw) { setStatus('Enter a URL first', 'error'); return; }
      const normalized = normalizeUrl(raw);
      if (!normalized) { setStatus('Invalid URL (must be http:// or https://)', 'error'); return; }
      setStatus('Probing ' + normalized + ' ...', '');
      const probe = await probeServer(normalized + 'td-engine.js');
      if (probe.ok) {
        setStatus('✓ Server reachable, CORS OK (HTTP ' + probe.status + ')', 'ok');
      } else {
        setStatus('✗ ' + (probe.error || 'Failed') + (probe.status ? ' (HTTP ' + probe.status + ')' : ''), 'error');
      }
    });

    document.getElementById('td-settings-save').addEventListener('click', function () {
      const raw = input.value.trim();
      const normalized = saveServerUrl(raw);
      if (raw && !normalized) {
        setStatus('Invalid URL — must be http:// or https://', 'error');
        return;
      }
      // Reload the page so all assets are re-fetched from the new server.
      // If saved URL is empty, just remove ?server= from the URL.
      const params = new URLSearchParams(global.location.search);
      if (normalized) {
        params.set('server', normalized);
      } else {
        params.delete('server');
      }
      const newSearch = params.toString();
      global.location.search = newSearch ? '?' + newSearch : '';
    });

    document.getElementById('td-settings-clear').addEventListener('click', function () {
      input.value = '';
      clearServerUrl();
      setStatus('Cleared — page will use this site\'s origin after reload.', '');
    });
  }

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', buildSettingsPanel);
  } else {
    buildSettingsPanel();
  }
})(typeof window !== 'undefined' ? window : this);
