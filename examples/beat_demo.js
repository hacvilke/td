// =============================================================================
// TD Engine - Sample Game: BEAT DEMO
// File: web/examples/beat_demo.js
//
// A minimal rhythm game demonstrating the BeatTracker system. Notes fall from
// the top of the screen on every beat; the player taps Space to "catch" them.
// Hitting on-beat builds combo + scores bonus; hitting off-beat resets combo.
//
// This demo showcases:
//   - td_beat_start / td_beat_is_on_beat / td_beat_register_hit
//   - TDBridge.onBeat(callback) for spawning notes on each beat tick
//   - The two-half-window on-beat detection from docs/RHYTHM_MECHANICS.md
//   - Combo tracking + best-combo scoring
//   - Dynamic entity creation/destruction (notes spawn + despawn)
//
// Controls:
//   Space  - Hit (catch the falling note)
//   R      - Restart (after game over)
//   Esc    - Engine pause overlay
//
// All entities are real ECS entities in the C++ engine's World. The engine's
// WASM main loop renders every frame; this JS file handles game rules.
// =============================================================================

(function () {
  'use strict';

  // Win32 VK codes (matches td::Key:: namespace in the C++ engine).
  const VK = {
    SPACE: 0x20,
    ESC:   0x1B,
    R:     0x52,
  };

  // ---- World constants -----------------------------------------------------
  const WIDTH = 800, HEIGHT = 600;
  const BPM = 100;                   // beats per minute (spb = 0.6s)
  const WINDOW_HALF = 0.15;          // 150ms half-window (300ms total)
  const NOTE_W = 60, NOTE_H = 20;
  const TARGET_Y = HEIGHT - 80;      // horizontal "hit line"
  const NOTE_TRAVEL_TIME = 1.8;      // seconds for a note to fall from top to target
  const NOTE_SPAWN_Y = -NOTE_H;      // off-screen above
  const NOTE_FALL_SPEED = (TARGET_Y - NOTE_SPAWN_Y) / NOTE_TRAVEL_TIME;

  // ---- Game state (lives in JS; the engine just renders + simulates) -----
  let songEntity = 0;
  let notes = [];                    // [{entityId, spawnBeat, x, y}]
  let particles = [];                // [{x, y, vx, vy, life, r, g, b}]
  let score = 0;
  let hits = 0;
  let misses = 0;
  let totalBeats = 0;
  let gameState = 'menu';            // 'menu' | 'playing' | 'gameover'
  let initialized = false;
  let beatFlashTimer = 0;            // visual pulse on each beat
  let hitFlashTimer = 0;             // visual flash on successful hit
  let missFlashTimer = 0;            // visual flash on miss

  // Cached cwrap handles (created once on init).
  let api = null;

  // ---- Exposed to web/index.html ------------------------------------------
  window.startTDExample = function () {
    if (!initialized) {
      initialized = true;
      cacheApi();
      setupGame();
    }
    if (gameState === 'gameover') {
      restartGame();
    }
    gameState = 'playing';
  };

  window.restartTDExample = function () {
    if (!initialized) {
      initialized = true;
      cacheApi();
      setupGame();
    }
    restartGame();
    gameState = 'playing';
  };

  // ---- API cache (cwrap handles are expensive to create; cache once) -----
  function cacheApi() {
    const M = TDBridge.wasmExports;
    api = {
      // Beat API
      beatStart:        M.cwrap('td_beat_start',           null,    ['number','number','number']),
      beatIsOnBeat:     M.cwrap('td_beat_is_on_beat',      'number',['number']),
      beatGetCount:     M.cwrap('td_beat_get_count',       'number',['number']),
      beatGetCombo:     M.cwrap('td_beat_get_combo',       'number',['number']),
      beatGetBestCombo: M.cwrap('td_beat_get_best_combo',  'number',['number']),
      beatRegisterHit:  M.cwrap('td_beat_register_hit',    'number',['number','number']),
      beatResetCombo:   M.cwrap('td_beat_reset_combo',     'number',['number']),
      beatSetBpm:       M.cwrap('td_beat_set_bpm',         null,    ['number','number']),
      // Entity API
      createEntity:     M.cwrap('td_create_entity',        'number',['string']),
      setPos:           M.cwrap('td_entity_set_position',  null,    ['number','number','number']),
      setSprite:        M.cwrap('td_entity_set_sprite',    null,
                                  ['number','number','number','number','number','number','number']),
      destroy:          M.cwrap('td_entity_destroy',       null,    ['number']),
      isValid:          M.cwrap('td_entity_is_valid',      'number',['number']),
      // Input
      isKeyDown:        M.cwrap('td_is_key_down',          'number',['number']),
    };
  }

  // ---- Setup ---------------------------------------------------------------
  function setupGame() {
    // Create the "song" entity that owns the BeatTracker.
    songEntity = api.createEntity('song');

    // Start beat tracking at 100 BPM with a 150ms half-window.
    api.beatStart(songEntity, BPM, WINDOW_HALF);

    // Register a JS callback to spawn a note on every beat tick.
    // (addFunction is wired up in TDBridge.onBeat; the engine fires this
    // callback from within BeatSystem::update on each beat fire.)
    TDBridge.onBeat(function (beatCount, beatTime) {
      totalBeats++;
      beatFlashTimer = 0.15;  // visual pulse duration
      if (gameState === 'playing') {
        spawnNote(beatCount);
      }
    });

    // Listen for the hit key.
    document.addEventListener('keydown', function (e) {
      if (e.keyCode === VK.SPACE) {
        e.preventDefault();
        if (gameState === 'playing') {
          handleHit();
        }
      } else if (e.keyCode === VK.R && gameState === 'gameover') {
        restartGame();
        gameState = 'playing';
      }
    });
  }

  // ---- Note spawning -------------------------------------------------------
  function spawnNote(beatCount) {
    // Spawn at a random x position in the playfield.
    const margin = 80;
    const x = margin + Math.random() * (WIDTH - 2 * margin);
    const id = api.createEntity('note');
    api.setPos(id, x, NOTE_SPAWN_Y);
    api.setSprite(id, NOTE_W, NOTE_H, 0.4, 0.9, 1.0, 1.0);  // cyan
    notes.push({ entityId: id, spawnBeat: beatCount, x: x, y: NOTE_SPAWN_Y });
  }

  // ---- Hit handling --------------------------------------------------------
  function handleHit() {
    if (!songEntity) return;

    // Check if we're on-beat using the engine's two-half-window detection.
    const onBeat = !!api.beatIsOnBeat(songEntity);

    // Find the closest note to the target line.
    let closestNote = null;
    let closestDist = Infinity;
    for (const note of notes) {
      const dist = Math.abs(note.y - TARGET_Y);
      if (dist < closestDist) {
        closestDist = dist;
        closestNote = note;
      }
    }

    if (onBeat && closestNote && closestDist < 40) {
      // Successful hit!
      const combo = api.beatRegisterHit(songEntity, /*strict=*/true);
      const bonus = 100 + combo * 10;
      score += bonus;
      hits++;
      hitFlashTimer = 0.2;

      // Destroy the caught note.
      api.destroy(closestNote.entityId);
      const idx = notes.indexOf(closestNote);
      if (idx >= 0) notes.splice(idx, 1);

      // Spawn hit particles.
      spawnParticles(closestNote.x, TARGET_Y, 12, 0.4, 1.0, 0.4);

      if (combo > 0 && combo % 10 === 0) {
        // Milestone combo bonus.
        score += 500;
      }
    } else {
      // Miss.
      api.beatResetCombo(songEntity);
      misses++;
      missFlashTimer = 0.2;
      spawnParticles(0, 0, 0, 0, 0, 0);  // no-op (kept for symmetry)
    }
  }

  // ---- Particle effects ----------------------------------------------------
  function spawnParticles(x, y, count, r, g, b) {
    for (let i = 0; i < count; i++) {
      const angle = (Math.PI * 2 * i) / count + Math.random() * 0.5;
      const speed = 80 + Math.random() * 120;
      particles.push({
        x: x, y: y,
        vx: Math.cos(angle) * speed,
        vy: Math.sin(angle) * speed,
        life: 0.4 + Math.random() * 0.3,
        maxLife: 0.7,
        r: r, g: g, b: b,
        size: 4 + Math.random() * 4,
      });
    }
  }

  // ---- Main update (called every animation frame via TDBridge hooks) -----
  // We piggyback on the engine's render loop by hooking into requestAnimationFrame
  // directly. The engine's fixed-step update (which fires BeatSystem::update
  // and our beat callback) runs independently inside td_init's main loop.

  let lastFrame = performance.now();
  function frame(now) {
    const dt = Math.min(0.1, (now - lastFrame) / 1000);
    lastFrame = now;

    if (gameState === 'playing') {
      // Move falling notes.
      for (const note of notes) {
        note.y += NOTE_FALL_SPEED * dt;
        api.setPos(note.entityId, note.x, note.y);
      }

      // Despawn notes that fell past the target line (player missed them).
      for (let i = notes.length - 1; i >= 0; i--) {
        if (notes[i].y > HEIGHT + NOTE_H) {
          api.destroy(notes[i].entityId);
          notes.splice(i, 1);
          misses++;
          api.beatResetCombo(songEntity);
          missFlashTimer = 0.2;
        }
      }

      // Update particles.
      for (const p of particles) {
        p.x += p.vx * dt;
        p.y += p.vy * dt;
        p.vy += 200 * dt;  // gravity
        p.life -= dt;
      }
      particles = particles.filter(p => p.life > 0);

      // Decay flash timers.
      if (beatFlashTimer > 0) beatFlashTimer -= dt;
      if (hitFlashTimer > 0)  hitFlashTimer  -= dt;
      if (missFlashTimer > 0) missFlashTimer -= dt;

      // Update HUD.
      updateHUD();
    }

    requestAnimationFrame(frame);
  }
  requestAnimationFrame(frame);

  // ---- HUD ----------------------------------------------------------------
  const hud = createHUD();
  function updateHUD() {
    const combo = api.beatGetCombo(songEntity);
    const bestCombo = api.beatGetBestCombo(songEntity);
    const beatCount = api.beatGetCount(songEntity);
    const accuracy = (hits + misses) > 0
      ? Math.round((hits / (hits + misses)) * 100)
      : 100;

    hud.score.textContent = 'Score: ' + score;
    hud.combo.textContent = 'Combo: ' + combo + (combo >= 10 ? ' 🔥' : '');
    hud.best.textContent = 'Best: ' + bestCombo;
    hud.beat.textContent = 'Beat: ' + beatCount + ' / ' + totalBeats;
    hud.acc.textContent = 'Accuracy: ' + accuracy + '%';
  }

  function createHUD() {
    let el = document.getElementById('beat-hud');
    if (el) return {
      score: el.querySelector('.hud-score'),
      combo: el.querySelector('.hud-combo'),
      best: el.querySelector('.hud-best'),
      beat: el.querySelector('.hud-beat'),
      acc: el.querySelector('.hud-acc'),
    };

    el = document.createElement('div');
    el.id = 'beat-hud';
    el.style.cssText =
      'position:fixed;top:10px;left:10px;color:#fff;font:14px monospace;' +
      'background:rgba(0,0,0,0.5);padding:8px 12px;border-radius:6px;' +
      'pointer-events:none;z-index:50;line-height:1.5;';
    el.innerHTML =
      '<div class="hud-score">Score: 0</div>' +
      '<div class="hud-combo">Combo: 0</div>' +
      '<div class="hud-best">Best: 0</div>' +
      '<div class="hud-beat">Beat: 0 / 0</div>' +
      '<div class="hud-acc">Accuracy: 100%</div>';
    document.body.appendChild(el);
    return {
      score: el.querySelector('.hud-score'),
      combo: el.querySelector('.hud-combo'),
      best: el.querySelector('.hud-best'),
      beat: el.querySelector('.hud-beat'),
      acc: el.querySelector('.hud-acc'),
    };
  }

  // ---- Restart -------------------------------------------------------------
  function restartGame() {
    // Destroy all existing notes.
    for (const note of notes) {
      api.destroy(note.entityId);
    }
    notes = [];
    particles = [];
    score = 0;
    hits = 0;
    misses = 0;
    totalBeats = 0;

    // Restart the beat tracker (resets beatCount + combo).
    api.beatStart(songEntity, BPM, WINDOW_HALF);
  }

  // ---- Cleanup on unload --------------------------------------------------
  window.addEventListener('beforeunload', function () {
    if (songEntity && api && api.isValid(songEntity)) {
      api.destroy(songEntity);
    }
  });

  // Log readiness.
  console.log('[beat_demo] Loaded. Call window.startTDExample() to begin.');
})();
