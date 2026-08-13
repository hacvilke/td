# TD Engine - Public API Integration Guide

How to consume third-party HTTP APIs from a TD Engine web game. The engine
itself stays dependency-free; networking happens in JavaScript via `fetch()`
and the results are pushed into the engine's ECS world via the `td_*` C API.

## The pattern

```
┌─────────────────────┐         ┌─────────────────────┐
│  Browser JavaScript │         │  td-engine.wasm     │
│                     │         │                     │
│  fetch(apiURL)      │  HTTP   │  ECS World          │
│    .then(data =>    │ ◀────── │  (entities, sprites)│
│      td_set_pos(    │         │                     │
│        ent,         │ ──────▶ │  Renderer (WebGL2)  │
│        data.x,      │  cwrap  │                     │
│        data.y))     │         │  Mixer (Web Audio)  │
└─────────────────────┘         └─────────────────────┘
```

**Why this split?** The engine is a pure C++ runtime - it knows nothing about
HTTP, REST, or JSON. Browsers already have `fetch()`, `URL`, `URLSearchParams`,
and `TextDecoder` built in. Doing API calls in JS and feeding the results into
the engine via the exported C API keeps the engine small and lets developers
use any HTTP API without engine changes.

## The four-step recipe

```javascript
// 1. Fetch the data (in JS, using the browser's native fetch).
const res = await fetch('https://api.example.com/data');
const data = await res.json();

// 2. Get a cwrap handle to the td_* function you need.
const td_create = Module.cwrap('td_create_entity', 'number', ['string']);
const td_set_pos = Module.cwrap('td_entity_set_position', null,
                                ['number', 'number', 'number']);
const td_set_spr = Module.cwrap('td_entity_set_sprite', null,
                                ['number', 'number', 'number',
                                 'number', 'number', 'number', 'number']);

// 3. Create an entity for each API result.
for (const item of data.items) {
    const ent = td_create(item.name);
    td_set_pos(ent, item.x, item.y);
    td_set_spr(ent, 32, 32, 1, 1, 1, 1);
}

// 4. The engine's main loop renders every frame automatically.
```

For polling APIs (e.g. weather that updates every 5 minutes), wrap the fetch in
a `setInterval` and update the existing entities' positions/sprites instead of
creating new ones.

---

## API survey

Below are public APIs that are useful in a browser game context, with concrete
integration ideas for each. All are CORS-enabled (so they work directly from
the browser) unless noted.

### 1. News Targeted API - `https://api.newstargeted.com`

**What it is.** A platform providing Roblox thumbnails (batched avatar
fetching with derived-cache headers), Discord helpers (user lookup, webhooks),
and a webhook relay proxy. Endpoints include `GET /avatar?userIds=...`,
`GET /roblox/users/:id`, `GET /roblox/games/:id`, plus webhook relay paths.
Built for browser-side use (Roblox Luau has the same origin policy as browsers).

**Relevance to a game engine.**

- **Player avatars in multiplayer browser games.** If your TD Engine web game
  has a lobby or scoreboard, fetch each player's Roblox avatar (by user id)
  as a PNG and upload it to the engine as a `Texture` via
  `Texture::create(config, pixels)`. The engine's SpriteBatch then renders
  that texture on the player's sprite.
- **Discord-rich presence.** Use the webhook relay to post game events
  ("Player X reached level 10") to a Discord channel from the browser -
  no server needed.
- **Live news feed in an in-game UI.** The news endpoints can drive a
  scrolling ticker rendered as a `SpriteBatch` text layer (TD Engine's
  scripting language, `td`, can be extended with a `text.draw` opcode).

**Integration sketch (avatar textures):**

```javascript
async function loadAvatarTexture(userId) {
    const res = await fetch(`https://api.newstargeted.com/avatar?userIds=${userId}`);
    const blob = await res.blob();
    const bitmap = await createImageBitmap(blob);
    // Copy pixels into WASM memory and call Texture::create.
    const canvas = new OffscreenCanvas(bitmap.width, bitmap.height);
    const ctx = canvas.getContext('2d');
    ctx.drawImage(bitmap, 0, 0);
    const imageData = ctx.getImageData(0, 0, bitmap.width, bitmap.height);
    const Module = TDBridge.wasmExports;
    const ptr = Module._malloc(imageData.data.length);
    Module.HEAPU8.set(imageData.data, ptr);
    // (Pseudocode - a td_create_texture export would wrap Texture::create)
    const texId = Module.ccall('td_create_texture', 'number',
        ['number', 'number', 'number', 'number'],
        [bitmap.width, bitmap.height, 4, ptr]);
    Module._free(ptr);
    return texId;
}
```

**CORS:** Yes - the API is designed for browser use.

### 2. National Weather Service API - `https://api.weather.gov`

**What it is.** Free (no key, no rate limit for reasonable use) REST API for
US weather forecasts, alerts, and observations. Returns GeoJSON.

**Relevance to a game engine.**

- **Dynamic in-game weather.** Fetch the forecast for a real location and
  drive the engine's particle system (rain, snow) + ambient lighting color.
  A driving game set in the player's actual city becomes wet when it's really
  raining outside.
- **Seasonal theming.** Use the observation temperature to tint the scene
  (cold = blue cast, hot = orange cast).
- **Storm alerts as gameplay events.** Severe weather alerts could spawn
  in-game storms or unlock weather-specific missions.

**CORS:** Yes - `api.weather.gov` sends `Access-Control-Allow-Origin: *`.

### 3. Open-Meteo - `https://api.open-meteo.com`

**What it is.** Free (no key) global weather API covering the whole planet
(not just the US like NWS). Provides hourly + daily forecasts, historical
data, and climate models.

**Relevance:** Same as NWS but global. Particularly good for games that want
"match the weather outside the player's window" anywhere in the world.

**CORS:** Yes.

### 4. OpenTriviaDatabase - `https://opentdb.com/api.php`

**What it is.** Free (no key) JSON API with thousands of trivia questions
across 24 categories. Returns questions, multiple-choice answers, and
correct answer indices.

**Relevance:**

- **Trivia / quiz games.** Pull questions and render them as in-game text
  sprites. Player answers via keyboard (1-4 keys) - the engine's input
  system reads `Key::Num1` through `Key::Num4` and the JS game logic checks
  correctness.
- **NPC dialogue.** Use trivia as conversation prompts from NPCs in an RPG.

**CORS:** Yes (with `Access-Control-Allow-Origin: *`).

### 5. PokéAPI - `https://pokeapi.co`

**What it is.** Free REST API with full Pokédex data: sprites, stats, types,
moves, evolution chains. No key required.

**Relevance:**

- **A Pokémon-style web game.** Fetch monster sprites as PNGs and load them
  as TD Engine textures. Stats (HP, attack, defense) drive the engine's
  RigidBodyComponent (mass) and a custom StatsComponent.
- **Procedural bestiary.** Any game that wants hundreds of monsters with
  art + stats without bundling them - PokéAPI gives you both.

**CORS:** Yes.

### 6. OpenStreetMap / Overpass API

**What it is.** Free (no key, fair-use limited) geo-data API. Returns
GeoJSON with roads, buildings, parks, water bodies for any bounding box.

**Relevance:**

- **Real-world city games.** Fetch the actual street grid around the player's
  GPS coordinates and generate a top-down driving/walking game level. Each
  road becomes a SpriteComponent with a road texture; each building becomes
  a ColliderComponent AABB.
- **Geography quizzes.** Show a map extract and ask "where is this?".

**CORS:** Yes for the main tile server; Overpass needs a `callback` JSON-P
trick or a proxy.

### 7. Datamuse - `https://api.datamuse.com`

**What it is.** Free word-finding API: "words that rhyme with X",
"synonyms of Y", "words that start with Z and mean ...". No key.

**Relevance:**

- **Word games.** Anagram, rhyme, synonym games can pull fresh words every
  round. The engine's SpriteBatch renders the letters; Datamuse supplies
  the dictionary.
- **Procedural NPC names.** "Give me 10 words that sound like 'forest'"
  → use them as fairy-tale NPC names.

**CORS:** Yes.

### 8. GitHub API - `https://api.github.com`

**What it is.** REST API for repos, users, issues, releases. 60
unauthenticated requests/hour per IP; 5000/hour with a token.

**Relevance:**

- **Community mod browsers in-game.** If your game supports user-generated
  content stored in a GitHub repo, the in-game browser can list mods via
  `GET /repos/:owner/:repo/contents/`. Each mod is a `.tdscene` file the
  engine's `td_load_scene()` can parse.
- **Live changelog.** Show `GET /repos/:owner/:repo/releases` as a scrolling
  news ticker on the title screen.

**CORS:** Yes.

### 9. Wikipedia / Wikidata - `https://en.wikipedia.org/w/api.php`

**What it is.** MediaWiki Action API. Search articles, fetch extracts,
get page images. No key.

**Relevance:**

- **Educational games.** A "guess the historical figure" game can pull a
  Wikipedia summary + image as the prompt.
- **In-game encyclopedia.** An RPG's bestiary can auto-populate from
  Wikipedia articles about real-world animals.

**CORS:** Yes (with `origin=*` parameter).

### 10. CoinGecko - `https://api.coingecko.com/api/v3`

**What it is.** Free crypto price API (no key for low-volume use).
Real-time prices, historical charts, market caps.

**Relevance:**

- **In-game economy tied to real markets.** A trading-sim game where the
  "gold" price actually tracks BTC/USD. Pull the price every 60s and update
  the in-game shop sprite's price label.
- **Vanity leaderboards.** "Your score is worth 0.0001 BTC" - convert
  in-game currency to a real-world reference for humor.

**CORS:** Yes.

### 11. Chess.com Pub API - `https://api.chess.com/pub`

**What it is.** Chess.com's public, key-free API: player profiles, game
history, puzzle of the day, live leaderboards.

**Relevance:**

- **Daily puzzle in your game's lobby.** Fetch the daily chess puzzle and
  render it on a chessboard made of TD Engine sprites. Players solve it for
  in-game currency.
- **Leaderboard imports.** Pull the top Chess.com players and use their
  names + avatars as "boss ghosts" in a chess-themed game.

**CORS:** Yes.

### 12. NASA APIs - `https://api.nasa.gov`

**What it is.** Astronomy Picture of the Day, Mars Rover photos, near-Earth
object tracking, etc. Free with a key (instant registration).

**Relevance:**

- **Space game backdrops.** APOD gives you a stunning new space image every
  day - load it as a full-screen background texture.
- **Mars terrain.** Mars Rover photos can be tilesets for a Mars-themed
  exploration game.

**CORS:** Yes.

---

## General guidance

### Caching

Browser-side: use `Cache-Control` headers from the API response to drive
`localStorage` caching. Example:

```javascript
async function fetchWithCache(url, ttlMs = 5 * 60 * 1000) {
    const key = 'tdcache:' + url;
    const cached = JSON.parse(localStorage.getItem(key) || 'null');
    if (cached && Date.now() - cached.t < ttlMs) return cached.v;
    const v = await (await fetch(url)).json();
    localStorage.setItem(key, JSON.stringify({ t: Date.now(), v }));
    return v;
}
```

### Rate limits

- Use `setInterval` instead of `requestAnimationFrame` for API polling -
  the engine renders at 60fps but most APIs only need to be polled every
  30-300 seconds.
- Back off on 429 responses. Most APIs honor `Retry-After`.

### Asset loading (textures from URLs)

When an API returns an image URL, fetch the bytes, decode via
`createImageBitmap`, copy the RGBA pixels into WASM memory, and call a
`td_create_texture`-style export. The engine's `Texture::create(config, pixels)`
takes raw RGBA bytes - perfect for this.

### Security

- Never put API keys in client-side code that ships to all users. Use a
  proxy endpoint on your own server for authenticated APIs.
- For read-only public APIs (NWS, Open-Meteo, PokéAPI, Datamuse, etc.),
  calling them directly from the browser is fine.

---

## Which APIs don't fit (and why)

- **Stripe / payment APIs.** Never call from the browser - use a server.
- **Email/SMS APIs (SendGrid, Twilio).** Same - server-side only.
- **Database APIs (Firebase, Supabase) with write access.** Use them via
  a server-side proxy or with strict row-level security policies.
- **Anything without CORS.** Will be blocked by the browser. Use a proxy.

---

## TL;DR

TD Engine's WASM bridge is intentionally HTTP-agnostic - the browser already
has `fetch()`. The workflow is: fetch in JS → parse JSON in JS → push the
results into the engine's ECS world via `Module.cwrap('td_*')`. Any
CORS-enabled REST API works. The 12 APIs surveyed above cover weather,
avatars, trivia, monsters, maps, words, code, encyclopedia, prices, chess,
and astronomy - enough to build dozens of games without bundling a single
data file.
