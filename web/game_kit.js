// =============================================================================
// TD Engine — Game Kit (v1.0)
// =============================================================================
// game_kit.js exposes four web-dev-oriented namespaces that broaden the
// engine's public API past the in-engine ECS / rendering / networking core:
//
//   TDAssets   — Free asset pipeline. Fetch textures / audio / JSON / text
//                from any URL (your own CDN, GitHub raw, Openverse, Freesound,
//                Unsplash, etc.) and feed the bytes into the engine with
//                one call. Built-in caching, retry, and decoding helpers.
//
//   TDCDN      — Multi-origin CDN routing with failover. Define a list of
//                CDN prefixes for your game's assets; the engine resolves
//                each logical path against them in order until one returns
//                200. Same API in dev (local files) and prod (CDN).
//
//   TDRest     — REST helper. Wraps fetch() with retry, timeout, auth header
//                injection, and JSON parsing. Use it for any HTTP API call
//                your game makes (the PUBLIC_APIS.md survey lists 25+ free,
//                CORS-enabled APIs that work directly from the browser).
//
//   TDServer   — Flexible client-server communication hooks. Wraps the
//                existing TDNet.Socket transport with:
//                  * typed channels (pub/sub per topic)
//                  * RPC with timeouts (built on TDNet.Socket.rpc)
//                  * SSE / long-poll fallback for read-only streams
//                  * presence (who's online in this room)
//                  * save data sync (push local saves to the server)
//
// All four namespaces are pure JS, IIFE-wrapped, with no external deps.
// They are loaded additively — failure to load one does not break the
// engine boot. Each exposes a .snapshot() for headless tests.
//
// Dependencies:
//   - TDNet.Socket (for TDServer's transport; falls back to no-op if missing)
//   - TDEngine.bridge.wasmExports (for TDAssets.loadTexture, optional)
//   - fetch() (browser-native; in Node 18+ this is global)
//
// Loading order in web/index.html:
//   ... net_websocket.js → game_kit.js → ...
// =============================================================================

(function (global) {
  'use strict';

  // -------------------------------------------------------------------------
  // Helpers shared across namespaces
  // -------------------------------------------------------------------------
  const LOG_PREFIX = '[td-game-kit]';
  function logInfo()  { try { (console.info  || console.log).apply(console, [LOG_PREFIX].concat([].slice.call(arguments))); } catch (_) {} }
  function logWarn()  { try { (console.warn  || console.log).apply(console, [LOG_PREFIX].concat([].slice.call(arguments))); } catch (_) {} }
  function logError() { try { (console.error || console.log).apply(console, [LOG_PREFIX].concat([].slice.call(arguments))); } catch (_) {} }

  // In-memory cache (with TTL) shared by TDAssets and TDRest.
  // Stored under a single key namespace so .snapshot() can see everything.
  const _cache = new Map(); // key -> { value, expiresAt, sizeBytes }
  function cacheGet(key) {
    const entry = _cache.get(key);
    if (!entry) return undefined;
    if (entry.expiresAt !== 0 && Date.now() > entry.expiresAt) {
      _cache.delete(key);
      return undefined;
    }
    return entry.value;
  }
  function cacheSet(key, value, ttlMs, sizeBytes) {
    _cache.set(key, {
      value,
      expiresAt: ttlMs > 0 ? Date.now() + ttlMs : 0,
      sizeBytes: sizeBytes || 0,
    });
  }
  function cacheClear() {
    const count = _cache.size;
    _cache.clear();
    return count;
  }
  function cacheSize() {
    let total = 0;
    for (const e of _cache.values()) total += e.sizeBytes || 0;
    return total;
  }

  // fetch() with timeout + retry. Returns the Response object.
  // `opts.timeoutMs` (default 15000), `opts.retries` (default 2),
  // `opts.headers` (object), `opts.method` (default 'GET'),
  // `opts.body` (string / Blob / ArrayBuffer).
  async function fetchWithRetry(url, opts) {
    opts = opts || {};
    const timeoutMs = opts.timeoutMs || 15000;
    const retries   = opts.retries != null ? opts.retries : 2;
    const method    = opts.method  || 'GET';
    const headers   = opts.headers || {};
    const body      = opts.body;

    let lastErr = null;
    for (let attempt = 0; attempt <= retries; attempt++) {
      const controller = (typeof AbortController !== 'undefined')
        ? new AbortController() : null;
      const timer = controller
        ? setTimeout(() => { try { controller.abort(); } catch (_) {} }, timeoutMs)
        : null;

      try {
        const fetchOpts = { method, headers, signal: controller ? controller.signal : undefined };
        if (body != null) fetchOpts.body = body;
        const res = await fetch(url, fetchOpts);
        if (timer) clearTimeout(timer);
        // Retry on 5xx; 4xx are caller errors, not retryable.
        if (res.status >= 500 && attempt < retries) {
          logWarn('fetch', method, url, '->', res.status, 'retry', attempt + 1);
          await new Promise(r => setTimeout(r, 300 * (attempt + 1)));
          continue;
        }
        return res;
      } catch (e) {
        if (timer) clearTimeout(timer);
        lastErr = e;
        if (attempt < retries) {
          logWarn('fetch', method, url, 'threw:', e.message, 'retry', attempt + 1);
          await new Promise(r => setTimeout(r, 300 * (attempt + 1)));
          continue;
        }
        throw e;
      }
    }
    // Unreachable.
    throw lastErr || new Error('fetch failed: ' + url);
  }

  // =========================================================================
  // TDAssets — free asset pipeline
  // =========================================================================
  const TDAssets = (function () {
    // In-memory + localStorage-backed asset cache. Texture bytes live in
    // WASM memory once uploaded; we cache only the *decoded* JS-side handles
    // (texId, audioBuffer, parsed JSON) keyed by URL.
    const TEX_CACHE_KEY_PREFIX = 'tda:tex:';
    const JSON_CACHE_KEY_PREFIX = 'tda:json:';

    // Fetch a URL as an ArrayBuffer with caching + retry.
    async function fetchBytes(url, opts) {
      opts = opts || {};
      const cacheKey = 'bytes:' + url;
      if (opts.cache !== false) {
        const cached = cacheGet(cacheKey);
        if (cached) return cached;
      }
      const res = await fetchWithRetry(url, opts);
      if (!res.ok) throw new Error('TDAssets.fetchBytes ' + url + ' -> ' + res.status);
      const buf = await res.arrayBuffer();
      if (opts.cache !== false) {
        cacheSet(cacheKey, buf, opts.ttlMs || 5 * 60 * 1000, buf.byteLength);
      }
      return buf;
    }

    // Fetch a URL as text with caching + retry.
    async function fetchText(url, opts) {
      opts = opts || {};
      const cacheKey = 'text:' + url;
      if (opts.cache !== false) {
        const cached = cacheGet(cacheKey);
        if (cached) return cached;
      }
      const res = await fetchWithRetry(url, opts);
      if (!res.ok) throw new Error('TDAssets.fetchText ' + url + ' -> ' + res.status);
      const txt = await res.text();
      if (opts.cache !== false) {
        cacheSet(cacheKey, txt, opts.ttlMs || 5 * 60 * 1000, txt.length);
      }
      return txt;
    }

    // Fetch + parse JSON with caching.
    async function fetchJson(url, opts) {
      opts = opts || {};
      const cacheKey = JSON_CACHE_KEY_PREFIX + url;
      if (opts.cache !== false) {
        const cached = cacheGet(cacheKey);
        if (cached) return cached;
      }
      const txt = await fetchText(url, opts);
      let parsed;
      try { parsed = JSON.parse(txt); }
      catch (e) { throw new Error('TDAssets.fetchJson ' + url + ': invalid JSON (' + e.message + ')'); }
      if (opts.cache !== false) {
        cacheSet(cacheKey, parsed, opts.ttlMs || 5 * 60 * 1000, txt.length);
      }
      return parsed;
    }

    // Decode image bytes into an ImageBitmap. Uses createImageBitmap when
    // available (works in browsers + Node 18+ with the right shim); falls
    // back to an <img> element + canvas when not.
    async function decodeImage(bytes, opts) {
      opts = opts || {};
      const blob = new Blob([bytes], { type: opts.mime || 'image/png' });
      if (typeof createImageBitmap === 'function') {
        return await createImageBitmap(blob);
      }
      // Fallback path (older browsers, or environments without ImageBitmap).
      return await new Promise((resolve, reject) => {
        const url = URL.createObjectURL(blob);
        const img = new Image();
        img.onload = () => { URL.revokeObjectURL(url); resolve(img); };
        img.onerror = (e) => { URL.revokeObjectURL(url); reject(new Error('image decode failed')); };
        img.src = url;
      });
    }

    // Decode audio bytes into an AudioBuffer using Web Audio's
    // decodeAudioData. Requires an AudioContext (creates one on first call).
    let _audioCtx = null;
    function getAudioContext() {
      if (_audioCtx) return _audioCtx;
      const Ctor = (typeof AudioContext !== 'undefined') ? AudioContext
                 : (typeof webkitAudioContext !== 'undefined') ? webkitAudioContext
                 : null;
      if (!Ctor) throw new Error('Web Audio API not available in this environment');
      _audioCtx = new Ctor();
      return _audioCtx;
    }
    async function decodeAudio(bytes) {
      const ctx = getAudioContext();
      // decodeAudioData copies the ArrayBuffer; pass a slice to be safe.
      return await ctx.decodeAudioData(bytes.slice(0));
    }

    // Upload decoded image pixels into the engine as a Texture. Returns
    // the texture ID (a number) that you can pass to TDEngine.ecs.setSprite
    // or any other texture-consuming API. Requires the engine to be
    // initialized (TDEngine.bridge.wasmExports available).
    function uploadTexture(bitmap) {
      const Module = global.TDEngine && global.TDEngine.bridge && global.TDEngine.bridge.wasmExports;
      if (!Module) throw new Error('TDAssets.uploadTexture: TDEngine not initialized');
      const w = bitmap.width, h = bitmap.height;
      // Use OffscreenCanvas if available; otherwise a regular canvas.
      let canvas;
      if (typeof OffscreenCanvas !== 'undefined') {
        canvas = new OffscreenCanvas(w, h);
      } else if (typeof document !== 'undefined') {
        canvas = document.createElement('canvas');
        canvas.width = w; canvas.height = h;
      } else {
        throw new Error('TDAssets.uploadTexture: no canvas available for pixel extraction');
      }
      const ctx = canvas.getContext('2d');
      ctx.drawImage(bitmap, 0, 0);
      const { data } = ctx.getImageData(0, 0, w, h);
      const ptr = Module._malloc(data.length);
      try {
        Module.HEAPU8.set(data, ptr);
        // td_create_texture(width, height, channels, rgbaPtr)
        const fn = Module.cwrap
          ? Module.cwrap('td_create_texture', 'number', ['number','number','number','number'])
          : Module._td_create_texture;
        if (typeof fn !== 'function') {
          throw new Error('td_create_texture is not exported by the engine build');
        }
        // Call either via cwrap wrapper or direct function.
        const texId = (fn.call ? fn.call(null, w, h, 4, ptr) : fn(w, h, 4, ptr));
        return texId;
      } finally {
        Module._free(ptr);
      }
    }

    // --- Public surface ---------------------------------------------------
    return {
      // Fetch + cache raw bytes.
      fetchBytes,
      // Fetch + cache text.
      fetchText,
      // Fetch + cache + parse JSON.
      fetchJson,

      // Decode helpers (no engine dependency — pure browser).
      decodeImage,
      decodeAudio,

      // Upload a decoded ImageBitmap into the engine as a Texture.
      // Returns the texture ID.
      uploadTexture,

      // One-shot convenience: fetch URL → decode → upload → return texId.
      // Use this for "load this image from the web as a sprite".
      async loadTexture(url, opts) {
        opts = opts || {};
        const cacheKey = TEX_CACHE_KEY_PREFIX + url;
        const cached = cacheGet(cacheKey);
        if (cached != null) return cached;
        const bytes = await fetchBytes(url, opts);
        const bmp = await decodeImage(bytes, opts);
        const texId = uploadTexture(bmp);
        cacheSet(cacheKey, texId, opts.ttlMs || 5 * 60 * 1000, bytes.byteLength);
        return texId;
      },

      // One-shot: fetch URL → decode → return AudioBuffer.
      // The buffer is ready to feed to TDEngine.audio or any Web Audio source.
      async loadAudio(url, opts) {
        opts = opts || {};
        const cacheKey = 'tda:audio:' + url;
        const cached = cacheGet(cacheKey);
        if (cached) return cached;
        const bytes = await fetchBytes(url, opts);
        const buf = await decodeAudio(bytes);
        cacheSet(cacheKey, buf, opts.ttlMs || 5 * 60 * 1000, bytes.byteLength);
        return buf;
      },

      // Get the AudioContext (creating one if needed). Useful for resuming
      // audio on first user gesture.
      getAudioContext,

      // Cache management.
      cacheGet, cacheSet, cacheClear,
      cacheSize,
      cacheCount() { return _cache.size; },
    };
  })();

  // =========================================================================
  // TDCDN — multi-origin CDN routing with failover
  // =========================================================================
  const TDCDN = (function () {
    // _origins is a list of {prefix, weight, healthy, lastFailMs}.
    // `prefix` is a URL prefix like 'https://cdn.example.com/mygame/v1/'.
    // `weight` (default 1) lets you prefer one CDN over another.
    // `healthy` is a runtime flag; set false on network error, restored by
    //   a background sweep (every 30s).
    const _origins = [];
    let _sweepTimer = null;

    function ensureSweep() {
      if (_sweepTimer || typeof setInterval !== 'function') return;
      _sweepTimer = setInterval(() => {
        const now = Date.now();
        for (const o of _origins) {
          if (!o.healthy && (now - o.lastFailMs) > 30000) {
            o.healthy = true; // try again
          }
        }
      }, 30000);
      if (_sweepTimer.unref) _sweepTimer.unref();
    }

    // Add a CDN origin. Returns the index (for removal).
    function addOrigin(prefix, opts) {
      if (!prefix || typeof prefix !== 'string') {
        throw new Error('TDCDN.addOrigin: prefix must be a non-empty string');
      }
      // Normalize: ensure trailing slash so we can concatenate paths.
      if (prefix[prefix.length - 1] !== '/') prefix += '/';
      opts = opts || {};
      const entry = {
        prefix,
        weight: opts.weight != null ? opts.weight : 1,
        healthy: true,
        lastFailMs: 0,
        label: opts.label || prefix,
      };
      _origins.push(entry);
      ensureSweep();
      return _origins.length - 1;
    }

    function removeOrigin(index) {
      if (index < 0 || index >= _origins.length) return false;
      _origins.splice(index, 1);
      return true;
    }

    function clearOrigins() { _origins.length = 0; }
    function listOrigins() {
      return _origins.map(o => ({ prefix: o.prefix, weight: o.weight,
                                  healthy: o.healthy, label: o.label }));
    }

    // Resolve a logical path against the origin list, trying each healthy
    // origin in weight order. Returns the first URL that responds 200 (or
    // 2xx/3xx). Throws if all origins fail.
    // `path` is treated as relative — leading '/' is stripped.
    // `opts.checkHead` (default true): do a HEAD request first to verify the
    //   origin serves the file. If false, just return the first healthy URL
    //   without verifying.
    async function resolve(path, opts) {
      opts = opts || {};
      const checkHead = opts.checkHead !== false;
      if (!path) throw new Error('TDCDN.resolve: path is required');
      const rel = path.charAt(0) === '/' ? path.slice(1) : path;

      // Sort by weight descending; among equal weights, keep insertion order.
      const order = _origins.map((o, i) => ({ o, i }))
                            .filter(x => x.o.healthy)
                            .sort((a, b) => (b.o.weight - a.o.weight) || (a.i - b.i));

      if (order.length === 0) {
        throw new Error('TDCDN.resolve: no healthy origins configured');
      }

      // If checkHead is false, just return the first URL.
      if (!checkHead) {
        return order[0].o.prefix + rel;
      }

      let lastErr = null;
      for (const { o } of order) {
        const url = o.prefix + rel;
        try {
          const res = await fetchWithRetry(url, { method: 'HEAD', retries: 0, timeoutMs: 5000 });
          if (res.ok) {
            return url;
          }
          if (res.status === 404) {
            // Not on this origin — try the next one.
            continue;
          }
          // 5xx or other: mark unhealthy and try next.
          o.healthy = false;
          o.lastFailMs = Date.now();
          lastErr = new Error(url + ' -> ' + res.status);
        } catch (e) {
          o.healthy = false;
          o.lastFailMs = Date.now();
          lastErr = e;
        }
      }
      throw new Error('TDCDN.resolve: all origins failed for ' + path +
                      (lastErr ? ': ' + lastErr.message : ''));
    }

    // Fetch a path (not a full URL) from the best origin. Returns the
    // Response. The caller can .arrayBuffer() / .text() / .json() it.
    async function fetchPath(path, opts) {
      opts = opts || {};
      const url = await resolve(path, { checkHead: opts.checkHead !== false });
      return await fetchWithRetry(url, opts);
    }

    // Convenience: resolve + fetch + return bytes.
    async function fetchBytes(path, opts) {
      const res = await fetchPath(path, opts);
      if (!res.ok) throw new Error('TDCDN.fetchBytes ' + path + ' -> ' + res.status);
      return await res.arrayBuffer();
    }
    async function fetchText(path, opts) {
      const res = await fetchPath(path, opts);
      if (!res.ok) throw new Error('TDCDN.fetchText ' + path + ' -> ' + res.status);
      return await res.text();
    }
    async function fetchJson(path, opts) {
      const txt = await fetchText(path, opts);
      try { return JSON.parse(txt); }
      catch (e) { throw new Error('TDCDN.fetchJson ' + path + ': invalid JSON (' + e.message + ')'); }
    }

    return {
      addOrigin, removeOrigin, clearOrigins, listOrigins,
      resolve, fetchPath, fetchBytes, fetchText, fetchJson,
    };
  })();

  // =========================================================================
  // TDRest — REST helper
  // =========================================================================
  const TDRest = (function () {
    // Default headers applied to every request. Use this for auth tokens,
    // CORS headers, etc. Override per-request via opts.headers.
    const _defaultHeaders = {};
    // Default timeout (15s) and retries (2) for every request.
    let _defaultTimeoutMs = 15000;
    let _defaultRetries   = 2;
    // Per-host rate-limit tracking: when a 429 is returned with Retry-After,
    // we back off that host until the retry-after time elapses.
    const _rateLimits = new Map(); // host -> resumeAtMs

    function setDefaultHeader(name, value) {
      if (value == null) delete _defaultHeaders[name];
      else _defaultHeaders[name] = String(value);
    }
    function setDefaultTimeout(ms)  { _defaultTimeoutMs = ms; }
    function setDefaultRetries(n)   { _defaultRetries = n; }

    function hostOf(url) {
      try { return new URL(url, (typeof location !== 'undefined' ? location.href : 'https://x/')).host; }
      catch (_) { return ''; }
    }

    function checkRateLimit(url) {
      const h = hostOf(url);
      const resumeAt = _rateLimits.get(h);
      if (resumeAt && Date.now() < resumeAt) {
        const wait = resumeAt - Date.now();
        return wait;
      }
      return 0;
    }
    function applyRateLimit(url, retryAfterSec) {
      const h = hostOf(url);
      _rateLimits.set(h, Date.now() + (retryAfterSec * 1000));
    }

    // Build the request, attach default headers, dispatch via fetchWithRetry.
    async function request(method, url, opts) {
      opts = opts || {};
      const wait = checkRateLimit(url);
      if (wait > 0) {
        const err = new Error('TDRest: rate-limited; retry in ' + Math.ceil(wait/1000) + 's');
        err.rateLimited = true;
        err.retryAfterMs = wait;
        throw err;
      }
      const headers = Object.assign({}, _defaultHeaders, opts.headers || {});
      const ro = {
        method,
        headers,
        timeoutMs: opts.timeoutMs != null ? opts.timeoutMs : _defaultTimeoutMs,
        retries:   opts.retries   != null ? opts.retries   : _defaultRetries,
      };
      if (opts.body != null) {
        if (typeof opts.body === 'string' || opts.body instanceof ArrayBuffer ||
            opts.body instanceof Blob || opts.body instanceof Uint8Array) {
          ro.body = opts.body;
        } else {
          // JSON-encode plain objects.
          if (!headers['Content-Type'] && !headers['content-type']) {
            headers['Content-Type'] = 'application/json';
          }
          ro.body = JSON.stringify(opts.body);
        }
      }
      const res = await fetchWithRetry(url, ro);
      // Handle 429 with Retry-After.
      if (res.status === 429) {
        const ra = res.headers.get('Retry-After');
        const sec = ra ? parseInt(ra, 10) || 1 : 1;
        applyRateLimit(url, sec);
      }
      return res;
    }

    // Convenience wrappers that parse the response.
    async function getJson(url, opts) {
      const res = await request('GET', url, opts);
      if (!res.ok) throw new Error('TDRest.getJson ' + url + ' -> ' + res.status);
      return await res.json();
    }
    async function postJson(url, body, opts) {
      opts = opts || {};
      const res = await request('POST', url, Object.assign({}, opts, { body }));
      if (!res.ok) throw new Error('TDRest.postJson ' + url + ' -> ' + res.status);
      return await res.json();
    }
    async function putJson(url, body, opts) {
      opts = opts || {};
      const res = await request('PUT', url, Object.assign({}, opts, { body }));
      if (!res.ok) throw new Error('TDRest.putJson ' + url + ' -> ' + res.status);
      return await res.json();
    }
    async function del(url, opts) {
      const res = await request('DELETE', url, opts);
      if (!res.ok) throw new Error('TDRest.del ' + url + ' -> ' + res.status);
      return res.status === 204 ? null : await res.json().catch(() => null);
    }

    return {
      // Configurable defaults.
      setDefaultHeader, setDefaultTimeout, setDefaultRetries,
      // Raw request (returns Response).
      request,
      // Parsed JSON shortcuts.
      getJson, postJson, putJson, del,
      // Rate-limit introspection (for tests / debugging).
      isRateLimited(url) { return checkRateLimit(url) > 0; },
      clearRateLimits() { _rateLimits.clear(); },
      // Cache pass-through (so users can cache API responses).
      cacheGet, cacheSet, cacheClear,
    };
  })();

  // =========================================================================
  // TDServer — flexible client-server communication hooks
  // =========================================================================
  //
  // TDServer sits on top of TDNet.Socket (the WebSocket transport) and adds
  // higher-level communication patterns that real games need:
  //
  //   1. Channels — pub/sub per topic. publish('explosion', {x,y}) on one
  //      client arrives as onMessage('explosion', {x,y}) on every other
  //      client subscribed to that channel. Decouples game code from the
  //      raw socket's "send to everyone" model.
  //
  //   2. RPC — request/response with timeouts. Built on TDNet.Socket.rpc
  //      but adds: typed args, multiple in-flight calls, exponential
  //      backoff on retry.
  //
  //   3. SSE / long-poll fallback — for read-only streams (leaderboards,
  //      news feeds) where WebSocket is overkill. Uses TDRest under the hood.
  //
  //   4. Presence — track who's online in this room. Server-side pings
  //      every 5s; clients maintain a peer list.
  //
  //   5. Save sync — push local TDPersistence save slots to the server
  //      for cross-device save roaming. Server stores them under the
  //      player's account ID.
  //
  // TDServer does NOT include a server — it's the client side. Pair it
  // with the standalone `tools/server/td_server.js` for a full stack.
  // =========================================================================
  const TDServer = (function () {
    let _socket   = null;     // TDNet.Socket instance (or null if not connected)
    let _serverUrl = null;    // wss://... URL we connected to
    let _playerId = null;     // assigned by the server on connect
    let _roomId   = null;     // current room
    let _authToken = null;    // optional bearer token

    // Channels: topic -> Set<callback>
    const _channels = new Map();
    // RPC pending calls: callId -> { resolve, reject, timer }
    const _pending = new Map();
    let _nextCallId = 1;
    // Presence: peerId -> { lastSeenMs, metadata }
    const _presence = new Map();
    // Server hooks: registered locally so a custom server can call into
    // game-defined handlers. (For the client side of "custom server
    // capabilities" — the server side is in tools/server/td_server.js.)
    const _hooks = new Map(); // hookName -> handler

    function _safeCall(fn, arg) {
      try { if (fn) fn(arg); } catch (e) { logError('handler threw:', e); }
    }

    // Connect to a TD engine server. Resolves when the socket opens and
    // the server acknowledges with a player-id assignment.
    // `url` is a ws:// or wss:// URL.
    // `opts.authToken` (optional) — bearer token sent in the hello frame.
    // `opts.roomId` (optional) — join a specific room on connect.
    async function connect(url, opts) {
      opts = opts || {};
      if (!global.TDNet || !global.TDNet.Socket) {
        throw new Error('TDServer.connect: TDNet.Socket not loaded (load net_websocket.js before game_kit.js)');
      }
      _serverUrl = url;
      _authToken = opts.authToken || null;
      _roomId   = opts.roomId   || null;

      return new Promise((resolve, reject) => {
        _socket = global.TDNet.Socket.connect(url, {
          onOpen: () => {
            // Send hello frame.
            _send({ t: 'hello', token: _authToken, room: _roomId });
          },
          onMessage: (msg) => _onMessage(msg),
          onClose: (reason) => {
            _socket = null;
            for (const [id, p] of _pending) {
              clearTimeout(p.timer);
              _safeCall(p.reject, new Error('socket closed: ' + (reason || '')));
            }
            _pending.clear();
            _presence.clear();
          },
        });
        // Wait for hello-ack (or timeout).
        const timer = setTimeout(() => {
          if (_playerId) resolve(_playerId);
          else reject(new Error('TDServer.connect: timed out waiting for hello-ack'));
        }, 8000);
        // The hello-ack handler in _onMessage clears the timer and resolves.
        _connectPromise = { resolve, reject, timer };
      });
    }
    let _connectPromise = null;

    function disconnect() {
      if (_socket) {
        try { _send({ t: 'bye' }); } catch (_) {}
        try { _socket.close(); } catch (_) {}
        _socket = null;
      }
      _playerId = null;
      _roomId = null;
    }

    function isConnected() { return _socket != null; }
    function getPlayerId() { return _playerId; }
    function getRoomId()   { return _roomId; }

    function _send(obj) {
      if (!_socket) { logWarn('send: not connected'); return false; }
      return _socket.send(obj);
    }

    function _onMessage(msg) {
      if (!msg || typeof msg !== 'object') return;
      switch (msg.t) {
        case 'helloAck': {
          _playerId = msg.id;
          _roomId   = msg.room || _roomId;
          if (_connectPromise) {
            clearTimeout(_connectPromise.timer);
            _connectPromise.resolve(_playerId);
            _connectPromise = null;
          }
          return;
        }
        case 'presence': {
          // Server periodically broadcasts the peer list for the room.
          _presence.clear();
          const now = Date.now();
          for (const p of (msg.peers || [])) {
            _presence.set(p.id, { lastSeenMs: now, metadata: p.meta || {} });
          }
          return;
        }
        case 'channel': {
          // { t:'channel', topic, payload, from }
          const subs = _channels.get(msg.topic);
          if (subs) {
            for (const cb of subs) {
              _safeCall(cb, { payload: msg.payload, from: msg.from });
            }
          }
          return;
        }
        case 'rpcResult': {
          // { t:'rpcResult', id, ok, result, error }
          const p = _pending.get(msg.id);
          if (!p) return;
          clearTimeout(p.timer);
          _pending.delete(msg.id);
          if (msg.ok) _safeCall(p.resolve, msg.result);
          else        _safeCall(p.reject, new Error(msg.error || 'rpc failed'));
          return;
        }
        case 'hook': {
          // Server invokes a client-side hook: { t:'hook', name, argsJson }
          const fn = _hooks.get(msg.name);
          if (!fn) {
            // Hook not registered — reply with an error so the server doesn't hang.
            _send({ t: 'hookResult', id: msg.id, ok: false,
                    error: 'no such hook: ' + msg.name });
            return;
          }
          let args = [];
          try { args = msg.argsJson ? JSON.parse(msg.argsJson) : []; } catch (_) {}
          Promise.resolve()
            .then(() => fn.apply(null, args))
            .then((result) => {
              _send({ t: 'hookResult', id: msg.id, ok: true,
                      resultJson: JSON.stringify(result === undefined ? null : result) });
            })
            .catch((e) => {
              _send({ t: 'hookResult', id: msg.id, ok: false, error: e.message || String(e) });
            });
          return;
        }
        default:
          // Unknown frame — pass through to any custom handler.
          if (_hooks.has('__raw__')) _safeCall(_hooks.get('__raw__'), msg);
      }
    }

    // --- Channels (pub/sub per topic) -------------------------------------
    function subscribe(topic, cb) {
      if (typeof topic !== 'string' || typeof cb !== 'function') {
        throw new Error('TDServer.subscribe: (topic, cb) required');
      }
      if (!_channels.has(topic)) _channels.set(topic, new Set());
      _channels.get(topic).add(cb);
      // Tell the server we're interested in this topic (server-side routing).
      _send({ t: 'sub', topic });
      return function unsubscribe() {
        const subs = _channels.get(topic);
        if (!subs) return;
        subs.delete(cb);
        if (subs.size === 0) {
          _channels.delete(topic);
          _send({ t: 'unsub', topic });
        }
      };
    }

    function publish(topic, payload, opts) {
      opts = opts || {};
      const frame = { t: 'channel', topic, payload };
      if (opts.to != null) frame.to = opts.to; // directed: send only to one peer
      return _send(frame);
    }

    // --- RPC with timeout -------------------------------------------------
    function callRemote(method, args, opts) {
      opts = opts || {};
      const timeoutMs = opts.timeoutMs || 10000;
      const id = _nextCallId++;
      return new Promise((resolve, reject) => {
        if (!_socket) { reject(new Error('not connected')); return; }
        const timer = setTimeout(() => {
          if (_pending.has(id)) {
            _pending.delete(id);
            reject(new Error('rpc timeout: ' + method));
          }
        }, timeoutMs);
        _pending.set(id, { resolve, reject, timer });
        _send({
          t: 'rpc',
          id, method,
          argsJson: JSON.stringify(args || []),
        });
      });
    }

    // --- SSE / long-poll fallback ----------------------------------------
    // Subscribe to a read-only stream via Server-Sent Events. Falls back to
    // polling if EventSource is not available. Returns a handle with .close().
    function subscribeStream(url, onEvent, opts) {
      opts = opts || {};
      if (typeof EventSource !== 'undefined') {
        const es = new EventSource(url, { withCredentials: !!opts.withCredentials });
        es.onmessage = (e) => {
          let data = e.data;
          try { data = JSON.parse(data); } catch (_) {}
          _safeCall(onEvent, data);
        };
        es.onerror = (e) => {
          if (opts.onError) _safeCall(opts.onError, e);
        };
        return { close: () => { try { es.close(); } catch (_) {} },
                 kind: 'sse' };
      }
      // Long-poll fallback.
      let stopped = false;
      const pollMs = opts.pollMs || 5000;
      let cursor = opts.cursorStart || null;
      async function poll() {
        while (!stopped) {
          try {
            const u = cursor ? url + (url.indexOf('?') >= 0 ? '&' : '?') + 'cursor=' + cursor : url;
            const res = await TDRest.request('GET', u, { timeoutMs: 15000, retries: 1 });
            if (res.ok) {
              const data = await res.json().catch(() => null);
              if (data) {
                if (data.cursor) cursor = data.cursor;
                _safeCall(onEvent, data.event || data);
              }
            }
          } catch (e) {
            if (opts.onError) _safeCall(opts.onError, e);
          }
          await new Promise(r => setTimeout(r, pollMs));
        }
      }
      poll();
      return { close: () => { stopped = true; }, kind: 'poll' };
    }

    // --- Presence ---------------------------------------------------------
    function peers() {
      const now = Date.now();
      const out = [];
      for (const [id, p] of _presence) {
        out.push({ id, ageMs: now - p.lastSeenMs, metadata: p.metadata });
      }
      return out;
    }
    function peerCount() { return _presence.size; }

    // --- Save sync -------------------------------------------------------
    // Push a local TDPersistence slot to the server. The server stores it
    // under (playerId, slotName) for cross-device roaming.
    async function pushSave(slotName) {
      if (!global.TDPersistence) throw new Error('TDPersistence not loaded');
      const json = global.TDPersistence.exportJson(slotName);
      if (json == null) throw new Error('no such save slot: ' + slotName);
      return await callRemote('savePush', [slotName, json], { timeoutMs: 20000 });
    }
    async function pullSave(slotName) {
      const result = await callRemote('savePull', [slotName], { timeoutMs: 20000 });
      if (result && result.json) {
        if (global.TDPersistence) {
          global.TDPersistence.importJson(result.json, slotName);
        }
      }
      return result;
    }
    async function listSaves() {
      return await callRemote('saveList', [], { timeoutMs: 10000 });
    }

    // --- Custom server hooks ---------------------------------------------
    // Register a client-side hook the server can invoke. The server calls
    // `server.callClientHook(peerId, hookName, args)` and the result is
    // sent back as a hookResult frame. Use this for server-pushed events
    // that need a response (e.g. "is this client ready for the next match?").
    function registerHook(name, handler) {
      if (typeof name !== 'string' || typeof handler !== 'function') {
        throw new Error('TDServer.registerHook: (name, handler) required');
      }
      _hooks.set(name, handler);
    }
    function unregisterHook(name) { return _hooks.delete(name); }

    // --- Snapshot (for tests) --------------------------------------------
    function snapshot() {
      return {
        connected: isConnected(),
        serverUrl: _serverUrl,
        playerId:  _playerId,
        roomId:    _roomId,
        channelCount: _channels.size,
        pendingRpc: _pending.size,
        peers: peers(),
        hookNames: Array.from(_hooks.keys()),
      };
    }

    return {
      connect, disconnect, isConnected, getPlayerId, getRoomId,
      subscribe, publish,
      callRemote,
      subscribeStream,
      peers, peerCount,
      pushSave, pullSave, listSaves,
      registerHook, unregisterHook,
      snapshot,
    };
  })();

  // =========================================================================
  // Expose the four namespaces globally.
  // =========================================================================
  global.TDAssets  = TDAssets;
  global.TDCDN     = TDCDN;
  global.TDRest    = TDRest;
  global.TDServer  = TDServer;

  // Also expose a TDGameKit aggregate for convenience.
  global.TDGameKit = {
    Assets: TDAssets,
    CDN: TDCDN,
    Rest: TDRest,
    Server: TDServer,
    version: '1.0.0',
    // Cache is shared across all four namespaces.
    cacheClear,
    cacheSize,
    cacheCount() { return _cache.size; },
  };

  logInfo('game_kit.js loaded — TDAssets, TDCDN, TDRest, TDServer ready');
})(typeof globalThis !== 'undefined' ? globalThis
   : typeof window !== 'undefined' ? window
   : typeof global !== 'undefined' ? global
   : this);
