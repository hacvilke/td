// =============================================================================
// TD Engine - Sample Game: VOID RUNNER
// File: web/examples/voidrunner.js
//
// A vertical space shooter that runs on the C++ TD Engine via WebAssembly.
// Demonstrates the full JS-on-WASM workflow:
//
//   - Dynamic entity creation/destruction (player, bullets, enemies, particles)
//   - SpriteComponent for rendering colored quads
//   - VelocityComponent + JS-side physics for movement
//   - AABB collision detection in JS (engine stores colliders)
//   - Wave-based enemy spawning with 3 enemy types
//   - Power-up system (rapid fire, triple shot, shield)
//   - Particle explosions on hit
//   - Parallax starfield (3 layers)
//   - Score + lives + game-over states
//
// Controls:
//   A / D or Left / Right  - Move horizontally
//   W / S or Up / Down     - Move vertically (bottom half of screen)
//   Space                  - Shoot (hold for auto-fire)
//   P                      - Pause
//   R                      - Restart (after game over)
//
// All entities are real ECS entities in the C++ engine's World. The engine's
// WASM main loop renders every frame; this JS file handles game rules.
// =============================================================================

(function () {
  'use strict';

  // Win32 VK codes (matches td::Key:: namespace in the C++ engine).
  const VK = {
    A: 0x41, D: 0x44, W: 0x57, S: 0x53,
    LEFT: 0x25, RIGHT: 0x27, UP: 0x26, DOWN: 0x28,
    SPACE: 0x20, ESC: 0x1B,
    P: 0x50, R: 0x52,
  };

  // ---- World constants (world units = pixels at 800x600) -------------------
  const WIDTH = 800, HEIGHT = 600;
  const PLAYER_W = 32, PLAYER_H = 32;
  const PLAYER_SPEED = 320;
  const BULLET_W = 4, BULLET_H = 14;
  const BULLET_SPEED = 600;
  const FIRE_COOLDOWN = 0.18;            // seconds between shots
  const ENEMY_BULLET_SPEED = 280;

  // ---- Game state (lives in JS; the engine just renders + simulates) ------
  let player = null;
  let bullets = [];          // player bullets
  let enemyBullets = [];
  let enemies = [];
  let particles = [];
  let powerups = [];
  let stars = [[], [], []];  // 3 parallax layers

  let score = 0;
  let lives = 3;
  let wave = 1;
  let waveTimer = 0;
  let waveEnemiesLeft = 0;
  let fireTimer = 0;
  let invulnTimer = 0;       // post-hit invulnerability
  let gameState = 'menu';    // 'menu' | 'playing' | 'paused' | 'gameover'
  let initialized = false;

  // Power-up timers (0 = inactive; >0 = seconds remaining)
  let powerRapid = 0;
  let powerTriple = 0;
  let powerShield = 0;

  // Cached cwrap handles (created once on init).
  let api = null;

  // ---- Exposed to web/index.html ------------------------------------------
  window.startTDExample = function () {
    if (!initialized) {
      initialized = true;
      cacheApi();
      buildStarfield();
    }
    resetGame();
    gameState = 'playing';
    startLoop();
  };

  window.restartTDExample = function () {
    if (!TDBridge || !TDBridge.ready) return;
    if (!initialized) {
      cacheApi();
      buildStarfield();
      initialized = true;
    }
    resetGame();
    gameState = 'playing';
  };

  // ---- API cache: get all cwrap handles once -------------------------------
  function cacheApi() {
    const M = TDBridge.wasmExports;
    api = {
      create:    M.cwrap('td_create_entity',       'number', ['string']),
      destroy:   M.cwrap('td_entity_destroy',      null,     ['number']),
      setPos:    M.cwrap('td_entity_set_position', null,     ['number', 'number', 'number']),
      getPos:    M.cwrap('td_entity_get_position', null,     ['number', 'number', 'number']),
      setVel:    M.cwrap('td_entity_set_velocity', null,     ['number', 'number', 'number']),
      setSpr:    M.cwrap('td_entity_set_sprite',   null,     ['number', 'number', 'number', 'number', 'number', 'number', 'number']),
      setCol:    M.cwrap('td_entity_set_collider', null,     ['number', 'number', 'number']),
      isKeyDown: M.cwrap('td_is_key_down',         'boolean', ['number']),
      _malloc:   M._malloc.bind(M),
      _free:     M._free.bind(M),
      HEAPF32:   M.HEAPF32,
    };
  }

  // ---- Helper: read entity position out of WASM ----------------------------
  function getPos(id) {
    const xPtr = api._malloc(8);
    const yPtr = api._malloc(8);
    try {
      api.getPos(id, xPtr, yPtr);
      return {
        x: api.HEAPF32[xPtr >> 2],
        y: api.HEAPF32[yPtr >> 2],
      };
    } finally {
      api._free(xPtr);
      api._free(yPtr);
    }
  }

  // ---- Starfield: 3 layers of small white squares at different speeds -----
  function buildStarfield() {
    // Layer 0: far (slow, small, dim)
    // Layer 1: mid
    // Layer 2: near (fast, big, bright)
    const counts = [60, 40, 20];
    const sizes  = [1, 2, 3];
    const speeds = [20, 50, 110];
    const bright = [0.35, 0.6, 1.0];
    for (let layer = 0; layer < 3; layer++) {
      for (let i = 0; i < counts[layer]; i++) {
        const id = api.create('star');
        const x = Math.random() * WIDTH;
        const y = Math.random() * HEIGHT;
        const s = sizes[layer];
        api.setPos(id, x, y);
        api.setSpr(id, s, s, bright[layer], bright[layer], bright[layer], 1);
        stars[layer].push({ id, x, y, speed: speeds[layer] });
      }
    }
  }

  function updateStars(dt) {
    for (let layer = 0; layer < 3; layer++) {
      for (const s of stars[layer]) {
        s.y += s.speed * dt;
        if (s.y > HEIGHT + 2) {
          s.y = -2;
          s.x = Math.random() * WIDTH;
        }
        api.setPos(s.id, s.x, s.y);
      }
    }
  }

  // ---- Entity factories ----------------------------------------------------
  function makePlayer() {
    const id = api.create('Player');
    api.setPos(id, WIDTH / 2, HEIGHT - 80);
    api.setSpr(id, PLAYER_W, PLAYER_H, 0.2, 0.9, 1.0, 1.0);  // cyan
    api.setCol(id, PLAYER_W * 0.7, PLAYER_H * 0.7);
    return { id, x: WIDTH / 2, y: HEIGHT - 80, vx: 0, vy: 0 };
  }

  function makeBullet(x, y, vx, vy, isPlayer) {
    const id = api.create(isPlayer ? 'Bullet' : 'EnemyBullet');
    api.setPos(id, x, y);
    api.setVel(id, vx, vy);
    const col = isPlayer
      ? [1.0, 1.0, 0.3]   // yellow for player
      : [1.0, 0.4, 0.7];  // pink for enemy
    api.setSpr(id, BULLET_W, BULLET_H, col[0], col[1], col[2], 1);
    api.setCol(id, BULLET_W, BULLET_H);
    return { id, x, y, vx, vy, isPlayer, life: 2.5 };
  }

  // Enemy types: each has distinct HP, speed, color, score value, behavior.
  //   drone  — 1 HP, straight down, fast, 100 pts
  //   hunter — 2 HP, tracks player X, medium, 200 pts
  //   tank   — 4 HP, slow, shoots back, 400 pts
  function makeEnemy(type, x, y) {
    const id = api.create('Enemy_' + type);
    api.setPos(id, x, y);
    api.setCol(id, 28, 28);
    let hp, speed, color, scoreVal;
    switch (type) {
      case 'drone':
        hp = 1; speed = 140; color = [1.0, 0.45, 0.45]; scoreVal = 100;
        api.setSpr(id, 24, 24, color[0], color[1], color[2], 1);
        api.setCol(id, 24, 24);
        break;
      case 'hunter':
        hp = 2; speed = 90; color = [1.0, 0.65, 0.2]; scoreVal = 200;
        api.setSpr(id, 28, 28, color[0], color[1], color[2], 1);
        api.setCol(id, 28, 28);
        break;
      case 'tank':
        hp = 4; speed = 50; color = [0.7, 0.4, 1.0]; scoreVal = 400;
        api.setSpr(id, 36, 36, color[0], color[1], color[2], 1);
        api.setCol(id, 36, 36);
        break;
    }
    return { id, type, x, y, vx: 0, vy: speed, hp, maxHp: hp, color, scoreVal, shootTimer: 1.0 + Math.random() };
  }

  function makeParticle(x, y, vx, vy, r, g, b, life) {
    const id = api.create('Particle');
    const size = 3 + Math.random() * 3;
    api.setPos(id, x, y);
    api.setSpr(id, size, size, r, g, b, 1);
    return { id, x, y, vx, vy, life, maxLife: life, size, r, g, b };
  }

  function makePowerup(x, y, kind) {
    const id = api.create('Powerup_' + kind);
    api.setPos(id, x, y);
    api.setCol(id, 18, 18);
    let col;
    switch (kind) {
      case 'rapid':   col = [1.0, 0.85, 0.2]; break;
      case 'triple':  col = [0.4, 1.0, 0.5];  break;
      case 'shield':  col = [0.4, 0.7, 1.0];  break;
    }
    api.setSpr(id, 18, 18, col[0], col[1], col[2], 1);
    return { id, x, y, vx: 0, vy: 80, kind };
  }

  // ---- Reset / start -------------------------------------------------------
  function resetGame() {
    // Destroy everything from a previous run.
    if (player) { api.destroy(player.id); player = null; }
    for (const b of bullets)       api.destroy(b.id);
    for (const b of enemyBullets)  api.destroy(b.id);
    for (const e of enemies)       api.destroy(e.id);
    for (const p of particles)     api.destroy(p.id);
    for (const p of powerups)      api.destroy(p.id);
    bullets = []; enemyBullets = []; enemies = []; particles = []; powerups = [];

    score = 0;
    lives = 3;
    wave = 1;
    waveTimer = 0;
    waveEnemiesLeft = 0;
    fireTimer = 0;
    invulnTimer = 1.0;
    powerRapid = 0;
    powerTriple = 0;
    powerShield = 0;

    player = makePlayer();
    startWave(1);
  }

  function startWave(n) {
    wave = n;
    waveTimer = 2.0;  // 2-second delay before enemies spawn
    waveEnemiesLeft = 4 + n * 2;  // 6, 8, 10, ...
    if (n > 1) {
      console.log('[voidrunner] wave ' + n + ' — ' + waveEnemiesLeft + ' enemies incoming');
    }
  }

  function spawnEnemy() {
    const x = 40 + Math.random() * (WIDTH - 80);
    const y = -30;
    let type;
    const r = Math.random();
    if (wave >= 3 && r < 0.2)      type = 'tank';
    else if (wave >= 2 && r < 0.5) type = 'hunter';
    else                           type = 'drone';
    enemies.push(makeEnemy(type, x, y));
  }

  // ---- Main loop -----------------------------------------------------------
  let rafId = null;
  let lastTime = 0;

  function startLoop() {
    if (rafId) cancelAnimationFrame(rafId);
    lastTime = performance.now();
    rafId = requestAnimationFrame(loop);
  }

  function loop(now) {
    const dt = Math.min((now - lastTime) / 1000, 0.05);
    lastTime = now;
    if (gameState === 'playing') {
      update(dt);
    }
    // Always update stars + particles even when paused (looks nice).
    if (gameState !== 'menu') {
      updateStars(dt);
      updateParticles(dt);
    }
    rafId = requestAnimationFrame(loop);
  }

  // ---- Update: input, movement, collisions, spawning -----------------------
  function update(dt) {
    // --- Input -------------------------------------------------------------
    const left  = api.isKeyDown(VK.A) || api.isKeyDown(VK.LEFT);
    const right = api.isKeyDown(VK.D) || api.isKeyDown(VK.RIGHT);
    const up    = api.isKeyDown(VK.W) || api.isKeyDown(VK.UP);
    const down  = api.isKeyDown(VK.S) || api.isKeyDown(VK.DOWN);
    const shoot = api.isKeyDown(VK.SPACE);

    // Pause toggle (edge-triggered)
    if (api.isKeyDown(VK.P) && !update._pPrev) {
      gameState = 'paused';
      showOverlay('Paused', 'Press P to resume', false);
    }
    update._pPrev = api.isKeyDown(VK.P);

    if (gameState !== 'playing') return;

    // --- Player movement --------------------------------------------------
    let vx = 0, vy = 0;
    if (left)  vx -= 1;
    if (right) vx += 1;
    if (up)    vy -= 1;
    if (down)  vy += 1;
    // Normalize diagonal
    if (vx && vy) { const inv = 1 / Math.SQRT2; vx *= inv; vy *= inv; }
    player.x += vx * PLAYER_SPEED * dt;
    player.y += vy * PLAYER_SPEED * dt;
    // Clamp: player stays in bottom 2/3 of screen
    const pad = PLAYER_W / 2;
    player.x = Math.max(pad, Math.min(WIDTH - pad, player.x));
    player.y = Math.max(HEIGHT / 2, Math.min(HEIGHT - pad, player.y));
    api.setPos(player.id, player.x, player.y);

    // --- Firing -----------------------------------------------------------
    fireTimer -= dt;
    if (shoot && fireTimer <= 0) {
      const cd = powerRapid > 0 ? FIRE_COOLDOWN * 0.4 : FIRE_COOLDOWN;
      fireTimer = cd;
      fireBullet();
    }

    // --- Power-up timers --------------------------------------------------
    if (powerRapid > 0)   powerRapid   = Math.max(0, powerRapid - dt);
    if (powerTriple > 0)  powerTriple  = Math.max(0, powerTriple - dt);
    if (powerShield > 0)  powerShield  = Math.max(0, powerShield - dt);
    if (invulnTimer > 0)  invulnTimer  = Math.max(0, invulnTimer - dt);

    // --- Player bullets ---------------------------------------------------
    for (let i = bullets.length - 1; i >= 0; i--) {
      const b = bullets[i];
      b.x += b.vx * dt;
      b.y += b.vy * dt;
      b.life -= dt;
      api.setPos(b.id, b.x, b.y);
      if (b.y < -20 || b.life <= 0) {
        api.destroy(b.id);
        bullets.splice(i, 1);
      }
    }

    // --- Enemy bullets ----------------------------------------------------
    for (let i = enemyBullets.length - 1; i >= 0; i--) {
      const b = enemyBullets[i];
      b.x += b.vx * dt;
      b.y += b.vy * dt;
      b.life -= dt;
      api.setPos(b.id, b.x, b.y);
      if (b.y > HEIGHT + 20 || b.life <= 0) {
        api.destroy(b.id);
        enemyBullets.splice(i, 1);
      }
    }

    // --- Enemies ----------------------------------------------------------
    for (let i = enemies.length - 1; i >= 0; i--) {
      const e = enemies[i];
      // Behavior by type
      if (e.type === 'hunter') {
        const dx = player.x - e.x;
        e.vx = Math.sign(dx) * 60;
      }
      e.x += e.vx * dt;
      e.y += e.vy * dt;
      // Off-screen cleanup
      if (e.y > HEIGHT + 40) {
        api.destroy(e.id);
        enemies.splice(i, 1);
        waveEnemiesLeft = Math.max(0, waveEnemiesLeft - 1);
        continue;
      }
      // Enemy shooting (tanks only)
      if (e.type === 'tank') {
        e.shootTimer -= dt;
        if (e.shootTimer <= 0) {
          e.shootTimer = 1.5 + Math.random() * 1.0;
          const dx = player.x - e.x;
          const dy = player.y - e.y;
          const len = Math.hypot(dx, dy) || 1;
          enemyBullets.push(makeBullet(
            e.x, e.y + 18,
            (dx / len) * ENEMY_BULLET_SPEED,
            (dy / len) * ENEMY_BULLET_SPEED,
            false
          ));
        }
      }
      api.setPos(e.id, e.x, e.y);
    }

    // --- Wave spawning ----------------------------------------------------
    if (waveEnemiesLeft > 0 && enemies.length < 6 + wave) {
      waveTimer -= dt;
      if (waveTimer <= 0) {
        spawnEnemy();
        waveEnemiesLeft--;
        waveTimer = 0.6 + Math.random() * 0.4;
      }
    } else if (waveEnemiesLeft === 0 && enemies.length === 0) {
      // Wave cleared
      console.log('[voidrunner] wave ' + wave + ' cleared! +500 bonus');
      score += 500;
      startWave(wave + 1);
    }

    // --- Powerups (falling) ----------------------------------------------
    for (let i = powerups.length - 1; i >= 0; i--) {
      const p = powerups[i];
      p.y += p.vy * dt;
      api.setPos(p.id, p.x, p.y);
      if (p.y > HEIGHT + 20) {
        api.destroy(p.id);
        powerups.splice(i, 1);
      }
    }

    // --- Collisions: player bullets vs enemies ----------------------------
    for (let i = bullets.length - 1; i >= 0; i--) {
      const b = bullets[i];
      for (let j = enemies.length - 1; j >= 0; j--) {
        const e = enemies[j];
        if (Math.abs(b.x - e.x) < 16 && Math.abs(b.y - e.y) < 16) {
          api.destroy(b.id);
          bullets.splice(i, 1);
          e.hp--;
          // Hit flash: tint white briefly via sprite color change
          if (e.hp > 0) {
            api.setSpr(e.id, 28, 28, 1, 1, 1, 1);
            // Restore color next frame (we just re-set it on next iteration).
            setTimeout(() => {
              if (enemies[j]) api.setSpr(e.id, e.type === 'drone' ? 24 : e.type === 'hunter' ? 28 : 36,
                                         e.type === 'drone' ? 24 : e.type === 'hunter' ? 28 : 36,
                                         e.color[0], e.color[1], e.color[2], 1);
            }, 60);
          } else {
            // Enemy destroyed
            score += e.scoreVal;
            spawnExplosion(e.x, e.y, e.color[0], e.color[1], e.color[2], e.type === 'tank' ? 20 : 10);
            // 10% chance to drop a powerup
            if (Math.random() < 0.12) {
              const kinds = ['rapid', 'triple', 'shield'];
              const kind = kinds[Math.floor(Math.random() * kinds.length)];
              powerups.push(makePowerup(e.x, e.y, kind));
            }
            api.destroy(e.id);
            enemies.splice(j, 1);
          }
          break;
        }
      }
    }

    // --- Collisions: enemy bullets vs player ------------------------------
    if (invulnTimer <= 0) {
      for (let i = enemyBullets.length - 1; i >= 0; i--) {
        const b = enemyBullets[i];
        if (Math.abs(b.x - player.x) < 16 && Math.abs(b.y - player.y) < 16) {
          api.destroy(b.id);
          enemyBullets.splice(i, 1);
          hitPlayer();
          break;
        }
      }
      // Enemies ramming player
      for (let i = enemies.length - 1; i >= 0; i--) {
        const e = enemies[i];
        if (Math.abs(e.x - player.x) < 22 && Math.abs(e.y - player.y) < 22) {
          spawnExplosion(e.x, e.y, e.color[0], e.color[1], e.color[2], 12);
          api.destroy(e.id);
          enemies.splice(i, 1);
          waveEnemiesLeft = Math.max(0, waveEnemiesLeft - 1);
          hitPlayer();
          break;
        }
      }
    }

    // --- Collisions: player vs powerups -----------------------------------
    for (let i = powerups.length - 1; i >= 0; i--) {
      const p = powerups[i];
      if (Math.abs(p.x - player.x) < 20 && Math.abs(p.y - player.y) < 20) {
        if (p.kind === 'rapid')   powerRapid   = 8;
        if (p.kind === 'triple')  powerTriple  = 10;
        if (p.kind === 'shield')  powerShield  = 6;
        api.destroy(p.id);
        powerups.splice(i, 1);
        console.log('[voidrunner] picked up ' + p.kind);
      }
    }

    // --- HUD update -------------------------------------------------------
    updateHUD();
  }

  function fireBullet() {
    if (powerTriple > 0) {
      bullets.push(makeBullet(player.x, player.y - 16, 0,   -BULLET_SPEED, true));
      bullets.push(makeBullet(player.x - 4, player.y - 14, -80, -BULLET_SPEED + 20, true));
      bullets.push(makeBullet(player.x + 4, player.y - 14,  80, -BULLET_SPEED + 20, true));
    } else {
      bullets.push(makeBullet(player.x, player.y - 16, 0, -BULLET_SPEED, true));
    }
  }

  function hitPlayer() {
    if (powerShield > 0) {
      powerShield = 0;
      spawnExplosion(player.x, player.y, 0.4, 0.7, 1.0, 18);
      invulnTimer = 1.0;
      console.log('[voidrunner] shield absorbed hit');
      return;
    }
    lives--;
    spawnExplosion(player.x, player.y, 1.0, 0.4, 0.4, 20);
    invulnTimer = 2.0;
    if (lives <= 0) {
      gameOver();
    } else {
      console.log('[voidrunner] lost a life — ' + lives + ' remaining');
    }
  }

  function spawnExplosion(x, y, r, g, b, count) {
    for (let i = 0; i < count; i++) {
      const ang = Math.random() * Math.PI * 2;
      const spd = 40 + Math.random() * 140;
      const life = 0.4 + Math.random() * 0.4;
      particles.push(makeParticle(x, y, Math.cos(ang) * spd, Math.sin(ang) * spd, r, g, b, life));
    }
  }

  function updateParticles(dt) {
    for (let i = particles.length - 1; i >= 0; i--) {
      const p = particles[i];
      p.x += p.vx * dt;
      p.y += p.vy * dt;
      p.vx *= 0.92;  // drag
      p.vy *= 0.92;
      p.life -= dt;
      const fade = Math.max(0, p.life / p.maxLife);
      api.setPos(p.id, p.x, p.y);
      api.setSpr(p.id, p.size, p.size, p.r * fade, p.g * fade, p.b * fade, fade);
      if (p.life <= 0) {
        api.destroy(p.id);
        particles.splice(i, 1);
      }
    }
  }

  function gameOver() {
    gameState = 'gameover';
    spawnExplosion(player.x, player.y, 1.0, 0.3, 0.3, 40);
    api.destroy(player.id);
    player = null;
    showOverlay('GAME OVER', 'Score: ' + score + '  |  Wave: ' + wave + ' — press R to restart', true);
    console.log('[voidrunner] GAME OVER — final score ' + score + ' (wave ' + wave + ')');
  }

  // ---- HUD (DOM overlay, not engine entities) ------------------------------
  let hudEl = null;
  function updateHUD() {
    if (!hudEl) {
      hudEl = document.getElementById('vr-hud');
      if (!hudEl) {
        hudEl = document.createElement('div');
        hudEl.id = 'vr-hud';
        hudEl.style.cssText =
          'position:fixed; top:12px; left:12px; color:#00D4FF; font-family:Menlo,Consolas,monospace;' +
          'font-size:14px; line-height:1.6; text-shadow:0 0 8px rgba(0,212,255,0.5);' +
          'pointer-events:none; z-index:50; user-select:none;';
        document.body.appendChild(hudEl);
      }
    }
    const powers = [];
    if (powerRapid  > 0) powers.push('RAPID '   + powerRapid.toFixed(1) + 's');
    if (powerTriple > 0) powers.push('TRIPLE '  + powerTriple.toFixed(1) + 's');
    if (powerShield > 0) powers.push('SHIELD '  + powerShield.toFixed(1) + 's');
    const powerLine = powers.length ? '  |  <span style="color:#FFD24A">' + powers.join(' · ') + '</span>' : '';
    hudEl.innerHTML =
      'SCORE <span style="color:#fff">' + score.toString().padStart(6, '0') + '</span>' +
      '   LIVES <span style="color:#ff6b6b">' + '●'.repeat(Math.max(0, lives)) + '</span>' +
      '   WAVE <span style="color:#fff">' + wave + '</span>' +
      powerLine;
  }

  function showOverlay(title, subtitle, isGameOver) {
    let el = document.getElementById('vr-overlay');
    if (!el) {
      el = document.createElement('div');
      el.id = 'vr-overlay';
      el.style.cssText =
        'position:fixed; inset:0; display:flex; align-items:center; justify-content:center;' +
        'background:rgba(5,8,15,0.85); z-index:60; font-family:Menlo,Consolas,monospace;' +
        'text-align:center; color:#fff;';
      document.body.appendChild(el);
    }
    el.innerHTML =
      '<div style="max-width:480px; padding:32px;">' +
        '<h1 style="margin:0 0 12px; font-size:42px; color:#00D4FF; text-shadow:0 0 20px rgba(0,212,255,0.6); letter-spacing:4px;">' +
          title +
        '</h1>' +
        '<p style="margin:0; opacity:0.85; line-height:1.6;">' + subtitle + '</p>' +
      '</div>';
    el.style.display = 'flex';

    if (isGameOver) {
      // Watch for R key to restart
      const onKey = (e) => {
        if (e.keyCode === VK.R) {
          window.removeEventListener('keydown', onKey);
          el.style.display = 'none';
          window.restartTDExample();
        }
      };
      window.addEventListener('keydown', onKey);
    } else {
      // Pause: hide on P press
      const onKey = (e) => {
        if (e.keyCode === VK.P) {
          window.removeEventListener('keydown', onKey);
          el.style.display = 'none';
          gameState = 'playing';
          update._pPrev = true;
        }
      };
      window.addEventListener('keydown', onKey);
    }
  }

  // Expose state for debugging
  window.VR = { getState: () => ({ score, lives, wave, gameState }) };
})();
