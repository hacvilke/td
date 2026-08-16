# TD Engine — Game Kit API

`web/game_kit.js` exposes four browser-side namespaces that broaden TD Engine's
public API past the in-engine ECS / rendering / networking core. They are aimed
squarely at **web-development workflows** — fetching assets from any URL,
routing them through CDN prefixes with failover, calling REST APIs with retry
+ auth, and talking to a self-hosted server through high-level channels, RPCs,
presence, and save sync.

| Namespace | Purpose |
|---|---|
| `TDAssets` | Free asset pipeline. Fetch textures / audio / JSON / text from any URL (your CDN, GitHub raw, Openverse, Freesound, Unsplash, etc.) and feed the bytes into the engine with one call. Built-in caching, retry, and decoding helpers. |
| `TDCDN` | Multi-origin CDN routing with failover. Define a list of CDN prefixes for your game's assets; the engine resolves each logical path against them in weight order until one returns 200. |
| `TDRest` | REST helper. Wraps `fetch()` with timeout, retry, auth-header injection, JSON encoding, and per-host rate-limit tracking. |
| `TDServer` | Flexible client-server communication hooks. Wraps the existing `TDNet.Socket` transport with typed channels, RPC with timeouts, SSE / long-poll fallback, presence, and save sync. |

All four are pure-JS IIFEs with **zero external dependencies**. They are loaded
additively in `web/index.html` — failure to load one does not break the engine
boot. Pair `TDServer` with the standalone `tools/server/td_server.js` for a
full client-server stack.

---

## 1. Loading

Add `<script src="game_kit.js"></script>` to your page **after**
`net_websocket.js` (which provides `TDNet.Socket`, the transport `TDServer`
sits on) and after `td_api.js` (which provides `TDEngine.bridge`, used by
`TDAssets.uploadTexture`). The four globals are then available immediately:

```html
<script src="js_bridge.js"></script>
<script src="td_api.js"></script>
<script src="net_websocket.js"></script>
<script src="game_kit.js"></script>      <!-- TDAssets, TDCDN, TDRest, TDServer -->
<script src="game.js"></script>
```

A convenience aggregate is also exposed:

```javascript
TDGameKit.Assets  === TDAssets;
TDGameKit.CDN     === TDCDN;
TDGameKit.Rest    === TDRest;
TDGameKit.Server  === TDServer;
TDGameKit.cacheClear();
TDGameKit.cacheSize();
TDGameKit.cacheCount();
```

---

## 2. TDAssets — free asset pipeline

The engine's WASM bridge is intentionally HTTP-agnostic — the browser already
has `fetch()`. `TDAssets` wraps that fetch with retry, caching, and the
decoding/upload helpers you need to turn a URL into an in-engine Texture or
an `AudioBuffer` ready for Web Audio.

### 2.1 Fetch primitives

```javascript
// Raw bytes with caching + retry.
const buf = await TDAssets.fetchBytes('https://cdn.example.com/sprites/ship.png');

// Text (UTF-8).
const src = await TDAssets.fetchText('https://cdn.example.com/level1.json');

// Parsed JSON.
const cfg = await TDAssets.fetchJson('https://cdn.example.com/config.json');
```

All three accept an optional `opts` object:

| Option | Default | Description |
|---|---|---|
| `opts.cache` | `true` | If false, bypass the in-memory cache. |
| `opts.ttlMs` | `5 * 60 * 1000` (5 min) | Cache entry time-to-live in ms. `0` = forever. |
| `opts.timeoutMs` | `15000` | Per-attempt fetch timeout. |
| `opts.retries` | `2` | Number of retries on network error or 5xx. |
| `opts.headers` | `{}` | Extra request headers. |

### 2.2 Decoding helpers

```javascript
// Decode image bytes -> ImageBitmap (uses createImageBitmap when available;
// falls back to <img> + canvas in older browsers).
const bytes = await TDAssets.fetchBytes(url);
const bmp   = await TDAssets.decodeImage(bytes, { mime: 'image/png' });

// Decode audio bytes -> AudioBuffer via Web Audio's decodeAudioData.
const audioBytes = await TDAssets.fetchBytes(audioUrl);
const audioBuf   = await TDAssets.decodeAudio(audioBytes);
```

### 2.3 Uploading into the engine

`TDAssets.uploadTexture(bitmap)` takes a decoded `ImageBitmap` (or `<img>`),
extracts its RGBA pixels via a 2D canvas, copies them into WASM memory, and
calls `td_create_texture` — returning the texture ID the engine assigned.
You can then pass that ID to `TDEngine.ecs.setSprite` or any other
texture-consuming API.

```javascript
await TDEngine.lifecycle.init('game-canvas');
const texId = await TDAssets.loadTexture('https://cdn.example.com/ship.png');
TDEngine.ecs.setSprite(entityId, 64, 64, 1, 1, 1, 1);  // sprite using the texture
```

### 2.4 One-shot convenience methods

```javascript
// fetch + decode + upload -> returns texId.
const texId = await TDAssets.loadTexture(url, { ttlMs: 60_000 });

// fetch + decode -> returns AudioBuffer ready for Web Audio.
const audioBuf = await TDAssets.loadAudio(url);
```

### 2.5 Cache management

`TDAssets` shares a single in-memory cache with `TDRest` so any cached entry
is visible to both namespaces.

```javascript
TDAssets.cacheGet(key);          // raw cache lookup
TDAssets.cacheSet(key, value, ttlMs, sizeBytes);
TDAssets.cacheClear();           // returns count of cleared entries
TDAssets.cacheSize();            // total bytes tracked
TDAssets.cacheCount();           // entry count
```

### 2.6 Real-world example: DiceBear avatars as textures

From `docs/PUBLIC_APIS.md` — DiceBear is a free, key-free avatar API.

```javascript
async function loadAvatar(seed) {
  const url = `https://api.dicebear.com/9.x/pixel-art/png?seed=${encodeURIComponent(seed)}&size=64`;
  return await TDAssets.loadTexture(url, { ttlMs: 24 * 60 * 60 * 1000 });  // cache 24h
}

const playerAvatar = await loadAvatar('player-1');
TDEngine.ecs.setSprite(playerEntity, 64, 64, 1, 1, 1, 1);
```

---

## 3. TDCDN — multi-origin CDN routing with failover

Most production games don't want to hardcode one CDN URL — they want a list
of CDNs (e.g. primary + secondary + a self-hosted fallback), and the engine
should automatically use whichever one serves the file. `TDCDN` does that.

### 3.1 Configuration

```javascript
// Add CDN prefixes (trailing slash auto-added).
TDCDN.addOrigin('https://cdn.primary.com/v1',   { weight: 10, label: 'primary'   });
TDCDN.addOrigin('https://cdn.secondary.com/v1', { weight:  5, label: 'secondary' });
TDCDN.addOrigin('/assets',                       { weight:  1, label: 'local'     });

// List + remove.
TDCDN.listOrigins();        // -> [{ prefix, weight, healthy, label }, ...]
TDCDN.removeOrigin(index);
TDCDN.clearOrigins();
```

- **`weight`** (default 1): higher weights are tried first. Among equal
  weights, insertion order is preserved.
- **`healthy`**: runtime flag. Set to `false` on network error or 5xx;
  automatically restored by a background sweep every 30 s so a transient
  outage doesn't permanently exclude a CDN.

### 3.2 Resolving a path

```javascript
// Resolve: try each healthy origin (in weight order) with a HEAD request;
// return the first URL that returns 200.
const url = await TDCDN.resolve('sprites/ship.png');
// -> 'https://cdn.primary.com/v1/sprites/ship.png'

// Skip the HEAD check (faster, but may return a 404 URL).
const url = await TDCDN.resolve('sprites/ship.png', { checkHead: false });

// Resolve + fetch in one call.
const bytes = await TDCDN.fetchBytes('sprites/ship.png');
const text  = await TDCDN.fetchText('levels/level1.json');
const cfg   = await TDCDN.fetchJson('config.json');
```

### 3.3 Failover behavior

- `404` on one origin → try the next origin.
- `5xx` on one origin → mark it unhealthy, try the next.
- Network error on one origin → mark it unhealthy, try the next.
- All origins fail → throw an `Error` with the last failure as the cause.

The unhealthy flag is cleared automatically by a 30-second sweep, so a CDN
that went down for a minute will be retried later.

### 3.4 Same API in dev and prod

In development, point `TDCDN` at your local dev server:

```javascript
TDCDN.addOrigin('http://localhost:8080/assets', { weight: 1 });
```

In production, add the production CDN first (with higher weight):

```javascript
TDCDN.addOrigin('https://cdn.example.com/v1', { weight: 10 });
TDCDN.addOrigin('http://localhost:8080/assets', { weight: 1 });  // fallback
```

Your game code stays the same: `TDCDN.fetchBytes('sprites/ship.png')`.

---

## 4. TDRest — REST helper

Wraps `fetch()` with the things every game ends up needing: timeout, retry,
default headers (for auth tokens), JSON encoding, and per-host rate-limit
tracking (honoring `Retry-After`).

### 4.1 Configurable defaults

```javascript
TDRest.setDefaultHeader('Authorization', 'Bearer my-game-token');
TDRest.setDefaultTimeout(10_000);   // 10s per attempt
TDRest.setDefaultRetries(3);        // 3 retries on 5xx / network error

// Remove a default header:
TDRest.setDefaultHeader('Authorization', null);
```

### 4.2 Convenience methods

```javascript
// GET -> parsed JSON.
const data = await TDRest.getJson('https://api.example.com/v1/items');

// POST -> JSON-encodes the body, sets Content-Type: application/json.
const created = await TDRest.postJson('https://api.example.com/v1/items',
                                       { name: 'Sword', price: 100 });

// PUT.
const updated = await TDRest.putJson('https://api.example.com/v1/items/42',
                                      { name: 'Sword+', price: 150 });

// DELETE.
await TDRest.del('https://api.example.com/v1/items/42');
```

### 4.3 Raw request (returns Response)

When you need the raw `Response` (e.g. to read headers, stream the body,
or handle non-JSON responses):

```javascript
const res = await TDRest.request('GET', url, {
  headers: { 'Accept': 'application/xml' },
  timeoutMs: 5000,
  retries: 0,
});
if (res.ok) {
  const xml = await res.text();
}
```

### 4.4 Retry + rate-limit behavior

- **Retry**: on network error or 5xx, TDRest retries up to `opts.retries`
  times with exponential backoff (300ms × attempt).
- **4xx**: not retried (caller error).
- **429**: TDRest reads the `Retry-After` header (seconds) and marks the
  host as rate-limited. Subsequent requests to that host throw immediately
  with `err.rateLimited = true` and `err.retryAfterMs` set, so the game can
  surface a "rate limited, try again in N seconds" message instead of
  burning through the user's retry budget.

```javascript
try {
  await TDRest.getJson('https://api.example.com/v1/items');
} catch (e) {
  if (e.rateLimited) {
    showUserMessage(`Rate-limited. Try again in ${Math.ceil(e.retryAfterMs / 1000)}s.`);
  } else {
    console.error(e);
  }
}
```

### 4.5 Caching API responses

`TDRest` shares the in-memory cache with `TDAssets`. Use it to cache
expensive or rarely-changing API responses:

```javascript
async function getPlayerProfile(playerId) {
  const cacheKey = 'profile:' + playerId;
  let profile = TDRest.cacheGet(cacheKey);
  if (profile) return profile;
  profile = await TDRest.getJson(`https://api.example.com/v1/players/${playerId}`);
  TDRest.cacheSet(cacheKey, profile, 5 * 60 * 1000, 1000);  // 5 min TTL
  return profile;
}
```

### 4.6 Real-world example: weather API → in-game weather

From `docs/PUBLIC_APIS.md` — the US National Weather Service API is free,
key-free, and CORS-enabled.

```javascript
async function applyRealWeather(lat, lon) {
  // NWS wants a two-step lookup: lat/lon -> grid -> forecast.
  const points = await TDRest.getJson(`https://api.weather.gov/points/${lat},${lon}`);
  const forecast = await TDRest.getJson(points.properties.forecast);
  const period = forecast.properties.periods[0];
  console.log(`Weather: ${period.shortForecast}, ${period.temperature}°${period.temperatureUnit}`);

  // Drive the engine's particle system + ambient light color.
  if (period.shortForecast.toLowerCase().includes('rain')) {
    setRainParticleRate(800);
  } else {
    setRainParticleRate(0);
  }
  if (period.temperature < 5)  setAmbientTint(0.8, 0.9, 1.0);  // cold blue
  if (period.temperature > 30) setAmbientTint(1.0, 0.9, 0.7);  // hot orange
}
```

---

## 5. TDServer — flexible client-server communication hooks

`TDServer` sits on top of `TDNet.Socket` (the existing WebSocket transport)
and adds the higher-level communication patterns that real games need. Pair
it with `tools/server/td_server.js` (the standalone self-hosted server) for
a full client-server stack — see `docs/SELF_HOSTED_SERVER.md`.

### 5.1 Connection

```javascript
const playerId = await TDServer.connect('wss://your.server/td', {
  authToken: 'player-session-token',
  roomId:    'arena-1',
});

TDServer.isConnected();   // true
TDServer.getPlayerId();   // 'p3'
TDServer.getRoomId();     // 'arena-1'

// Later:
TDServer.disconnect();
```

`connect` resolves once the server acknowledges with a `helloAck` frame
containing the assigned `playerId`. If the server doesn't respond within
8 seconds, the promise rejects.

### 5.2 Channels (pub/sub per topic)

Channels are the recommended way to broadcast game events. Instead of
"send this to everyone in the room", you publish to a topic and only
peers who subscribed receive it. The server routes per-room; subscribers
in other rooms don't see your messages.

```javascript
// Subscribe — returns an unsubscribe function.
const unsubscribe = TDServer.subscribe('explosions', ({ payload, from }) => {
  spawnExplosion(payload.x, payload.y, from);
});

// Publish to every subscriber in the room.
TDServer.publish('explosions', { x: 100, y: 200 });

// Directed: send only to one peer.
TDServer.publish('whisper', { text: 'psst' }, { to: targetPeerId });

// Stop listening.
unsubscribe();
```

### 5.3 RPC (request/response with timeout)

Built on top of the socket's RPC frame. Returns a Promise that resolves
with the server's result (or rejects on error / timeout).

```javascript
// Call a server-side RPC method.
const leaderboard = await TDServer.callRemote('getLeaderboard',
                                              { game: 'pong', limit: 10 },
                                              { timeoutMs: 5000 });

// The server can register handlers (see tools/server/td_server.js):
//   server.registerRpc('getLeaderboard', (args, ctx) => { ... return top10; });
```

### 5.4 SSE / long-poll fallback

For read-only streams (leaderboards, news feeds, server status) where
WebSocket is overkill, `subscribeStream` uses Server-Sent Events when
available, falling back to HTTP long-polling when not.

```javascript
const handle = TDServer.subscribeStream(
  'https://your.server/stream/leaderboard',
  (event) => updateLeaderboardUI(event),
  {
    onError: (e) => console.warn('stream error', e),
    withCredentials: false,    // send cookies?
    pollMs: 5000,              // long-poll fallback interval
  }
);

// Later:
handle.close();
```

### 5.5 Presence

The server broadcasts a presence frame every 5 seconds (configurable in
`td_server.js`) listing every peer currently in the room. `TDServer`
maintains the list locally.

```javascript
const peers = TDServer.peers();
// -> [{ id: 'p2', ageMs: 1234, metadata: {} }, ...]

TDServer.peerCount();   // number of other peers in the room
```

### 5.6 Save sync (cross-device save roaming)

Push a local `TDPersistence` save slot to the server, or pull it back on
a different device. The server stores saves under `(playerId, slotName)`.

```javascript
// Save locally (as usual)...
TDPersistence.save('checkpoint-1');

// ...then push to the server so it's available on any device.
await TDServer.pushSave('checkpoint-1');

// On a different browser:
const saves = await TDServer.listSaves();
// -> { slots: [{ name, timestamp, sizeBytes, version }, ...] }
await TDServer.pullSave('checkpoint-1');  // pulls + imports into TDPersistence
```

### 5.7 Custom client hooks

The server can invoke client-side handlers via the `hook` frame. This is
the inverse of RPC: the server asks the client a question and waits for
the answer. Use it for things like "is this client ready for the next
match?".

```javascript
// Client side:
TDServer.registerHook('isReady', () => {
  return playerHealth > 0 && !playerIsLoading;
});

// Server side (in tools/server/td_server.js):
const ready = await server.callClientHook(peerId, 'isReady', [], { timeoutMs: 5000 });
if (ready) startNextMatch();
```

### 5.8 Snapshot (for tests / debugging)

```javascript
const snap = TDServer.snapshot();
// -> {
//   connected: true,
//   serverUrl: 'wss://your.server/td',
//   playerId: 'p3',
//   roomId: 'arena-1',
//   channelCount: 2,
//   pendingRpc: 1,
//   peers: [{ id, ageMs, metadata }, ...],
//   hookNames: ['isReady'],
// }
```

---

## 6. Putting it all together — a full game-dev workflow

Here is the boot sequence for a multiplayer game that uses every part of
the Game Kit:

```javascript
// 1. Initialize the engine.
await TDEngine.lifecycle.init('game-canvas');

// 2. Configure CDN routing (production CDN + local fallback).
TDCDN.addOrigin('https://cdn.mygame.com/v1', { weight: 10 });
TDCDN.addOrigin('/assets',                    { weight:  1 });

// 3. Load all textures via the asset pipeline (caching + decoding + upload).
const shipTex   = await TDCDN.fetchJson('config.json').then(c => c.shipSprite)
                  .then(url => TDAssets.loadTexture(url));
const avatarTex = await TDAssets.loadTexture(
                    `https://api.dicebear.com/9.x/pixel-art/png?seed=${playerName}`);

// 4. Configure REST defaults for the game's HTTP API.
TDRest.setDefaultHeader('Authorization', `Bearer ${sessionToken}`);

// 5. Connect to the self-hosted server.
const playerId = await TDServer.connect('wss://mygame.example.com/td', {
  authToken: sessionToken,
  roomId:    'arena-1',
});

// 6. Subscribe to game channels.
TDServer.subscribe('explosions', ({ payload, from }) => {
  spawnExplosion(payload.x, payload.y, from);
});
TDServer.subscribe('chat', ({ payload, from }) => {
  showChatMessage(from, payload.text);
});

// 7. Register a client hook the server can call.
TDServer.registerHook('isReady', () => loaded && playerEntity != null);

// 8. Each frame: send player position via channel, draw, etc.
function gameLoop(dt) {
  TDEngine.ecs.setPosition(playerEntity, playerX, playerY);
  TDServer.publish('position', { x: playerX, y: playerY, texId: avatarTex });
}

// 9. On important events: push save to server for cross-device sync.
async function onLevelComplete() {
  TDPersistence.save('checkpoint-1');
  await TDServer.pushSave('checkpoint-1');
}
```

This is the full stack: CDN routing for asset delivery, free APIs for
content (DiceBear, NWS, Openverse, etc.), a self-hosted server for
multiplayer + save roaming, and channels + RPCs for in-game communication.

---

## 7. Test coverage

`tests/test_game_kit.js` runs **32 tests** covering:

- `TDAssets`: fetchText, fetchJson (parse + invalid-JSON rejection),
  fetchBytes, loadAudio, cache store/get/clear.
- `TDCDN`: addOrigin + listOrigins, resolve (with and without HEAD check),
  failover on 404, weight-based ordering, "no origins configured" error,
  fetchJson convenience.
- `TDRest`: default header injection, 5xx retry, 429 rate-limit tracking,
  postJson JSON encoding.
- `TDServer`: registerHook + unregisterHook, snapshot.

All tests run in Node via a `vm` sandbox with a fake `fetch` and fake
`WebSocket`; the same file also runs in a browser. Run them with:

```bash
$ node tests/test_game_kit.js
```
