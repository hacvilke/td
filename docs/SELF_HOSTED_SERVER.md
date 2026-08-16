# TD Engine — Standalone Self-Hosted Server

`tools/server/td_server.js` is a complete, runnable Node.js server for
hosting a TD Engine multiplayer game. It implements the wire protocol that
`web/game_kit.js`'s `TDServer` namespace speaks, plus the asset-proxy and
RPC dispatch features that real games need. Pair it with `TDServer` (the
browser-side client in `web/game_kit.js`) for a full client-server stack.

The server has **one npm dependency**: `ws` (already in the engine's
`package.json`). No other packages are required.

---

## 1. Features

1. **WebSocket relay** — rooms, presence, channel pub/sub, RPC dispatch,
   custom client hooks.
2. **HTTP asset proxy** — protects API keys for Freesound, Unsplash, etc.
   by keeping the token server-side and forwarding only the response bytes
   to the browser.
3. **File-backed save roaming** — players can sign in from any browser and
   pull their saves (per-`playerId` + `slotName`).
4. **Room management** — create / list / join rooms; max-players cap;
   auto-vacuum when empty.
5. **Static file server** for your game's `index.html` + assets (so you can
   run a single process for both the page and the WebSocket).
6. **Configurable** via JSON file, env vars, or CLI flags.

---

## 2. Quick start

```bash
# 1. From the engine root:
$ npm install                    # installs `ws`

# 2. Create a secrets file (optional — only if you use the asset proxy):
$ cat > secrets.json <<EOF
{
  "freesound": {
    "baseUrl": "https://freesound.org/apiv2",
    "header":  "Authorization",
    "value":   "Token YOUR_FREESOUND_TOKEN"
  },
  "unsplash": {
    "baseUrl": "https://api.unsplash.com",
    "header":  "Authorization",
    "value":   "Client-ID YOUR_UNSPLASH_KEY"
  }
}
EOF

# 3. Put your game's static files in ./public:
$ mkdir -p public
$ cp ../examples/web-game/index.html public/
$ cp ../examples/web-game/game.js    public/

# 4. Start the server:
$ node tools/server/td_server.js \
    --port 8080 \
    --static ./public \
    --saves  ./saves \
    --secrets ./secrets.json

# 5. Open http://localhost:8080/ in your browser.
```

The server logs:

```
[td_server] listening on http://localhost:8080
[td_server] serving static from: ./public
[td_server] saves stored in: ./saves
[td_server] proxy secrets available for: freesound, unsplash
```

---

## 3. Configuration

Three layers (later layers override earlier):

1. **JSON config file** — `td-server.json` in the CWD, or path passed via
   `--config`. All fields optional.
2. **Environment variables** — `TD_PORT`, `TD_STATIC_DIR`, `TD_SAVES_DIR`,
   `TD_MAX_PLAYERS`.
3. **CLI flags** — `--port`, `--static`, `--saves`, `--secrets`.

### Config file format

```json
{
  "port": 8080,
  "staticDir": "./public",
  "savesDir":  "./saves",
  "maxPlayersPerRoom": 16,
  "presenceIntervalMs": 5000,
  "cors": { "origins": ["https://yourgame.com", "https://staging.yourgame.com"] },
  "secrets": {
    "freesound": {
      "baseUrl": "https://freesound.org/apiv2",
      "header":  "Authorization",
      "value":   "Token YOUR_FREESOUND_TOKEN"
    }
  }
}
```

| Field | Default | Description |
|---|---|---|
| `port` | `8080` | HTTP + WebSocket listen port. |
| `staticDir` | `null` | If set, serve static files from this directory. `null` = no static serving (WebSocket-only server). |
| `savesDir` | `./saves` | Directory for file-backed save roaming. Created if missing. |
| `maxPlayersPerRoom` | `16` | Hard cap on peers per room. |
| `presenceIntervalMs` | `5000` | How often the server broadcasts the room's peer list. |
| `cors.origins` | `["*"]` | List of allowed origins. Use specific origins in production. |
| `secrets` | `{}` | Named secret entries for the asset proxy. See §5 below. |

---

## 4. Wire protocol

All frames are JSON. The browser-side `TDServer` namespace (in
`web/game_kit.js`) speaks this protocol automatically — you usually don't
need to write frame-level code.

### Client → server

| Frame | Purpose |
|---|---|
| `{ t:'hello', token?, room? }` | Initial handshake. Server responds with `helloAck` and assigns a `playerId`. |
| `{ t:'bye' }` | Graceful disconnect. |
| `{ t:'sub', topic }` | Subscribe to a channel. |
| `{ t:'unsub', topic }` | Unsubscribe. |
| `{ t:'channel', topic, payload, to? }` | Publish to a channel. `to` (optional) targets one peer. |
| `{ t:'rpc', id, method, argsJson }` | Call a server RPC method. `argsJson` is a JSON-encoded array. |
| `{ t:'hookResult', id, ok, resultJson?, error? }` | Reply to a server-invoked client hook. |

### Server → client

| Frame | Purpose |
|---|---|
| `{ t:'helloAck', id, room }` | Acknowledges `hello`; assigns `playerId` (= `id`). |
| `{ t:'presence', peers:[{id,meta}] }` | Periodic peer list for the room. |
| `{ t:'channel', topic, payload, from }` | Delivered channel message. `from` = sender's `playerId`. |
| `{ t:'rpcResult', id, ok, result?, error? }` | RPC reply. |
| `{ t:'hook', id, name, argsJson }` | Server invokes a client-side hook. |
| `{ t:'error', error }` | Out-of-band error (e.g. "room full"). |

---

## 5. Built-in RPC methods

The server ships with these RPC handlers ready to use (no game code
required):

| Method | Args | Returns | Description |
|---|---|---|---|
| `savePush` | `[slotName, json]` | `{ ok, sizeBytes, slotName }` | Write a save slot to disk under `${savesDir}/${playerId}/${slotName}.json`. |
| `savePull` | `[slotName]` | `{ json }` (or `{ json: null }` if missing) | Read a save slot. |
| `saveList` | `[]` | `{ slots: [{ name, timestamp, sizeBytes, version }] }` | List the player's save slots. |
| `roomList` | `[]` | `{ rooms: [{ id, playerCount, maxPlayers }] }` | List all non-empty rooms. |
| `roomCreate` | `[roomId?]` | `{ roomId }` | Create a room (auto-name if not given) and move the caller into it. |
| `whoami` | `[]` | `{ id, room }` | Echo the caller's `playerId` and current `room`. |

The browser-side `TDServer` namespace wraps `savePush`/`savePull`/`saveList`
in convenience methods (`TDServer.pushSave()`, `TDServer.pullSave()`,
`TDServer.listSaves()`).

---

## 6. Game-defined RPC handlers

Game code can register its own RPC handlers — this is the "custom game API
and standalone self-hosted server architecture" the user asks for. From
your server bootstrap:

```javascript
// server-bootstrap.js
const { startServer } = require('./tools/server/td_server.js');

const { server, httpServer } = startServer({
  port: 8080,
  staticDir: './public',
  savesDir:  './saves',
  secrets:   require('./secrets.json'),
  maxPlayersPerRoom: 16,
});

// Register a game-specific RPC handler.
server.registerRpc('submitScore', (args, ctx) => {
  const [score, level] = args;
  // ctx.peerId is the calling player's ID.
  // ctx.room is the player's current room.
  // ctx.server is the TdServer instance (for calling other methods).
  console.log(`Player ${ctx.peerId} scored ${score} on level ${level}`);
  // Persist to a DB, update leaderboards, broadcast a 'newHighScore' event...
  ctx.server._broadcastToRoom(ctx.room, {
    t: 'channel', topic: 'newHighScore',
    payload: { playerId: ctx.peerId, score, level },
  });
  return { ok: true, rank: 42 };
});

// Call into a client-side hook (e.g. to ask "are you ready for the next match?").
async function startMatchWhenReady(roomId) {
  const room = server.rooms.get(roomId);
  if (!room) return;
  const readyResults = await Promise.allSettled(
    Array.from(room).map(pid =>
      server.callClientHook(pid, 'isReady', [], { timeoutMs: 5000 })
    )
  );
  const allReady = readyResults.every(r => r.status === 'fulfilled' && r.value);
  if (allReady) {
    server._broadcastToRoom(roomId, { t: 'channel', topic: 'matchStart' });
  }
}
```

Run your bootstrap script instead of `td_server.js` directly:

```bash
$ node server-bootstrap.js
```

---

## 7. HTTP asset proxy

The asset proxy protects API keys for authenticated APIs (Freesound,
Unsplash, etc.) by keeping the token server-side. Configure each secret
in your config:

```json
{
  "secrets": {
    "freesound": {
      "baseUrl": "https://freesound.org/apiv2",
      "header":  "Authorization",
      "value":   "Token YOUR_FREESOUND_TOKEN"
    },
    "unsplash": {
      "baseUrl": "https://api.unsplash.com",
      "header":  "Authorization",
      "value":   "Client-ID YOUR_UNSPLASH_KEY"
    }
  }
}
```

Then from the browser, request:

```
GET https://your.server/proxy/freesound/search/text/?query=footstep+wood
GET https://your.server/proxy/unsplash/search/photos?query=brick+wall
```

The server:
1. Looks up `cfg.secrets['freesound']` to get the base URL + auth header.
2. Fetches `https://freesound.org/apiv2/search/text/?query=footstep+wood`
   with `Authorization: Token YOUR_FREESOUND_TOKEN` injected.
3. Pipes the response bytes back to the browser.

The browser never sees the token. Combine with `TDAssets.loadAudio` /
`TDAssets.loadTexture` for a one-liner:

```javascript
// Load a Freesound footstep as an AudioBuffer:
const audio = await TDAssets.loadAudio(
  'https://your.server/proxy/freesound/sounds/1234/download/'
);

// Load an Unsplash photo as a texture:
const tex = await TDAssets.loadTexture(
  'https://your.server/proxy/unsplash/photos/abc123/download?w=1024'
);
```

---

## 8. Static file serving

If `staticDir` is set, the server serves files from that directory at the
root path. `index.html` is served for `/`. Common MIME types are
auto-detected (`.html`, `.js`, `.css`, `.json`, `.png`, `.jpg`, `.svg`,
`.wasm`, `.wav`, `.mp3`, `.ogg`, `.ico`, `.txt`). Cache-Control is set to
`no-cache` for easy dev iteration — set up a CDN in front of the server
for production caching.

Path traversal is prevented: any request whose resolved path escapes the
`staticDir` returns 403.

---

## 9. CORS

By default, the server allows all origins (`Access-Control-Allow-Origin: *`).
For production, restrict to your game's origin:

```json
{
  "cors": { "origins": ["https://mygame.com", "https://staging.mygame.com"] }
}
```

The server handles `OPTIONS` preflight requests automatically.

---

## 10. Production deployment

### 10.1 Reverse proxy (recommended)

Run `td_server.js` on a high port (e.g. 8080) behind nginx or Caddy, which
handles TLS termination, gzip, and HTTP/2:

```nginx
server {
    listen 443 ssl http2;
    server_name yourgame.example.com;

    ssl_certificate     /etc/letsencrypt/live/yourgame.example.com/fullchain.pem;
    ssl_certificate_key /etc/letsencrypt/live/yourgame.example.com/privkey.pem;

    location / {
        proxy_pass http://127.0.0.1:8080;
        proxy_http_version 1.1;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
    }

    location /td {
        proxy_pass http://127.0.0.1:8080;
        proxy_http_version 1.1;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection "upgrade";
        proxy_read_timeout 86400;
    }
}
```

### 10.2 Process management

Use `systemd`, `pm2`, or `docker` to keep the server running:

```bash
# pm2
$ pm2 start tools/server/td_server.js --name td-server -- --config /etc/td/td-server.json
$ pm2 save
$ pm2 startup
```

### 10.3 Docker

```dockerfile
FROM node:20-alpine
WORKDIR /app
COPY package*.json ./
RUN npm install --production
COPY . .
EXPOSE 8080
CMD ["node", "tools/server/td_server.js", "--config", "/etc/td/td-server.json"]
```

---

## 11. Client-side pairing

In your game's `index.html`, load `game_kit.js` after `net_websocket.js`:

```html
<script src="js_bridge.js"></script>
<script src="td_api.js"></script>
<script src="net_websocket.js"></script>
<script src="game_kit.js"></script>
<script src="game.js"></script>
```

In `game.js`:

```javascript
await TDEngine.lifecycle.init('game-canvas');

const playerId = await TDServer.connect('wss://yourgame.example.com/td', {
  authToken: sessionToken,
  roomId:    'arena-1',
});

TDServer.subscribe('explosions', ({ payload, from }) => {
  spawnExplosion(payload.x, payload.y, from);
});

TDServer.publish('explosions', { x: 100, y: 200 });
```

See `docs/GAME_KIT.md` for the full `TDServer` API.

---

## 12. Test coverage

`tests/test_td_server.js` runs **48 tests** covering:

- Config: `parseArgs`, `loadConfig` (defaults, CLI, env vars).
- `TdServer` class (no network): peer lifecycle, RPC dispatch
  (success + unknown method), room management, channel pub/sub
  (broadcast + directed), save sync (push/pull/list), client hooks
  (success + timeout), presence broadcast.
- E2E via real `ws` + real HTTP server: hello/helloAck, two-client
  presence, RPC round-trip (`roomList`), channel pub/sub across two
  clients, game-registered RPC handler.

Run them with:

```bash
$ node tests/test_td_server.js
```

---

## 13. Architecture

```
                       ┌────────────────────────────────────────────┐
                       │              tools/server/td_server.js     │
                       │                                            │
   Browser ────────►   │  ┌─────────────┐   ┌──────────────────┐    │
   (game_kit.js)       │  │ HTTP server │ ─ │ Static file serve│    │
                       │  │             │ ─ │ Asset proxy       │    │
                       │  │             │ ─ │ CORS preflight     │    │
                       │  └──────┬──────┘   └──────────────────┘    │
                       │         │                                  │
                       │  ┌──────▼──────┐   ┌──────────────────┐    │
                       │  │ WebSocket   │ ─ │ TdServer          │    │
                       │  │ Server (ws) │   │  - peers + rooms  │    │
                       │  │             │   │  - channel pub/sub│    │
                       │  │             │   │  - RPC dispatch   │    │
                       │  │             │   │  - client hooks   │    │
                       │  │             │   │  - presence       │    │
                       │  │             │   │  - save sync      │    │
                       │  └─────────────┘   └──────────────────┘    │
                       │                                            │
                       │  ┌─────────────────────────────────────┐   │
                       │  │ File system                          │   │
                       │  │  ${savesDir}/${playerId}/${slot}.json│   │
                       │  └─────────────────────────────────────┘   │
                       └────────────────────────────────────────────┘
```

The HTTP server and WebSocket server share a single TCP port (the
WebSocket upgrade happens on the same listener). The `TdServer` class owns
all game state and is the single source of truth for peers, rooms, and
RPC handlers.
