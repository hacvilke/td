// =============================================================================
// TD Engine — Modular API Layer
// File: web/td_api.js
//
// Wraps the low-level TDBridge / Module._td_* C API in a clean, namespaced,
// Godot-like JavaScript API. This is the public API web games should use.
//
// Old (still works, deprecated):
//   const Module = TDBridge.wasmExports;
//   const td_init = Module.cwrap('td_init', null, ['number','number']);
//   await TDBridge.init('game-canvas');
//   td_init(800, 600);
//   const id = Module.cwrap('td_create_entity','number',['string'])('Player');
//   Module.cwrap('td_entity_set_position',null,['number','number','number'])
//         (id, 100, 200);
//
// New (modular, recommended):
//   await TDEngine.init('game-canvas');        // lifecycle
//   const id = TDEngine.ecs.create('Player');  // ECS
//   TDEngine.ecs.setPosition(id, 100, 200);
//   TDEngine.input.isKeyDown(0x41);            // Input
//   TDEngine.beat.start(id, 120, 0.15);        // Beat / rhythm
//   TDEngine.script.load(src, 'name');         // Scripting
//   TDEngine.i18n.setLocale('fr');             // Localization
//   TDEngine.audio.resume();                   // Audio
//   TDEngine.touch.count();                    // Touch
//   TDEngine.gamepad.axis(0, 0);               // Gamepad
//   TDEngine.shaderGraph.compile(nodeCount);   // Shader graph
//
// Each subsystem is a property on the TDEngine object. Subsystems are lazy
// (they look up the underlying Module.cwrap on first use) so unused
// subsystems add zero runtime cost.
//
// TDEngine also re-exports TDDeprecated + TDServerRouter for convenience:
//   TDEngine.deprecated.warn('old_api', 'new_api', '2.0');
//   TDEngine.server.getCurrentUrl();
//   TDEngine.server.openSettings();
//
// Compat: TDEngine.bridge === TDBridge, so existing code that uses
// TDBridge directly continues to work unchanged.
// =============================================================================

(function (global) {
  'use strict';

  // Track whether the engine has been initialized.
  let _initialized = false;
  let _ready = false;
  const _readyCallbacks = [];

  function ensureModule() {
    if (!global.TDBridge || !global.TDBridge.wasmExports) {
      throw new Error('TDEngine not initialized — call TDEngine.init(canvasId) first');
    }
    return global.TDBridge.wasmExports;
  }

  // Cache for cwrap'd functions (so we don't re-wrap on every call)
  const _wrapCache = new Map();
  function wrap(name, retType, argTypes) {
    const key = name + '|' + (retType || '') + '|' + (argTypes ? argTypes.join(',') : '');
    if (_wrapCache.has(key)) return _wrapCache.get(key);
    const Module = ensureModule();
    const fn = Module.cwrap(name, retType || null, argTypes || []);
    _wrapCache.set(key, fn);
    return fn;
  }

  // Helper: convert a JS string to a WASM pointer (malloc + write UTF8),
  // return the pointer. Caller MUST call TDEngine._free(ptr) when done.
  // Useful for APIs that take char* but cwrap's 'string' arg type doesn't
  // work for some reason (e.g. very long strings).
  function newStr(s) {
    const Module = ensureModule();
    const bytes = (typeof TextEncoder !== 'undefined')
      ? new TextEncoder().encode(s)
      : Uint8Array.from(s, c => c.charCodeAt(0));
    const ptr = Module._malloc(bytes.length + 1);
    if (!ptr) throw new Error('TDEngine._malloc failed');
    Module.HEAPU8.set(bytes, ptr);
    Module.HEAPU8[ptr + bytes.length] = 0;
    return ptr;
  }
  function free(ptr) { ensureModule()._free(ptr); }

  // -------------------------------------------------------------------------
  // Subsystem: lifecycle
  // -------------------------------------------------------------------------
  const lifecycle = {
    async init(canvasId) {
      if (_initialized) return;
      await global.TDBridge.init(canvasId);
      _initialized = true;
      _ready = true;
      for (const cb of _readyCallbacks) {
        try { cb(); } catch (e) { console.error('[TDEngine] ready cb:', e); }
      }
      _readyCallbacks.length = 0;
    },
    onReady(cb) {
      if (_ready) { try { cb(); } catch (e) { console.error('[TDEngine] ready cb:', e); } return; }
      _readyCallbacks.push(cb);
    },
    shutdown() {
      _initialized = false;
      _ready = false;
      const Module = global.TDBridge && global.TDBridge.wasmExports;
      if (Module) wrap('td_shutdown', null, []).call(null);
    },
    getVersion() { return wrap('td_get_version', 'string', []).call(null); },
    isReady() { return _ready; },
    resize(w, h) { wrap('td_resize', null, ['number', 'number']).call(null, w, h); },
  };

  // -------------------------------------------------------------------------
  // Subsystem: ECS
  // -------------------------------------------------------------------------
  const ecs = {
    create(name) {
      return wrap('td_create_entity', 'number', ['string']).call(null, name || 'Entity');
    },
    destroy(id) { wrap('td_entity_destroy', null, ['number']).call(null, id); },
    isValid(id) { return wrap('td_entity_is_valid', 'number', ['number']).call(null, id) !== 0; },
    count() { return wrap('td_get_entity_count', 'number', []).call(null); },
    setPosition(id, x, y) { wrap('td_entity_set_position', null, ['number','number','number']).call(null, id, x, y); },
    getPosition(id) {
      // td_entity_get_position writes to two float* out-params; we use
      // Module._malloc + HEAPF32 to read them back.
      const Module = ensureModule();
      const ptr = Module._malloc(8);
      try {
        wrap('td_entity_get_position', null, ['number','number']).call(null, id, ptr);
        return { x: Module.HEAPF32[ptr >> 2], y: Module.HEAPF32[(ptr >> 2) + 1] };
      } finally { Module._free(ptr); }
    },
    setVelocity(id, vx, vy) { wrap('td_entity_set_velocity', null, ['number','number','number']).call(null, id, vx, vy); },
    setSprite(id, w, h, r, g, b, a) {
      wrap('td_entity_set_sprite', null, ['number','number','number','number','number','number','number'])
        .call(null, id, w, h, r, g, b, a);
    },
    setCollider(id, w, h) { wrap('td_entity_set_collider', null, ['number','number','number']).call(null, id, w, h); },
  };

  // -------------------------------------------------------------------------
  // Subsystem: Input
  // -------------------------------------------------------------------------
  const input = {
    isKeyDown(vk) { return wrap('td_is_key_down', 'number', ['number']).call(null, vk) !== 0; },
    isMouseDown(button) { return wrap('td_is_mouse_down', 'number', ['number']).call(null, button) !== 0; },
    getMousePos() {
      const Module = ensureModule();
      const ptr = Module._malloc(8);
      try {
        wrap('td_get_mouse_pos', null, ['number']).call(null, ptr);
        return { x: Module.HEAPF32[ptr >> 2], y: Module.HEAPF32[(ptr >> 2) + 1] };
      } finally { Module._free(ptr); }
    },
    // Win32 VK codes for convenience
    Key: {
      Backspace: 0x08, Tab: 0x09, Enter: 0x0D, Escape: 0x1B, Space: 0x20,
      Left: 0x25, Up: 0x26, Right: 0x27, Down: 0x28,
      A: 0x41, B: 0x42, C: 0x43, D: 0x44, E: 0x45, F: 0x46, G: 0x47,
      H: 0x48, I: 0x49, J: 0x4A, K: 0x4B, L: 0x4C, M: 0x4D, N: 0x4E,
      O: 0x4F, P: 0x50, Q: 0x51, R: 0x52, S: 0x53, T: 0x54, U: 0x55,
      V: 0x56, W: 0x57, X: 0x58, Y: 0x59, Z: 0x5A,
      Num0: 0x30, Num1: 0x31, Num2: 0x32, Num3: 0x33, Num4: 0x34,
      Num5: 0x35, Num6: 0x36, Num7: 0x37, Num8: 0x38, Num9: 0x39,
      F1: 0x70, F2: 0x71, F3: 0x72, F4: 0x73, F5: 0x74, F6: 0x75,
      F7: 0x76, F8: 0x77, F9: 0x78, F10: 0x79, F11: 0x7A, F12: 0x7B,
      Shift: 0x10, Control: 0x11, Alt: 0x12,
    },
    Mouse: { Left: 0, Right: 1, Middle: 2 },
  };

  // -------------------------------------------------------------------------
  // Subsystem: Beat / rhythm
  // -------------------------------------------------------------------------
  const beat = {
    start(entityId, bpm, windowHalfSec) {
      wrap('td_beat_start', null, ['number','number','number']).call(null, entityId, bpm, windowHalfSec || 0.15);
    },
    stop(entityId) { wrap('td_beat_stop', null, ['number']).call(null, entityId); },
    isOnBeat(entityId) { return wrap('td_beat_is_on_beat', 'number', ['number']).call(null, entityId) !== 0; },
    getCount(entityId) { return wrap('td_beat_get_count', 'number', ['number']).call(null, entityId); },
    getNextBeatTime(entityId) { return wrap('td_beat_get_next_beat_time', 'number', ['number']).call(null, entityId); },
    getLastBeatTime(entityId) { return wrap('td_beat_get_last_beat_time', 'number', ['number']).call(null, entityId); },
    registerHit(entityId, strict) {
      return wrap('td_beat_register_hit', 'number', ['number','number']).call(null, entityId, strict ? 1 : 0);
    },
    getCombo(entityId) { return wrap('td_beat_get_combo', 'number', ['number']).call(null, entityId); },
    getBestCombo(entityId) { return wrap('td_beat_get_best_combo', 'number', ['number']).call(null, entityId); },
    resetCombo(entityId) { wrap('td_beat_reset_combo', null, ['number']).call(null, entityId); },
    setBpm(entityId, bpm) { wrap('td_beat_set_bpm', null, ['number','number']).call(null, entityId, bpm); },
    setCallback(cb) {
      // td_beat_set_callback expects a C function pointer. Emscripten's
      // addFunction lets us register a JS function as a callable pointer.
      const Module = ensureModule();
      const ptr = Module.addFunction(cb, 'vi');  // void(int)
      wrap('td_beat_set_callback', null, ['number']).call(null, ptr);
      return ptr;
    },
    playSound(entityId, wavIndex) { wrap('td_beat_play_sound', null, ['number','number']).call(null, entityId, wavIndex); },
  };

  // -------------------------------------------------------------------------
  // Subsystem: Scripting (tdscript VM)
  // -------------------------------------------------------------------------
  const script = {
    load(src, name) { return wrap('td_script_load', 'number', ['string','string']).call(null, src, name || '<web>'); },
    call(handle, fnName, argsJson) {
      return wrap('td_script_call', 'string', ['number','string','string']).call(null, handle, fnName, argsJson || '[]');
    },
    unload(handle) { wrap('td_script_unload', null, ['number']).call(null, handle); },
  };

  // -------------------------------------------------------------------------
  // Subsystem: i18n / Localization
  // -------------------------------------------------------------------------
  const i18n = {
    load(localeStr, json) { wrap('td_i18n_load', null, ['string','string']).call(null, localeStr, json); },
    setLocale(localeStr) { wrap('td_i18n_set_locale', null, ['string']).call(null, localeStr); },
    t(key) { return wrap('td_i18n_t', 'string', ['string']).call(null, key); },
    isRtl() { return wrap('td_i18n_is_rtl', 'number', []).call(null) !== 0; },
  };

  // -------------------------------------------------------------------------
  // Subsystem: Audio
  // -------------------------------------------------------------------------
  const audio = {
    resume() { if (global.TDBridge && TDBridge.resumeAudio) TDBridge.resumeAudio(); },
    // Low-level: fill a stereo int16 PCM buffer. Used internally by the
    // bridge's Web Audio integration; exposed here for completeness.
    fillBuffer(outPtr, numFrames) {
      wrap('td_fill_audio_buffer', null, ['number','number']).call(null, outPtr, numFrames);
    },
  };

  // -------------------------------------------------------------------------
  // Subsystem: Touch (mobile + touchscreens)
  // -------------------------------------------------------------------------
  const touch = {
    beginFrame() { wrap('td_touch_begin_frame', null, []).call(null); },
    start(id, x, y, pressure) { wrap('td_touch_start', null, ['number','number','number','number']).call(null, id, x, y, pressure || 1.0); },
    move(id, x, y, pressure) { wrap('td_touch_move', null, ['number','number','number','number']).call(null, id, x, y, pressure || 1.0); },
    end(id, x, y) { wrap('td_touch_end', null, ['number','number','number']).call(null, id, x, y); },
    count() { return wrap('td_touch_count', 'number', []).call(null); },
    x(idx) { return wrap('td_touch_x', 'number', ['number']).call(null, idx || 0); },
    y(idx) { return wrap('td_touch_y', 'number', ['number']).call(null, idx || 0); },
    pinchScale() { return wrap('td_touch_pinch_scale', 'number', []).call(null); },
  };

  // -------------------------------------------------------------------------
  // Subsystem: Gamepad
  // -------------------------------------------------------------------------
  const gamepad = {
    beginFrame() { wrap('td_gamepad_begin_frame', null, []).call(null); },
    setConnected(idx, connected, id, mapping) {
      wrap('td_gamepad_set_connected', null, ['number','number','string','string']).call(null, idx, connected ? 1 : 0, id || '', mapping || '');
    },
    setButton(idx, btn, pressed) { wrap('td_gamepad_set_button', null, ['number','number','number']).call(null, idx, btn, pressed ? 1 : 0); },
    setAnalog(idx, btn, value) { wrap('td_gamepad_set_analog', null, ['number','number','number']).call(null, idx, btn, value); },
    setAxis(idx, axis, value) { wrap('td_gamepad_set_axis', null, ['number','number','number']).call(null, idx, axis, value); },
    buttonPressed(idx, btn) { return wrap('td_gamepad_button_pressed', 'number', ['number','number']).call(null, idx, btn) !== 0; },
    axis(idx, axis) { return wrap('td_gamepad_axis', 'number', ['number','number']).call(null, idx, axis); },
  };

  // -------------------------------------------------------------------------
  // Subsystem: Shader graph
  // -------------------------------------------------------------------------
  const shaderGraph = {
    compile(nodeCount) { return wrap('td_shader_graph_compile', 'string', ['number']).call(null, nodeCount); },
  };

  // -------------------------------------------------------------------------
  // Subsystem: 3D Physics (PhysicsWorld3D)
  //
  // Full 3D rigid body physics: spheres, boxes, capsules, convex hulls.
  // Sequential impulse solver with Coulomb friction, restitution, sleeping,
  // and constraints (distance, point, hinge).  Backed by the new
  // src/physics/physics_world_3d.{h,cpp} C++ engine, exposed via the
  // td_physics_* C bridge in wasm/emscripten_main.cpp.
  //
  // Usage:
  //   TDEngine.physics.init(0, -9.81, 0);                 // create world with gravity
  //   const floor = TDEngine.physics.addBody(0, 0,-5, 0, true);  // static floor
  //   TDEngine.physics.setBoxCollider(floor, 10, 1, 10);
  //   const ball = TDEngine.physics.addBody(1, 0, 5, 0, false);
  //   TDEngine.physics.setSphereCollider(ball, 0.5);
  //   TDEngine.physics.setRestitution(ball, 0.8);
  //   // Each frame:
  //   TDEngine.physics.step(1/60);
  //   const pos = TDEngine.physics.getPosition(ball);     // {x, y, z}
  // -------------------------------------------------------------------------
  const physics = {
    // ---- World lifecycle -------------------------------------------------
    init(gravityX, gravityY, gravityZ) {
      return wrap('td_physics_init', 'number', ['number','number','number'])
        .call(null, gravityX, gravityY, gravityZ);
    },
    shutdown() { wrap('td_physics_shutdown', null, []).call(null); },
    step(dt)   { wrap('td_physics_step', null, ['number']).call(null, dt); },

    // ---- Body management -------------------------------------------------
    // addBody(mass, x, y, z, isStatic) -> bodyId
    addBody(mass, x, y, z, isStatic) {
      return wrap('td_physics_add_body', 'number',
                  ['number','number','number','number','number'])
        .call(null, mass, x, y, z, isStatic ? 1 : 0);
    },
    bodyCount() {
      return wrap('td_physics_body_count', 'number', []).call(null);
    },

    // ---- Colliders -------------------------------------------------------
    setSphereCollider(bodyId, radius, offX=0, offY=0, offZ=0) {
      wrap('td_physics_set_sphere_collider', null,
           ['number','number','number','number','number'])
        .call(null, bodyId, radius, offX, offY, offZ);
    },
    setBoxCollider(bodyId, hx, hy, hz, offX=0, offY=0, offZ=0) {
      wrap('td_physics_set_box_collider', null,
           ['number','number','number','number','number','number','number'])
        .call(null, bodyId, hx, hy, hz, offX, offY, offZ);
    },
    setCapsuleCollider(bodyId, radius, height, axis=1, offX=0, offY=0, offZ=0) {
      wrap('td_physics_set_capsule_collider', null,
           ['number','number','number','number','number','number','number'])
        .call(null, bodyId, radius, height, axis, offX, offY, offZ);
    },

    // ---- Body state ------------------------------------------------------
    setPosition(bodyId, x, y, z) {
      wrap('td_physics_set_position', null, ['number','number','number','number'])
        .call(null, bodyId, x, y, z);
    },
    setVelocity(bodyId, vx, vy, vz) {
      wrap('td_physics_set_velocity', null, ['number','number','number','number'])
        .call(null, bodyId, vx, vy, vz);
    },
    getPosition(bodyId) {
      const Module = ensureModule();
      const ptr = Module._malloc(12);   // 3 floats
      wrap('td_physics_get_position', null,
           ['number','number','number','number'])
        .call(null, bodyId, ptr, ptr+4, ptr+8);
      const result = {
        x: Module.HEAPF32[ptr >> 2],
        y: Module.HEAPF32[(ptr+4) >> 2],
        z: Module.HEAPF32[(ptr+8) >> 2],
      };
      Module._free(ptr);
      return result;
    },
    getVelocity(bodyId) {
      const Module = ensureModule();
      const ptr = Module._malloc(12);
      wrap('td_physics_get_velocity', null,
           ['number','number','number','number'])
        .call(null, bodyId, ptr, ptr+4, ptr+8);
      const result = {
        x: Module.HEAPF32[ptr >> 2],
        y: Module.HEAPF32[(ptr+4) >> 2],
        z: Module.HEAPF32[(ptr+8) >> 2],
      };
      Module._free(ptr);
      return result;
    },
    getOrientation(bodyId) {
      // Returns quaternion {x, y, z, w}
      const Module = ensureModule();
      const ptr = Module._malloc(16);   // 4 floats
      wrap('td_physics_get_orientation', null,
           ['number','number','number','number','number'])
        .call(null, bodyId, ptr, ptr+4, ptr+8, ptr+12);
      const result = {
        x: Module.HEAPF32[ptr >> 2],
        y: Module.HEAPF32[(ptr+4) >> 2],
        z: Module.HEAPF32[(ptr+8) >> 2],
        w: Module.HEAPF32[(ptr+12) >> 2],
      };
      Module._free(ptr);
      return result;
    },

    // ---- Forces / impulses ----------------------------------------------
    applyForce(bodyId, fx, fy, fz) {
      wrap('td_physics_apply_force', null, ['number','number','number','number'])
        .call(null, bodyId, fx, fy, fz);
    },
    applyImpulse(bodyId, ix, iy, iz) {
      wrap('td_physics_apply_impulse', null, ['number','number','number','number'])
        .call(null, bodyId, ix, iy, iz);
    },
    applyTorque(bodyId, tx, ty, tz) {
      wrap('td_physics_apply_torque', null, ['number','number','number','number'])
        .call(null, bodyId, tx, ty, tz);
    },

    // ---- Material properties --------------------------------------------
    setRestitution(bodyId, e) {
      wrap('td_physics_set_restitution', null, ['number','number'])
        .call(null, bodyId, e);
    },
    setFriction(bodyId, f) {
      wrap('td_physics_set_friction', null, ['number','number'])
        .call(null, bodyId, f);
    },
    setGravityScale(bodyId, s) {
      wrap('td_physics_set_gravity_scale', null, ['number','number'])
        .call(null, bodyId, s);
    },
    setUseGravity(bodyId, useGravity) {
      wrap('td_physics_set_use_gravity', null, ['number','number'])
        .call(null, bodyId, useGravity ? 1 : 0);
    },

    // ---- Constraints -----------------------------------------------------
    addDistanceConstraint(bodyA, bodyB, targetDistance) {
      return wrap('td_physics_add_distance_constraint', 'number',
                  ['number','number','number'])
        .call(null, bodyA, bodyB, targetDistance);
    },
    addHingeConstraint(bodyA, bodyB, axisX, axisY, axisZ) {
      return wrap('td_physics_add_hinge_constraint', 'number',
                  ['number','number','number','number','number'])
        .call(null, bodyA, bodyB, axisX, axisY, axisZ);
    },

    // ---- Queries ---------------------------------------------------------
    contactCount() {
      return wrap('td_physics_contact_count', 'number', []).call(null);
    },
    // raycast(ox,oy,oz, dx,dy,dz, maxDist) -> {bodyId, point, normal} or null
    raycast(ox, oy, oz, dx, dy, dz, maxDist) {
      const Module = ensureModule();
      const pPtr = Module._malloc(12);   // 3 floats for point
      const nPtr = Module._malloc(12);   // 3 floats for normal
      const bodyId = wrap('td_physics_raycast', 'number',
        ['number','number','number','number','number','number','number',
         'number','number','number','number','number','number'])
        .call(null, ox, oy, oz, dx, dy, dz, maxDist,
              pPtr, pPtr+4, pPtr+8, nPtr, nPtr+4, nPtr+8);
      let result = null;
      if (bodyId >= 0) {
        result = {
          bodyId,
          point: {
            x: Module.HEAPF32[pPtr >> 2],
            y: Module.HEAPF32[(pPtr+4) >> 2],
            z: Module.HEAPF32[(pPtr+8) >> 2],
          },
          normal: {
            x: Module.HEAPF32[nPtr >> 2],
            y: Module.HEAPF32[(nPtr+4) >> 2],
            z: Module.HEAPF32[(nPtr+8) >> 2],
          },
        };
      }
      Module._free(pPtr);
      Module._free(nPtr);
      return result;
    },
  };

  // -------------------------------------------------------------------------
  // Compose the TDEngine namespace
  // -------------------------------------------------------------------------
  const TDEngine = {
    // Subsystems
    lifecycle, ecs, input, beat, script, i18n, audio, touch, gamepad,
    shaderGraph, physics,

    // Convenience: direct access to the low-level bridge + Module
    get bridge() { return global.TDBridge; },
    get module() { return global.TDBridge && global.TDBridge.wasmExports; },

    // Memory helpers (for advanced use)
    _malloc: function (n) { return ensureModule()._malloc(n); },
    _free: free,
    _newStr: newStr,
    _wrap: wrap,  // escape hatch: TDEngine._wrap('td_xyz', null, ['number'])(42)

    // Re-exports for one-stop-shop API
    get deprecated() { return global.TDDeprecated || null; },
    get server() { return global.TDServerRouter || null; },

    // Version of this API layer (semver)
    version: '1.0.0',
  };

  global.TDEngine = TDEngine;

  // -------------------------------------------------------------------------
  // Deprecation shims: warn when old patterns are used
  // -------------------------------------------------------------------------
  // We can't intercept every legacy call, but we can detect the most common
  // anti-pattern: accessing TDBridge.wasmExports directly instead of going
  // through TDEngine. We log a deprecation warning the first 3 times.
  if (global.TDBridge && global.TDDeprecated) {
    let directAccessCount = 0;
    const origDesc = Object.getOwnPropertyDescriptor(global.TDBridge, 'wasmExports');
    if (origDesc && origDesc.get) {
      try {
        Object.defineProperty(global.TDBridge, 'wasmExports', {
          configurable: true,
          get: function () {
            directAccessCount++;
            if (directAccessCount <= 3 && global.TDDeprecated) {
              global.TDDeprecated.warn(
                'TDBridge.wasmExports (direct)',
                'TDEngine.module or TDEngine.<subsystem>.*',
                '1.0'
              );
            }
            return origDesc.get.call(this);
          },
        });
      } catch (e) { /* if defineProperty fails, skip the deprecation hook */ }
    }
  }

})(typeof window !== 'undefined' ? window : this);
