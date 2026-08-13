// =============================================================================
// TD Engine - Sample Game: Pong
// File: web/examples/pong.js
//
// A complete Pong game written in pure JavaScript that runs on the C++ TD
// Engine via WebAssembly. This demonstrates the JS-on-WASM workflow:
//
//   1. The C++ engine (Parts 1-6) is compiled to WASM (Part 7).
//   2. The JS bridge (wasm/js_bridge.js) loads the WASM module.
//   3. This JS file uses TDBridge to create entities, attach components, and
//      run game logic - all without writing a line of C++.
//
// The C++ engine handles rendering (SpriteBatch), the fixed-step game loop,
// and the ECS world. JavaScript handles game rules (scoring, ball reset,
// AI for the right paddle).
//
// Keys:
//   W / S       - Left paddle (player)
//   Up / Down   - Right paddle (AI or P2)
//   Space       - Pause / resume
//   Escape      - Show pause overlay
// =============================================================================

(function () {
  'use strict';

  // Win32 VK codes (matches td::Key:: namespace in the C++ engine).
  const VK = {
    W: 0x57, S: 0x53,
    UP: 0x26, DOWN: 0x28,
    SPACE: 0x20, ESC: 0x1B,
  };

  // Game constants (in world units = pixels at 800x600).
  const WIDTH = 800, HEIGHT = 600;
  const PADDLE_W = 15, PADDLE_H = 80;
  const BALL_SIZE = 12;
  const WALL_H = 10;
  const PADDLE_SPEED = 400;
  const BALL_SPEED = 300;
  const WIN_SCORE = 7;

  // Game state (lives in JS; the engine just renders + simulates).
  let leftPaddle, rightPaddle, ball, topWall, bottomWall;
  let leftScore = 0, rightScore = 0;
  let paused = false;
  let initialized = false;

  // ---------------------------------------------------------------------------
  // startTDExample - called by web/index.html when the user clicks Start.
  // Exposed on window so the HTML can find it.
  // ---------------------------------------------------------------------------
  window.startTDExample = function () {
    if (initialized) return;
    initialized = true;
    initGame();
  };

  window.restartTDExample = function () {
    if (!TDBridge || !TDBridge.ready) return;
    // Destroy existing entities and reset.
    if (leftPaddle)   leftPaddle.destroy();
    if (rightPaddle)  rightPaddle.destroy();
    if (ball)         ball.destroy();
    if (topWall)      topWall.destroy();
    if (bottomWall)   bottomWall.destroy();
    leftScore = 0; rightScore = 0;
    initGame();
  };

  function initGame() {
    if (!TDBridge || !TDBridge.ready) {
      console.warn('[pong] TDBridge not ready');
      return;
    }

    const Module = TDBridge.wasmExports;
    const td_create = Module.cwrap('td_create_entity', 'number', ['string']);
    const td_set_pos = Module.cwrap('td_entity_set_position', null, ['number', 'number', 'number']);
    const td_set_vel = Module.cwrap('td_entity_set_velocity', null, ['number', 'number', 'number']);
    const td_set_spr = Module.cwrap('td_entity_set_sprite', null, ['number', 'number', 'number', 'number', 'number', 'number', 'number']);
    const td_set_col = Module.cwrap('td_entity_set_collider', null, ['number', 'number', 'number']);

    // --- Left paddle (player) ----------------------------------------------
    {
      const id = td_create('LeftPaddle');
      td_set_pos(id, 30, HEIGHT / 2);
      td_set_vel(id, 0, 0);
      td_set_spr(id, PADDLE_W, PADDLE_H, 0.2, 0.8, 1.0, 1.0);  // cyan
      td_set_col(id, PADDLE_W, PADDLE_H);
      leftPaddle = { id, x: 30, y: HEIGHT / 2, vy: 0 };
    }

    // --- Right paddle (AI) --------------------------------------------------
    {
      const id = td_create('RightPaddle');
      td_set_pos(id, WIDTH - 30, HEIGHT / 2);
      td_set_vel(id, 0, 0);
      td_set_spr(id, PADDLE_W, PADDLE_H, 1.0, 0.4, 0.4, 1.0);  // red
      td_set_col(id, PADDLE_W, PADDLE_H);
      rightPaddle = { id, x: WIDTH - 30, y: HEIGHT / 2, vy: 0 };
    }

    // --- Ball ---------------------------------------------------------------
    {
      const id = td_create('Ball');
      td_set_pos(id, WIDTH / 2, HEIGHT / 2);
      const ang = (Math.random() - 0.5) * 0.5;
      const dir = Math.random() < 0.5 ? -1 : 1;
      td_set_vel(id, dir * BALL_SPEED * Math.cos(ang), BALL_SPEED * Math.sin(ang));
      td_set_spr(id, BALL_SIZE, BALL_SIZE, 1, 1, 1, 1);
      td_set_col(id, BALL_SIZE, BALL_SIZE);
      ball = { id, x: WIDTH / 2, y: HEIGHT / 2, vx: dir * BALL_SPEED * Math.cos(ang), vy: BALL_SPEED * Math.sin(ang) };
    }

    // --- Walls --------------------------------------------------------------
    {
      const id = td_create('TopWall');
      td_set_pos(id, WIDTH / 2, WALL_H / 2);
      td_set_spr(id, WIDTH, WALL_H, 0.4, 0.4, 0.4, 1.0);
      td_set_col(id, WIDTH, WALL_H);
      topWall = { id };
    }
    {
      const id = td_create('BottomWall');
      td_set_pos(id, WIDTH / 2, HEIGHT - WALL_H / 2);
      td_set_spr(id, WIDTH, WALL_H, 0.4, 0.4, 0.4, 1.0);
      td_set_col(id, WIDTH, WALL_H);
      bottomWall = { id };
    }

    // Start the JS-side per-frame loop. We use requestAnimationFrame and
    // call into the WASM input query functions to read keyboard state.
    requestAnimationFrame(gameLoop);
  }

  // ---------------------------------------------------------------------------
  // gameLoop - runs every animation frame. Reads input from the engine, updates
  // game state in JS, and pushes position/velocity changes back to the engine.
  // ---------------------------------------------------------------------------
  function gameLoop() {
    if (!initialized) return;

    const now = performance.now();
    const dt = Math.min((now - (gameLoop._last || now)) / 1000, 0.25);
    gameLoop._last = now;

    if (!paused) {
      update(dt);
    }

    requestAnimationFrame(gameLoop);
  }

  function update(dt) {
    const Module = TDBridge.wasmExports;
    const td_is_key = Module.cwrap('td_is_key_down', 'boolean', ['number']);
    const td_set_pos = Module.cwrap('td_entity_set_position', null, ['number', 'number', 'number']);
    const td_set_vel = Module.cwrap('td_entity_set_velocity', null, ['number', 'number', 'number']);
    const td_get_pos = Module.cwrap('td_entity_get_position', null, ['number', 'number', 'number']);

    // Helper: read an entity's current position from the engine.
    function getPos(id) {
      const xPtr = Module._malloc(8);
      const yPtr = Module._malloc(8);
      try {
        td_get_pos(id, xPtr, yPtr);
        return {
          x: Module.HEAPF32[xPtr >> 2],
          y: Module.HEAPF32[yPtr >> 2],
        };
      } finally {
        Module._free(xPtr);
        Module._free(yPtr);
      }
    }

    // --- Pause toggle ------------------------------------------------------
    if (td_is_key(VK.SPACE) && !update._spacePrev) {
      paused = !paused;
    }
    update._spacePrev = td_is_key(VK.SPACE);
    if (paused) return;

    // --- Left paddle (W/S) -------------------------------------------------
    {
      let vy = 0;
      if (td_is_key(VK.W)) vy -= PADDLE_SPEED;
      if (td_is_key(VK.S)) vy += PADDLE_SPEED;
      leftPaddle.y += vy * dt;
      // Clamp to playfield.
      const minY = WALL_H + PADDLE_H / 2;
      const maxY = HEIGHT - WALL_H - PADDLE_H / 2;
      if (leftPaddle.y < minY) leftPaddle.y = minY;
      if (leftPaddle.y > maxY) leftPaddle.y = maxY;
      td_set_pos(leftPaddle.id, leftPaddle.x, leftPaddle.y);
    }

    // --- Right paddle (AI: track the ball) ---------------------------------
    {
      const targetY = ball.y;
      const dy = targetY - rightPaddle.y;
      const aiSpeed = PADDLE_SPEED * 0.85;  // slightly slower than player
      if (Math.abs(dy) > 4) {
        rightPaddle.y += Math.sign(dy) * aiSpeed * dt;
      }
      const minY = WALL_H + PADDLE_H / 2;
      const maxY = HEIGHT - WALL_H - PADDLE_H / 2;
      if (rightPaddle.y < minY) rightPaddle.y = minY;
      if (rightPaddle.y > maxY) rightPaddle.y = maxY;
      td_set_pos(rightPaddle.id, rightPaddle.x, rightPaddle.y);
    }

    // --- Ball movement + collision -----------------------------------------
    ball.x += ball.vx * dt;
    ball.y += ball.vy * dt;

    // Top/bottom walls
    if (ball.y < WALL_H + BALL_SIZE / 2) {
      ball.y = WALL_H + BALL_SIZE / 2;
      ball.vy = -ball.vy;
    }
    if (ball.y > HEIGHT - WALL_H - BALL_SIZE / 2) {
      ball.y = HEIGHT - WALL_H - BALL_SIZE / 2;
      ball.vy = -ball.vy;
    }

    // Left paddle
    if (ball.vx < 0 &&
        ball.x - BALL_SIZE / 2 < leftPaddle.x + PADDLE_W / 2 &&
        ball.x > leftPaddle.x &&
        ball.y > leftPaddle.y - PADDLE_H / 2 &&
        ball.y < leftPaddle.y + PADDLE_H / 2) {
      ball.x = leftPaddle.x + PADDLE_W / 2 + BALL_SIZE / 2;
      ball.vx = -ball.vx;
      // Add spin based on where the ball hit the paddle.
      const hitPos = (ball.y - leftPaddle.y) / (PADDLE_H / 2);
      ball.vy += hitPos * 100;
    }

    // Right paddle
    if (ball.vx > 0 &&
        ball.x + BALL_SIZE / 2 > rightPaddle.x - PADDLE_W / 2 &&
        ball.x < rightPaddle.x &&
        ball.y > rightPaddle.y - PADDLE_H / 2 &&
        ball.y < rightPaddle.y + PADDLE_H / 2) {
      ball.x = rightPaddle.x - PADDLE_W / 2 - BALL_SIZE / 2;
      ball.vx = -ball.vx;
      const hitPos = (ball.y - rightPaddle.y) / (PADDLE_H / 2);
      ball.vy += hitPos * 100;
    }

    // Clamp ball speed
    const speed = Math.hypot(ball.vx, ball.vy);
    const maxSpeed = BALL_SPEED * 1.8;
    if (speed > maxSpeed) {
      ball.vx = (ball.vx / speed) * maxSpeed;
      ball.vy = (ball.vy / speed) * maxSpeed;
    }

    // Scoring
    if (ball.x < -BALL_SIZE) {
      rightScore++;
      resetBall(-1);
      flashScore('right');
    } else if (ball.x > WIDTH + BALL_SIZE) {
      leftScore++;
      resetBall(1);
      flashScore('left');
    }

    td_set_pos(ball.id, ball.x, ball.y);
    td_set_vel(ball.id, ball.vx, ball.vy);

    // Check for winner
    if (leftScore >= WIN_SCORE || rightScore >= WIN_SCORE) {
      const winner = leftScore >= WIN_SCORE ? 'Player' : 'AI';
      TDBridge.wasmExports && console.log('[pong] ' + winner + ' wins!');
      paused = true;
      // Show a simple alert overlay (in a real game, draw it on-canvas).
      setTimeout(function () {
        alert(winner + ' wins! ' + leftScore + ' - ' + rightScore + '\nClick Restart to play again.');
      }, 100);
    }
  }

  function resetBall(direction) {
    const Module = TDBridge.wasmExports;
    const td_set_pos = Module.cwrap('td_entity_set_position', null, ['number', 'number', 'number']);
    const td_set_vel = Module.cwrap('td_entity_set_velocity', null, ['number', 'number', 'number']);

    ball.x = WIDTH / 2;
    ball.y = HEIGHT / 2;
    const ang = (Math.random() - 0.5) * 0.5;
    ball.vx = direction * BALL_SPEED * Math.cos(ang);
    ball.vy = BALL_SPEED * Math.sin(ang);
    td_set_pos(ball.id, ball.x, ball.y);
    td_set_vel(ball.id, ball.vx, ball.vy);
  }

  function flashScore(side) {
    // Could trigger a visual flash effect here.
    console.log('[pong] score: player ' + leftScore + ' - ai ' + rightScore);
  }
})();
