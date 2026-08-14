// =============================================================================
// TD Engine - Gauntlet Part 7: JavaScript Bridge
// File: wasm/js_bridge.js
//
// Loads td-engine.wasm (compiled by `make web`) and bridges the browser to
// the C++ engine. The web/engine-wrapper.ts module builds a clean TypeScript
// API on top of TDBridge; web/index.html uses TDBridge directly.
//
// Key insight: browser KeyboardEvent.keyCode is the same numeric value as the
// Win32 virtual key code (a historical artifact of the DOM spec). The TD
// Engine's Key:: namespace uses Win32 VK codes (e.g. Key::A = 0x41). So we
// forward e.keyCode directly with no translation table.
//
// Audio: Web Audio API. The C++ Mixer produces int16 PCM; we convert to
// float32 [-1, 1] for Web Audio's output buffer.
//
// Usage (from web/index.html):
//   <script src="td-engine.js"></script>      <!-- emcc glue -->
//   <script src="../wasm/js_bridge.js"></script>
//   <script>
//     TDBridge.onReady(() => console.log('engine ready'));
//     await TDBridge.init('game-canvas');
//   </script>
// =============================================================================

(function (global) {
  'use strict';

  // Win32 VK codes for game-relevant keys. Used to decide which keys should
  // have their default browser behavior suppressed (so arrow keys don't
  // scroll the page, space doesn't page-down, etc.).
  const GAME_VK_CODES = new Set([
    0x08, // Backspace
    0x09, // Tab
    0x0D, // Enter
    0x1B, // Escape
    0x20, // Space
    0x25, 0x26, 0x27, 0x28, // Arrow Left/Up/Right/Down
    // A-Z and 0-9 are also game-relevant; we check ranges at runtime.
  ]);

  function isGameKey(vk) {
    if (GAME_VK_CODES.has(vk)) return true;
    if (vk >= 0x30 && vk <= 0x39) return true;  // 0-9
    if (vk >= 0x41 && vk <= 0x5A) return true;  // A-Z
    if (vk >= 0x70 && vk <= 0x7B) return true;  // F1-F12
    return false;
  }

  // ---------------------------------------------------------------------------
  // TDBridge singleton
  // ---------------------------------------------------------------------------
  const TDBridge = {
    // --- Public state -------------------------------------------------------
    ready: false,
    wasmExports: null,
    canvas: null,
    gl: null,

    // --- Internal state -----------------------------------------------------
    _readyCallbacks: [],
    _logCallbacks: [],
    _audioCtx: null,
    _scriptNode: null,
    _audioBufferL: null,   // reused int16 buffer (left+right interleaved)
    _audioBufferR: null,
    _mouseLeftDown: false,
    _mouseRightDown: false,
    _mouseX: 0,
    _mouseY: 0,

    // -------------------------------------------------------------------------
    // Register a callback fired exactly once when the engine reports ready.
    // -------------------------------------------------------------------------
    onReady(callback) {
      if (typeof callback !== 'function') return;
      if (this.ready) {
        try { callback(); } catch (e) { console.error('TDBridge.onReady:', e); }
      } else {
        this._readyCallbacks.push(callback);
      }
    },

    // -------------------------------------------------------------------------
    // Register a callback fired for every engine log line.
    // -------------------------------------------------------------------------
    onLog(callback) {
      if (typeof callback === 'function') this._logCallbacks.push(callback);
    },

    // -------------------------------------------------------------------------
    // init(canvasId) -> Promise<void>
    //
    // Acquires the WebGL 2 context, loads the WASM module via Emscripten's
    // generated td-engine.js glue, and calls td_init(width, height).
    // -------------------------------------------------------------------------
    async init(canvasId) {
      const canvas = (typeof canvasId === 'string')
        ? document.getElementById(canvasId)
        : canvasId;
      if (!canvas) throw new Error(`TDBridge.init: canvas "${canvasId}" not found`);
      this.canvas = canvas;

      // ---- WebGL 2 context -------------------------------------------------
      const gl = canvas.getContext('webgl2', {
        alpha: false,
        antialias: true,
        premultipliedAlpha: false,
        preserveDrawingBuffer: false,
        powerPreference: 'high-performance',
      });
      if (!gl) throw new Error('WebGL 2 is not supported in this browser');
      this.gl = gl;

      // Size the canvas to its CSS box * devicePixelRatio for crisp rendering.
      const dpr = window.devicePixelRatio || 1;
      const cssW = canvas.clientWidth  || 800;
      const cssH = canvas.clientHeight || 600;
      canvas.width  = Math.floor(cssW * dpr);
      canvas.height = Math.floor(cssH * dpr);

      // ---- Load Emscripten glue --------------------------------------------
      // Emscripten's td-engine.js looks for a global `Module` config object
      // and merges it with its defaults, then loads td-engine.wasm and
      // creates the GL context on Module.canvas.
      const Module = await this._loadEmscriptenModule(canvas);

      // Wait for Module.asm to be populated AND calledRun to be true.
      await this._waitForRuntime(Module);

      this.wasmExports = Module;

      // ---- Wire browser input -> WASM --------------------------------------
      this._setupBrowserInput();

      // ---- Wire Web Audio bridge -------------------------------------------
      this._setupAudioBridge();

      // ---- Boot the engine --------------------------------------------------
      const td_init    = Module.cwrap('td_init',         null, ['number', 'number']);
      const td_version = Module.cwrap('td_get_version',  'string');
      td_init(canvas.width, canvas.height);
      this._emitLog('info', td_version());

      this.ready = true;
      for (const cb of this._readyCallbacks) {
        try { cb(); } catch (e) { console.error('TDBridge ready cb:', e); }
      }
      this._readyCallbacks.length = 0;
    },

    // -------------------------------------------------------------------------
    // _loadEmscriptenModule(canvas) -> Promise<Module>
    //
    // Loads td-engine.js (emcc-generated glue). The glue script attaches a
    // global `Module` object that we pre-configure with our canvas + print
    // hooks before the script executes.
    // -------------------------------------------------------------------------
    _loadEmscriptenModule(canvas) {
      return new Promise((resolve, reject) => {
        const moduleConfig = {
          canvas: canvas,
          print:  (text) => this._emitLog('info',  text),
          printErr:(text) => this._emitLog('warn', text),
          onAbort:(reason) => {
            this._emitLog('error', 'WASM abort: ' + reason);
            console.error('TD Engine WASM aborted:', reason);
          },
          noInitialRun: false,
          noExitRuntime: true,
          onRuntimeInitialized: () => {
            this._emitLog('info', 'Emscripten runtime initialized');
          },
        };

        // CRITICAL: pre-set global.Module BEFORE the emcc glue script runs.
        // The glue does `var Module = typeof Module != 'undefined' ? Module : {};`
        // at the top, so it picks up our config (canvas, print hooks, etc.).
        global.Module = moduleConfig;

        // Match any <script> whose src starts with 'td-engine.js' so we catch
        // both 'td-engine.js' and 'td-engine.js?v=5' cache-bust variants.
        // Using attribute-exactly-equal would MISS the ?v=... variant, which
        // caused the bug where the bridge re-injected a second td-engine.js
        // tag and triggered 'EmscriptenEH already declared'.
        const scripts = document.querySelectorAll('script[src]');
        let existingScript = null;
        for (let i = 0; i < scripts.length; i++) {
          const s = scripts[i].getAttribute('src');
          // strip query string, compare basename
          const base = s.split('?')[0].split('/').pop();
          if (base === 'td-engine.js') { existingScript = scripts[i]; break; }
        }

        if (!existingScript) {
          // Dynamically inject with the same cache-bust as the host page.
          // We sniff ?v=... from the bridge's own <script> tag so the cache
          // key stays in lockstep with index.html.
          const myTag = document.currentScript || document.querySelector('script[src*="js_bridge.js"]');
          let version = '';
          if (myTag) {
            const m = /\?v=([^&]+)/.exec(myTag.getAttribute('src') || '');
            if (m) version = '?v=' + m[1];
          }
          const script = document.createElement('script');
          script.src = 'td-engine.js' + version;
          script.async = true;
          script.onload  = () => resolve(global.Module);
          script.onerror = () => reject(new Error('Failed to load td-engine.js'));
          document.body.appendChild(script);
        } else {
          // Script tag already present - poll for Module readiness. The glue
          // merges our global.Module config on parse, then sets .asm after
          // WASM compilation finishes.
          const start = performance.now();
          const poll = () => {
            if (global.Module && global.Module.asm) {
              resolve(global.Module);
            } else if (performance.now() - start > 15000) {
              reject(new Error('Timed out waiting for Emscripten Module'));
            } else {
              setTimeout(poll, 50);
            }
          };
          poll();
        }
      });
    },

    // -------------------------------------------------------------------------
    // _waitForRuntime(Module) -> Promise<void>
    //
    // Resolves once the Emscripten runtime has both compiled the WASM
    // (Module.asm is set) AND run the main() entry point (Module.calledRun).
    // The latter is set by Module.onRuntimeInitialized -> run() -> calledRun=true.
    // Some Emscripten configs call onRuntimeInitialized BEFORE calledRun is
    // flipped, so we explicitly wait for both flags.
    // -------------------------------------------------------------------------
    _waitForRuntime(Module) {
      return new Promise((resolve, reject) => {
        const start = performance.now();
        const check = () => {
          if (Module.asm && Module.calledRun) {
            resolve();
          } else if (performance.now() - start > 20000) {
            reject(new Error('Timed out waiting for Emscripten runtime'));
          } else {
            setTimeout(check, 30);
          }
        };
        check();
      });
    },

    // -------------------------------------------------------------------------
    // _setupBrowserInput()
    //
    // Browser event listeners that forward to the WASM input functions.
    // Uses e.keyCode (Win32 VK code) - no translation needed.
    // -------------------------------------------------------------------------
    _setupBrowserInput() {
      const Module = this.wasmExports;
      const td_set_key   = Module.cwrap('td_set_key_state',   null, ['number', 'boolean']);
      const td_set_mouse = Module.cwrap('td_set_mouse_state', null, ['number', 'number', 'boolean', 'boolean']);
      const td_resize    = Module.cwrap('td_resize',          null, ['number', 'number']);

      document.addEventListener('keydown', (e) => {
        td_set_key(e.keyCode, true);
        if (isGameKey(e.keyCode)) e.preventDefault();
      });

      document.addEventListener('keyup', (e) => {
        td_set_key(e.keyCode, false);
      });

      this.canvas.addEventListener('mousemove', (e) => {
        const rect = this.canvas.getBoundingClientRect();
        const dpr = window.devicePixelRatio || 1;
        this._mouseX = (e.clientX - rect.left) * dpr;
        this._mouseY = (e.clientY - rect.top)  * dpr;
        td_set_mouse(this._mouseX, this._mouseY, this._mouseLeftDown, this._mouseRightDown);
      });

      this.canvas.addEventListener('mousedown', (e) => {
        if (e.button === 0) this._mouseLeftDown  = true;
        if (e.button === 2) this._mouseRightDown = true;
        td_set_mouse(this._mouseX, this._mouseY, this._mouseLeftDown, this._mouseRightDown);
        e.preventDefault();
      });

      this.canvas.addEventListener('mouseup', (e) => {
        if (e.button === 0) this._mouseLeftDown  = false;
        if (e.button === 2) this._mouseRightDown = false;
        td_set_mouse(this._mouseX, this._mouseY, this._mouseLeftDown, this._mouseRightDown);
      });

      // Disable the right-click context menu so right-drag can be used for
      // camera control inside the canvas.
      this.canvas.addEventListener('contextmenu', (e) => e.preventDefault());

      // Window resize -> td_resize. Debounced via rAF.
      let resizePending = false;
      const onResize = () => {
        if (resizePending) return;
        resizePending = true;
        requestAnimationFrame(() => {
          resizePending = false;
          const dpr = window.devicePixelRatio || 1;
          const w = Math.floor((this.canvas.clientWidth  || window.innerWidth)  * dpr);
          const h = Math.floor((this.canvas.clientHeight || window.innerHeight) * dpr);
          if (w > 0 && h > 0 && (w !== this.canvas.width || h !== this.canvas.height)) {
            this.canvas.width  = w;
            this.canvas.height = h;
            td_resize(w, h);
          }
        });
      };
      window.addEventListener('resize', onResize);
    },

    // -------------------------------------------------------------------------
    // _setupAudioBridge()
    //
    // Web Audio AudioContext + ScriptProcessor. Each audio quantum, calls
    // td_fill_audio_buffer(int16Ptr, numFrames) which asks the C++ Mixer to
    // mix numFrames * 2 int16 samples (stereo interleaved). We then copy
    // them into the output Float32 channels scaled to [-1, 1].
    // -------------------------------------------------------------------------
    _setupAudioBridge() {
      try {
        const AudioCtx = window.AudioContext || window.webkitAudioContext;
        if (!AudioCtx) {
          this._emitLog('warn', 'Web Audio API unavailable - audio disabled');
          return;
        }
        this._audioCtx = new AudioCtx({ sampleRate: 44100 });

        // ScriptProcessor: 0 input channels, 2 output channels (stereo),
        // 4096-frame buffer. Universal browser support; AudioWorklet would
        // be more modern but adds a separate build step + worker latency.
        const scriptNode = this._audioCtx.createScriptProcessor(4096, 0, 2);
        this._scriptNode = scriptNode;

        const Module = this.wasmExports;
        const td_fill_audio = Module.cwrap(
          'td_fill_audio_buffer', null, ['number', 'number']
        );

        // Pre-allocate a WASM-side int16 buffer big enough for the largest
        // possible quantum (4096 frames * 2 channels * 2 bytes).
        const MAX_FRAMES = 4096;
        const wasmInt16Buf = Module._malloc(MAX_FRAMES * 2 * 2);

        scriptNode.onaudioprocess = (e) => {
          const left  = e.outputBuffer.getChannelData(0);
          const right = e.outputBuffer.getChannelData(1);
          const n = left.length;   // = right.length
          if (n > MAX_FRAMES) {
            // Shouldn't happen, but guard anyway.
            left.fill(0); right.fill(0); return;
          }
          // Ask the C++ Mixer to fill the int16 buffer.
          td_fill_audio(wasmInt16Buf, n);
          // View the WASM memory as int16 and split into L/R channels.
          const heapInt16 = Module.HEAP16;
          const offset = wasmInt16Buf >> 1;  // byte offset -> int16 index
          for (let i = 0; i < n; i++) {
            const l = heapInt16[offset + i * 2];
            const r = heapInt16[offset + i * 2 + 1];
            left[i]  = l / 32768.0;
            right[i] = r / 32768.0;
          }
        };

        scriptNode.connect(this._audioCtx.destination);
        this._emitLog('info', 'Web Audio bridge initialized (44100Hz, stereo, 4096-frame quantum)');
      } catch (err) {
        this._emitLog('warn', 'Audio bridge setup failed: ' + err.message);
      }
    },

    // -------------------------------------------------------------------------
    // Resume the AudioContext after a user gesture. Browsers block audio
    // until the user clicks/taps. Call this from a click handler.
    // -------------------------------------------------------------------------
    resumeAudio() {
      if (this._audioCtx && this._audioCtx.state === 'suspended') {
        this._audioCtx.resume();
      }
    },

    // -------------------------------------------------------------------------
    // loadScene(sceneText) -> void
    // -------------------------------------------------------------------------
    loadScene(sceneText) {
      if (!this.ready) throw new Error('TDBridge not ready - call init() first');
      if (typeof sceneText !== 'string') {
        throw new TypeError('loadScene expects a string');
      }
      const td_load = this.wasmExports.cwrap('td_load_scene', null, ['string']);
      td_load(sceneText);
    },

    // -------------------------------------------------------------------------
    // Manual input injection (used by tests / automated input / TS wrapper).
    // -------------------------------------------------------------------------
    setKey(vkCode, pressed) {
      if (!this.ready) return;
      const td_set_key = this.wasmExports.cwrap('td_set_key_state', null, ['number', 'boolean']);
      td_set_key(vkCode, !!pressed);
    },

    setMouse(x, y, leftDown, rightDown) {
      if (!this.ready) return;
      const td_set_mouse = this.wasmExports.cwrap('td_set_mouse_state', null,
                                                  ['number', 'number', 'boolean', 'boolean']);
      td_set_mouse(x, y, !!leftDown, !!rightDown);
    },

    // -------------------------------------------------------------------------
    // shutdown() -> void
    // -------------------------------------------------------------------------
    shutdown() {
      if (!this.ready) return;
      const td_shutdown = this.wasmExports.cwrap('td_shutdown', null);
      td_shutdown();
      if (this._scriptNode) this._scriptNode.disconnect();
      if (this._audioCtx)   this._audioCtx.close();
      this.ready = false;
    },

    // -------------------------------------------------------------------------
    // readWasmString(ptr) -> string
    //
    // Reads a NUL-terminated UTF-8 string from WASM linear memory.
    // -------------------------------------------------------------------------
    readWasmString(ptr) {
      if (!this.wasmExports || ptr === 0) return '';
      return this.wasmExports.UTF8ToString(ptr);
    },

    // -------------------------------------------------------------------------
    // Beat Tracker API (rhythm-game mechanics)
    //
    // Wraps the td_beat_* C functions with a clean JS interface. See
    // docs/RHYTHM_MECHANICS.md for the design and web/examples/beat_demo.js
    // for a complete sample game.
    //
    // Example:
    //   const song = TDBridge.createEntity('song');
    //   TDBridge.beatStart(song, 140, 0.15);    // 140 BPM, 150ms half-window
    //   TDBridge.onBeat((count, time) => {
    //     console.log(`Beat ${count} at ${time.toFixed(3)}s`);
    //   });
    //   // later, when player presses a key:
    //   if (TDBridge.beatIsOnBeat(song)) {
    //     const combo = TDBridge.beatRegisterHit(song, /*strict=*/true);
    //     console.log(`Combo: ${combo}`);
    //   }
    // -------------------------------------------------------------------------
    _beatApi: null,  // cached cwrap handles (created on first use)

    _cacheBeatApi() {
      if (this._beatApi) return this._beatApi;
      const M = this.wasmExports;
      this._beatApi = {
        start:          M.cwrap('td_beat_start',            null,   ['number','number','number']),
        stop:           M.cwrap('td_beat_stop',             null,   ['number']),
        isOnBeat:       M.cwrap('td_beat_is_on_beat',       'number', ['number']),
        getCount:       M.cwrap('td_beat_get_count',        'number', ['number']),
        getNextBeat:    M.cwrap('td_beat_get_next_beat_time', 'number', ['number']),
        getLastBeat:    M.cwrap('td_beat_get_last_beat_time', 'number', ['number']),
        registerHit:    M.cwrap('td_beat_register_hit',     'number', ['number','number']),
        getCombo:       M.cwrap('td_beat_get_combo',        'number', ['number']),
        getBestCombo:   M.cwrap('td_beat_get_best_combo',   'number', ['number']),
        resetCombo:     M.cwrap('td_beat_reset_combo',      'number', ['number']),
        setCallback:    M.cwrap('td_beat_set_callback',     null,   ['number']),
        setBpm:         M.cwrap('td_beat_set_bpm',          null,   ['number','number']),
        playSound:      M.cwrap('td_beat_play_sound',       null,   ['number','number']),
        createEntity:   M.cwrap('td_create_entity',         'number', ['string']),
        setPos:         M.cwrap('td_entity_set_position',   null,   ['number','number','number']),
        setSprite:      M.cwrap('td_entity_set_sprite',     null,   ['number','number','number','number','number','number','number']),
        destroy:        M.cwrap('td_entity_destroy',        null,   ['number']),
        isValid:        M.cwrap('td_entity_is_valid',       'number', ['number']),
      };
      return this._beatApi;
    },

    // Start beat tracking on an entity. BPM 60-600, windowHalf in seconds
    // (typical: 0.10 - 0.20 for forgiving, 0.05 - 0.08 for hardcore).
    beatStart(entityId, bpm, windowHalfSec) {
      this._cacheBeatApi().start(entityId, bpm, windowHalfSec);
    },

    beatStop(entityId) {
      this._cacheBeatApi().stop(entityId);
    },

    // Returns true if the player's current timing is inside the on-beat window.
    beatIsOnBeat(entityId) {
      return !!this._cacheBeatApi().isOnBeat(entityId);
    },

    beatGetCount(entityId) {
      return this._cacheBeatApi().getCount(entityId);
    },

    beatGetNextBeatTime(entityId) {
      return this._cacheBeatApi().getNextBeat(entityId);
    },

    beatGetLastBeatTime(entityId) {
      return this._cacheBeatApi().getLastBeat(entityId);
    },

    // Register a hit. If strict=true, misses reset combo to 0.
    // Returns the new combo count.
    beatRegisterHit(entityId, strict) {
      return this._cacheBeatApi().registerHit(entityId, strict ? 1 : 0);
    },

    beatGetCombo(entityId) {
      return this._cacheBeatApi().getCombo(entityId);
    },

    beatGetBestCombo(entityId) {
      return this._cacheBeatApi().getBestCombo(entityId);
    },

    beatResetCombo(entityId) {
      return this._cacheBeatApi().resetCombo(entityId);
    },

    beatSetBpm(entityId, newBpm) {
      this._cacheBeatApi().setBpm(entityId, newBpm);
    },

    // Register a JS callback fired on every beat tick.
    // Signature: (beatCount: number, beatTime: number) => void
    // The callback is invoked from within the engine's fixed-step update,
    // so it must be fast (<1ms) and must not call back into TDBridge in a
    // way that mutates the World mid-iteration.
    onBeat(callback) {
      if (typeof callback !== 'function') {
        // Clear any existing callback.
        this._cacheBeatApi().setCallback(0);
        return;
      }
      // addFunction: converts a JS function into a callable C function pointer.
      // Signature 'vif' = void(int, float). Requires -s ALLOW_TABLE_GROWTH=1
      // and Module.addFunction in EXPORTED_RUNTIME_METHODS.
      const M = this.wasmExports;
      if (typeof M.addFunction !== 'function') {
        throw new Error('Module.addFunction is not available. The WASM build ' +
                        'needs -s ALLOW_TABLE_GROWTH=1 and addFunction in ' +
                        'EXPORTED_RUNTIME_METHODS.');
      }
      const ptr = M.addFunction(callback, 'vif');
      this._cacheBeatApi().setCallback(ptr);
    },

    // -------------------------------------------------------------------------
    // Entity helpers (high-level wrappers around td_create_entity + the
    // td_entity_set_* family). Lets web game devs create entities with a
    // fluent one-liner instead of repeating cwrap boilerplate.
    // -------------------------------------------------------------------------

    // createEntity(name?) -> entityId (uint32)
    // Creates a bare entity. Use setEntityPosition / setEntitySprite to
    // attach components.
    createEntity(name) {
      return this._cacheBeatApi().createEntity(name || 'Entity');
    },

    setEntityPosition(entityId, x, y) {
      this._cacheBeatApi().setPos(entityId, x, y);
    },

    // setEntitySprite(entityId, w, h, [r,g,b,a])
    // r,g,b,a default to white (1,1,1,1).
    setEntitySprite(entityId, w, h, r, g, b, a) {
      this._cacheBeatApi().setSprite(entityId, w, h,
        r === undefined ? 1 : r,
        g === undefined ? 1 : g,
        b === undefined ? 1 : b,
        a === undefined ? 1 : a);
    },

    destroyEntity(entityId) {
      this._cacheBeatApi().destroy(entityId);
    },

    isEntityValid(entityId) {
      return !!this._cacheBeatApi().isValid(entityId);
    },

    // -------------------------------------------------------------------------
    // _emitLog(level, message) -> void
    // -------------------------------------------------------------------------
    _emitLog(level, message) {
      const line = String(message).replace(/\n$/, '');
      if (this._logCallbacks.length === 0) {
        if (level === 'error') console.error('[TD]', line);
        else if (level === 'warn') console.warn('[TD]', line);
        else console.log('[TD]', line);
        return;
      }
      for (const cb of this._logCallbacks) {
        try { cb({ level, message: line }); } catch (e) { /* swallow */ }
      }
    },
  };

  // ---------------------------------------------------------------------------
  // Expose globally. window.TDBridge is what web/index.html and
  // web/engine-wrapper.ts reach for.
  // ---------------------------------------------------------------------------
  global.TDBridge = TDBridge;

})(typeof window !== 'undefined' ? window : this);
