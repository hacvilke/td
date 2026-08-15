'use strict';

// =============================================================================
// TD Engine — TDScript Runtime (browser + Node.js)
// File: web/tdscript_runtime.js
//
// This is the runtime that compiled TDScript code (.td → .js) runs against.
// It provides:
//
//   - Log.info / Log.warn / Log.error    — logging
//   - Network.broadcastNotification(msg) — server → all clients
//   - Network.sendToClient(peerId, msg)  — server → one client
//   - Network.broadcastState(field, val) — push replicated state to all clients
//   - Network.callRpc(peerId, method, args, mode) — client → server RPC
//   - Physics.checkVoxelCollision(pos)   — server-side voxel ray/AABB test
//   - Vector3(x, y, z)                   — 3D vector math (matches C++ Vector3)
//   - Math                                — passthrough to JS Math
//   - __td_rpc_register(class, method, mode, fn)
//   - __td_repl_register(class, fields[])
//   - __td_script_main(className)        — instantiate + onServerStart hook
//
// In the browser, this file is loaded BEFORE any compiled TDScript code.
// It wires into TDEngine.net (if available) for the actual transport.
//
// In Node.js (dedicated server), it wires into the WebSocket server.
//
// Network protocol:
//   All messages are JSON-RPC frames (same wire format as net_websocket.js).
//   RPCs use method "tdscript.rpc" with params {class, method, args, mode}.
//   Replicated state uses method "tdscript.repl" with params {class, field, value}.
// =============================================================================

(function (global) {
  if (global.TDScriptRuntime) return;  // already loaded

  // --- Vector3 (mirrors src/core/vector3.h) ---------------------------------
  function Vector3(x, y, z) {
    if (!(this instanceof Vector3)) return new Vector3(x, y, z);
    this.x = typeof x === 'number' ? x : 0;
    this.y = typeof y === 'number' ? y : 0;
    this.z = typeof z === 'number' ? z : 0;
  }
  Vector3.prototype = {
    add: function (o) { return new Vector3(this.x + o.x, this.y + o.y, this.z + o.z); },
    sub: function (o) { return new Vector3(this.x - o.x, this.y - o.y, this.z - o.z); },
    mul: function (s) { return new Vector3(this.x * s, this.y * s, this.z * s); },
    length: function () { return Math.sqrt(this.x * this.x + this.y * this.y + this.z * this.z); },
    normalized: function () {
      const L = this.length();
      if (L < 1e-9) return new Vector3(0, 0, 0);
      return new Vector3(this.x / L, this.y / L, this.z / L);
    },
    toString: function () { return 'Vector3(' + this.x + ', ' + this.y + ', ' + this.z + ')'; },
  };

  // --- Log -----------------------------------------------------------------
  const Log = {
    info: function (msg) { console.log('[tdscript] ' + msg); },
    warn: function (msg) { console.warn('[tdscript] ' + msg); },
    error: function (msg) { console.error('[tdscript] ' + msg); },
  };

  // --- Physics (voxel collision stub — real impl in C++ VoxelWorld) --------
  const Physics = {
    // Returns true if the given position is inside a solid voxel.
    // In the browser this calls into the WASM VoxelWorld via TDEngine.bridge.
    // In Node (dedicated server) it calls a native binding if available.
    checkVoxelCollision: function (pos) {
      if (global.TDEngine && global.TDEngine.bridge && global.TDEngine.bridge.wasmExports) {
        try {
          const fn = global.TDEngine.bridge.wasmExports._td_voxel_is_solid;
          if (fn) return !!fn(pos.x, pos.y, pos.z);
        } catch (e) { /* fall through */ }
      }
      // Stub: treat y < 0 as solid (ground plane)
      return pos.y < 0;
    },
  };

  // --- RPC + Replication registry ------------------------------------------
  const rpcTable = new Map();       // key: "ClassName.method" → { mode, fn }
  const replTable = new Map();      // key: "ClassName" → [fieldName, ...]
  const instances = new Map();      // key: "ClassName" → instance

  function rpcKey(cls, method) { return cls + '.' + method; }

  function __td_rpc_register(cls, method, mode, fn) {
    rpcTable.set(rpcKey(cls, method), { mode: mode, fn: fn });
  }

  function __td_repl_register(cls, fields) {
    replTable.set(cls, fields);
    // Wrap each registered instance's fields in getter/setter
    const inst = instances.get(cls);
    if (inst) installReplicatedAccessors(inst, cls, fields);
  }

  function installReplicatedAccessors(instance, cls, fields) {
    for (const fname of fields) {
      // Move this.fname → this._fname, define get/set on instance
      const backing = '_' + fname;
      if (instance[fname] !== undefined && instance[backing] === undefined) {
        instance[backing] = instance[fname];
        delete instance[fname];
      } else if (instance[backing] === undefined) {
        instance[backing] = null;
      }
      Object.defineProperty(instance, fname, {
        configurable: true,
        enumerable: true,
        get: function () { return this[backing]; },
        set: function (v) {
          this[backing] = v;
          // Notify the network layer that this field changed
          Network.broadcastState(cls + '.' + fname, v);
        },
      });
    }
  }

  // --- Network (wires into TDEngine.net if available) ----------------------
  const Network = {
    // Broadcast a notification string to all connected clients.
    broadcastNotification: function (msg) {
      _sendFrame({
        jsonrpc: '2.0',
        method: 'tdscript.notify',
        params: { message: msg },
      }, /*broadcast=*/true);
    },

    // Send a notification to one specific client.
    sendToClient: function (peerId, msg) {
      _sendFrame({
        jsonrpc: '2.0',
        method: 'tdscript.notify',
        params: { message: msg, peerId: peerId },
      }, /*broadcast=*/false, peerId);
    },

    // Push a replicated field update to all clients.
    broadcastState: function (field, value) {
      _sendFrame({
        jsonrpc: '2.0',
        method: 'tdscript.repl',
        params: { field: field, value: value },
      }, /*broadcast=*/true);
    },

    // Client → server RPC invocation. Called from client code, delivered to server.
    callRpc: function (peerId, cls, method, args, mode) {
      _sendFrame({
        jsonrpc: '2.0',
        method: 'tdscript.rpc',
        params: { class: cls, method: method, args: args || [], mode: mode || 'reliable' },
      }, /*broadcast=*/false, peerId);
    },

    // Dispatch an incoming RPC frame (server-side). Called by the transport
    // when a 'tdscript.rpc' method frame arrives.
    dispatchRpc: function (frame, peerId) {
      if (!frame || frame.method !== 'tdscript.rpc') return false;
      const p = frame.params || {};
      const entry = rpcTable.get(rpcKey(p.class, p.method));
      if (!entry) {
        Log.warn('RPC not registered: ' + p.class + '.' + p.method);
        return false;
      }
      const inst = instances.get(p.class);
      if (!inst) {
        Log.warn('No instance for class: ' + p.class);
        return false;
      }
      try {
        const result = entry.fn(inst, p.args || []);
        if (frame.id !== undefined && result !== undefined) {
          _sendFrame({ jsonrpc: '2.0', id: frame.id, result: result }, false, peerId);
        }
      } catch (e) {
        Log.error('RPC ' + p.class + '.' + p.method + ' threw: ' + e.message);
        if (frame.id !== undefined) {
          _sendFrame({ jsonrpc: '2.0', id: frame.id, error: { code: -32000, message: e.message } }, false, peerId);
        }
      }
      return true;
    },

    // Apply a replicated state update (client-side).
    applyReplicated: function (frame) {
      if (!frame || frame.method !== 'tdscript.repl') return false;
      const p = frame.params || {};
      const dot = p.field.lastIndexOf('.');
      if (dot < 0) return false;
      const cls = p.field.substring(0, dot);
      const fname = p.field.substring(dot + 1);
      const inst = instances.get(cls);
      if (!inst) return false;
      // Bypass the setter to avoid echo-back
      inst['_' + fname] = p.value;
      return true;
    },
  };

  // --- Transport shim ------------------------------------------------------
  // _sendFrame routes a JSON-RPC frame through whatever transport is available:
  //   1. TDEngine.net (browser, when connected to a TD game server)
  //   2. global.__td_net_send (Node server: wired by td-server to the WebSocket)
  //   3. no-op (no transport configured — useful for unit tests)
  function _sendFrame(frame, broadcast, peerId) {
    if (global.TDEngine && global.TDEngine.net && typeof global.TDEngine.net.send === 'function') {
      global.TDEngine.net.send(frame, { broadcast: !!broadcast, peerId: peerId });
      return;
    }
    if (typeof global.__td_net_send === 'function') {
      global.__td_net_send(frame, { broadcast: !!broadcast, peerId: peerId });
      return;
    }
    // No transport — drop silently. Tests can assert via Network.lastFrame.
    Network.lastFrame = frame;
  }

  // --- Entry point: instantiate + run onServerStart ------------------------
  function __td_script_main(className) {
    if (!global[className]) {
      Log.error('TDScript: class ' + className + ' not found in global scope');
      return null;
    }
    const inst = new global[className]();
    instances.set(className, inst);
    // If the class registered replicated fields, install accessors now
    const repl = replTable.get(className);
    if (repl) installReplicatedAccessors(inst, className, repl);
    // Call onServerStart if defined
    if (typeof inst.onServerStart === 'function') {
      try { inst.onServerStart(); }
      catch (e) { Log.error('onServerStart threw: ' + e.message); }
    }
    return inst;
  }

  // --- Export ---------------------------------------------------------------
  global.TDScriptRuntime = {
    Vector3: Vector3,
    Log: Log,
    Physics: Physics,
    Network: Network,
    Math: Math,  // passthrough
    rpcTable: rpcTable,
    replTable: replTable,
    instances: instances,
    __td_rpc_register: __td_rpc_register,
    __td_repl_register: __td_repl_register,
    __td_script_main: __td_script_main,
  };

  // Also expose globals that compiled TDScript code expects at top level
  global.Vector3 = Vector3;
  global.Log = Log;
  global.Physics = Physics;
  global.Network = Network;
  global.Math = Math;
  global.__td_rpc_register = __td_rpc_register;
  global.__td_repl_register = __td_repl_register;
  global.__td_script_main = __td_script_main;

})(typeof window !== 'undefined' ? window : (typeof global !== 'undefined' ? global : this));
