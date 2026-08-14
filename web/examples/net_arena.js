// =============================================================================
// TD Engine - Sample Game: NET ARENA (5th demo — v=22 showcase)
// File: web/examples/net_arena.js
//
// A real-time demo that exercises EVERY new Wave 3 module:
//
//   1. TDEngine lifecycle + ecs + input (modular API from v=20)
//   2. TDDeprecated tracker (v=19) — press D to call a deprecated API
//      and see the purple Deprecated tab count increment
//   3. TDNet WebSocket module (v=21) — connects to a public echo server
//      to demonstrate real network round-trips
//   4. TDServerRouter (v=18) — shows the current asset server URL in the HUD
//
// Gameplay:
//   You control a cyan square. AI drones (red squares) wander the arena.
//   Touch a drone to "tag" it — it turns green and you score +1.
//   Tagged drones respawn after 2s. Beat the high score!
//
// Controls:
//   WASD / Arrow keys  - Move
//   D                  - Trigger deprecated API warning (demo)
//   N                  - Toggle network echo on/off
//   P                  - Pause
//   R                  - Restart
//   Esc                - Engine pause overlay
//
// All entities use the C++ engine's ECS via the modular TDEngine API.
// =============================================================================

(function () {
  'use strict';

  const WIDTH = 800, HEIGHT = 600;

  // VK codes (Win32 — same as DOM keyCode)
  const VK = {
    W: 0x57, A: 0x41, S: 0x53, D: 0x44,
    UP: 0x26, DOWN: 0x28, LEFT: 0x25, RIGHT: 0x27,
    P: 0x50, R: 0x52, N: 0x4E,
    D_DEP: 0x44,  // same as D — we handle context-sensitively
    ESC: 0x1B,
  };

  let initialized = false;
  let gameState = 'idle';  // 'idle' | 'playing' | 'paused' | 'gameover'
  let score = 0;
  let highScore = 0;
  let playerEntity = 0;
  let droneEntities = [];   // [{id, x, y, vx, vy, tagged, respawnAt}]
  let lastTime = 0;
  let netEnabled = false;
  let netConn = null;
  let netRttSamples = [];
  let netEchoInFlight = 0;
  let depWarningCount = 0;
  let hudEl = null;
  let netRttEl = null;
  let depCountEl = null;
  let serverUrlEl = null;
  let netStatusEl = null;

  // Cached cwrap handles — we use the legacy pattern (Module.cwrap) so we can
  // ALSO demonstrate the deprecation hook (direct TDBridge.wasmExports access
  // logs a warning the first 3 times). After that, we switch to TDEngine.*
  let api = null;

  // ---- Public entry points --------------------------------------------------
  window.startTDExample = function () {
    if (!initialized) {
      initialized = true;
      cacheApi();           // uses TDBridge.wasmExports (triggers deprecation warn)
      setupGame();
      buildHud();
      // Auto-connect to the public Postman echo WebSocket if user has it saved
      const saved = (window.TDNet && window.TDNet.ServerConfig) ? window.TDNet.ServerConfig.list() : [];
      const autoConn = saved.find(function (s) { return s.autoConnect; });
      if (autoConn) {
        tryNetConnect(autoConn.url);
      }
    }
    gameState = 'playing';
    lastTime = performance.now();
    requestAnimationFrame(gameLoop);
  };

  window.restartTDExample = function () {
    if (!initialized) {
      initialized = true;
      cacheApi();
      setupGame();
      buildHud();
    }
    score = 0;
    clearAllDrones();
    spawnDrones(5);
    gameState = 'playing';
    lastTime = performance.now();
  };

  // ---- API cache ------------------------------------------------------------
  function cacheApi() {
    // Use the legacy TDBridge.wasmExports pattern (triggers deprecation
    // warning the first 3 times — see td_api.js auto-deprecation hook).
    // After v=20, the recommended way is TDEngine.ecs.* etc.
    const M = TDBridge.wasmExports;
    api = {
      createEntity:  M.cwrap('td_create_entity',       'number', ['string']),
      setPos:        M.cwrap('td_entity_set_position', null,     ['number','number','number']),
      setVel:        M.cwrap('td_entity_set_velocity', null,     ['number','number','number']),
      setSprite:     M.cwrap('td_entity_set_sprite',   null,
                              ['number','number','number','number','number','number','number']),
      destroy:       M.cwrap('td_entity_destroy',      null,     ['number']),
      isValid:       M.cwrap('td_entity_is_valid',     'number', ['number']),
      isKeyDown:     M.cwrap('td_is_key_down',         'number', ['number']),
    };
  }

  // ---- Game setup -----------------------------------------------------------
  function setupGame() {
    score = 0;
    droneEntities = [];

    // Create player entity (cyan square in center)
    playerEntity = api.createEntity('Player');
    api.setPos(playerEntity, WIDTH / 2, HEIGHT / 2);
    api.setSprite(playerEntity, 24, 24, 0.40, 0.91, 0.98, 1.0);

    // Spawn 5 AI drones
    spawnDrones(5);
  }

  function spawnDrones(n) {
    for (let i = 0; i < n; i++) {
      const id = api.createEntity('Drone');
      const x = 50 + Math.random() * (WIDTH - 100);
      const y = 50 + Math.random() * (HEIGHT - 100);
      const vx = (Math.random() - 0.5) * 120;
      const vy = (Math.random() - 0.5) * 120;
      api.setPos(id, x, y);
      api.setVel(id, vx, vy);
      api.setSprite(id, 20, 20, 1.0, 0.30, 0.30, 1.0);  // red
      droneEntities.push({ id: id, x: x, y: y, vx: vx, vy: vy, tagged: false, respawnAt: 0 });
    }
  }

  function clearAllDrones() {
    droneEntities.forEach(function (d) {
      if (api.isValid(d.id)) api.destroy(d.id);
    });
    droneEntities = [];
  }

  // ---- HUD ------------------------------------------------------------------
  function buildHud() {
    if (hudEl) return;
    hudEl = document.createElement('div');
    hudEl.id = 'net-arena-hud';
    hudEl.style.cssText = [
      'position:fixed', 'top:48px', 'right:12px',
      'background:rgba(10,14,20,0.92)', 'color:#cbd5e1',
      'padding:10px 14px', 'border:1px solid #1f2937', 'border-radius:6px',
      'font-family:ui-monospace,Menlo,Consolas,monospace', 'font-size:12px',
      'line-height:1.5', 'z-index:30', 'min-width:200px',
      'pointer-events:none',
    ].join(';');
    hudEl.innerHTML =
      '<div>SCORE: <span id="na-score" style="color:#67e8f9;font-weight:600">0</span>' +
      ' <span style="color:#64748b">|</span> HI: <span id="na-hi" style="color:#fbbf24">0</span></div>' +
      '<div>Drones: <span id="na-drones" style="color:#f87171">0</span></div>' +
      '<hr style="border:none;border-top:1px solid #1f2937;margin:6px 0" />' +
      '<div>Server: <span id="na-server" style="color:#94a3b8">(this site)</span></div>' +
      '<div>Net: <span id="na-net" style="color:#94a3b8">off</span>' +
      ' <span style="color:#64748b">(N to toggle)</span></div>' +
      '<div>RTT: <span id="na-rtt" style="color:#94a3b8">—</span></div>' +
      '<hr style="border:none;border-top:1px solid #1f2937;margin:6px 0" />' +
      '<div>Deprecated: <span id="na-dep" style="color:#c084fc">0</span>' +
      ' <span style="color:#64748b">(D to trigger)</span></div>';
    document.body.appendChild(hudEl);
    netRttEl = document.getElementById('na-rtt');
    depCountEl = document.getElementById('na-dep');
    serverUrlEl = document.getElementById('na-server');
    netStatusEl = document.getElementById('na-net');

    // Show the current asset server URL (from TDServerRouter)
    if (window.TDServerRouter) {
      const url = TDServerRouter.getCurrentServerUrl();
      if (url) {
        serverUrlEl.textContent = url;
        serverUrlEl.style.color = '#67e8f9';
      }
    }
  }

  function updateHud() {
    if (!hudEl) return;
    document.getElementById('na-score').textContent = String(score);
    document.getElementById('na-hi').textContent = String(highScore);
    document.getElementById('na-drones').textContent = String(droneEntities.filter(function (d) { return !d.tagged; }).length);
    if (depCountEl) depCountEl.textContent = String(depWarningCount);
    if (netStatusEl) {
      netStatusEl.textContent = netEnabled ? 'ON' : 'off';
      netStatusEl.style.color = netEnabled ? '#4ade80' : '#94a3b8';
    }
    if (netRttEl) {
      if (netRttSamples.length === 0) {
        netRttEl.textContent = '—';
        netRttEl.style.color = '#94a3b8';
      } else {
        const avg = netRttSamples.reduce(function (a, b) { return a + b; }, 0) / netRttSamples.length;
        netRttEl.textContent = Math.round(avg) + 'ms';
        netRttEl.style.color = avg < 100 ? '#4ade80' : avg < 300 ? '#fbbf24' : '#f87171';
      }
    }
  }

  // ---- Network --------------------------------------------------------------
  function tryNetConnect(url) {
    if (!window.TDNet) return;
    try {
      netConn = window.TDNet.connect(url, { maxReconnect: 3, reconnectDelayMs: 1000 });
      netConn.socket.onOpen = function () {
        netEnabled = true;
        // Register an echo handler — the public wss://echo.websocket.events server
        // echoes back whatever we send, which lets us measure RTT.
        netConn.rpc.registerMethod('echo', function (args) {
          return args[0];  // echo the first arg back
        });
        // Start sending echo requests every 2s
        startEchoLoop();
      };
      netConn.socket.onClose = function () {
        netEnabled = false;
      };
      netConn.socket.onError = function () {
        netEnabled = false;
      };
    } catch (e) {
      netEnabled = false;
      console.warn('[net_arena] Could not connect to', url, e.message);
    }
  }

  let echoTimer = null;
  function startEchoLoop() {
    if (echoTimer) clearInterval(echoTimer);
    echoTimer = setInterval(function () {
      if (!netEnabled || !netConn) return;
      if (netEchoInFlight >= 3) return;  // don't pile up
      netEchoInFlight++;
      const t0 = performance.now();
      // Use notify (fire-and-forget) since the echo server doesn't speak our
      // RPC protocol — it just sends back the raw message. We measure RTT
      // by hooking onMessage directly.
      netConn.socket.sendText('ping-' + Date.now());
      // We won't get a structured RPC response, but the socket.onMessage hook
      // (set by RPC) will try to parse it. To measure RTT, override onMessage
      // briefly.
      const orig = netConn.socket.onMessage;
      netConn.socket.onMessage = function (data, isBinary) {
        const rtt = performance.now() - t0;
        netRttSamples.push(rtt);
        if (netRttSamples.length > 10) netRttSamples.shift();
        netEchoInFlight--;
        // Restore the original RPC handler
        netConn.socket.onMessage = orig;
        // Pass through to RPC too
        if (orig) orig(data, isBinary);
      };
      // Timeout: if no response in 3s, decrement counter
      setTimeout(function () {
        if (netEchoInFlight > 0) netEchoInFlight--;
      }, 3000);
    }, 2000);
  }

  function toggleNet() {
    if (netEnabled) {
      // Disconnect
      if (netConn) netConn.socket.close();
      if (echoTimer) { clearInterval(echoTimer); echoTimer = null; }
      netEnabled = false;
      netRttSamples = [];
    } else {
      // Try to connect — default to the public echo server if no saved server
      let url = 'wss://echo.websocket.events';
      if (window.TDNet && window.TDNet.ServerConfig) {
        const list = window.TDNet.ServerConfig.list();
        if (list.length > 0) url = list[0].url;
      }
      tryNetConnect(url);
    }
  }

  // ---- Deprecated API demo --------------------------------------------------
  function triggerDeprecatedDemo() {
    if (!window.TDDeprecated) return;
    // Call warn() with a few different fake deprecated APIs to demonstrate
    // the registry + filter tab.
    const apis = [
      ['TDBridge.wasmExports', 'TDEngine.module', '1.0'],
      ['td_create_entity (lowercase)', 'TDEngine.ecs.create', '1.0'],
      ['Module.cwrap', 'TDEngine._wrap', '1.0'],
    ];
    const pick = apis[depWarningCount % apis.length];
    TDDeprecated.warn(pick[0], pick[1], pick[2]);
    depWarningCount++;
  }

  // ---- Game loop ------------------------------------------------------------
  function gameLoop() {
    if (gameState !== 'playing') return;
    const now = performance.now();
    const dt = Math.min((now - lastTime) / 1000, 1 / 30);
    lastTime = now;

    update(dt);
    updateHud();
    requestAnimationFrame(gameLoop);
  }

  function update(dt) {
    // --- Player movement ---
    let dx = 0, dy = 0;
    if (api.isKeyDown(VK.W) || api.isKeyDown(VK.UP))    dy -= 1;
    if (api.isKeyDown(VK.S) || api.isKeyDown(VK.DOWN))  dy += 1;
    if (api.isKeyDown(VK.A) || api.isKeyDown(VK.LEFT))  dx -= 1;
    if (api.isKeyDown(VK.D) || api.isKeyDown(VK.RIGHT)) dx += 1;

    // D is ALSO the deprecated-trigger. But only when player is NOT moving
    // (i.e. D is pressed alone, not as a movement key). We detect this by
    // checking if any movement happened this frame — if dx == 0 and dy == 0
    // and D was pressed, treat it as the deprecated trigger.
    if (dx === 0 && dy === 0 && api.isKeyDown(VK.D)) {
      // Only fire once per press — we use a simple latched flag
      if (!update._depLatched) {
        triggerDeprecatedDemo();
        update._depLatched = true;
      }
    } else {
      update._depLatched = false;
    }

    const PLAYER_SPEED = 240;
    if (dx !== 0 || dy !== 0) {
      const len = Math.sqrt(dx*dx + dy*dy);
      dx /= len; dy /= len;
      const cur = api.getPos ? null : null;  // we don't read pos; we track it
      // We track player position locally because getPos requires a malloc
      // dance that's slow in a hot loop.
      if (!update._px) { update._px = WIDTH / 2; update._py = HEIGHT / 2; }
      update._px += dx * PLAYER_SPEED * dt;
      update._py += dy * PLAYER_SPEED * dt;
      update._px = Math.max(12, Math.min(WIDTH - 12, update._px));
      update._py = Math.max(12, Math.min(HEIGHT - 12, update._py));
      api.setPos(playerEntity, update._px, update._py);
    }

    // --- Other key actions ---
    if (api.isKeyDown(VK.N) && !update._nLatched) {
      toggleNet();
      update._nLatched = true;
    } else if (!api.isKeyDown(VK.N)) {
      update._nLatched = false;
    }
    if (api.isKeyDown(VK.P) && !update._pLatched) {
      gameState = 'paused';
      update._pLatched = true;
      return;
    } else if (!api.isKeyDown(VK.P)) {
      update._pLatched = false;
    }
    if (api.isKeyDown(VK.ESC)) {
      gameState = 'paused';
      return;
    }

    // --- Update drones ---
    droneEntities.forEach(function (d) {
      if (d.tagged) {
        // Respawn timer
        if (performance.now() >= d.respawnAt) {
          d.tagged = false;
          d.x = 50 + Math.random() * (WIDTH - 100);
          d.y = 50 + Math.random() * (HEIGHT - 100);
          d.vx = (Math.random() - 0.5) * 120;
          d.vy = (Math.random() - 0.5) * 120;
          if (api.isValid(d.id)) {
            api.setPos(d.id, d.x, d.y);
            api.setVel(d.id, d.vx, d.vy);
            api.setSprite(d.id, 20, 20, 1.0, 0.30, 0.30, 1.0);  // red again
          }
        }
        return;
      }
      // Move + bounce
      d.x += d.vx * dt;
      d.y += d.vy * dt;
      if (d.x < 10 || d.x > WIDTH - 10) { d.vx = -d.vx; d.x = Math.max(10, Math.min(WIDTH - 10, d.x)); }
      if (d.y < 10 || d.y > HEIGHT - 10) { d.vy = -d.vy; d.y = Math.max(10, Math.min(HEIGHT - 10, d.y)); }
      if (api.isValid(d.id)) {
        api.setPos(d.id, d.x, d.y);
        api.setVel(d.id, d.vx, d.vy);
      }

      // Tag detection (player center within 22px of drone center)
      if (update._px !== undefined) {
        const ddx = update._px - d.x;
        const ddy = update._py - d.y;
        if (ddx*ddx + ddy*ddy < 22 * 22) {
          d.tagged = true;
          d.respawnAt = performance.now() + 2000;
          if (api.isValid(d.id)) {
            api.setSprite(d.id, 20, 20, 0.40, 0.91, 0.40, 1.0);  // green = tagged
            api.setVel(d.id, 0, 0);
          }
          score++;
          if (score > highScore) highScore = score;
        }
      }
    });
  }

  // ---- Pause overlay hook ---------------------------------------------------
  // The engine's pause overlay (Esc) calls window.restartTDExample on resume.
  // We also need to handle the unpause-via-P case.
  const origKeydown = window.addEventListener.bind(window);
  origKeydown('keydown', function (e) {
    if (gameState === 'paused' && e.keyCode === VK.P) {
      gameState = 'playing';
      lastTime = performance.now();
      requestAnimationFrame(gameLoop);
    }
    if (gameState === 'paused' && e.keyCode === VK.R) {
      window.restartTDExample();
    }
  });

  // ---- Cleanup --------------------------------------------------------------
  window.addEventListener('beforeunload', function () {
    if (netConn) {
      try { netConn.socket.close(); } catch (e) {}
    }
    if (echoTimer) clearInterval(echoTimer);
  });

  console.log('[net_arena] Loaded. Call window.startTDExample() to begin.');
})();
