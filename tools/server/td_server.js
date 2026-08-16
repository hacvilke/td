#!/usr/bin/env node
// =============================================================================
// TD Engine — Standalone Self-Hosted Server
// =============================================================================
// td_server.js is a complete, runnable Node.js server for hosting a TD Engine
// multiplayer game. It implements the wire protocol that web/game_kit.js's
// TDServer namespace speaks, plus the asset-proxy and RPC dispatch features
// that real games need.
//
// Features
// --------
//   1. WebSocket relay (rooms, presence, channel pub/sub, RPC dispatch,
//      custom client hooks).
//   2. HTTP asset proxy — protects API keys for Freesound, Unsplash, etc.
//      by keeping the token server-side and forwarding only the response
//      bytes to the browser.
//   3. File-backed save roaming — players can sign in from any browser and
//      pull their saves (per-playerId + slotName).
//   4. Room management — create / list / join rooms; max-players cap;
//      auto-vacuum when empty.
//   5. Static file server for your game's index.html + assets (so you can
//      run a single process for both the page and the WebSocket).
//   6. Configurable via JSON file, env vars, or CLI flags.
//
// Usage
// -----
//   $ node tools/server/td_server.js \
//       --port 8080 \
//       --static ./public \
//       --saves ./saves \
//       --secrets ./secrets.json
//
// Or with a config file:
//   $ node tools/server/td_server.js --config td-server.json
//
// Config file format (JSON, all fields optional):
//   {
//     "port": 8080,
//     "staticDir": "./public",
//     "savesDir": "./saves",
//     "secrets": { "freesound": { "baseUrl": "...", "header": "Authorization", "value": "Token ..." } },
//     "maxPlayersPerRoom": 16,
//     "presenceIntervalMs": 5000,
//     "cors": { "origins": ["*"] }
//   }
//
// Wire protocol
// -------------
// All frames are JSON. Client -> server frames have a `t` field:
//   { t:'hello', token?, room? }          — initial handshake
//   { t:'bye' }                            — graceful disconnect
//   { t:'sub', topic }                     — subscribe to a channel
//   { t:'unsub', topic }                   — unsubscribe
//   { t:'channel', topic, payload, to? }   — publish to a channel
//   { t:'rpc', id, method, argsJson }      — call a server RPC method
//   { t:'hookResult', id, ok, resultJson?, error? } — reply to a server-hook call
//
// Server -> client frames:
//   { t:'helloAck', id, room }             — assigns playerId
//   { t:'presence', peers:[{id,meta}] }    — periodic peer list
//   { t:'channel', topic, payload, from }  — delivered channel message
//   { t:'rpcResult', id, ok, result?, error? } — RPC reply
//   { t:'hook', id, name, argsJson }       — server invokes a client hook
//
// Dependencies: only `ws` (already in the engine's package.json). No other
// npm packages required.
// =============================================================================

'use strict';

const http = require('http');
const https = require('https');
const fs = require('fs');
const path = require('path');
const url = require('url');
const crypto = require('crypto');

let WebSocket;
try { WebSocket = require('ws'); }
catch (e) {
  console.error('[td_server] FATAL: "ws" module not found. Run `npm install ws` first.');
  process.exit(1);
}

// =========================================================================
// Config parsing
// =========================================================================
function parseArgs(argv) {
  const out = {};
  for (let i = 2; i < argv.length; i++) {
    const a = argv[i];
    if (a.startsWith('--')) {
      const key = a.slice(2);
      const val = argv[i + 1];
      if (val && !val.startsWith('--')) { out[key] = val; i++; }
      else out[key] = true;
    }
  }
  return out;
}

function loadConfig(argv) {
  let cfg = {};
  // 1. Config file (if --config given or td-server.json exists).
  const cfgPath = argv.config || (fs.existsSync('td-server.json') ? 'td-server.json' : null);
  if (cfgPath && fs.existsSync(cfgPath)) {
    try { cfg = JSON.parse(fs.readFileSync(cfgPath, 'utf8')); }
    catch (e) { console.warn('[td_server] bad config file:', e.message); }
  }
  // 2. Env vars (override config).
  if (process.env.TD_PORT)               cfg.port = parseInt(process.env.TD_PORT, 10);
  if (process.env.TD_STATIC_DIR)         cfg.staticDir = process.env.TD_STATIC_DIR;
  if (process.env.TD_SAVES_DIR)          cfg.savesDir = process.env.TD_SAVES_DIR;
  if (process.env.TD_MAX_PLAYERS)        cfg.maxPlayersPerRoom = parseInt(process.env.TD_MAX_PLAYERS, 10);
  // 3. CLI args (override everything).
  if (argv.port)      cfg.port = parseInt(argv.port, 10);
  if (argv.static)    cfg.staticDir = argv.static;
  if (argv.saves)     cfg.savesDir = argv.saves;
  if (argv.secrets) {
    try { cfg.secrets = JSON.parse(fs.readFileSync(argv.secrets, 'utf8')); }
    catch (e) { console.warn('[td_server] bad secrets file:', e.message); }
  }
  // Defaults.
  if (!cfg.port)               cfg.port = 8080;
  if (!cfg.staticDir)          cfg.staticDir = null;
  if (!cfg.savesDir)           cfg.savesDir = './saves';
  if (!cfg.maxPlayersPerRoom)  cfg.maxPlayersPerRoom = 16;
  if (!cfg.presenceIntervalMs) cfg.presenceIntervalMs = 5000;
  if (!cfg.cors)               cfg.cors = { origins: ['*'] };
  if (!cfg.secrets)            cfg.secrets = {};
  return cfg;
}

// =========================================================================
// TdServer — the room/presence/RPC state machine
// =========================================================================
class TdServer {
  constructor(cfg) {
    this.cfg = cfg;
    // peerId -> { ws, room, meta, joinedAt, lastSeenMs }
    this.peers = new Map();
    // roomId -> Set<peerId>
    this.rooms = new Map();
    // topic subscriptions: `${roomId}::${topic}` -> Set<peerId>
    this.subscriptions = new Map();
    // RPC handlers: methodName -> function(args, ctx) -> result
    this.rpcHandlers = new Map();
    // Server -> client hook invocations in flight
    this.pendingHookCalls = new Map();
    this.nextPeerId = 1;
    this.nextHookCallId = 1;
    this.savesDir = cfg.savesDir;
    if (this.savesDir) {
      try { fs.mkdirSync(this.savesDir, { recursive: true }); } catch (_) {}
    }
    this._registerDefaultRpcHandlers();
  }

  _registerDefaultRpcHandlers() {
    this.registerRpc('savePush', (args, ctx) => {
      const [slotName, json] = args;
      if (!slotName || typeof slotName !== 'string') throw new Error('slotName required');
      if (!this.savesDir) throw new Error('server: savesDir not configured');
      const playerDir = path.join(this.savesDir, String(ctx.peerId));
      try { fs.mkdirSync(playerDir, { recursive: true }); } catch (_) {}
      const safe = String(slotName).replace(/[^A-Za-z0-9._-]/g, '_');
      const filePath = path.join(playerDir, safe + '.json');
      fs.writeFileSync(filePath, json, 'utf8');
      return { ok: true, sizeBytes: json.length, slotName };
    });
    this.registerRpc('savePull', (args, ctx) => {
      const [slotName] = args;
      if (!slotName || typeof slotName !== 'string') throw new Error('slotName required');
      if (!this.savesDir) return { json: null };
      const playerDir = path.join(this.savesDir, String(ctx.peerId));
      const safe = String(slotName).replace(/[^A-Za-z0-9._-]/g, '_');
      const filePath = path.join(playerDir, safe + '.json');
      if (!fs.existsSync(filePath)) return { json: null };
      return { json: fs.readFileSync(filePath, 'utf8') };
    });
    this.registerRpc('saveList', (args, ctx) => {
      if (!this.savesDir) return { slots: [] };
      const playerDir = path.join(this.savesDir, String(ctx.peerId));
      if (!fs.existsSync(playerDir)) return { slots: [] };
      const slots = [];
      for (const f of fs.readdirSync(playerDir)) {
        if (!f.endsWith('.json')) continue;
        try {
          const stat = fs.statSync(path.join(playerDir, f));
          const envelope = JSON.parse(fs.readFileSync(path.join(playerDir, f), 'utf8'));
          slots.push({
            name: f.replace(/\.json$/, ''),
            timestamp: envelope.timestamp || stat.mtimeMs,
            sizeBytes: stat.size,
            version: envelope.version,
          });
        } catch (_) { /* skip malformed */ }
      }
      return { slots };
    });
    this.registerRpc('roomList', () => {
      const list = [];
      for (const [roomId, set] of this.rooms) {
        list.push({ id: roomId, playerCount: set.size,
                    maxPlayers: this.cfg.maxPlayersPerRoom });
      }
      return { rooms: list };
    });
    this.registerRpc('roomCreate', (args, ctx) => {
      const roomId = args[0] || ('room-' + Math.random().toString(36).slice(2, 8));
      if (!this.rooms.has(roomId)) this.rooms.set(roomId, new Set());
      this._movePeerToRoom(ctx.peerId, roomId);
      return { roomId };
    });
    this.registerRpc('whoami', (args, ctx) => {
      const peer = this.peers.get(ctx.peerId);
      return { id: ctx.peerId, room: peer ? peer.room : null };
    });
  }

  // --- Game-defined RPC handlers ------------------------------------------
  registerRpc(method, handler) {
    if (typeof method !== 'string' || typeof handler !== 'function') {
      throw new Error('registerRpc: (method, handler) required');
    }
    this.rpcHandlers.set(method, handler);
  }
  unregisterRpc(method) { return this.rpcHandlers.delete(method); }

  // --- Peer lifecycle -----------------------------------------------------
  _nextId() { return 'p' + (this.nextPeerId++); }

  addPeer(ws) {
    const peerId = this._nextId();
    this.peers.set(peerId, {
      ws, room: null, meta: {},
      joinedAt: Date.now(), lastSeenMs: Date.now(),
    });
    return peerId;
  }

  removePeer(peerId) {
    const peer = this.peers.get(peerId);
    if (!peer) return;
    if (peer.room) this._removeFromRoom(peerId, peer.room);
    this.peers.delete(peerId);
  }

  _movePeerToRoom(peerId, roomId) {
    const peer = this.peers.get(peerId);
    if (!peer) return;
    if (peer.room === roomId) return;
    if (peer.room) this._removeFromRoom(peerId, peer.room);
    if (!this.rooms.has(roomId)) this.rooms.set(roomId, new Set());
    const set = this.rooms.get(roomId);
    if (set.size >= this.cfg.maxPlayersPerRoom) {
      throw new Error('room full: ' + roomId);
    }
    set.add(peerId);
    peer.room = roomId;
  }

  _removeFromRoom(peerId, roomId) {
    const set = this.rooms.get(roomId);
    if (!set) return;
    set.delete(peerId);
    if (set.size === 0) this.rooms.delete(roomId);
    const prefix = roomId + '::';
    for (const key of this.subscriptions.keys()) {
      if (key.startsWith(prefix)) {
        const subs = this.subscriptions.get(key);
        subs.delete(peerId);
        if (subs.size === 0) this.subscriptions.delete(key);
      }
    }
  }

  // --- Wire send helpers --------------------------------------------------
  _sendTo(peerId, obj) {
    const peer = this.peers.get(peerId);
    if (!peer || peer.ws.readyState !== WebSocket.OPEN) return false;
    try { peer.ws.send(JSON.stringify(obj)); return true; }
    catch (_) { return false; }
  }

  _broadcastToRoom(roomId, obj, exceptPeerId) {
    const set = this.rooms.get(roomId);
    if (!set) return;
    for (const pid of set) {
      if (exceptPeerId && pid === exceptPeerId) continue;
      this._sendTo(pid, obj);
    }
  }

  // --- Frame dispatch -----------------------------------------------------
  _onFrame(peerId, frame) {
    const peer = this.peers.get(peerId);
    if (!peer) return;
    peer.lastSeenMs = Date.now();

    switch (frame.t) {
      case 'hello': return this._handleHello(peerId, frame);
      case 'bye':   return this.removePeer(peerId);
      case 'sub':   return this._handleSub(peerId, frame.topic);
      case 'unsub': return this._handleUnsub(peerId, frame.topic);
      case 'channel': return this._handleChannel(peerId, frame);
      case 'rpc':   return this._handleRpc(peerId, frame);
      case 'hookResult': return this._handleHookResult(peerId, frame);
      default:
        // Unknown frame — ignore (forward-compatible).
    }
  }

  _handleHello(peerId, frame) {
    const peer = this.peers.get(peerId);
    if (!peer) return;
    if (frame.room) {
      try { this._movePeerToRoom(peerId, frame.room); }
      catch (e) {
        this._sendTo(peerId, { t: 'error', error: e.message });
        return;
      }
    } else {
      this._movePeerToRoom(peerId, 'lobby');
    }
    peer.meta = frame.meta || {};
    this._sendTo(peerId, { t: 'helloAck', id: peerId, room: peer.room });
    this._broadcastPresence(peer.room);
  }

  _handleSub(peerId, topic) {
    const peer = this.peers.get(peerId);
    if (!peer || !peer.room || !topic) return;
    const key = peer.room + '::' + topic;
    if (!this.subscriptions.has(key)) this.subscriptions.set(key, new Set());
    this.subscriptions.get(key).add(peerId);
  }
  _handleUnsub(peerId, topic) {
    const peer = this.peers.get(peerId);
    if (!peer || !peer.room || !topic) return;
    const key = peer.room + '::' + topic;
    const subs = this.subscriptions.get(key);
    if (!subs) return;
    subs.delete(peerId);
    if (subs.size === 0) this.subscriptions.delete(key);
  }

  _handleChannel(peerId, frame) {
    const peer = this.peers.get(peerId);
    if (!peer || !peer.room || !frame.topic) return;
    if (frame.to != null) {
      this._sendTo(frame.to, {
        t: 'channel', topic: frame.topic, payload: frame.payload, from: peerId,
      });
      return;
    }
    const key = peer.room + '::' + frame.topic;
    const subs = this.subscriptions.get(key);
    if (!subs) return;
    const out = { t: 'channel', topic: frame.topic, payload: frame.payload, from: peerId };
    for (const pid of subs) {
      if (pid !== peerId) this._sendTo(pid, out);
    }
  }

  async _handleRpc(peerId, frame) {
    const { id, method, argsJson } = frame;
    const handler = this.rpcHandlers.get(method);
    if (!handler) {
      this._sendTo(peerId, { t: 'rpcResult', id, ok: false,
                              error: 'no such method: ' + method });
      return;
    }
    let args = [];
    try { args = argsJson ? JSON.parse(argsJson) : []; } catch (_) {}
    const peer = this.peers.get(peerId);
    const ctx = { peerId, room: peer ? peer.room : null, server: this };
    try {
      const result = await Promise.resolve().then(() => handler(args, ctx));
      this._sendTo(peerId, { t: 'rpcResult', id, ok: true,
                              result: result === undefined ? null : result });
    } catch (e) {
      this._sendTo(peerId, { t: 'rpcResult', id, ok: false,
                              error: e.message || String(e) });
    }
  }

  // --- Server -> client hook invocation -----------------------------------
  callClientHook(peerId, hookName, args, opts) {
    opts = opts || {};
    const timeoutMs = opts.timeoutMs || 10000;
    const id = 'h' + (this.nextHookCallId++);
    return new Promise((resolve, reject) => {
      const peer = this.peers.get(peerId);
      if (!peer || peer.ws.readyState !== WebSocket.OPEN) {
        reject(new Error('peer not connected: ' + peerId));
        return;
      }
      const timer = setTimeout(() => {
        if (this.pendingHookCalls.has(id)) {
          this.pendingHookCalls.delete(id);
          reject(new Error('hook timeout: ' + hookName));
        }
      }, timeoutMs);
      this.pendingHookCalls.set(id, { resolve, reject, timer });
      this._sendTo(peerId, {
        t: 'hook', id, name: hookName,
        argsJson: JSON.stringify(args || []),
      });
    });
  }

  _handleHookResult(peerId, frame) {
    const p = this.pendingHookCalls.get(frame.id);
    if (!p) return;
    clearTimeout(p.timer);
    this.pendingHookCalls.delete(frame.id);
    if (frame.ok) {
      let result = null;
      try { result = frame.resultJson ? JSON.parse(frame.resultJson) : null; } catch (_) {}
      p.resolve(result);
    } else {
      p.reject(new Error(frame.error || 'hook failed'));
    }
  }

  // --- Presence broadcast -------------------------------------------------
  _broadcastPresence(roomId) {
    if (!roomId) return;
    const set = this.rooms.get(roomId);
    if (!set) return;
    const peers = [];
    for (const pid of set) {
      const p = this.peers.get(pid);
      if (!p) continue;
      peers.push({ id: pid, meta: p.meta || {} });
    }
    this._broadcastToRoom(roomId, { t: 'presence', peers });
  }

  startPresenceLoop() {
    if (this._presenceTimer) clearInterval(this._presenceTimer);
    this._presenceTimer = setInterval(() => {
      for (const roomId of this.rooms.keys()) {
        this._broadcastPresence(roomId);
      }
    }, this.cfg.presenceIntervalMs);
    if (this._presenceTimer.unref) this._presenceTimer.unref();
  }

  // --- Asset proxy --------------------------------------------------------
  handleAssetProxy(req, res, parsed) {
    const m = parsed.pathname.match(/^\/proxy\/([^/]+)\/(.+)$/);
    if (!m) {
      res.writeHead(404, { 'Content-Type': 'text/plain' });
      res.end('not found');
      return;
    }
    const secretKey = m[1];
    const restPath = m[2];
    const secret = this.cfg.secrets[secretKey];
    if (!secret) {
      res.writeHead(404, { 'Content-Type': 'text/plain' });
      res.end('no such secret: ' + secretKey);
      return;
    }
    const baseUrl = secret.baseUrl.replace(/\/$/, '');
    const targetUrl = baseUrl + '/' + restPath + (parsed.search || '');
    const headers = {};
    if (secret.header) headers[secret.header] = secret.value;
    const lib = targetUrl.startsWith('https:') ? https : http;
    const proxyReq = lib.request(targetUrl, { headers }, (proxyRes) => {
      res.writeHead(proxyRes.statusCode || 200, proxyRes.headers);
      proxyRes.pipe(res);
    });
    proxyReq.on('error', (e) => {
      res.writeHead(502, { 'Content-Type': 'text/plain' });
      res.end('proxy error: ' + e.message);
    });
    proxyReq.end();
  }

  // --- Static file server -------------------------------------------------
  serveStatic(req, res, parsed) {
    if (!this.cfg.staticDir) {
      res.writeHead(404, { 'Content-Type': 'text/plain' });
      res.end('not found');
      return;
    }
    let p = decodeURIComponent(parsed.pathname);
    if (p === '/' || p === '') p = '/index.html';
    const filePath = path.normalize(path.join(this.cfg.staticDir, p));
    if (!filePath.startsWith(path.resolve(this.cfg.staticDir))) {
      res.writeHead(403, { 'Content-Type': 'text/plain' });
      res.end('forbidden');
      return;
    }
    fs.readFile(filePath, (err, data) => {
      if (err) {
        res.writeHead(404, { 'Content-Type': 'text/plain' });
        res.end('not found');
        return;
      }
      const ext = path.extname(filePath).toLowerCase();
      const mimeTypes = {
        '.html': 'text/html', '.js': 'text/javascript',
        '.css': 'text/css', '.json': 'application/json',
        '.png': 'image/png', '.jpg': 'image/jpeg', '.jpeg': 'image/jpeg',
        '.gif': 'image/gif', '.svg': 'image/svg+xml',
        '.wasm': 'application/wasm', '.wav': 'audio/wav',
        '.mp3': 'audio/mpeg', '.ogg': 'audio/ogg',
        '.ico': 'image/x-icon', '.txt': 'text/plain',
      };
      const ct = mimeTypes[ext] || 'application/octet-stream';
      res.writeHead(200, { 'Content-Type': ct,
                           'Cache-Control': 'no-cache' });
      res.end(data);
    });
  }
}

// =========================================================================
// HTTP + WebSocket server bootstrap
// =========================================================================
function startServer(cfg) {
  // Apply defaults for fields that callers (especially tests) may omit.
  cfg = Object.assign({
    port: 8080,
    staticDir: null,
    savesDir: './saves',
    maxPlayersPerRoom: 16,
    presenceIntervalMs: 5000,
    cors: { origins: ['*'] },
    secrets: {},
  }, cfg);

  const tdServer = new TdServer(cfg);

  const httpServer = http.createServer((req, res) => {
    const corsOrigins = (cfg.cors && cfg.cors.origins) || ['*'];
    const origin = req.headers.origin;
    if (corsOrigins.includes('*') || (origin && corsOrigins.includes(origin))) {
      res.setHeader('Access-Control-Allow-Origin', origin || '*');
      res.setHeader('Access-Control-Allow-Methods', 'GET, POST, OPTIONS');
      res.setHeader('Access-Control-Allow-Headers', 'Content-Type, Authorization');
    }
    if (req.method === 'OPTIONS') {
      res.writeHead(204);
      res.end();
      return;
    }
    const parsed = url.parse(req.url, true);
    if (parsed.pathname.startsWith('/proxy/')) {
      tdServer.handleAssetProxy(req, res, parsed);
    } else {
      tdServer.serveStatic(req, res, parsed);
    }
  });

  const wss = new WebSocket.Server({ server: httpServer });
  wss.on('connection', (ws) => {
    const peerId = tdServer.addPeer(ws);
    ws.on('message', (raw) => {
      let frame;
      try { frame = JSON.parse(raw); }
      catch (e) { return; }
      tdServer._onFrame(peerId, frame);
    });
    ws.on('close', () => tdServer.removePeer(peerId));
    ws.on('error', () => tdServer.removePeer(peerId));
  });

  tdServer.startPresenceLoop();
  httpServer.listen(cfg.port, () => {
    console.log('[td_server] listening on http://localhost:' + cfg.port);
    if (cfg.staticDir) console.log('[td_server] serving static from: ' + cfg.staticDir);
    if (cfg.savesDir)  console.log('[td_server] saves stored in: ' + cfg.savesDir);
    const secretKeys = Object.keys(cfg.secrets);
    if (secretKeys.length) console.log('[td_server] proxy secrets available for: ' + secretKeys.join(', '));
  });

  return { server: tdServer, httpServer, wss };
}

// =========================================================================
// Main + exports
// =========================================================================
if (require.main === module) {
  const argv = parseArgs(process.argv);
  const cfg = loadConfig(argv);
  startServer(cfg);
} else {
  module.exports = { TdServer, startServer, parseArgs, loadConfig };
}
