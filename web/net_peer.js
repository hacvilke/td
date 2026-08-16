// =============================================================================
// TD Engine — Peer-to-Peer Transport (BroadcastChannel-backed)
// File: web/net_peer.js
//
// Real multiplayer for the web — no server required. Uses the browser's
// BroadcastChannel API to broadcast typed messages between tabs/windows
// on the same origin. Every tab is a peer; the channel is the transport.
//
// Why this exists alongside TDNet.Socket:
//   TDNet.Socket connects to a WebSocket server (requires hosting).
//   TDNet.Peer connects tabs on the same browser (zero setup).
//   Both expose the same API shape (send/onMessage/onPeerJoin/onPeerLeave)
//   so game code is transport-agnostic. Swap TDNet.Peer for TDNet.Socket
//   when you outgrow single-browser play.
//
// Public API:
//
//   TDNet.Peer.join(channelName, opts?)
//      Join (or create) a peer channel. Returns a Peer instance.
//        opts.peerId   — override the auto-generated peer ID
//        opts.onJoin   — function(peerId) called when a new peer joins
//        opts.onLeave  — function(peerId) called when a peer leaves
//        opts.onMessage — function(peerId, data) called for every message
//        opts.onState  — function(state) connection state changes
//      The Peer auto-broadcasts a 'hello' on join and responds to hellos
//      from other peers so everyone discovers each other within ~100ms.
//
//   peer.send(data)
//      Broadcast to all peers. data is any JSON-able value (objects,
//      arrays, numbers, strings). Returns true if queued, false if
//      no peers are connected.
//
//   peer.sendTo(peerId, data)
//      Send to a specific peer. Returns true if peer is known.
//
//   peer.peers()
//      Returns array of known peer IDs (excluding self).
//
//   peer.rtt()
//      Returns the last measured round-trip time to the slowest known
//      peer, in milliseconds. 0 if no peers or no samples yet.
//
//   peer.leave()
//      Broadcast a 'bye' message and close the channel.
//
//   peer.on(event, cb)
//      Add a listener. Events: 'join', 'leave', 'message', 'state'.
//
// Wire format (JSON over BroadcastChannel):
//   { "t": "hello",  "id": "<peerId>" }                        — announcement
//   { "t": "helloAck","id": "<peerId>", "to": "<otherPeerId>" } — directed ack
//   { "t": "bye",    "id": "<peerId>" }                        — leaving
//   { "t": "ping",   "id": "<peerId>", "ts": <ms> }            — RTT probe
//   { "t": "pong",   "id": "<peerId>", "ts": <ms> }            — RTT response
//   { "t": "data",   "id": "<peerId>", "to": "<peerId|*>", "d": <payload> }
//
// Strictly additive: if BroadcastChannel isn't available (older browsers),
// join() throws. Falls back gracefully — games can feature-detect:
//   if (TDNet.Peer.isSupported()) { ... } else { ... single-player ... }
// =============================================================================

(function (global) {
  'use strict';

  function isSupported() {
    try {
      return typeof BroadcastChannel !== 'undefined';
    } catch (e) { return false; }
  }

  function generatePeerId() {
    return 'p_' + Math.random().toString(36).slice(2, 10) + '_' + Date.now().toString(36);
  }

  function join(channelName, opts) {
    if (!isSupported()) {
      throw new Error('TDNet.Peer: BroadcastChannel not available in this browser');
    }
    if (typeof channelName !== 'string' || !channelName) {
      throw new Error('TDNet.Peer: channelName required');
    }
    opts = opts || {};
    return new Peer(channelName, opts);
  }

  function Peer(channelName, opts) {
    const self = this;
    this.channelName = channelName;
    this.peerId = opts.peerId || generatePeerId();
    this._opts = opts;
    this._bc = new BroadcastChannel('td-peer:' + channelName);
    this._peers = new Map();          // peerId -> { lastSeen, rttMs }
    this._listeners = { join: [], leave: [], message: [], state: [] };
    this._state = 'connecting';
    this._rttTimer = null;
    this._rttIntervalMs = 1000;       // probe every 1s
    this._staleMs = 5000;             // peer considered gone after 5s silence

    // Wire callbacks from opts into the listener arrays
    if (typeof opts.onJoin === 'function') this._listeners.join.push(opts.onJoin);
    if (typeof opts.onLeave === 'function') this._listeners.leave.push(opts.onLeave);
    if (typeof opts.onMessage === 'function') this._listeners.message.push(opts.onMessage);
    if (typeof opts.onState === 'function') this._listeners.state.push(opts.onState);

    this._bc.onmessage = function (ev) { self._onRaw(ev.data); };

    // Announce ourselves. Existing peers will reply with helloAck so we
    // discover them; we'll also receive their future hellos.
    this._broadcast({ t: 'hello', id: this.peerId });

    // Start RTT probes + stale-peer sweep
    this._rttTimer = setInterval(function () {
      self._probeRtt();
      self._sweepStale();
    }, this._rttIntervalMs);

    // Initial state — we're "connected" once we send hello. Real peer
    // count populates as helloAcks arrive.
    this._setState('connected');
  }

  Peer.prototype._setState = function (s) {
    if (this._state === s) return;
    this._state = s;
    this._emit('state', s);
  };

  Peer.prototype._emit = function (event, payload) {
    const arr = this._listeners[event];
    if (!arr) return;
    for (let i = 0; i < arr.length; i++) {
      try { arr[i](payload); } catch (e) { /* swallow */ }
    }
  };

  Peer.prototype._broadcast = function (msg) {
    try { this._bc.postMessage(msg); } catch (e) { /* swallow */ }
  };

  Peer.prototype._onRaw = function (msg) {
    if (!msg || typeof msg !== 'object' || typeof msg.t !== 'string') return;
    if (msg.id === this.peerId) return;  // ignore our own echoes

    switch (msg.t) {
      case 'hello':
        // A new peer announced. Record them + reply with helloAck so they
        // learn about us. Don't emit 'join' yet — wait for helloAck so
        // both sides agree the peer is real.
        this._peers.set(msg.id, { lastSeen: Date.now(), rttMs: 0 });
        this._broadcast({ t: 'helloAck', id: this.peerId, to: msg.id });
        // We DO emit join here — the peer is real, they just announced.
        this._emit('join', msg.id);
        break;

      case 'helloAck':
        // Only act if this ack is for us.
        if (msg.to !== this.peerId) return;
        if (!this._peers.has(msg.id)) {
          this._peers.set(msg.id, { lastSeen: Date.now(), rttMs: 0 });
          this._emit('join', msg.id);
        } else {
          this._peers.get(msg.id).lastSeen = Date.now();
        }
        break;

      case 'bye':
        if (this._peers.has(msg.id)) {
          this._peers.delete(msg.id);
          this._emit('leave', msg.id);
        }
        break;

      case 'ping':
        // Reply with a DIRECTED pong (to: msg.id) so only the pinger
        // processes it. Broadcasting pong to all peers would corrupt their
        // RTT measurements with someone else's send timestamp.
        this._broadcast({ t: 'pong', id: this.peerId, to: msg.id, ts: msg.ts });
        // Also mark them as seen (and create the peer entry if this is the
        // first we've heard of them — they may not have sent hello yet).
        if (!this._peers.has(msg.id)) {
          this._peers.set(msg.id, { lastSeen: Date.now(), rttMs: 0 });
        } else {
          this._peers.get(msg.id).lastSeen = Date.now();
        }
        break;

      case 'pong':
        // Only process pongs directed at us (the `to` field is set by the
        // pinger's reply; if it's missing or doesn't match, ignore — this
        // pong was meant for someone else and would corrupt our RTT).
        if (msg.to !== undefined && msg.to !== this.peerId) return;
        if (this._peers.has(msg.id)) {
          const peer = this._peers.get(msg.id);
          peer.lastSeen = Date.now();
          if (typeof msg.ts === 'number') {
            peer.rttMs = Date.now() - msg.ts;
          }
        }
        break;

      case 'data':
        // Directed (to === our ID) or broadcast (to === '*').
        if (msg.to === this.peerId || msg.to === '*') {
          if (this._peers.has(msg.id)) this._peers.get(msg.id).lastSeen = Date.now();
          this._emit('message', { peerId: msg.id, data: msg.d });
        }
        break;
    }
  };

  Peer.prototype._probeRtt = function () {
    if (this._peers.size === 0) return;
    this._broadcast({ t: 'ping', id: this.peerId, ts: Date.now() });
  };

  Peer.prototype._sweepStale = function () {
    const now = Date.now();
    const gone = [];
    this._peers.forEach(function (info, id) {
      if (now - info.lastSeen > this._staleMs) gone.push(id);
    }, this);
    gone.forEach(function (id) {
      this._peers.delete(id);
      this._emit('leave', id);
    }, this);
  };

  // ---- public methods -------------------------------------------------------

  Peer.prototype.send = function (data) {
    if (this._peers.size === 0) return false;
    this._broadcast({ t: 'data', id: this.peerId, to: '*', d: data });
    return true;
  };

  Peer.prototype.sendTo = function (peerId, data) {
    if (!this._peers.has(peerId)) return false;
    this._broadcast({ t: 'data', id: this.peerId, to: peerId, d: data });
    return true;
  };

  Peer.prototype.peers = function () {
    return Array.from(this._peers.keys());
  };

  Peer.prototype.rtt = function () {
    // Report the slowest peer's RTT — that's the effective network latency
    // for broadcast messaging. 0 if no peers.
    let max = 0;
    this._peers.forEach(function (info) {
      if (info.rttMs > max) max = info.rttMs;
    });
    return max;
  };

  Peer.prototype.on = function (event, cb) {
    if (this._listeners[event] && typeof cb === 'function') {
      this._listeners[event].push(cb);
    }
    return this;
  };

  Peer.prototype.leave = function () {
    if (this._rttTimer) { clearInterval(this._rttTimer); this._rttTimer = null; }
    this._broadcast({ t: 'bye', id: this.peerId });
    try { this._bc.close(); } catch (e) {}
    this._peers.clear();
    this._setState('disconnected');
  };

  // ---- module export --------------------------------------------------------

  const PeerModule = {
    join: join,
    isSupported: isSupported,
    Peer: Peer,
    version: '1.0.0',
  };

  // Attach to TDNet namespace if it exists, otherwise standalone
  if (global.TDNet) {
    global.TDNet.Peer = PeerModule;
  } else {
    global.TDNet = { Peer: PeerModule };
  }

})(typeof window !== 'undefined' ? window : this);
