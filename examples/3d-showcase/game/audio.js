// =============================================================================
// audio — Procedural sound effects via Web Audio API.
// -----------------------------------------------------------------------------
// Mirrors TDEngine.audio.* (which calls td_fill_audio_buffer on the C++ side
// for the engine's PCM mixer).  When WASM is loaded, the engine's audio
// backend is authoritative; we still use Web Audio directly here for the
// procedural SFX because they're synthesized at runtime (no asset loading).
//
// In a real game you'd ship .wav files and use TDEngine.audio.* to mix them.
// Here we synthesize short bleeps so the showcase is fully self-contained —
// no asset downloads needed for the demo to be audible.
// =============================================================================

(function (global) {
  'use strict';

  let _ctx = null;
  let _master = null;
  let _muted = false;

  function ensureContext() {
    if (_ctx) return _ctx;
    try {
      const Ctx = global.AudioContext || global.webkitAudioContext;
      if (!Ctx) return null;
      _ctx = new Ctx();
      _master = _ctx.createGain();
      _master.gain.value = 0.4;
      _master.connect(_ctx.destination);
    } catch (e) {
      _ctx = null;
    }
    return _ctx;
  }

  function resume() {
    const ctx = ensureContext();
    if (ctx && ctx.state === 'suspended') ctx.resume();
    // Also tell the engine's audio subsystem to resume — this calls
    // TDBridge.resumeAudio() which flips the Web Audio node the engine
    // owns (created on first td_init call) into running state.
    if (global.TDEngine && global.TDEngine.audio && global.TDEngine.audio.resume) {
      try { global.TDEngine.audio.resume(); } catch (e) { /* noop */ }
    }
  }

  // ---- Synthesised one-shot SFX -----------------------------------------
  //
  // Each SFX is a short oscillator + gain envelope.  Tiny, but enough to
  // confirm the audio subsystem is wired up end-to-end.

  function blip(freq, durationMs, type, gainPeak) {
    const ctx = ensureContext();
    if (!ctx || _muted) return;
    const osc = ctx.createOscillator();
    const gain = ctx.createGain();
    osc.type = type || 'sine';
    osc.frequency.value = freq;
    const now = ctx.currentTime;
    const dur = (durationMs || 100) / 1000;
    gain.gain.setValueAtTime(0, now);
    gain.gain.linearRampToValueAtTime(gainPeak || 0.3, now + 0.005);
    gain.gain.exponentialRampToValueAtTime(0.0001, now + dur);
    osc.connect(gain).connect(_master);
    osc.start(now);
    osc.stop(now + dur + 0.02);
  }

  function noiseBurst(durationMs, gainPeak, filterFreq) {
    const ctx = ensureContext();
    if (!ctx || _muted) return;
    const dur = (durationMs || 80) / 1000;
    const buffer = ctx.createBuffer(1, Math.ceil(ctx.sampleRate * dur), ctx.sampleRate);
    const data = buffer.getChannelData(0);
    for (let i = 0; i < data.length; i++) {
      data[i] = (Math.random() * 2 - 1) * (1 - i / data.length);
    }
    const src = ctx.createBufferSource();
    src.buffer = buffer;
    const filter = ctx.createBiquadFilter();
    filter.type = 'lowpass';
    filter.frequency.value = filterFreq || 800;
    const gain = ctx.createGain();
    gain.gain.value = gainPeak || 0.2;
    src.connect(filter).connect(gain).connect(_master);
    src.start();
  }

  // Named SFX library — used by the game code.
  const SFX = {
    jump:    () => blip(440, 120, 'square', 0.18),
    land:    () => noiseBurst(60, 0.12, 400),
    shoot:   () => { blip(880, 80, 'sawtooth', 0.18); noiseBurst(40, 0.08, 2400); },
    spawn:   () => blip(660, 90, 'triangle', 0.20),
    beat:    () => blip(1320, 50, 'sine', 0.10),
    impact:  (intensity) => noiseBurst(80 + (intensity||0.5)*100, 0.06 + (intensity||0.5)*0.15, 200 + (intensity||0.5)*1500),
    toggle:  () => blip(523, 60, 'triangle', 0.15),
    save:    () => { blip(523, 60, 'sine', 0.15); setTimeout(()=>blip(784, 80, 'sine', 0.15), 60); },
    load:    () => { blip(784, 60, 'sine', 0.15); setTimeout(()=>blip(523, 80, 'sine', 0.15), 60); },
  };

  function play(name, ...args) {
    if (SFX[name]) SFX[name](...args);
  }

  function setMuted(m) {
    _muted = m;
    if (_master) _master.gain.value = m ? 0 : 0.4;
  }
  function isMuted() { return _muted; }

  global.TDSandbox = global.TDSandbox || {};
  global.TDSandbox.audio = { resume, play, setMuted, isMuted, SFX };
})(typeof window !== 'undefined' ? window : this);
