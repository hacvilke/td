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

### 13. Star Wars API (SWAPI) - `https://swapi.info/api`

**What it is.** The original "hello world" of public APIs. Fully free,
no key, no rate limit for reasonable use. Returns JSON for people, films,
starships, vehicles, species, planets - 200+ resources total. Mirrored at
`swapi.dev` and `swapi.info`; the `.info` mirror is faster and more reliable
as of 2024.

**Relevance to a game engine.**

- **Franchise tie-in games.** A Star Wars tower-defense or RPG can pull
  real character data (Luke's height, Vader's mass) and use it to scale
  sprites - Vader is literally 2.02m tall, so his sprite is 2x the player's.
- **Procedural starship roster.** `GET /starships` returns 36 ships with
  model, manufacturer, crew size, hyperdrive rating. Each becomes a
  `SpriteComponent` + a `StatsComponent` (crew = HP, hyperdrive = speed).
- **Planet tilesets.** `GET /planets` returns terrain/climate strings
  ("desert", "tundra") - use them to pick which TD Engine tile texture
  to load for that level.

**Integration sketch (starship roster):**

```javascript
const td_create = Module.cwrap('td_create_entity', 'number', ['string']);
const td_set_spr = Module.cwrap('td_entity_set_sprite', null,
    ['number','number','number','number','number','number','number']);
const td_set_pos = Module.cwrap('td_entity_set_position', null,
    ['number','number','number']);

const res = await fetch('https://swapi.info/api/starships');
const { results } = await res.json();
results.forEach((ship, i) => {
    const ent = td_create(ship.name);
    td_set_pos(ent, 64 + (i % 8) * 96, 64 + Math.floor(i / 8) * 96);
    // Hyperdrive rating 1-5 → sprite scale 0.5-1.5.
    const scale = 1.6 - Math.min(parseFloat(ship.hyperdrive_rating) || 4, 5);
    td_set_spr(ent, 64, 64, scale, scale, 1, 1, 1);
});
```

**CORS:** Yes.

### 14. An API of Ice And Fire - `https://anapioficeandfire.com/api`

**What it is.** Free, key-free JSON API for the Game of Thrones universe:
books, characters, houses. 2000+ characters with allegiances, titles,
spouse, died-in-book markers. Built and hosted by Joey Hoer.

**Relevance to a game engine.**

- **House-based strategy game.** `GET /houses` returns each house's name,
  words ("Winter is Coming"), seat, region, coat-of-arms URL. Each house
  becomes a faction in a TD Engine RTS - the words become the faction's
  tooltip, the region picks the map biome.
- **Character-driven RPG.** Pull a character's POV-book count and use it as
  their level. Characters marked `died` are removed from the roster.
- **Allegiance graph.** Each character has a `allegiances` array of house
  URLs - build a politics/faction map for a Game-of-Thrones-style visual
  novel.

**CORS:** Yes.

### 15. Harry Potter API - `https://hp-api.onrender.com`

**What it is.** Free, key-free JSON API listing Harry Potter characters
(with house, wand, patronus), spells, and houses. Hosted on Render's
free tier so it can be slow on first hit (cold start ~10s).

**Relevance to a game engine.**

- **Wand dueling game.** `GET /spells` returns 150+ spells with name +
  type (charm, jinx, curse). Player draws a gesture → engine matches it
  to a spell → JS fetches the spell's name + type from the API to display
  as the projectile's label.
- **House-sorted lobby.** Fetch characters by house (`/house/gryffindor`)
  and use them as preset bot opponents in a multiplayer lobby - each bot's
  sprite is tinted with the house color.
- **Patronus mini-game.** Each character's `patronus` field is a string
  ("stag", "otter", "doe"). Match it to a TD Engine sprite to render
  the player's patronus.

**CORS:** Yes.

### 16. DiceBear Avatars - `https://api.dicebear.com`

**What it is.** Free, key-free avatar generation API. Pass a `seed` string
and a style name (`avataaars`, `bottts`, `pixel-art`, `identicon`, etc.) and
get back an SVG, PNG, or JSON avatar. 30+ art styles. Same seed → same avatar,
so it's deterministic per user.

**Relevance to a game engine.**

- **Procedural NPC portraits.** Generate 100 NPCs by hashing their name →
  seed → fetch PNG → upload to the engine as a Texture. Each NPC gets a
  unique, recognizable face without shipping any portrait art.
- **Multiplayer avatars without uploads.** Players pick a name; the engine
  fetches `dicebear.com/9.x/bottts/png?seed=NAME` and uses that as their
  in-game avatar. No file upload, no moderation needed.
- **Identicon minimap icons.** Use the `identicon` style for low-detail
  geometric icons - perfect for minimap blips that need to be unique but
  don't need to look like faces.

**Integration sketch (NPC portrait textures):**

```javascript
async function loadDiceBearTexture(seed, style = 'pixel-art', size = 64) {
    const url = `https://api.dicebear.com/9.x/${style}/png?seed=${encodeURIComponent(seed)}&size=${size}`;
    const blob = await (await fetch(url)).blob();
    const bitmap = await createImageBitmap(blob);
    const canvas = new OffscreenCanvas(bitmap.width, bitmap.height);
    const ctx = canvas.getContext('2d');
    ctx.drawImage(bitmap, 0, 0);
    const { data } = ctx.getImageData(0, 0, bitmap.width, bitmap.height);
    const ptr = Module._malloc(data.length);
    Module.HEAPU8.set(data, ptr);
    const texId = Module.ccall('td_create_texture', 'number',
        ['number','number','number','number'],
        [bitmap.width, bitmap.height, 4, ptr]);
    Module._free(ptr);
    return texId;
}
```

**CORS:** Yes.

### 17. Speedrun.com API - `https://www.speedrun.com/api/v1`

**What it is.** Free, key-free REST API for the speedrun.com leaderboard
database. Returns games, categories, runs (with player, time, video URL),
players, levels, and recent world records. 30,000+ games indexed.

**Relevance to a game engine.**

- **"Ghost" replays in a racing game.** Fetch the top 10 runs for a track
  in your game (if you've submitted it to speedrun.com) and use the run
  times to spawn ghost opponents at the appropriate pace.
- **In-game leaderboard browser.** Build a UI overlay that lists the top
  runs for the player's current game, with player name + flag + time.
  Tapping a row opens the run's video URL in a new tab.
- **Daily challenge seeding.** Use `GET /leaderboards/:game/:category`
  to find the current world record; the player's daily challenge is to
  beat 80% of the WR time.

**CORS:** Yes.

### 18. TheMealDB - `https://www.themealdb.com/api/json/v1/1`

**What it is.** Free, key-free recipe database (test key `1`). Returns
meal names, instructions, ingredient lists, and high-res meal photo URLs.
Hundreds of recipes from around the world. No auth - just use the literal
key `1` in the URL.

**Relevance to a game engine.**

- **Cooking game.** A restaurant sim can fetch a real recipe per in-game
  day, render the meal photo as a sprite, and use the ingredient list as
  the player's prep checklist. Customer orders pull from `random.php`.
- **Food-art tileset.** `filter.php?c=Seafood` returns dozens of meal
  photos - download them all once and use as a sprite sheet for a match-3
  cooking game (each tile is a dish).
- **Culinary trivia.** Use the meal's `strArea` ("Italian", "Japanese")
  as a category tag in a "guess the cuisine" minigame.

**CORS:** Yes.

### 19. Imgflip Meme API - `https://api.imgflip.com`

**What it is.** Free, key-free (anonymous read; free account for captioning).
`GET /get_memes` returns the top 100 most popular meme templates with their
blank image URLs and box counts. `POST /caption_image` (needs account)
captions a template and returns a finished meme PNG URL.

**Relevance to a game engine.**

- **Meme-driven party game.** A Jackbox-style game where each round
  fetches a random meme template, players caption it via keyboard input,
  and the engine renders the captions as text sprites over the meme image.
- **Procedural NPC dialogue.** Caption a meme template with each NPC's
  line of dialogue; the resulting image becomes the NPC's portrait
  sprite. Memes-as-NPCs is a legitimate comedic art style (cf. *Meme Run*).
- **Loading-screen entertainment.** While the WASM module downloads,
  fetch and display a few captioned memes. The engine's SpriteBatch can
  render the meme image + a typewriter text overlay.

**CORS:** Yes (GET). The POST `caption_image` endpoint also works from
the browser if you have an Imgflip account.

### 20. Advice Slip API - `https://api.adviceslip.com`

**What it is.** Free, key-free API that returns a random "advice slip"
(slip = single piece of advice, ~12 words). `GET /advice` returns one
random slip; `GET /advice/:id` returns a specific one. ~300 slips total.

**Relevance to a game engine.**

- **Loading-screen tips.** Every loading screen fetches a new slip and
  shows it as a "Tip: ..." text sprite. Players see something fresh each
  time without you writing 300 tips yourself.
- **Wise-old-man NPC.** A hermit NPC in your RPG speaks only in API advice.
  Each interaction calls `/advice` and the slip becomes his dialogue line.
- **Death-screen flavor text.** When the player dies, fetch a slip and
  show it as the epitaph. "Wear sunscreen" has never hit harder.

**CORS:** Yes.

### 21. Openverse - `https://api.openverse.org/v1`

**What it is.** Free, key-free (anonymous tier; register for higher
limits) aggregator of CC-licensed and public-domain media. Searches
across Flickr, Wikimedia, Europeana, SoundCloud, ccMixter, and dozens
more. Returns image and audio results with license metadata.

**Relevance to a game engine.**

- **Royalty-free texture art.** `GET /images?q=stone+texture&license=cc0`
  returns hundreds of CC0 stone textures. Fetch the JPEG, decode to RGBA,
  push into `Texture::create`. No more shipping textures in your repo.
- **CC-licensed background music.** `GET /audio?q=chiptune&length=short`
  returns short audio files. Download as ArrayBuffer, decode via Web Audio's
  `decodeAudioData`, and feed the float32 samples to the engine's Mixer.
- **License-safe asset browser.** Build an in-game asset picker that
  surfaces only CC0 assets - ship your game without worrying about
  attribution tracking for the 200 textures you embedded.

**CORS:** Yes.

### 22. JokeAPI - `https://v2.jokeapi.dev/joke`

**What it is.** Free, key-free joke API. Returns jokes by category
(Programming, Misc, Dark, Pun, Spooky, Christmas) with content filtering
(safe-mode strips NSFW, religious, political). Supports single-part and
two-part (setup + delivery) jokes.

**Relevance to a game engine.**

- **Comedy NPC.** A jester character tells a new joke every interaction.
  Two-part jokes work especially well: the setup is the first dialogue
  box, the delivery is the punchline after a 1-second beat.
- **Loading-screen jokes.** Show a programming joke while the WASM
  module downloads. Filter to `Programming` category + `safe-mode`.
- **Joke-book item.** An in-game "Joke Book" item the player can read;
  each page turn fetches a new joke. The book's UI is just a TD Engine
  SpriteBatch + text layer.

**CORS:** Yes.

### 23. ZenQuotes - `https://zenquotes.io/api`

**What it is.** Free, key-free inspirational quote API. `GET /random`
returns one random quote with author; `GET /quotes` returns 50 random
quotes; `GET /today` returns the quote of the day. Replaces the
now-defunct Quotable.io.

**Relevance to a game engine.**

- **Loading screen "quote of the day".** Same as Advice Slip but with
  named authors - feels more literary. Pull once at boot, cache in
  `localStorage`, refresh daily.
- **Tombstone epitaphs.** A roguelike where each dead run shows a
  philosophical quote on the tombstone.
- **Wisdom-granting items.** A "Book of Wisdom" item that, when read,
  displays a random quote in a modal overlay rendered by the engine.

**CORS:** Yes.

### 24. Freesound - `https://freesound.org/apiv2`

**What it is.** The largest CC-licensed sound effects library on the
web. 800,000+ sounds (footsteps, gunshots, ambient loops, music stabs).
Free OAuth2 token required (instant registration). Search, preview
(low-quality MP3), and download (full-quality WAV/FLAC) endpoints.

**Relevance to a game engine.**

- **Procedural SFX library.** A game with hundreds of footstep variants,
  gunshots, and UI clicks would normally ship a 50MB audio bundle. With
  Freesound, fetch on demand: `GET /search/text/?query=footstep+wood`
  returns 30 sounds, the engine plays whichever one matches the player's
  current surface.
- **Crowd-sourced ambient beds.** `GET /search/text/?query=forest+ambient`
  returns 30+ field recordings. Pick one per level for a procedural
  soundscape.
- **Foley minigame.** A "foley artist" game where the player matches
  real recorded sounds to on-screen actions.

**Note:** Requires an OAuth2 bearer token. Do NOT embed the token in
client-side code that ships to all users - proxy through your own
server, or pre-fetch the sounds at build time and bundle them. See the
"Security" section below.

**CORS:** Yes.

### 25. Unsplash - `https://api.unsplash.com`

**What it is.** High-quality stock photography (4M+ images) from
professional photographers. Free OAuth2 Client-ID required (instant
registration). 50 requests/hour anonymous, 5000/hour with a token.

**Relevance to a game engine.**

- **Photoreal backdrops.** A visual novel set in real-world locations -
  `GET /search/photos?query=paris+cafe` returns gorgeous 4K photos. Load
  each as a full-screen background Texture.
- **Procedural card art.** A trading-card game where each card's art is
  a curated Unsplash photo matching its keyword. "Mountain" card →
  Unsplash mountain photo → card art.
- **Real-world texture source.** `GET /search/photos?query=brick+wall+texture`
  → use as the engine's tile texture for a brick wall.

**Note:** Same as Freesound - requires a Client-ID. Proxy or pre-fetch
at build time; don't ship the key in client JS.

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
CORS-enabled REST API works. The 25 APIs surveyed above cover:

| # | Domain | APIs |
|---|--------|------|
| 1-2 | Game platform helpers | News Targeted (Roblox/Discord), GitHub |
| 3-4 | Real-world weather | NWS, Open-Meteo |
| 5 | Trivia / quiz content | OpenTriviaDatabase |
| 6 | Monster bestiary | PokeAPI |
| 7 | Real-world maps | OpenStreetMap |
| 8 | Word games | Datamuse |
| 9 | Encyclopedia | Wikipedia |
| 10 | In-game economy | CoinGecko |
| 11 | Puzzles | Chess.com |
| 12 | Space art | NASA |
| 13 | Sci-fi franchise data | SWAPI (Star Wars) |
| 14 | Fantasy franchise data | An API of Ice And Fire (GoT) |
| 15 | Magic-school franchise | Harry Potter API |
| 16 | Procedural avatars | DiceBear |
| 17 | Leaderboards / replays | Speedrun.com |
| 18 | Cooking game assets | TheMealDB |
| 19 | Meme generation | Imgflip |
| 20 | Loading-screen tips | Advice Slip |
| 21 | CC0 media library | Openverse |
| 22 | Jokes / comedy NPC | JokeAPI |
| 23 | Inspirational quotes | ZenQuotes |
| 24 | CC-licensed SFX | Freesound (free key) |
| 25 | High-quality photography | Unsplash (free key) |

All 25 are CORS-enabled (so they work directly from the browser). 23 of 25
are key-free; the remaining 2 (Freesound, Unsplash) need a free OAuth2 token
that you should proxy through your own server rather than embed in client
JS. That's enough franchise data, art, audio, recipes, memes, jokes, and
real-world context to build dozens of games without bundling a single data
file.
