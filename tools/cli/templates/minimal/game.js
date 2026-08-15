// __GAME_NAME__
//
// This is the entry point for your TD Engine game. The engine is loaded
// by index.html (js_bridge.js + td_api.js + td-engine.wasm), and your
// game.js runs after they're ready.

'use strict';

(async function () {
  // Initialize the engine with the canvas.
  await TDEngine.init('game-canvas');
  document.getElementById('loading').classList.add('hidden');

  // Create a player entity.
  const player = TDEngine.ecs.create('Player');
  TDEngine.ecs.setPosition(player, 100, 100);
  TDEngine.ecs.setSprite(player, 32, 32, 1, 1, 1, 1);

  // Main game loop. TDEngine.audio pumps the mixer; you just update logic
  // here and the engine handles rendering on its own internal tick.
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
})();
