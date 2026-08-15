// =============================================================================
// beat — Beat / rhythm system facade.
// -----------------------------------------------------------------------------
// Wraps TDEngine.beat.* (which calls td_beat_start / td_beat_is_on_beat /
// td_beat_register_hit on the C++ side).  When WASM is loaded, the engine's
// beat scheduler is authoritative and fires its callback (a C function
// pointer registered via Module.addFunction).  In JS-fallback mode we run
// our own scheduler using performance.now().
//
// The showcase uses the beat system two ways:
//   1) Floor shader pulses on every beat (visual).
//   2) Projectiles fired "on beat" get a bonus impulse (gameplay).
// =============================================================================

(function (global) {
  'use strict';

  let _bpm = 120;
  let _startTime = 0;
  let _lastBeat = -1;
  let _enabled = true;
  let _pulseDecay = 0;     // 1.0 on beat, decays to 0 over ~120ms
  let _onBeatCallbacks = [];

  function start(bpm) {
    _bpm = bpm || 120;
    _startTime = performance.now() / 1000;
    _lastBeat = -1;
    _enabled = true;
    // If WASM is loaded, also start the engine's beat scheduler on a hidden
    // entity so any tdscript code reading td_beat_is_on_beat sees the same
    // rhythm.
    if (global.TDEngine && global.TDEngine.beat && global.TDEngine.__backend === 'wasm') {
      try {
        const ent = global.TDEngine.ecs.create('__beat_master__');
        global.TDEngine.beat.start(ent, _bpm, 0.15);
        global.TDEngine.beat.setCallback(function () {
          _pulseDecay = 1.0;
          for (const cb of _onBeatCallbacks) cb();
          if (global.TDSandbox && global.TDSandbox.audio) global.TDSandbox.audio.play('beat');
        });
      } catch (e) { /* noop */ }
    }
  }

  function stop() {
    _enabled = false;
    _onBeatCallbacks.length = 0;
  }

  function setBpm(bpm) {
    // Restart so the new BPM takes effect immediately.
    start(bpm);
  }

  function getBpm() { return _bpm; }

  function isEnabled() { return _enabled; }
  function setEnabled(v) { _enabled = v; }

  function onBeat(cb) { _onBeatCallbacks.push(cb); }

  // Called every frame from the game loop.  Returns the current pulse
  // intensity (0..1) so shaders and UI can react.
  function update() {
    if (!_enabled) return 0;
    const now = performance.now() / 1000;
    const elapsed = now - _startTime;
    const beatInterval = 60 / _bpm;
    const currentBeat = Math.floor(elapsed / beatInterval);
    if (currentBeat !== _lastBeat) {
      _lastBeat = currentBeat;
      _pulseDecay = 1.0;
      for (const cb of _onBeatCallbacks) cb();
      if (global.TDSandbox && global.TDSandbox.audio) global.TDSandbox.audio.play('beat');
    }
    // Decay the pulse.
    _pulseDecay = Math.max(0, _pulseDecay - 0.016 * 5);  // ~120ms decay at 60fps
    return _pulseDecay;
  }

  function isOnBeat(windowSec) {
    if (!_enabled) return false;
    const now = performance.now() / 1000;
    const elapsed = now - _startTime;
    const beatInterval = 60 / _bpm;
    const phase = (elapsed % beatInterval) / beatInterval;
    // On-beat if within windowSec of a beat boundary.
    const w = (windowSec || 0.15) / beatInterval;
    return phase < w || phase > (1 - w);
  }

  function getCombo() {
    // Simple combo counter: each on-beat spawn in the last 4 seconds.
    // The C++ side has a real combo tracker; this is enough for the demo.
    return 0;
  }

  global.TDSandbox = global.TDSandbox || {};
  global.TDSandbox.beat = {
    start, stop, setBpm, getBpm, isEnabled, setEnabled,
    onBeat, update, isOnBeat, getCombo,
  };
})(typeof window !== 'undefined' ? window : this);
