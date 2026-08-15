// =============================================================================
// network_stub — Networking / TDScript integration facade.
// -----------------------------------------------------------------------------
// TD Engine's networking layer has two halves:
//   1) TDEngine.net.* — JSON-RPC over WebSocket (web/net_websocket.js, 28
//      tests).  Real-time, reliable, browser-native.
//   2) TDScript @rpc decorators — server-authoritative gameplay code in
//      .td files.  The compiler (src/scripting/tdscript/) emits JS that
//      calls into TDEngine.net to send/receive RPC frames.
//
// This showcase doesn't run a server, but it DEMONSTRATES the integration
// by reading project.td (if present) and exposing:
//   - getProjectConfig()  -> { name, version, networking, entry }
//   - syncEntity(entityId) -> emits a fake "replicated" frame so you can see
//     how a multiplayer server would push entity state to clients
//   - onRpc(method, cb)   -> register a server-RPC handler (mirrors the
//     @rpc(reliable) decorator in TDScript)
//
// In a real multiplayer game you'd run `td serve`, which boots the WebSocket
// server on port 8081, loads your project.td, compiles server_main.td, and
// starts dispatching RPCs.  See:
//   web/tdscript_runtime.js  — runtime that compiled .td code runs against
//   web/td_client_bootstrap.js — auto-connects the browser to project.td's
//                                networking.defaultServer on page load
// =============================================================================

(function (global) {
  'use strict';

  let _projectConfig = null;
  let _rpcHandlers = new Map();
  let _connected = false;
  let _socket = null;

  // ---- Project config ---------------------------------------------------

  async function loadProjectConfig() {
    // Try to fetch project.td from the same directory.
    try {
      const r = await fetch('./project.td');
      if (r.ok) {
        const text = await r.text();
        // project.td is JSON (see tools/cli/lib/project_tds.js).
        _projectConfig = JSON.parse(text);
      }
    } catch (e) {
      // No project.td — that's fine; we'll use defaults.
    }
    if (!_projectConfig) {
      _projectConfig = {
        name: 'td-sandbox-showcase',
        version: '1.0.0',
        engineVersion: '2026.4.1',
        config: {
          renderBackend: 'WebGL2',
          targetFrameRate: 60,
          wasmMemory: 'default',
          canvasId: 'game-canvas',
        },
        networking: {
          defaultServer: 'ws://127.0.0.1:8081/room',
          tickRateHz: 60,
          protocol: 'WebSocket',
          modes: ['RELIABLE_ORDERED'],
        },
        entry: {
          mainScript: 'src/server/server_main.td',
          clientScript: 'src/client/client_main.td',
        },
      };
    }
    return _projectConfig;
  }

  function getProjectConfig() { return _projectConfig; }

  // ---- RPC handlers (mirrors TDScript @rpc decorator) ------------------
  //
  // In TDScript, you'd write:
  //   @rpc(reliable)
  //   public void receiveClientInput(PlayerInputState input, uint32 tick) {
  //     this.playerPosition += input.moveDir * 0.16;
  //   }
  //
  // The compiler emits JS that calls Network.registerRpc('receiveClientInput',
  // function(input, tick) { ... }).  Here we expose the same shape so the
  // showcase can demonstrate the pattern.

  function registerRpc(method, handler) {
    _rpcHandlers.set(method, handler);
  }

  function callRpc(method, args) {
    const h = _rpcHandlers.get(method);
    if (h) {
      try { return h(args); } catch (e) { console.error('[td-rpc]', method, e); }
    } else {
      console.warn('[td-rpc] No handler for', method);
    }
    return null;
  }

  function listRpcs() { return Array.from(_rpcHandlers.keys()); }

  // ---- Entity sync (mirrors TDScript `replicated` fields) --------------
  //
  // In TDScript, you'd write:
  //   replicated Vector3 playerPosition;
  //
  // The runtime automatically sends a "rep" frame containing the field's
  // current value to all clients every networking.tickRateHz.  Here we
  // expose the same call so the showcase can emit a (visible) frame.

  function syncEntity(entityId) {
    const e = global.TDSandbox.ecs.get(entityId);
    if (!e) return;
    const frame = {
      type: 'tdscript.repl',
      entityId: entityId,
      fields: {
        position: e.position,
        color: e.color,
      },
      tick: Math.floor(performance.now() / 16.66),
    };
    // If we have a live socket, send it.  Otherwise just log.
    if (_socket && _socket.readyState === 1) {
      _socket.send(JSON.stringify(frame));
    } else {
      // Silent — keeps the console clean.  Flip TD_DEBUG_NET to 1 to see frames.
      if (global.TD_DEBUG_NET) console.log('[td-net:rep]', frame);
    }
  }

  // ---- Connection (uses the engine's WebSocket transport) --------------
  //
  // If you ran `td serve` and the project.td has a defaultServer URL, we
  // can connect to it.  This is what web/td_client_bootstrap.js does
  // automatically — we expose it here so the showcase can show the flow.

  async function connect(url) {
    if (_socket) { try { _socket.close(); } catch (e) {} }
    const target = url || (_projectConfig && _projectConfig.networking
                           ? _projectConfig.networking.defaultServer : null);
    if (!target) {
      console.warn('[td-net] No defaultServer configured in project.td');
      return false;
    }
    try {
      _socket = new WebSocket(target);
      _socket.onopen = () => { _connected = true; console.log('[td-net] connected to', target); };
      _socket.onclose = () => { _connected = false; console.log('[td-net] disconnected'); };
      _socket.onmessage = (ev) => {
        let frame;
        try { frame = JSON.parse(ev.data); } catch (e) { return; }
        if (frame.type === 'tdscript.rpc') {
          callRpc(frame.method, frame.args);
        }
      };
      return true;
    } catch (e) {
      console.warn('[td-net] connect failed:', e.message);
      return false;
    }
  }

  function disconnect() {
    if (_socket) { try { _socket.close(); } catch (e) {} _socket = null; }
    _connected = false;
  }

  function isConnected() { return _connected; }

  global.TDSandbox = global.TDSandbox || {};
  global.TDSandbox.network = {
    loadProjectConfig, getProjectConfig,
    registerRpc, callRpc, listRpcs,
    syncEntity,
    connect, disconnect, isConnected,
  };
})(typeof window !== 'undefined' ? window : this);
