// __GAME_NAME__
//
// This is the entry point for your TD Engine game. The engine is loaded
// by index.html (js_bridge.js + td_api.js — td-engine.js is injected at
// init time), and your game.js runs after they're ready.

'use strict';

(async function () {
  try {
    // Initialize the engine with the canvas. This loads the WASM module
    // and starts the engine's internal main loop (which renders every frame
    // automatically — you only need to update game logic in your loop).
    await TDEngine.lifecycle.init('game-canvas');
    const loading = document.getElementById('loading');
    if (loading) loading.classList.add('hidden');

    // Create a player entity.
    const player = TDEngine.ecs.create('Player');
    TDEngine.ecs.setPosition(player, 100, 100);
    TDEngine.ecs.setSprite(player, 32, 32, 1, 1, 1, 1);  // 32x32 white quad

    // Main game loop. The engine renders automatically; we just update logic.
    let t = 0;
    function frame() {
      t += 0.016;
      // Bounce the player in a circle.
      TDEngine.ecs.setPosition(player,
        400 + Math.cos(t) * 100,
        300 + Math.sin(t) * 100
      );
      requestAnimationFrame(frame);
    }
    requestAnimationFrame(frame);
  } catch (e) {
    // Show boot errors on the loading screen instead of leaving a blank page.
    const loading = document.getElementById('loading');
    if (loading) {
      loading.innerHTML = '<h1 style="color:#f87171">Engine failed to start</h1>' +
                          '<pre style="text-align:left;background:#1a1a1a;color:#fcd34d;' +
                          'padding:16px;border-radius:8px;overflow:auto;max-width:80vw">' +
                          (e && e.stack ? e.stack : String(e)) + '</pre>';
    }
    console.error('[__GAME_NAME__] boot failed:', e);
  }
})();
