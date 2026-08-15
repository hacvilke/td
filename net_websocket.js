// =============================================================================
// TD Engine — Web Network Module (WebSocket transport)
// File: web/net_websocket.js
//
// Exposes the engine's networking abstractions to web games. Browsers cannot
// do raw UDP (the engine's desktop transport uses UDP), so we provide a
// WebSocket-based transport that mirrors the same NetPeer / RPC API.
//
// Two layers:
//
//   1. TDNet.Socket — low-level WebSocket wrapper with reconnect, message
//      queueing during disconnect, and binary+text message support.
//        const sock = TDNet.Socket.connect('wss://my-server/room');
//        sock.send(new Uint8Array([1,2,3]));
//        sock.onMessage = (data, isBinary) => { ... };
//        sock.close();
//
//   2. TDNet.RPC — high-level remote-procedure-call layer built on Socket.
//      Mirrors the C++ RpcServer in src/net/transport.h.
//        TDNet.RPC.registerMethod('ping', (args) => 'pong:' + args[0]);
//        TDNet.RPC.callRemote('ping', ['hello'], 5000)
//          .then(result => console.log('got:', result))
//          .catch(err => console.error('rpc failed:', err));
//
//   3. TDNet.ServerConfig — saved server profiles in localStorage. Lets
//      users save their multiplayer server URL once and auto-connect on
//      subsequent visits. UI hook: TDNet.ServerConfig.openPanel().
//
// Wire format (matches the C++ RpcServer for desktop<->web interop):
//   RPC request:  { "id": 123, "m": "methodName", "a": [arg1, arg2, ...] }
//   RPC response: { "id": 123, "r": result } | { "id": 123, "e": "error msg" }
//   RPC fire-and-forget: { "m": "methodName", "a": [...] }  (no id = no reply)
//
// All messages are sent as JSON strings (text frames). Binary frames are
// passed through unchanged for raw byte transport.
// =============================================================================

(function (global) {
  'use strict';

  // ===========================================================================
  // Low-level Socket — WebSocket wrapper with reconnect + queueing
  // ===========================================================================

  const SOCKET_STATES = {
    CONNECTING: 0,
    OPEN: 1,
    CLOSING: 2,
    CLOSED: 3,
  };

  function Socket(url, opts) {
    opts = opts || {};
    this.url = url;
    this.opts = opts;
    this.state = SOCKET_STATES.CLOSED;
    this._ws = null;
    this._queue = [];          // messages buffered while not OPEN
    this._reconnectAttempts = 0;
    this._reconnectTimer = null;
    this._maxReconnect = opts.maxReconnect || 5;
    this._reconnectDelayMs = opts.reconnectDelayMs || 1000;
    this._binaryType = opts.binaryType || 'arraybuffer';
    this._autoReconnect = opts.autoReconnect !== false;
    this.onOpen = null;
    this.onClose = null;
    this.onError = null;
    this.onMessage = null;
    this.onStateChange = null;

    this._open();
  }

  Socket.prototype._open = function () {
    const self = this;
    try {
      this._ws = new WebSocket(this.url);
    } catch (e) {
      this._fail(e);
      return;
    }
    this._ws.binaryType = this._binaryType;
    this.state = SOCKET_STATES.CONNECTING;
    this._notifyStateChange();

    this._ws.onopen = function () {
      self.state = SOCKET_STATES.OPEN;
      self._reconnectAttempts = 0;
      self._notifyStateChange();
      // Flush queued messages
      while (self._queue.length > 0) {
        self._ws.send(self._queue.shift());
      }
      if (typeof self.onOpen === 'function') self.onOpen();
    };

    this._ws.onclose = function (ev) {
      self.state = SOCKET_STATES.CLOSED;
      self._notifyStateChange();
      if (typeof self.onClose === 'function') self.onClose(ev);
      if (self._autoReconnect && self._reconnectAttempts < self._maxReconnect) {
        self._reconnectAttempts++;
        const delay = self._reconnectDelayMs * Math.pow(2, self._reconnectAttempts - 1);
        self._reconnectTimer = setTimeout(function () { self._open(); }, delay);
      }
    };

    this._ws.onerror = function (ev) {
      if (typeof self.onError === 'function') self.onError(ev);
    };

    this._ws.onmessage = function (ev) {
      const isBinary = (ev.data instanceof ArrayBuffer) ||
                       (ev.data instanceof Blob);
      if (typeof self.onMessage === 'function') {
        self.onMessage(ev.data, isBinary);
      }
    };
  };

  Socket.prototype._fail = function (err) {
    this.state = SOCKET_STATES.CLOSED;
    this._notifyStateChange();
    if (typeof this.onError === 'function') this.onError(err);
  };

  Socket.prototype._notifyStateChange = function () {
    if (typeof this.onStateChange === 'function') {
      this.onStateChange(this.state);
    }
  };

  Socket.prototype.send = function (data) {
    if (this.state === SOCKET_STATES.OPEN && this._ws) {
      this._ws.send(data);
      return true;
    }
    // Queue for later
    this._queue.push(data);
    return false;
  };

  Socket.prototype.sendText = function (s) { return this.send(String(s)); };
  Socket.prototype.sendJSON = function (obj) { return this.send(JSON.stringify(obj)); };

  Socket.prototype.close = function () {
    this._autoReconnect = false;  // explicit close — don't reconnect
    if (this._reconnectTimer) {
      clearTimeout(this._reconnectTimer);
      this._reconnectTimer = null;
    }
    if (this._ws) {
      try { this._ws.close(); } catch (e) {}
    }
  };

  Socket.prototype.isOpen = function () { return this.state === SOCKET_STATES.OPEN; };
  Socket.prototype.getState = function () { return this.state; };

  Socket.STATES = SOCKET_STATES;

  // ===========================================================================
  // RPC — high-level request/response over a Socket
  // ===========================================================================

  function RPC(socket) {
    this.socket = socket;
    this._nextId = 1;
    this._pending = new Map();        // id -> { resolve, reject, timer }
    this._methods = new Map();        // methodName -> callback
    this._defaultTimeoutMs = 5000;

    const self = this;
    socket.onMessage = function (data, isBinary) {
      if (isBinary) return;  // RPC only uses text frames
      let msg;
      try {
        msg = JSON.parse(typeof data === 'string' ? data : new TextDecoder().decode(data));
      } catch (e) {
        return;  // not JSON — ignore
      }
      self._handleMessage(msg);
    };
  }

  RPC.prototype.registerMethod = function (name, callback) {
    this._methods.set(name, callback);
  };

  RPC.prototype.unregisterMethod = function (name) {
    this._methods.delete(name);
  };

  RPC.prototype.callRemote = function (method, args, timeoutMs) {
    const self = this;
    return new Promise(function (resolve, reject) {
      if (!self.socket.isOpen()) {
        reject(new Error('socket not open'));
        return;
      }
      const id = self._nextId++;
      const msg = { id: id, m: method, a: args || [] };
      const t = setTimeout(function () {
        if (self._pending.has(id)) {
          self._pending.delete(id);
          reject(new Error('RPC timeout: ' + method + ' (' + (timeoutMs || self._defaultTimeoutMs) + 'ms)'));
        }
      }, timeoutMs || self._defaultTimeoutMs);
      self._pending.set(id, { resolve: resolve, reject: reject, timer: t });
      self.socket.sendJSON(msg);
    });
  };

  // Fire-and-forget: no id, no reply, no Promise
  RPC.prototype.notify = function (method, args) {
    if (!this.socket.isOpen()) return false;
    this.socket.sendJSON({ m: method, a: args || [] });
    return true;
  };

  RPC.prototype._handleMessage = function (msg) {
    // Is this a response to one of our calls?
    if (msg.id !== undefined && this._pending.has(msg.id)) {
      const p = this._pending.get(msg.id);
      this._pending.delete(msg.id);
      clearTimeout(p.timer);
      if (msg.e !== undefined) {
        p.reject(new Error(msg.e));
      } else {
        p.resolve(msg.r);
      }
      return;
    }
    // Is this an incoming RPC request?
    if (msg.m !== undefined) {
      const cb = this._methods.get(msg.m);
      if (!cb) {
        // Unknown method — send error response if request had an id
        if (msg.id !== undefined) {
          this.socket.sendJSON({ id: msg.id, e: 'unknown method: ' + msg.m });
        }
        return;
      }
      try {
        const result = cb(msg.a || []);
        // If request had an id, send response (handle both sync + Promise results)
        if (msg.id !== undefined) {
          if (result && typeof result.then === 'function') {
            const self = this;
            result.then(function (r) { self.socket.sendJSON({ id: msg.id, r: r }); })
                  .catch(function (e) { self.socket.sendJSON({ id: msg.id, e: String(e && e.message || e) }); });
          } else {
            this.socket.sendJSON({ id: msg.id, r: result });
          }
        }
      } catch (e) {
        if (msg.id !== undefined) {
          this.socket.sendJSON({ id: msg.id, e: String(e && e.message || e) });
        }
      }
    }
  };

  RPC.prototype.setDefaultTimeout = function (ms) {
    this._defaultTimeoutMs = ms;
  };

  RPC.prototype.close = function () {
    // Reject all pending calls
    this._pending.forEach(function (p) {
      clearTimeout(p.timer);
      p.reject(new Error('RPC closed'));
    });
    this._pending.clear();
    this._methods.clear();
  };

  // ===========================================================================
  // ServerConfig — saved server profiles (localStorage)
  // ===========================================================================

  const STORAGE_KEY = 'td_engine_net_servers';

  function loadServers() {
    try {
      const raw = global.localStorage.getItem(STORAGE_KEY);
      if (!raw) return [];
      const arr = JSON.parse(raw);
      return Array.isArray(arr) ? arr : [];
    } catch (e) { return []; }
  }

  function saveServers(arr) {
    try { global.localStorage.setItem(STORAGE_KEY, JSON.stringify(arr)); } catch (e) {}
  }

  const ServerConfig = {
    list: loadServers,

    add: function (profile) {
      const arr = loadServers();
      // profile = { name, url, autoConnect }
      if (!profile || !profile.url) return null;
      const entry = {
        name: profile.name || 'Server ' + (arr.length + 1),
        url: profile.url,
        autoConnect: !!profile.autoConnect,
        addedAt: Date.now(),
      };
      arr.push(entry);
      saveServers(arr);
      return entry;
    },

    remove: function (url) {
      const arr = loadServers();
      const filtered = arr.filter(function (s) { return s.url !== url; });
      saveServers(filtered);
      return filtered.length !== arr.length;
    },

    clear: function () { saveServers([]); },

    setAutoConnect: function (url, autoConnect) {
      const arr = loadServers();
      let changed = false;
      arr.forEach(function (s) {
        if (s.url === url) { s.autoConnect = !!autoConnect; changed = true; }
      });
      if (changed) saveServers(arr);
      return changed;
    },

    // Open a UI panel for managing saved servers
    openPanel: function () {
      if (typeof document === 'undefined') return;
      let panel = document.getElementById('td-net-panel');
      if (panel) { panel.style.display = 'flex'; return; }

      panel = document.createElement('div');
      panel.id = 'td-net-panel';
      panel.style.cssText = [
        'position:fixed','top:0','left:0','right:0','bottom:0',
        'background:rgba(0,0,0,0.7)','z-index:1100',
        'display:flex','align-items:center','justify-content:center',
        'font-family:ui-monospace,Menlo,Consolas,monospace',
      ].join(';');

      panel.innerHTML = `
        <div style="background:#11161e;color:#cbd5e1;border:1px solid #1f2937;border-radius:8px;padding:24px;max-width:560px;width:90%;box-shadow:0 12px 40px rgba(0,0,0,0.6)">
          <div style="display:flex;justify-content:space-between;align-items:baseline;margin-bottom:16px">
            <h2 style="margin:0;color:#67e8f9;font-size:18px">Multiplayer Servers</h2>
            <button id="td-net-close" style="background:transparent;border:none;color:#94a3b8;cursor:pointer;font-size:20px">×</button>
          </div>
          <div id="td-net-list" style="margin-bottom:12px;max-height:240px;overflow-y:auto"></div>
          <hr style="border:none;border-top:1px solid #1f2937;margin:14px 0" />
          <h3 style="margin:0 0 8px;font-size:14px;color:#94a3b8">Add new server</h3>
          <input id="td-net-name" type="text" placeholder="Name (e.g. My VPN)" style="width:100%;box-sizing:border-box;padding:8px 10px;background:#0a0e14;color:#e2e8f0;border:1px solid #1f2937;border-radius:4px;font-family:inherit;font-size:13px;margin-bottom:6px" />
          <input id="td-net-url" type="text" placeholder="wss://my-server.example.com/room" style="width:100%;box-sizing:border-box;padding:8px 10px;background:#0a0e14;color:#e2e8f0;border:1px solid #1f2937;border-radius:4px;font-family:inherit;font-size:13px;margin-bottom:6px" />
          <label style="display:flex;align-items:center;gap:6px;font-size:12px;color:#94a3b8;margin-bottom:10px">
            <input id="td-net-auto" type="checkbox" /> Auto-connect on page load
          </label>
          <button id="td-net-add" style="width:100%;padding:10px;background:#0e7490;color:#fff;border:none;border-radius:4px;cursor:pointer;font-family:inherit;font-weight:600">Add server</button>
        </div>
      `;
      document.body.appendChild(panel);

      function render() {
        const list = document.getElementById('td-net-list');
        if (!list) return;
        const arr = loadServers();
        if (arr.length === 0) {
          list.innerHTML = '<p style="margin:0;font-size:13px;color:#64748b;text-align:center;padding:16px">No saved servers yet</p>';
          return;
        }
        list.innerHTML = arr.map(function (s) {
          return '<div style="display:flex;align-items:center;gap:8px;padding:8px;border-bottom:1px solid #1f2937;font-size:13px">' +
                 '<div style="flex:1;min-width:0">' +
                   '<div style="color:#e2e8f0;font-weight:600">' + escapeHtml(s.name) + '</div>' +
                   '<div style="color:#64748b;font-size:11px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap">' + escapeHtml(s.url) + '</div>' +
                 '</div>' +
                 '<label style="font-size:11px;color:#94a3b8;display:flex;align-items:center;gap:4px"><input type="checkbox" data-auto="' + escapeHtml(s.url) + '" ' + (s.autoConnect ? 'checked' : '') + '> Auto</label>' +
                 '<button data-del="' + escapeHtml(s.url) + '" style="background:transparent;border:1px solid #334155;color:#f87171;padding:4px 8px;cursor:pointer;border-radius:4px;font-family:inherit;font-size:11px">Remove</button>' +
                 '</div>';
        }).join('');

        // Wire up the auto-connect checkboxes
        list.querySelectorAll('input[data-auto]').forEach(function (cb) {
          cb.addEventListener('change', function () {
            ServerConfig.setAutoConnect(cb.getAttribute('data-auto'), cb.checked);
          });
        });
        // Wire up the remove buttons
        list.querySelectorAll('button[data-del]').forEach(function (btn) {
          btn.addEventListener('click', function () {
            ServerConfig.remove(btn.getAttribute('data-del'));
            render();
          });
        });
      }

      function escapeHtml(s) {
        return String(s).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;');
      }

      document.getElementById('td-net-close').addEventListener('click', function () {
        panel.style.display = 'none';
      });
      panel.addEventListener('click', function (e) {
        if (e.target === panel) panel.style.display = 'none';
      });
      document.getElementById('td-net-add').addEventListener('click', function () {
        const name = document.getElementById('td-net-name').value.trim();
        const url = document.getElementById('td-net-url').value.trim();
        const auto = document.getElementById('td-net-auto').checked;
        if (!url) return;
        if (!/^wss?:\/\//.test(url)) {
          alert('Server URL must start with ws:// or wss://');
          return;
        }
        ServerConfig.add({ name: name, url: url, autoConnect: auto });
        document.getElementById('td-net-name').value = '';
        document.getElementById('td-net-url').value = '';
        document.getElementById('td-net-auto').checked = false;
        render();
      });

      render();
    },
  };

  // ===========================================================================
  // Compose the TDNet namespace
  // ===========================================================================

  const TDNet = {
    Socket: Socket,
    RPC: RPC,
    ServerConfig: ServerConfig,

    // Convenience: connect + wrap in RPC in one call
    connect: function (url, opts) {
      const sock = new Socket(url, opts);
      const rpc = new RPC(sock);
      return { socket: sock, rpc: rpc };
    },

    STATES: SOCKET_STATES,
  };

  global.TDNet = TDNet;

  // Hook into the modular API
  if (global.TDEngine) {
    global.TDEngine.net = TDNet;
  }

})(typeof window !== 'undefined' ? window : this);
