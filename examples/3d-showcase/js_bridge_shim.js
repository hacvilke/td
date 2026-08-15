// =============================================================================
// TD Sandbox — JS bridge shim
// -----------------------------------------------------------------------------
// This file lets the showcase game use the TDEngine.* API whether or not the
// compiled WASM is present.  When the WASM is loaded (via `td serve` or by
// shipping td-engine.js alongside this page), TDEngine.physics.* calls hit
// the real C++ physics engine.  When the WASM is absent, the showcase uses
// the pure-JS reference physics in game/physics_js.js, but goes through the
// exact same TDEngine.physics.* call sites so the code reads identically.
//
// We DO NOT replace TDEngine.* here — we only install a "presence flag" the
// game can read:
//     TDEngine.__backend === 'wasm'  | 'js-fallback'
//
// The game's physics_js.js installs itself as TDEngine.physics when the WASM
// backend is unavailable, so the rest of the game code never has to branch.
// =============================================================================

(function (global) {
  'use strict';

  // Detect whether the compiled engine is loaded by trying to fetch the
  // wasm file.  This is async; we expose a promise the game can await.
  function detectWasm() {
    return new Promise(function (resolve) {
      // If the host page already defined TDBridge.wasmExports, trust it.
      if (global.TDBridge && global.TDBridge.wasmExports) {
        resolve(true);
        return;
      }
      // Try to fetch td-engine.wasm from the engine root.  HEAD is enough.
      var root = global.TD_ENGINE_ROOT || './';
      var url = root + 'td-engine.wasm';
      // Use fetch with HEAD; if it 404s, fall back to JS.
      if (typeof fetch !== 'function') {
        resolve(false);
        return;
      }
      fetch(url, { method: 'HEAD' }).then(function (r) {
        resolve(r.ok);
      }).catch(function () {
        resolve(false);
      });
    });
  }

  global.TDWasmDetector = {
    detect: detectWasm,
  };
})(typeof window !== 'undefined' ? window : this);
