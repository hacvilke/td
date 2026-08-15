// =============================================================================
// tutorial — Guided feature tour for the showcase.
// -----------------------------------------------------------------------------
// Each step explains one engine subsystem: what it does, the API it exposes,
// and how to try it in the sandbox.  Mirrors the docs page content but in
// an interactive, just-in-time form.
// =============================================================================

(function (global) {
  'use strict';

  const STEPS = [
    {
      title: 'Welcome to TD Sandbox',
      tags: ['overview'],
      body: `
<p><b>TD Sandbox</b> is a 3D physics playground that exercises every TD Engine
subsystem — ECS, physics, input, audio, i18n, persistence, beat, shader graph,
and networking — in a single playable scene.</p>
<p>This tour walks you through each subsystem in about 2 minutes.  You can
dismiss it at any time with the <code>?</code> button in the top-right corner,
or close it now and explore on your own.</p>
<p>By the end you'll have a working reference template you can copy to start
your own 3D game.  Each subsystem shown here is implemented for real in the
engine's C++ core and exposed through the JavaScript <code>TDEngine.*</code>
API.</p>`,
    },
    {
      title: '3D Physics Engine',
      tags: ['physics', 'rigidbody', 'collision', 'raycast'],
      body: `
<p>The scene in front of you is simulated by TD Engine's 3D physics:
rigid body dynamics, GJK/EPA collision detection, sequential impulse solver
with restitution and Coulomb friction, sleeping, and raycast queries.</p>
<p>The C++ engine lives in <code>src/physics/</code> (~2,800 LOC) and exposes
28 functions through the C bridge (<code>td_physics_*</code>).  The JavaScript
API wraps them under <code>TDEngine.physics.*</code>:</p>
<pre>TDEngine.physics.init(0, -9.81, 0);
const ball = TDEngine.physics.addBody(1, 0, 5, 0, false);
TDEngine.physics.setSphereCollider(ball, 0.5);
TDEngine.physics.setRestitution(ball, 0.8);
TDEngine.physics.step(1/60);
const pos = TDEngine.physics.getPosition(ball);  // {x, y, z}</pre>
<p>Try it now: <b>press 1-4 to spawn physics props</b>, then <b>left-click
to fire an energy ball</b> that applies an impulse on impact.</p>`,
    },
    {
      title: 'ECS (Entity-Component-System)',
      tags: ['ecs', 'archetype', 'data-oriented'],
      body: `
<p>Every object in the scene — the floor, the player capsule, the boxes, the
energy balls — is an <b>entity</b> managed by TD Engine's Archetype ECS
(<code>src/ecs/</code>).  Archetype ECS stores components in cache-friendly
contiguous arrays for data-oriented performance.</p>
<p>The JavaScript API mirrors the C++ side:</p>
<pre>const id = TDEngine.ecs.create('Player');
TDEngine.ecs.setPosition(id, 100, 200);
TDEngine.ecs.setSprite(id, 32, 32, 1, 1, 1, 1);
TDEngine.ecs.setCollider(id, 32, 32);
TDEngine.ecs.count();  // entity count
TDEngine.ecs.destroy(id);</pre>
<p>In this showcase each entity also owns a physics body — the game loop
calls <code>ecs.syncFromPhysics(e)</code> each frame to copy the body's
transform back into the entity before rendering.</p>`,
    },
    {
      title: 'Input (Keyboard + Mouse + Gamepad + Touch)',
      tags: ['input', 'keyboard', 'mouse', 'gamepad', 'touch'],
      body: `
<p>TD Engine's input subsystem (<code>src/platform/</code>) mirrors the
Win32 virtual-key codes so the same code works on Windows desktop and in
the browser.  The JS API exposes keyboard, mouse, touch, and gamepad:</p>
<pre>TDEngine.input.isKeyDown(TDEngine.input.Key.Space);
TDEngine.input.isMouseDown(TDEngine.input.Mouse.Left);
TDEngine.input.getMousePos();
TDEngine.input.touch.count();
TDEngine.input.gamepad.axis(0, 0);</pre>
<p><b>Try it now:</b></p>
<p><kbd>W</kbd><kbd>A</kbd><kbd>S</kbd><kbd>D</kbd> — move the player capsule<br>
<kbd>Mouse</kbd> — look around (click canvas first to capture pointer)<br>
<kbd>Space</kbd> — jump<br>
<kbd>Left-click</kbd> — fire energy ball<br>
<kbd>R</kbd> — reset the scene</p>`,
    },
    {
      title: 'Audio (Procedural SFX)',
      tags: ['audio', 'webaudio', 'sfx'],
      body: `
<p>TD Engine's audio backend (<code>src/audio/</code>) mixes PCM samples
through <code>td_fill_audio_buffer</code>, with the browser's Web Audio API
providing the device.  The JavaScript API:</p>
<pre>TDEngine.audio.resume();  // unlock audio (browser policy)
TDEngine.audio.fillBuffer(outPtr, numFrames);</pre>
<p>In this showcase we synthesize short procedural SFX (jump, land, shoot,
spawn, beat) using Web Audio oscillators and noise bursts.  In a real game
you'd ship <code>.wav</code> files and let the C++ mixer handle them.</p>
<p><b>Try it now:</b> jump (you'll hear a blip), fire a ball (zap), or toggle
the beat system to hear the rhythm pulse.</p>`,
    },
    {
      title: 'i18n (Runtime Locale Switching)',
      tags: ['i18n', 'localization', 'locale'],
      body: `
<p>TD Engine ships an i18n subsystem (<code>src/i18n/</code>) that supports
runtime locale switching — no page reload required.  The C++ side stores
string tables per locale; the JS API loads them and translates keys:</p>
<pre>TDEngine.i18n.load('es', jsonStr);
TDEngine.i18n.setLocale('es');
TDEngine.i18n.t('hint.jump');  // -> "Saltar"
TDEngine.i18n.isRtl();          // -> false</pre>
<p><b>Try it now:</b> click the <code>🌐</code> button in the top bar to
cycle through English → Español → Français.  All HUD labels update instantly.</p>`,
    },
    {
      title: 'Persistence (Save / Load)',
      tags: ['persistence', 'save', 'load', 'localStorage'],
      body: `
<p>TD Engine's persistence layer (<code>web/persistence.js</code>) wraps
the browser's <code>localStorage</code> (or IndexedDB for larger blobs) in
a versioned, namespaced API:</p>
<pre>TDPersistence.save('mygame', 'slot1', { entities, score });
const state = TDPersistence.load('mygame', 'slot1');
TDPersistence.listSlots('mygame');</pre>
<p><b>Try it now:</b> spawn some props, then press <code>💾</code> in the
top bar to save the scene.  Press <code>↺</code> to reset, then <code>📂</code>
to restore your saved state.</p>`,
    },
    {
      title: 'Beat / Rhythm + Shader Graph',
      tags: ['beat', 'rhythm', 'shader', 'glsl', 'sync'],
      body: `
<p>Two subsystems, one demo:</p>
<p><b>Beat</b> (<code>src/beat/</code>) — a BPM-accurate scheduler.  The C++
side fires a callback on every beat; the JS API exposes <code>isOnBeat</code>,
<code>registerHit</code>, combo tracking, and a callback hook:</p>
<pre>TDEngine.beat.start(entityId, 120, 0.15);
TDEngine.beat.isOnBeat(entityId);     // true near beat boundary
TDEngine.beat.registerHit(entityId, true);
TDEngine.beat.setCallback(() => { /* on beat */ });</pre>
<p><b>Shader Graph</b> (<code>src/renderer/shader_graph.cpp</code>) — a node
graph compiler that emits GLSL.  The floor you're standing on uses a custom
shader that pulses on every beat.  Press <kbd>B</kbd> to toggle the beat
system and watch the floor react.</p>
<p><b>Try it now:</b> press <kbd>B</kbd>, then fire an energy ball while the
floor is pulsing — on-beat shots get a bonus impulse.</p>`,
    },
  ];

  let _currentStep = 0;
  let _onClose = null;

  function show(step) {
    _currentStep = Math.max(0, Math.min(step, STEPS.length - 1));
    render();
    document.getElementById('tutorial-overlay').classList.remove('hidden');
  }

  function hide() {
    document.getElementById('tutorial-overlay').classList.add('hidden');
    if (_onClose) _onClose();
  }

  function next() {
    if (_currentStep < STEPS.length - 1) show(_currentStep + 1);
    else hide();
  }

  function prev() {
    if (_currentStep > 0) show(_currentStep - 1);
  }

  function render() {
    const s = STEPS[_currentStep];
    document.getElementById('tutorial-step-num').textContent = `${_currentStep + 1} / ${STEPS.length}`;
    document.getElementById('tutorial-title').textContent = s.title;
    document.getElementById('tutorial-body').innerHTML = s.body;
    const tags = document.getElementById('tutorial-tags');
    tags.innerHTML = '';
    (s.tags || []).forEach(t => {
      const el = document.createElement('span');
      el.className = 'tut-tag';
      el.textContent = t;
      tags.appendChild(el);
    });
    // Dots
    const dots = document.getElementById('tutorial-dots');
    dots.innerHTML = '';
    for (let i = 0; i < STEPS.length; i++) {
      const d = document.createElement('span');
      d.className = 'tut-dot';
      if (i === _currentStep) d.classList.add('active');
      else if (i < _currentStep) d.classList.add('done');
      dots.appendChild(d);
    }
    // Prev/Next disabled states
    document.getElementById('tutorial-prev').disabled = _currentStep === 0;
    const nextBtn = document.getElementById('tutorial-next');
    nextBtn.textContent = _currentStep === STEPS.length - 1 ? 'Finish' : 'Next →';
  }

  function onClose(cb) { _onClose = cb; }

  // Wire buttons.
  function attach() {
    document.getElementById('tutorial-close').addEventListener('click', hide);
    document.getElementById('tutorial-next').addEventListener('click', next);
    document.getElementById('tutorial-prev').addEventListener('click', prev);
    document.getElementById('btn-tutorial').addEventListener('click', () => show(0));
  }

  global.TDSandbox = global.TDSandbox || {};
  global.TDSandbox.tutorial = { show, hide, next, prev, onClose, attach, STEPS };
})(typeof window !== 'undefined' ? window : this);
