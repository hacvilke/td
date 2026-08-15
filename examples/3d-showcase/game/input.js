// =============================================================================
// input — Unified keyboard / mouse / gamepad / touch input.
// -----------------------------------------------------------------------------
// Wraps TDEngine.input.* (which calls td_is_key_down / td_get_mouse_pos /
// td_gamepad_axis on the C++ side).  When WASM is loaded, the engine's input
// mirror is authoritative; when it isn't, we maintain our own state in JS.
//
// The game code never branches on backend — it just calls:
//   TDSandbox.input.isKeyDown('W')
//   TDSandbox.input.mouseDelta()
//   TDSandbox.input.gamepadAxis(0, 'leftX')
//   TDSandbox.input.touches()
//
// We also implement "edge-triggered" keys (isKeyJustPressed) which the engine
// doesn't expose directly — usually you'd compare current vs previous frame
// in your game logic.  We do it here once so the game code stays clean.
// =============================================================================

(function (global) {
  'use strict';

  // VK codes (Win32) — matches TDEngine.input.Key.* and the C++ engine's
  // td::Key:: namespace.  We accept either the VK code or a friendly name.
  const KEY_MAP = {
    Backspace: 0x08, Tab: 0x09, Enter: 0x0D, Shift: 0x10, Control: 0x11,
    Alt: 0x12, Escape: 0x1B, Space: 0x20, PageUp: 0x21, PageDown: 0x22,
    End: 0x23, Home: 0x24,
    Left: 0x25, Up: 0x26, Right: 0x27, Down: 0x28,
    Insert: 0x2D, Delete: 0x2E,
    D0: 0x30, D1: 0x31, D2: 0x32, D3: 0x33, D4: 0x34,
    D5: 0x35, D6: 0x36, D7: 0x37, D8: 0x38, D9: 0x39,
    A: 0x41, B: 0x42, C: 0x43, D: 0x44, E: 0x45, F: 0x46, G: 0x47,
    H: 0x48, I: 0x49, J: 0x4A, K: 0x4B, L: 0x4C, M: 0x4D, N: 0x4E,
    O: 0x4F, P: 0x50, Q: 0x51, R: 0x52, S: 0x53, T: 0x54, U: 0x55,
    V: 0x56, W: 0x57, X: 0x58, Y: 0x59, Z: 0x5A,
    F1: 0x70, F2: 0x71, F3: 0x72, F4: 0x73, F5: 0x74, F6: 0x75,
    F7: 0x76, F8: 0x77, F9: 0x78, F10: 0x79, F11: 0x7A, F12: 0x7B,
  };

  // Current + previous frame keyboard state — for edge detection.
  const _down  = Object.create(null);  // vkCode -> true (held this frame)
  const _prev  = Object.create(null);  // vkCode -> true (held last frame)

  // Mouse state.
  const _mouse = {
    x: 0, y: 0,
    deltaX: 0, deltaY: 0,
    buttons: [false, false, false],  // [left, right, middle]
    prevButtons: [false, false, false],
    wheelDelta: 0,
  };

  // Touches (mobile).
  let _touches = [];  // [{id, x, y, pressure}]

  // Pointer lock state.
  let _pointerLocked = false;

  // ---- Helpers -----------------------------------------------------------

  function resolveKey(k) {
    if (typeof k === 'number') return k;
    if (typeof k === 'string') {
      if (KEY_MAP[k] !== undefined) return KEY_MAP[k];
      // Single-char shortcut: 'a' -> 'A' -> 0x41, '1' -> 'D1' -> 0x31
      if (k.length === 1) {
        const upper = k.toUpperCase();
        if (KEY_MAP[upper] !== undefined) return KEY_MAP[upper];
        if (k >= '0' && k <= '9') return KEY_MAP['D' + k];
      }
    }
    return 0;
  }

  // ---- Public API --------------------------------------------------------

  function isKeyDown(k) {
    // If WASM engine is loaded, defer to it for the source of truth.
    if (global.TDEngine && global.TDEngine.input && global.TDEngine.__backend === 'wasm') {
      const vk = resolveKey(k);
      return global.TDEngine.input.isKeyDown(vk);
    }
    return !!_down[resolveKey(k)];
  }

  function isKeyJustPressed(k) {
    const vk = resolveKey(k);
    return !!_down[vk] && !_prev[vk];
  }

  function isKeyJustReleased(k) {
    const vk = resolveKey(k);
    return !_down[vk] && !!_prev[vk];
  }

  function mousePos() {
    return { x: _mouse.x, y: _mouse.y };
  }

  function mouseDelta() {
    return { x: _mouse.deltaX, y: _mouse.deltaY };
  }

  function isMouseDown(button) {
    return !!_mouse.buttons[button || 0];
  }

  function isMouseJustPressed(button) {
    const b = button || 0;
    return _mouse.buttons[b] && !_mouse.prevButtons[b];
  }

  function wheelDelta() { return _mouse.wheelDelta; }

  function isPointerLocked() { return _pointerLocked; }

  function requestPointerLock(el) {
    if (el && el.requestPointerLock) el.requestPointerLock();
  }

  function exitPointerLock() {
    if (document.exitPointerLock) document.exitPointerLock();
  }

  function touches() { return _touches; }

  function gamepadAxis(padIdx, name) {
    const pads = navigator.getGamepads ? navigator.getGamepads() : [];
    const pad = pads[padIdx || 0];
    if (!pad) return 0;
    // Standard mapping: axes[0]=left X, axes[1]=left Y, axes[2]=right X, axes[3]=right Y
    switch (name) {
      case 'leftX':  return pad.axes[0] || 0;
      case 'leftY':  return pad.axes[1] || 0;
      case 'rightX': return pad.axes[2] || 0;
      case 'rightY': return pad.axes[3] || 0;
      case 'triggerL': return pad.buttons[6] ? pad.buttons[6].value : 0;
      case 'triggerR': return pad.buttons[7] ? pad.buttons[7].value : 0;
      default: return 0;
    }
  }

  function gamepadButton(padIdx, buttonIdx) {
    const pads = navigator.getGamepads ? navigator.getGamepads() : [];
    const pad = pads[padIdx || 0];
    if (!pad || !pad.buttons[buttonIdx]) return false;
    return pad.buttons[buttonIdx].pressed;
  }

  // ---- Frame bookkeeping -------------------------------------------------
  //
  // The game loop calls endFrame() at the end of each tick.  This snapshots
  // the current state into _prev so the next frame's edge-detection works.

  function endFrame() {
    // Snapshot keyboard.
    for (const k in _down) _prev[k] = _down[k];
    // Snapshot mouse.
    _mouse.prevButtons[0] = _mouse.buttons[0];
    _mouse.prevButtons[1] = _mouse.buttons[1];
    _mouse.prevButtons[2] = _mouse.buttons[2];
    _mouse.deltaX = 0;
    _mouse.deltaY = 0;
    _mouse.wheelDelta = 0;
  }

  // ---- Event wiring ------------------------------------------------------

  function _onKeyDown(e) {
    const vk = e.keyCode || e.which;
    if (!vk) return;
    _down[vk] = true;
    // Prevent default for game-control keys (space, arrows) to stop page scroll.
    if (vk === 0x20 || vk === 0x25 || vk === 0x26 || vk === 0x27 || vk === 0x28) {
      e.preventDefault();
    }
  }
  function _onKeyUp(e) {
    const vk = e.keyCode || e.which;
    if (!vk) return;
    _down[vk] = false;
  }
  function _onMouseDown(e) {
    if (e.button >= 0 && e.button < 3) _mouse.buttons[e.button] = true;
  }
  function _onMouseUp(e) {
    if (e.button >= 0 && e.button < 3) _mouse.buttons[e.button] = false;
  }
  function _onMouseMove(e) {
    if (_pointerLocked) {
      _mouse.deltaX += e.movementX || 0;
      _mouse.deltaY += e.movementY || 0;
    }
    _mouse.x = e.clientX;
    _mouse.y = e.clientY;
  }
  function _onWheel(e) {
    _mouse.wheelDelta += e.deltaY;
  }
  function _onPointerLockChange() {
    _pointerLocked = !!document.pointerLockElement;
  }
  function _onTouchStart(e) {
    for (const t of e.changedTouches) {
      _touches.push({ id: t.identifier, x: t.clientX, y: t.clientY, pressure: t.force || 1 });
    }
  }
  function _onTouchMove(e) {
    for (const t of e.changedTouches) {
      const existing = _touches.find(x => x.id === t.identifier);
      if (existing) { existing.x = t.clientX; existing.y = t.clientY; existing.pressure = t.force || 1; }
    }
  }
  function _onTouchEnd(e) {
    for (const t of e.changedTouches) {
      _touches = _touches.filter(x => x.id !== t.identifier);
    }
  }

  function attach() {
    window.addEventListener('keydown',  _onKeyDown);
    window.addEventListener('keyup',    _onKeyUp);
    window.addEventListener('mousedown',_onMouseDown);
    window.addEventListener('mouseup',  _onMouseUp);
    window.addEventListener('mousemove',_onMouseMove);
    window.addEventListener('wheel',    _onWheel, { passive: true });
    document.addEventListener('pointerlockchange', _onPointerLockChange);
    window.addEventListener('touchstart', _onTouchStart, { passive: true });
    window.addEventListener('touchmove',  _onTouchMove,  { passive: true });
    window.addEventListener('touchend',   _onTouchEnd,   { passive: true });
    window.addEventListener('touchcancel',_onTouchEnd,   { passive: true });
  }

  // ---- Init --------------------------------------------------------------

  // Wire events immediately — input state is read every frame.
  attach();

  global.TDSandbox = global.TDSandbox || {};
  global.TDSandbox.input = {
    isKeyDown, isKeyJustPressed, isKeyJustReleased,
    mousePos, mouseDelta, isMouseDown, isMouseJustPressed, wheelDelta,
    isPointerLocked, requestPointerLock, exitPointerLock,
    touches, gamepadAxis, gamepadButton,
    endFrame,
    KEY_MAP,
  };
})(typeof window !== 'undefined' ? window : this);
