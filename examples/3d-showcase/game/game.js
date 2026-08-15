// =============================================================================
// game — Main game loop for TD Sandbox.
// -----------------------------------------------------------------------------
// Wires together all the engine subsystems into a playable 3D physics
// playground.  This is the "Root.td" / main scene script equivalent — in a
// real game you'd write this logic in TDScript (.td) and it would run on
// the server, with the client just rendering the replicated state.  Here
// we run it client-side because there's no server.
//
// Subsystems exercised (in order of first use during boot):
//   1. TDEngine.lifecycle.init       — boot the engine on a canvas
//   2. TDEngine.physics.*            — 3D rigid body world
//   3. TDSandbox.ecs.*               — entity management
//   4. TDSandbox.renderer.*          — WebGL2 scene rendering
//   5. TDSandbox.input.*             — keyboard/mouse/gamepad/touch
//   6. TDSandbox.audio.*             — procedural SFX
//   7. TDSandbox.i18n.*              — runtime locale switching
//   8. TDSandbox.beat.*              — rhythm-accurate scheduler
//   9. TDSandbox.network.*           — project.td config + RPC stubs
//  10. TDPersistence.*               — save/load scene state
//  11. TDSandbox.tutorial.*          — feature tour overlay
// =============================================================================

(function (global) {
  'use strict';

  const TDS = global.TDSandbox;
  const TDE = global.TDEngine;

  // ---- Game state -------------------------------------------------------
  const STATE = {
    running: false,
    player: null,        // { entity, bodyId }
    floor: null,         // { entity, bodyId }
    projectiles: [],     // [{ entity, bodyId, life }]
    pendingImpulses: [], // for on-beat bonus
    godMode: false,
    lastTime: 0,
    fpsAvg: 0,
    fpsCounter: 0,
    fpsLastReport: 0,
    lineSegments: [],    // for raycast viz
    lineLife: 0,
  };

  // ---- Boot -------------------------------------------------------------

  async function boot() {
    setStatus('Detecting engine backend…');
    const hasWasm = await global.TDWasmDetector.detect();
    TDE.__backend = hasWasm ? 'wasm' : 'js-fallback';
    console.log('[td-sandbox] backend:', TDE.__backend);

    setStatus(hasWasm ? 'Initializing WASM engine…' : 'Loading JS fallback…');
    if (hasWasm) {
      try {
        await TDE.lifecycle.init('game-canvas');
      } catch (e) {
        console.warn('[td-sandbox] WASM init failed, falling back to JS:', e);
        TDE.__backend = 'js-fallback';
      }
    }
    if (TDE.__backend === 'js-fallback') {
      // Install the JS physics as TDEngine.physics.
      Object.assign(TDE, { physics: TDS.physicsJs });
      // Mock lifecycle.init so later code that calls it doesn't break.
      if (!TDE.lifecycle) TDE.lifecycle = { init: async () => {}, getVersion: () => 'js-fallback', resize: () => {} };
      if (!TDE.ecs) TDE.ecs = { create: (n) => TDS.ecs.create(n).id, count: () => TDS.ecs.count() };
    }

    setStatus('Initializing renderer…');
    TDS.renderer.init(document.getElementById('game-canvas'));
    TDS.renderer.resize();
    window.addEventListener('resize', () => TDS.renderer.resize());

    setStatus('Initializing physics…');
    TDE.physics.init(0, -9.81, 0);

    setStatus('Loading project config…');
    await TDS.network.loadProjectConfig();

    setStatus('Spawning scene…');
    spawnFloor();
    spawnPlayer();
    spawnInitialProps();

    setStatus('Starting beat scheduler…');
    TDS.beat.start(120);

    setStatus('Ready');
    setTimeout(() => {
      document.getElementById('loading-screen').classList.add('hidden');
    }, 200);

    // Wire UI buttons.
    wireUI();
    // Wire tutorial overlay.
    TDS.tutorial.attach();
    // Show tutorial on first visit.
    if (!localStorage.getItem('td-sandbox-tutorial-seen')) {
      TDS.tutorial.show(0);
      localStorage.setItem('td-sandbox-tutorial-seen', '1');
    }

    // Start game loop.
    STATE.running = true;
    STATE.lastTime = performance.now();
    requestAnimationFrame(loop);
  }

  function setStatus(msg) {
    const el = document.getElementById('loading-status');
    if (el) el.textContent = msg;
    const bar = document.getElementById('loading-bar-fill');
    if (bar) {
      const pct = parseFloat(bar.style.width) || 0;
      bar.style.width = Math.min(100, pct + 15) + '%';
    }
  }

  // ---- Scene construction ----------------------------------------------

  function spawnFloor() {
    const e = TDS.ecs.create('Floor', 'floor');
    const bodyId = TDE.physics.addBody(0, 0, 0, 0, true);
    TDE.physics.setStaticPlaneCollider(bodyId);
    e.bodyId = bodyId;
    e.color = { r: 0.10, g: 0.18, b: 0.28 };
    STATE.floor = { entity: e, bodyId };
  }

  function spawnPlayer() {
    const e = TDS.ecs.create('Player', 'player');
    const bodyId = TDE.physics.addBody(1, 0, 2, 0, false);
    TDE.physics.setCapsuleCollider(bodyId, 0.4, 1.6);
    TDE.physics.setFriction(bodyId, 0.6);
    TDE.physics.setRestitution(bodyId, 0.0);
    e.bodyId = bodyId;
    e.color = { r: 0.40, g: 0.90, b: 0.95 };
    STATE.player = { entity: e, bodyId };
  }

  function spawnInitialProps() {
    // A few starting boxes to play with.
    for (let i = 0; i < 6; i++) {
      spawnProp('box', -3 + i * 1.2, 5 + i * 0.5, -3 + (i % 3));
    }
    // A bouncy sphere.
    spawnProp('sphere', 3, 4, 2);
    // A pendulum anchored to the sky.
    spawnPendulum(0, 8, -5);
  }

  function spawnProp(kind, x, y, z, color) {
    const e = TDS.ecs.create(kind, 'prop');
    const bodyId = TDE.physics.addBody(1, x, y, z, false);
    if (kind === 'sphere') {
      TDE.physics.setSphereCollider(bodyId, 0.5);
      TDE.physics.setRestitution(bodyId, 0.7);
    } else if (kind === 'box') {
      TDE.physics.setBoxCollider(bodyId, 0.5, 0.5, 0.5);
      TDE.physics.setRestitution(bodyId, 0.2);
    } else if (kind === 'capsule') {
      TDE.physics.setCapsuleCollider(bodyId, 0.4, 1.0);
    }
    e.bodyId = bodyId;
    e.color = color || { r: 0.7 + Math.random() * 0.3, g: 0.5 + Math.random() * 0.3, b: 0.3 + Math.random() * 0.3 };
    return e;
  }

  function spawnStack(x, z, count) {
    for (let i = 0; i < (count || 5); i++) {
      spawnProp('box', x, 0.5 + i * 1.05, z, { r: 0.8 - i * 0.1, g: 0.4, b: 0.3 });
    }
  }

  function spawnPendulum(x, y, z) {
    // Anchor: a static body in the sky.  Bob: a dynamic sphere connected
    // by a distance constraint.  Demonstrates the constraint solver.
    const anchor = TDS.ecs.create('PendulumAnchor', 'anchor');
    const anchorBody = TDE.physics.addBody(0, x, y, z, true);
    anchor.bodyId = anchorBody;
    anchor.color = { r: 0.5, g: 0.5, b: 0.5 };
    const bob = TDS.ecs.create('PendulumBob', 'prop');
    const bobBody = TDE.physics.addBody(1, x, y - 3, z, false);
    TDE.physics.setSphereCollider(bobBody, 0.4);
    TDE.physics.setRestitution(bobBody, 0.5);
    bob.bodyId = bobBody;
    bob.color = { r: 0.9, g: 0.7, b: 0.2 };
    bob.data.isPendulum = true;
    bob.data.anchorBody = anchorBody;
    TDE.physics.addDistanceConstraint(anchorBody, bobBody, 3.0);
    return bob;
  }

  function spawnEnergyBall(origin, direction) {
    const e = TDS.ecs.create('EnergyBall', 'projectile');
    const bodyId = TDE.physics.addBody(0.2, origin.x, origin.y, origin.z, false);
    TDE.physics.setSphereCollider(bodyId, 0.25);
    TDE.physics.setRestitution(bodyId, 0.9);
    TDE.physics.setFriction(bodyId, 0.1);
    TDE.physics.setGravityScale(bodyId, 0.0);  // floats
    // Apply initial velocity.
    const speed = 30;
    TDE.physics.applyImpulse(bodyId, direction.x * speed * 0.2, direction.y * speed * 0.2, direction.z * speed * 0.2);
    e.bodyId = bodyId;
    e.color = { r: 0.5, g: 0.95, b: 1.0 };
    e.emissive = 1.0;
    e.tag = 'energy';
    TDS.physicsJs.setTag(bodyId, 'energy');
    STATE.projectiles.push({ entity: e, bodyId, life: 3.0 });
    TDS.audio.play('shoot');
  }

  // ---- Per-frame update -------------------------------------------------

  function loop(now) {
    if (!STATE.running) return;
    const dt = Math.min(0.05, (now - STATE.lastTime) / 1000);
    STATE.lastTime = now;
    update(dt);
    render(now / 1000);
    updateHud(now, dt);
    TDS.input.endFrame();
    requestAnimationFrame(loop);
  }

  function update(dt) {
    // 1) Input → player movement.
    handlePlayerInput(dt);

    // 2) Physics step.
    TDE.physics.step(dt);

    // 3) Sync entity transforms from physics.
    for (const e of TDS.ecs.all()) {
      if (e.bodyId) TDS.ecs.syncFromPhysics(e);
    }

    // 4) Projectiles: tick lifetime, remove dead.
    for (let i = STATE.projectiles.length - 1; i >= 0; i--) {
      const p = STATE.projectiles[i];
      p.life -= dt;
      if (p.life <= 0) {
        TDS.ecs.destroy(p.entity.id);
        STATE.projectiles.splice(i, 1);
      }
    }

    // 5) Beat pulse update.
    TDS.beat.update();

    // 6) Pending on-beat impulses: if any projectiles were queued, give them
    // a bonus impulse on the next beat.
    while (STATE.pendingImpulses.length && TDS.beat.isOnBeat(0.2)) {
      const bodyId = STATE.pendingImpulses.shift();
      TDE.physics.applyImpulse(bodyId, 0, 5, 0);
    }

    // 7) Raycast viz lines fade.
    STATE.lineLife -= dt;
    if (STATE.lineLife <= 0) STATE.lineSegments.length = 0;

    // 8) Camera follows player (first-person).
    //    Camera is locked to the player's head position — no smoothing, no
    //    world-space offset.  Mouse-look (yaw/pitch) controls view direction
    //    only, so the camera always looks where the player is facing.
    //    The previous code placed the camera at player.pos + (0, 3, 6) in
    //    WORLD space, which meant turning 180° put the camera in front of
    //    the player.  First-person avoids that entirely.
    if (STATE.player && !STATE.godMode) {
      const pos = TDE.physics.getPosition(STATE.player.bodyId);
      // Capsule: r=0.4, h=1.6 → total height 2.4m, center at pos.y.
      // Eye height ~0.6m above capsule center (≈1.8m above feet).
      const EYE_OFFSET_Y = 0.6;
      TDS.renderer.setCameraPos(pos.x, pos.y + EYE_OFFSET_Y, pos.z);
    }
  }

  function handlePlayerInput(dt) {
    if (!STATE.player) return;
    const input = TDS.input;
    const bodyId = STATE.player.bodyId;
    const yaw = TDS.renderer.getCameraYaw();

    // Movement: WASD relative to camera yaw.
    let mx = 0, mz = 0;
    if (input.isKeyDown('W')) mz -= 1;
    if (input.isKeyDown('S')) mz += 1;
    if (input.isKeyDown('A')) mx -= 1;
    if (input.isKeyDown('D')) mx += 1;
    // Normalize.
    const len = Math.sqrt(mx * mx + mz * mz);
    if (len > 0) { mx /= len; mz /= len; }
    // Camera forward (XZ plane, ignoring pitch) at yaw=0 is (0, 0, -1).
    // Right at yaw=0 is (1, 0, 0).  Rotating by yaw:
    //   forward = ( sin(yaw), 0, -cos(yaw))
    //   right   = ( cos(yaw), 0,  sin(yaw))
    // W (mz=-1) moves +forward, D (mx=+1) moves +right.
    // velocity = (-mz) * forward + mx * right
    const cos = Math.cos(yaw), sin = Math.sin(yaw);
    const wx = (-mz) * sin + mx * cos;
    const wz = (-mz) * (-cos) + mx * sin;
    const speed = STATE.godMode ? 15 : 8;
    TDE.physics.applyForce(bodyId, wx * speed * 4, 0, wz * speed * 4);

    // Gamepad: left stick (same convention).
    const gx = input.gamepadAxis(0, 'leftX');
    const gy = input.gamepadAxis(0, 'leftY');
    if (Math.abs(gx) > 0.1 || Math.abs(gy) > 0.1) {
      const gwx = (-gy) * sin + gx * cos;
      const gwz = (-gy) * (-cos) + gx * sin;
      TDE.physics.applyForce(bodyId, gwx * speed * 4, 0, gwz * speed * 4);
    }

    // Jump (edge-triggered).
    if (input.isKeyJustPressed('Space')) {
      const v = TDE.physics.getVelocity(bodyId);
      if (v.y < 1.0) {  // only if grounded-ish
        TDE.physics.applyImpulse(bodyId, 0, 6, 0);
        TDS.audio.play('jump');
      }
    }

    // Mouse look (only when pointer is locked OR when right mouse held).
    // Yaw: mouse right (delta.x > 0) → turn right → yaw increases.
    // Pitch: mouse up (delta.y < 0) → look up → pitch increases.
    const delta = input.mouseDelta();
    if (input.isPointerLocked() || input.isMouseDown(2)) {
      const newYaw = TDS.renderer.getCameraYaw() + delta.x * 0.0025;
      const newPitch = TDS.renderer.getCameraPitch() - delta.y * 0.0025;
      TDS.renderer.setCameraYawPitch(newYaw, Math.max(-1.4, Math.min(1.4, newPitch)));
    }

    // Shoot: left-click fires an energy ball from the camera.
    if (input.isMouseJustPressed(0)) {
      const cam = TDS.renderer.getCameraPos();
      // Direction from camera yaw + pitch.
      const y = TDS.renderer.getCameraYaw();
      const p = TDS.renderer.getCameraPitch();
      const cp = Math.cos(p);
      const dir = {
        x: Math.sin(y) * cp,
        y: Math.sin(p),
        z: -Math.cos(y) * cp,
      };
      // Spawn slightly in front of camera so we don't self-collide.
      const origin = { x: cam.x + dir.x, y: cam.y + dir.y, z: cam.z + dir.z };
      spawnEnergyBall(origin, dir);
      // If on beat, queue a bonus impulse.
      if (TDS.beat.isOnBeat(0.2)) {
        const p = STATE.projectiles[STATE.projectiles.length - 1];
        if (p) STATE.pendingImpulses.push(p.bodyId);
      }
      // Visualize the ray.
      STATE.lineSegments.push({
        a: origin,
        b: { x: origin.x + dir.x * 20, y: origin.y + dir.y * 20, z: origin.z + dir.z * 20 },
      });
      STATE.lineLife = 0.5;
    }

    // Reset scene.
    if (input.isKeyJustPressed('R')) resetScene();

    // Spawn menu shortcuts.
    if (input.isKeyJustPressed('1')) spawnProp('box', 0, 5, 0);
    if (input.isKeyJustPressed('2')) spawnProp('sphere', 0, 5, 0);
    if (input.isKeyJustPressed('3')) spawnProp('capsule', 0, 5, 0);
    if (input.isKeyJustPressed('4')) spawnStack(0, 0, 5);

    // Toggle beat.
    if (input.isKeyJustPressed('B')) {
      TDS.beat.setEnabled(!TDS.beat.isEnabled());
      toast(TDS.beat.isEnabled() ? 'toast.beatOn' : 'toast.beatOff');
      TDS.audio.play('toggle');
    }

    // God mode.
    if (input.isKeyJustPressed('F2')) {
      STATE.godMode = !STATE.godMode;
      TDE.physics.setGravityScale(bodyId, STATE.godMode ? 0 : 1);
      TDE.physics.setUseGravity(bodyId, !STATE.godMode);
      toast(STATE.godMode ? 'toast.godOn' : 'toast.godOff');
      TDS.audio.play('toggle');
    }
    if (STATE.godMode) {
      // Free-fly: space=up, shift=down, mouse steers.
      const v = TDE.physics.getVelocity(bodyId);
      // Damp velocity to make god-mode feel floaty.
      TDE.physics.setVelocity(bodyId, v.x * 0.85, v.y * 0.85, v.z * 0.85);
      if (input.isKeyDown('Space')) TDE.physics.applyImpulse(bodyId, 0, 0.5, 0);
      if (input.isKeyDown('Shift')) TDE.physics.applyImpulse(bodyId, 0, -0.5, 0);
    }
  }

  function render(time) {
    const R = TDS.renderer;
    R.resize();
    R.clear();
    R.renderSky(time);
    const beatPulse = TDS.beat.isEnabled() ? (TDS.beat.update() || 0) : 0;
    R.renderFloor(time, beatPulse);
    // Render all entities (skip floor — it has its own shader).
    for (const e of TDS.ecs.all()) {
      if (e.kind === 'floor') continue;
      R.renderBody({
        position: e.position,
        collider: TDE.physics.getBody(e.bodyId) ? TDE.physics.getBody(e.bodyId).collider : null,
        color: e.color,
        emissive: e.emissive || 0,
        tag: e.tag || e.kind,
      }, time);
    }
    // Render constraint lines (pendulum).
    const lines = [];
    for (const e of TDS.ecs.byKind('prop')) {
      if (e.data && e.data.isPendulum) {
        const anchor = TDE.physics.getPosition(e.data.anchorBody);
        lines.push({ a: anchor, b: e.position });
      }
    }
    // Raycast viz.
    for (const seg of STATE.lineSegments) lines.push(seg);
    R.renderLines(lines, [0.4, 0.9, 1.0]);
  }

  // ---- HUD ---------------------------------------------------------------

  function updateHud(now, dt) {
    STATE.fpsCounter++;
    if (now - STATE.fpsLastReport > 500) {
      const fps = STATE.fpsCounter / ((now - STATE.fpsLastReport) / 1000);
      STATE.fpsAvg = Math.round(fps);
      STATE.fpsCounter = 0;
      STATE.fpsLastReport = now;
      document.getElementById('hud-fps').textContent = STATE.fpsAvg;
      document.getElementById('hud-bodies').textContent = TDE.physics.bodyCount();
      document.getElementById('hud-contacts').textContent = TDE.physics.contactCount();
      document.getElementById('hud-tris').textContent = TDS.renderer.triCount().toLocaleString();
    }
    // Beat indicator pulse.
    const ind = document.getElementById('hud-beat');
    if (TDS.beat.isEnabled() && TDS.beat.isOnBeat(0.1)) {
      ind.classList.add('pulse');
    } else {
      ind.classList.remove('pulse');
    }
    document.getElementById('hud-bpm').textContent = TDS.beat.getBpm();
  }

  // ---- UI wiring --------------------------------------------------------

  function wireUI() {
    // Spawn menu buttons.
    document.querySelectorAll('.spawn-btn').forEach(btn => {
      btn.addEventListener('click', () => {
        const kind = btn.getAttribute('data-spawn');
        const cam = TDS.renderer.getCameraPos();
        const yaw = TDS.renderer.getCameraYaw();
        const pitch = TDS.renderer.getCameraPitch();
        const cp = Math.cos(pitch);
        // Direction camera is looking (matches shoot direction).
        const dir = {
          x: Math.sin(yaw) * cp,
          y: Math.sin(pitch),
          z: -Math.cos(yaw) * cp,
        };
        // Spawn 3m in front of the camera.
        const x = cam.x + dir.x * 3;
        const y = cam.y + 1;
        const z = cam.z + dir.z * 3;
        if (kind === 'box')      spawnProp('box', x, y, z);
        else if (kind === 'sphere')  spawnProp('sphere', x, y, z);
        else if (kind === 'capsule') spawnProp('capsule', x, y, z);
        else if (kind === 'stack')   spawnStack(x, z, 5);
        else if (kind === 'pendulum') spawnPendulum(x, y + 3, z);
        else if (kind === 'energy') {
          const d = { x: dir.x, y: dir.y - 0.2, z: dir.z };
          spawnEnergyBall({ x, y, z }, d);
        }
        TDS.audio.play('spawn');
      });
    });

    // Save / load / export / import / reset.
    document.getElementById('btn-save').addEventListener('click', saveScene);
    document.getElementById('btn-load').addEventListener('click', loadScene);
    document.getElementById('btn-export').addEventListener('click', exportSceneFile);
    document.getElementById('btn-import').addEventListener('click', () => {
      document.getElementById('import-file-input').click();
    });
    document.getElementById('import-file-input').addEventListener('change', importSceneFile);
    document.getElementById('btn-reset').addEventListener('click', resetScene);

    // Locale switcher.
    document.getElementById('btn-locale').addEventListener('click', cycleLocale);

    // Click canvas to capture pointer lock (for mouse-look).
    const canvas = document.getElementById('game-canvas');
    canvas.addEventListener('click', () => {
      if (!TDS.input.isPointerLocked() && !STATE.godMode) {
        TDS.input.requestPointerLock(canvas);
      }
    });
    // ESC to release pointer lock (browser handles this automatically, but
    // also exit god mode if active).
    window.addEventListener('keydown', (e) => {
      if (e.key === 'Escape' && STATE.godMode) {
        STATE.godMode = false;
        TDE.physics.setGravityScale(STATE.player.bodyId, 1);
        TDE.physics.setUseGravity(STATE.player.bodyId, true);
      }
    });

    // Touch: tap spawn menu to drop props on mobile.
    if (TDS.input.touches().length > 0) {
      document.getElementById('btn-tutorial').click();
    }
  }

  function cycleLocale() {
    const locales = TDS.i18n.listLocales();
    const current = TDS.i18n.getLocale();
    const next = locales[(locales.indexOf(current) + 1) % locales.length];
    TDS.i18n.setLocale(next);
    TDS.audio.play('toggle');
  }

  function saveScene() {
    const state = {
      entities: TDS.ecs.serialize(),
      playerPos: TDE.physics.getPosition(STATE.player.bodyId),
    };
    if (global.TDPersistence && global.TDPersistence.save) {
      global.TDPersistence.save('td-sandbox', 'autosave', state);
    } else {
      localStorage.setItem('td-sandbox-autosave', JSON.stringify(state));
    }
    toast('toast.saved');
    TDS.audio.play('save');
  }

  function loadScene() {
    let state;
    if (global.TDPersistence && global.TDPersistence.load) {
      state = global.TDPersistence.load('td-sandbox', 'autosave');
    } else {
      const raw = localStorage.getItem('td-sandbox-autosave');
      if (raw) state = JSON.parse(raw);
    }
    if (!state) { toast('toast.reset'); return; }
    resetScene();
    // Restore entities.
    for (const e of state.entities) {
      const entity = TDS.ecs.create(e.name, e.kind);
      const bodyId = TDE.physics.addBody(1, e.position.x, e.position.y, e.position.z, false);
      if (e.collider) {
        if (e.collider.type === 'sphere')  TDE.physics.setSphereCollider(bodyId, e.collider.radius);
        else if (e.collider.type === 'box') TDE.physics.setBoxCollider(bodyId, e.collider.hx, e.collider.hy, e.collider.hz);
        else if (e.collider.type === 'capsule') TDE.physics.setCapsuleCollider(bodyId, e.collider.radius, e.collider.height);
        if (e.collider.restitution !== undefined) TDE.physics.setRestitution(bodyId, e.collider.restitution);
        if (e.collider.friction !== undefined) TDE.physics.setFriction(bodyId, e.collider.friction);
      }
      entity.bodyId = bodyId;
      entity.color = e.color;
      entity.data = e.data || {};
    }
    if (state.playerPos) TDE.physics.setPosition(STATE.player.bodyId, state.playerPos.x, state.playerPos.y, state.playerPos.z);
    toast('toast.loaded');
    TDS.audio.play('load');
  }

  function resetScene() {
    // Destroy all entities except floor and player.
    for (const e of TDS.ecs.all()) {
      if (e.kind === 'floor' || e.kind === 'player') continue;
      TDS.ecs.destroy(e.id);
    }
    STATE.projectiles.length = 0;
    STATE.pendingImpulses.length = 0;
    STATE.lineSegments.length = 0;
    // Reset player position.
    TDE.physics.setPosition(STATE.player.bodyId, 0, 2, 0);
    TDE.physics.setVelocity(STATE.player.bodyId, 0, 0, 0);
    // Re-spawn initial props.
    spawnInitialProps();
    toast('toast.reset');
  }

  // ---- Export / Import scene as JSON file --------------------------------
  // Ported from the Tariu physics sandbox pattern: instead of just saving
  // to localStorage, let the user download the scene as a .tdscene.json
  // file they can share, version-control, or reload later.  The format
  // matches what saveScene() writes to localStorage so either source can
  // feed loadScene().
  function exportSceneFile() {
    const state = {
      format: 'td-sandbox-scene',
      version: 1,
      savedAt: new Date().toISOString(),
      entities: TDS.ecs.serialize(),
      playerPos: TDE.physics.getPosition(STATE.player.bodyId),
    };
    const json = JSON.stringify(state, null, 2);
    const blob = new Blob([json], { type: 'application/json' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = 'td-sandbox-' + Date.now() + '.tdscene.json';
    document.body.appendChild(a);
    a.click();
    document.body.removeChild(a);
    setTimeout(() => URL.revokeObjectURL(url), 1000);
    toast('toast.saved');
    TDS.audio.play('save');
  }

  function importSceneFile(event) {
    const file = event.target.files && event.target.files[0];
    if (!file) return;
    const reader = new FileReader();
    reader.onload = (e) => {
      try {
        const state = JSON.parse(e.target.result);
        if (!state || !Array.isArray(state.entities)) {
          throw new Error('Not a TD Sandbox scene file');
        }
        resetScene();
        for (const ent of state.entities) {
          const entity = TDS.ecs.create(ent.name, ent.kind);
          const bodyId = TDE.physics.addBody(1, ent.position.x, ent.position.y, ent.position.z, false);
          if (ent.collider) {
            if (ent.collider.type === 'sphere')
              TDE.physics.setSphereCollider(bodyId, ent.collider.radius);
            else if (ent.collider.type === 'box')
              TDE.physics.setBoxCollider(bodyId, ent.collider.hx, ent.collider.hy, ent.collider.hz);
            else if (ent.collider.type === 'capsule')
              TDE.physics.setCapsuleCollider(bodyId, ent.collider.radius, ent.collider.height);
            if (ent.collider.restitution !== undefined) TDE.physics.setRestitution(bodyId, ent.collider.restitution);
            if (ent.collider.friction !== undefined) TDE.physics.setFriction(bodyId, ent.collider.friction);
          }
          entity.bodyId = bodyId;
          entity.color = ent.color;
          entity.data = ent.data || {};
        }
        if (state.playerPos) {
          TDE.physics.setPosition(STATE.player.bodyId, state.playerPos.x, state.playerPos.y, state.playerPos.z);
        }
        toast('toast.loaded');
        TDS.audio.play('load');
      } catch (err) {
        console.error('[td-sandbox] import failed:', err);
        toast('toast.reset');
      }
    };
    reader.readAsText(file);
    // Reset the input so the same file can be re-imported later.
    event.target.value = '';
  }

  function toast(i18nKey) {
    const msg = TDS.i18n.t(i18nKey);
    // Simple toast: show in the loading-status location briefly.
    let toastEl = document.getElementById('td-toast');
    if (!toastEl) {
      toastEl = document.createElement('div');
      toastEl.id = 'td-toast';
      toastEl.style.cssText = 'position:fixed;top:60px;left:50%;transform:translateX(-50%);background:#11161e;border:1px solid #67e8f9;color:#67e8f9;padding:8px 16px;border-radius:6px;font-family:ui-monospace,Menlo,monospace;font-size:13px;z-index:200;opacity:0;transition:opacity 0.2s;pointer-events:none;';
      document.body.appendChild(toastEl);
    }
    toastEl.textContent = msg;
    toastEl.style.opacity = '1';
    clearTimeout(toastEl._t);
    toastEl._t = setTimeout(() => { toastEl.style.opacity = '0'; }, 1600);
  }

  // ---- Crash boundary ---------------------------------------------------

  function crash(message, stack) {
    STATE.running = false;
    document.getElementById('crash-message').textContent = message;
    document.getElementById('crash-stack').textContent = stack || '';
    document.getElementById('crash-screen').classList.add('show');
  }
  document.getElementById('crash-reload').addEventListener('click', () => location.reload());

  // ---- Boot --------------------------------------------------------------

  // Wait for DOM ready, then boot.
  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', boot);
  } else {
    boot();
  }

  // Expose for debugging.
  global.TDSandboxGame = { STATE, boot, resetScene, saveScene, loadScene };

})(typeof window !== 'undefined' ? window : this);
