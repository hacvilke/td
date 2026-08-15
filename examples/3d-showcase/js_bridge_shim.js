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
//
// IMPORTANT: A HEAD-only check on td-engine.wasm is NOT enough — the wasm
// file may exist on the CDN while the td-engine.js glue script that boots
// Emscripten's Module was never loaded into the page.  Calling
// TDEngine.lifecycle.init() in that state throws
//   "Cannot read properties of undefined (reading 'init')"
// because global.TDBridge is undefined.  So this shim actually injects the
// script tag and waits for either TDBridge.wasmExports to populate or the
// script to error out.
// =============================================================================

(function (global) {
  'use strict';

  var _scriptLoaded = false;
  var _scriptPromise = null;

  // Inject <script src="td-engine.js"> into the page.  Idempotent.
  function loadWasmScript() {
    if (_scriptPromise) return _scriptPromise;
    _scriptPromise = new Promise(function (resolve, reject) {
      var root = global.TD_ENGINE_ROOT || './';
      var url = root + 'td-engine.js';
      var s = document.createElement('script');
      s.src = url;
      s.async = true;
      s.onload = function () { _scriptLoaded = true; resolve(); };
      s.onerror = function () {
        _scriptPromise = null;  // allow retry
        reject(new Error('Failed to load ' + url));
      };
      document.head.appendChild(s);
    });
    return _scriptPromise;
  }

  // Wait up to `timeoutMs` for TDBridge.wasmExports to be populated by
  // the Emscripten glue.  Resolves true if ready, false on timeout.
  function waitForWasmReady(timeoutMs) {
    var start = performance.now();
    return new Promise(function (resolve) {
      function check() {
        if (global.TDBridge && global.TDBridge.wasmExports) {
          resolve(true);
          return;
        }
        if (performance.now() - start > timeoutMs) {
          resolve(false);
          return;
        }
        setTimeout(check, 50);
      }
      check();
    });
  }

  // Detect whether the compiled engine is loaded AND ready.  This is the
  // function the game awaits before deciding which backend to use.
  function detectWasm() {
    return new Promise(function (resolve) {
      // Already-loaded (e.g. via td serve that pre-injected the script).
      if (global.TDBridge && global.TDBridge.wasmExports) {
        resolve(true);
        return;
      }
      // Try to inject the script and wait for it to bootstrap.
      loadWasmScript().then(function () {
        return waitForWasmReady(8000);
      }).then(function (ready) {
        resolve(!!ready);
      }).catch(function () {
        resolve(false);
      });
    });
  }

  global.TDWasmDetector = {
    detect: detectWasm,
    // Exposed for tests / inspection — not used by the game loop.
    isScriptLoaded: function () { return _scriptLoaded; },
  };
})(typeof window !== 'undefined' ? window : this);
