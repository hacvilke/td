// =============================================================================
// TD Engine — Authoritative Server (Node.js)
// File: web/net_auth_server.js
//
// A minimal authoritative multiplayer server for TD Engine browser games.
// Runs on Node.js with the `ws` package. Provides:
//
//   - Rooms: each room is an isolated world. Players join by room id.
//   - Snapshot broadcasting: at a fixed tick rate (default 20Hz), the server
//     sends each client a snapshot of the room state. Supports delta
//     compression (only entities that changed since the client's last ack).
//   - RPC routing: clients can call registered server methods; the server
//     can call methods on specific clients (or all clients in a room).
//   - Anti-cheat hooks: gameplay code runs SERVER-SIDE. Clients only send
//     INPUTS (key presses, mouse clicks), not authoritative state. The
//     server validates inputs and applies them to the simulation.
//
// WHY NOT THE C++ TRANSPORT?
//   The C++ NetPeer in src/net/transport.h is transport-agnostic but has no
//   concrete implementation (it's a stub). For browser games, the realistic
//   deployment target is a Node.js process — it's free to host on Glitch,
//   Render, Fly.io, etc., speaks WebSocket natively, and lets game devs
//   write their server logic in JS (same language as the client).
//
// USAGE
//   // server.js
//   const { createServer } = require('./runtime/net_auth_server.js');
//   const server = createServer({ port: 8080, tickRate: 20 });
//
//   server.defineRoom('default', {
//     // Called when a player joins. Returns their initial state.
//     onPlayerJoin: (peerId, profile) => ({ x: 0, y: 0, vx: 0, vy: 0, name: profile.name }),
//
//     // Called when a player leaves. Clean up (e.g. drop their entity).
//     onPlayerLeave: (peerId, state) => {},
//
//     // Called when a player sends an input. Apply + validate it.
//     onInput: (peerId, input, state, room) => {
//       if (input.left)  state.x -= 5;
//       if (input.right) state.x += 5;
//       if (input.jump)  state.vy = -10;
//       // Anti-cheat: clamp position
//       state.x = Math.max(-1000, Math.min(1000, state.x));
//     },
//
//     // Called every tick. Update the simulation (physics, AI, etc.).
//     onTick: (dt, players, room) => {
//       for (const [id, p] of Object.entries(players)) {
//         p.y += p.vy;
//         p.vy += 0.5;     // gravity
//         if (p.y > 0) p.y = 0, p.vy = 0;  // floor
//       }
//     },
//
//     // RPC methods: clients can call these via TDNet.RPC.callRemote('spawn', {...})
//     rpc: {
//       spawn: (peerId, args, room) => { /* return result */ return { ok: true }; },
//     },
//   });
//
//   server.start();
//
// CLIENT (browser)
//   const sock = TDNet.Socket.connect('ws://localhost:8080/room/default');
//   sock.send(JSON.stringify({ t: 'input', input: { left: true } }));
//   sock.onMessage = (data) => {
//     const msg = JSON.parse(data);
//     if (msg.t === 'snapshot') {
//       interp.pushSnapshot(msg.snapshot);
//     }
//   };
// =============================================================================

'use strict';

// ---------------------------------------------------------------------------
// Wire protocol (JSON, all messages tagged with `t`)
//
// Client -> Server:
//   { t: 'hello', profile: { name: 'Alice' } }       // first message
//   { t: 'input', input: {...} }                       // gameplay input
//   { t: 'rpc', id: 123, m: 'spawn', a: [...] }        // RPC call
//   { t: 'ack', seq: 42 }                              // snapshot ack
//
// Server -> Client:
//   { t: 'welcome', peerId: 7, roomId: 'default', tickRate: 20 }
//   { t: 'snapshot', snapshot: { seq, time, entities, base, removed } }
//   { t: 'rpc', id: 123, r: result } | { t: 'rpc', id: 123, e: 'error' }
//   { t: 'rpc', m: 'method', a: [...] }                // server -> client call
//   { t: 'kick', reason: '...' }
// ---------------------------------------------------------------------------

const DEFAULT_PORT = 8080;
const DEFAULT_TICK_RATE = 20;
const MAX_PEERS_PER_ROOM = 64;
const SNAPSHOT_FULL_EVERY = 10;   // send full snapshot every 10 ticks (1/2 sec at 20Hz)

function createServer(opts) {
  opts = opts || {};
  const port = opts.port || DEFAULT_PORT;
  const tickRate = opts.tickRate || DEFAULT_TICK_RATE;
  const tickMs = 1000 / tickRate;

  let WebSocketServer = null;
  try { WebSocketServer = require('ws').WebSocketServer; }
  catch (e) {
    throw new Error('net_auth_server requires the "ws" npm package. Install with: npm install ws');
  }

  const roomDefs = new Map();    // roomId -> room definition (handlers)
  const rooms = new Map();       // roomId -> Room instance
  let nextPeerId = 1;
  let wss = null;
  let tickTimer = null;
  let started = false;

  // -------------------------------------------------------------------------
  // Room class
  // -------------------------------------------------------------------------

  class Room {
    constructor(id, def) {
      this.id = id;
      this.def = def;
      this.players = new Map();        // peerId -> { ws, state, lastAckSeq, lastSentFullSeq }
      this.seq = 0;
      this.lastTickTime = Date.now();
      this.inputs = [];                // queued inputs to process next tick
    }

    addPlayer(peerId, ws, profile) {
      if (this.players.size >= MAX_PEERS_PER_ROOM) {
        ws.send(JSON.stringify({ t: 'kick', reason: 'Room is full' }));
        ws.close();
        return false;
      }
      const state = this.def.onPlayerJoin
        ? this.def.onPlayerJoin(peerId, profile || {})
        : { x: 0, y: 0 };
      this.players.set(peerId, {
        ws, state,
        lastAckSeq: -1,
        lastSentFullSeq: -1,
        joinedAt: Date.now(),
      });
      ws.send(JSON.stringify({
        t: 'welcome',
        peerId,
        roomId: this.id,
        tickRate,
      }));
      return true;
    }

    removePlayer(peerId) {
      const p = this.players.get(peerId);
      if (!p) return;
      if (this.def.onPlayerLeave) {
        try { this.def.onPlayerLeave(peerId, p.state, this); } catch (e) { logErr(e); }
      }
      this.players.delete(peerId);
    }

    handleInput(peerId, input) {
      const p = this.players.get(peerId);
      if (!p) return;
      this.inputs.push({ peerId, input });
    }

    handleRpc(peerId, id, method, args) {
      const p = this.players.get(peerId);
      if (!p) return;
      const handler = this.def.rpc && this.def.rpc[method];
      if (!handler) {
        p.ws.send(JSON.stringify({ t: 'rpc', id, e: `Unknown method: ${method}` }));
        return;
      }
      try {
        const result = handler(peerId, args || [], this);
        if (result && typeof result.then === 'function') {
          result.then(
            (r) => p.ws.send(JSON.stringify({ t: 'rpc', id, r })),
            (err) => p.ws.send(JSON.stringify({ t: 'rpc', id, e: String(err.message || err) }))
          );
        } else {
          p.ws.send(JSON.stringify({ t: 'rpc', id, r: result }));
        }
      } catch (e) {
        p.ws.send(JSON.stringify({ t: 'rpc', id, e: String(e.message || e) }));
      }
    }

    callClient(peerId, method, args) {
      const p = this.players.get(peerId);
      if (!p) return;
      p.ws.send(JSON.stringify({ t: 'rpc', m: method, a: args || [] }));
    }

    broadcastRpc(method, args, exceptPeer) {
      const msg = JSON.stringify({ t: 'rpc', m: method, a: args || [] });
      for (const [peerId, p] of this.players) {
        if (peerId === exceptPeer) continue;
        p.ws.send(msg);
      }
    }

    ack(peerId, seq) {
      const p = this.players.get(peerId);
      if (p) p.lastAckSeq = seq;
    }

    tick() {
      const now = Date.now();
      const dt = (now - this.lastTickTime) / 1000;
      this.lastTickTime = now;

      // Process queued inputs.
      for (const { peerId, input } of this.inputs) {
        const p = this.players.get(peerId);
        if (p && this.def.onInput) {
          try { this.def.onInput(peerId, input, p.state, this); } catch (e) { logErr(e); }
        }
      }
      this.inputs.length = 0;

      // Run simulation tick.
      if (this.def.onTick) {
        const playersObj = {};
        for (const [id, p] of this.players) playersObj[id] = p.state;
        try { this.def.onTick(dt, playersObj, this); } catch (e) { logErr(e); }
        // Sync back any mutations (onTick may have written into playersObj).
        for (const [id, st] of Object.entries(playersObj)) {
          const p = this.players.get(parseInt(id, 10));
          if (p) p.state = st;
        }
      }

      // Build + send snapshots.
      this.seq++;
      const fullSnapshot = this.buildFullSnapshot(now);
      for (const [peerId, p] of this.players) {
        const isFullTick = (this.seq - p.lastSentFullSeq) >= SNAPSHOT_FULL_EVERY
                          || p.lastSentFullSeq < 0
                          || p.lastAckSeq < 0
                          || p.lastAckSeq < (this.seq - 30);
        if (isFullTick) {
          p.ws.send(JSON.stringify({
            t: 'snapshot',
            snapshot: { seq: this.seq, time: now, entities: fullSnapshot },
          }));
          p.lastSentFullSeq = this.seq;
        } else {
          const delta = this.buildDelta(p.lastAckSeq, fullSnapshot);
          p.ws.send(JSON.stringify({
            t: 'snapshot',
            snapshot: Object.assign({ seq: this.seq, time: now }, delta),
          }));
        }
      }
    }

    buildFullSnapshot(now) {
      const entities = {};
      for (const [id, p] of this.players) {
        entities[id] = p.state;
      }
      return entities;
    }

    buildDelta(baseSeq, currentEntities) {
      // Find the full snapshot at baseSeq. We need to keep a small history
      // for delta computation. Store last N full snapshots.
      if (!this._history) this._history = [];
      // Drop history older than 1 second.
      this._history = this._history.filter((h) => h.seq > this.seq - tickRate * 2);
      const base = this._history.find((h) => h.seq === baseSeq);
      if (!base) {
        // No base available — send full.
        return { entities: currentEntities };
      }
      const delta = {};
      const removed = [];
      for (const [id, cur] of Object.entries(currentEntities)) {
        const old = base.entities[id];
        if (!old) {
          delta[id] = cur;   // new entity
        } else {
          const fields = diffEntity(old, cur);
          if (fields && Object.keys(fields).length > 0) delta[id] = fields;
        }
      }
      for (const [id] of Object.entries(base.entities)) {
        if (!currentEntities[id]) removed.push(parseInt(id, 10));
      }
      // Push current snapshot to history.
      this._history.push({ seq: this.seq, entities: currentEntities });
      return { base: baseSeq, entities: delta, removed };
    }
  }

  function diffEntity(a, b) {
    const out = {};
    for (const [k, v] of Object.entries(b)) {
      if (a[k] !== v) out[k] = v;
    }
    return out;
  }

  function logErr(e) {
    process.stderr.write(`[td:auth-server] ${e && e.stack ? e.stack : e}\n`);
  }

  // -------------------------------------------------------------------------
  // Public API
  // -------------------------------------------------------------------------

  function defineRoom(roomId, def) {
    roomDefs.set(roomId, def);
    if (!rooms.has(roomId)) {
      rooms.set(roomId, new Room(roomId, def));
    }
  }

  function getRoom(roomId) {
    return rooms.get(roomId);
  }

  function start() {
    if (started) {
      logErr('Server already started.');
      return;
    }
    started = true;
    wss = new WebSocketServer({ port });
    wss.on('connection', (ws, req) => {
      // Parse room id from URL: ws://host/room/<roomId>
      const parsed = new URL(req.url, 'http://localhost');
      const m = (parsed.pathname || '').match(/^\/room\/(.+)$/);
      const roomId = m ? decodeURIComponent(m[1]) : 'default';

      let room = rooms.get(roomId);
      if (!room && roomDefs.has(roomId)) {
        room = new Room(roomId, roomDefs.get(roomId));
        rooms.set(roomId, room);
      }
      if (!room) {
        ws.send(JSON.stringify({ t: 'kick', reason: `Unknown room: ${roomId}` }));
        ws.close();
        return;
      }

      const peerId = nextPeerId++;
      let welcomed = false;

      ws.on('message', (raw) => {
        let msg;
        try { msg = JSON.parse(raw.toString()); } catch {
          ws.send(JSON.stringify({ t: 'kick', reason: 'Invalid JSON' }));
          ws.close();
          return;
        }
        switch (msg.t) {
          case 'hello':
            if (welcomed) return;
            welcomed = room.addPlayer(peerId, ws, msg.profile || {});
            break;
          case 'input':
            if (welcomed) room.handleInput(peerId, msg.input);
            break;
          case 'rpc':
            if (welcomed) room.handleRpc(peerId, msg.id, msg.m, msg.a);
            break;
          case 'ack':
            if (welcomed) room.ack(peerId, msg.seq);
            break;
          default:
            // Unknown message type — ignore (forward compatibility).
            break;
        }
      });

      ws.on('close', () => {
        if (welcomed) room.removePlayer(peerId);
      });

      ws.on('error', (e) => logErr(e));
    });

    // Tick loop.
    tickTimer = setInterval(() => {
      for (const room of rooms.values()) {
        try { room.tick(); } catch (e) { logErr(e); }
      }
    }, tickMs);

    process.stderr.write(`[td:auth-server] Listening on ws://localhost:${port}\n`);
    process.stderr.write(`[td:auth-server] Tick rate: ${tickRate}Hz (every ${tickMs}ms)\n`);
    process.stderr.write(`[td:auth-server] Rooms: ${Array.from(roomDefs.keys()).join(', ') || '(none defined)'}\n`);
  }

  function stop() {
    if (!started) return;
    started = false;
    if (tickTimer) clearInterval(tickTimer);
    if (wss) wss.close();
    process.stderr.write('[td:auth-server] Stopped.\n');
  }

  return {
    defineRoom,
    getRoom,
    start,
    stop,
    get rooms() { return rooms; },
    get tickRate() { return tickRate; },
  };
}

// ---------------------------------------------------------------------------
// Exports
// ---------------------------------------------------------------------------

if (typeof module !== 'undefined' && module.exports) {
  module.exports = { createServer };
}
if (typeof global !== 'undefined') {
  global.TDNet = global.TDNet || {};
  global.TDNet.AuthServer = { createServer };
}
