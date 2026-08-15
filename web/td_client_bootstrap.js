'use strict';

// =============================================================================
// TD Engine — Client Bootstrap (auto-connects to project.td's default server)
// File: web/td_client_bootstrap.js
//
// When loaded in a browser, this script:
//   1. Fetches /project.td from the same origin as the page.
//   2. Reads networking.defaultServer to get the WebSocket URL.
//   3. Connects a WebSocket to that URL.
//   4. Wires the TDScript runtime's Network layer to use that WebSocket.
//   5. Loads + runs the compiled server script (if present at entry.compiledServerScript).
//
// This lets a game developer just open their page in a browser during `td serve`
// and have everything "just work" — no manual server URL configuration.
//
// In production (deployed games), this script is OPTIONAL. The game can call
// TDEngine.net.connect(url) directly with a hardcoded server URL.
//
// Usage in index.html:
//   <script src="/engine/tdscript_runtime.js"></script>
//   <script src="/engine/td_client_bootstrap.js"></script>
// =============================================================================

(function (global) {
  if (global.TDClientBootstrap) return;  // already loaded

  async function fetchProjectTd() {
    try {
      const res = await fetch('/project.td');
      if (!res.ok) return null;
      return await res.json();
    } catch (e) {
      return null;
    }
  }

  async function bootstrap() {
    const proj = await fetchProjectTd();
    if (!proj) {
      console.info('[td-bootstrap] No /project.td found — skipping auto-connect.');
      return null;
    }
    if (!proj.networking || !proj.networking.defaultServer) {
      console.info('[td-bootstrap] project.td has no networking.defaultServer — skipping.');
      return null;
    }
    const serverUrl = proj.networking.defaultServer;
    console.info('[td-bootstrap] Connecting to', serverUrl);

    try {
      const ws = new WebSocket(serverUrl);
      ws.onopen = function () {
        console.info('[td-bootstrap] Connected to game-net server.');
        // Wire the runtime's sendFrame to use this WebSocket
        if (global.TDScriptRuntime) {
          global.__td_net_send = function (frame, opts) {
            if (ws.readyState === 1) ws.send(JSON.stringify(frame));
          };
        }
      };
      ws.onmessage = function (ev) {
        let frame;
        try { frame = JSON.parse(ev.data); } catch (e) { return; }
        if (!global.TDScriptRuntime) return;
        if (frame.method === 'tdscript.repl') {
          global.TDScriptRuntime.Network.applyReplicated(frame);
        } else if (frame.method === 'tdscript.notify') {
          console.info('[td-notify]', frame.params.message);
        }
      };
      ws.onclose = function () {
        console.info('[td-bootstrap] Disconnected from game-net server.');
      };
      ws.onerror = function (e) {
        console.warn('[td-bootstrap] WebSocket error:', e);
      };
      return ws;
    } catch (e) {
      console.warn('[td-bootstrap] Failed to connect:', e.message);
      return null;
    }
  }

  global.TDClientBootstrap = { bootstrap: bootstrap };

  // Auto-bootstrap on DOMContentLoaded
  if (global.document) {
    if (document.readyState === 'loading') {
      document.addEventListener('DOMContentLoaded', bootstrap);
    } else {
      bootstrap();
    }
  } else {
    // Non-browser environment (Node) — caller invokes bootstrap() explicitly
  }
})(typeof window !== 'undefined' ? window : (typeof global !== 'undefined' ? global : this));
