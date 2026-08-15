// =============================================================================
// TD Sandbox — JS bridge shim
// -----------------------------------------------------------------------------
// The 3D showcase currently runs in PURE JS-FALLBACK mode.  The C++ WASM
// engine (web/td-engine.{js,wasm} + wasm/js_bridge.js defining TDBridge) is
// not yet wired into this page — the renderer, physics, ECS, audio, input,
// i18n, beat, persistence, and tutorial are all implemented in pure JS
// under game/*.js.
//
// This shim exists so that the rest of the game code can call
// `await TDWasmDetector.detect()` and get back `false` cleanly, without
// spurious console warnings about TDBridge being undefined.
//
// When the C++ WASM engine is ready to take over the showcase:
//   1. Add <script src="../../web/js_bridge.js"></script> to index.html
//      BEFORE this shim.
//   2. Add <script src="../../web/td-engine.js"></script> after that.
//   3. Replace detectWasm() with the real loader (inject td-engine.js,
//      wait for TDBridge.wasmExports, return true/false).
//
// Until then, return false unconditionally so the game boots straight into
// JS-fallback mode with no noise.
// =============================================================================

(function (global) {
  'use strict';

  // Future WASM loader — left here as documentation of the intended shape.
  // Currently always returns false because the WASM glue isn't loaded.
  function detectWasm() {
    return Promise.resolve(false);
  }

  global.TDWasmDetector = {
    detect: detectWasm,
    // For inspection / debugging.
    isScriptLoaded: function () { return false; },
  };
})(typeof window !== 'undefined' ? window : this);
