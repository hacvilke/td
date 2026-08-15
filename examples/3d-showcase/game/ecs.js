// =============================================================================
// ecs — Entity-Component-System facade for the showcase.
// -----------------------------------------------------------------------------
// Wraps TDEngine.ecs.* (which calls td_create_entity / td_entity_set_position
// etc. on the C++ side).  In this showcase every entity also owns a physics
// body (rigid body id from TDEngine.physics.*).  When WASM is loaded, the
// C++ Archetype ECS is the source of truth; in JS-fallback mode we just use
// a Map — the game code never sees the difference.
//
// The facade adds a per-entity "kind" tag so the renderer and game logic can
// dispatch on type (player, prop, projectile, constraint-anchor, etc.).
// =============================================================================

(function (global) {
  'use strict';

  let _nextId = 1;
  const _entities = new Map();

  function create(name, kind) {
    // If the WASM engine is loaded, ask it for a real entity id; otherwise
    // allocate one from our JS-side counter.
    let id;
    if (global.TDEngine && global.TDEngine.ecs && global.TDEngine.__backend === 'wasm') {
      id = global.TDEngine.ecs.create(name || 'Entity');
    } else {
      id = _nextId++;
    }
    const e = {
      id,
      name: name || 'Entity',
      kind: kind || 'prop',  // 'player' | 'prop' | 'projectile' | 'anchor' | 'floor'
      bodyId: 0,             // physics body id (0 = no body)
      position: { x: 0, y: 0, z: 0 },
      color: { r: 0.7, g: 0.7, b: 0.8 },
      emissive: 0,
      // Per-frame flags
      dead: false,
      // Custom data (game code can stash anything here).
      data: {},
    };
    _entities.set(id, e);
    return e;
  }

  function destroy(id) {
    const e = _entities.get(id);
    if (!e) return;
    // Remove the physics body too.  Without this, every "destroy" call
    // leaks a body in the physics world — invisible, still simulated,
    // still consuming broadphase + solver time.  After a few minutes of
    // spawning + resetting, bodyCount balloons and FPS collapses.
    // (This was bug "physics body leak on destroy" — fixed 2026-08-16.)
    if (e.bodyId && global.TDEngine && global.TDEngine.physics && global.TDEngine.physics.removeBody) {
      global.TDEngine.physics.removeBody(e.bodyId);
    }
    e.dead = true;
    _entities.delete(id);
  }

  function get(id) { return _entities.get(id); }
  function all() { return Array.from(_entities.values()); }
  function count() { return _entities.size; }
  function byKind(kind) { return all().filter(e => e.kind === kind && !e.dead); }

  // Sync the entity's transform from the physics body.  Called each frame
  // before render.  When WASM is loaded, this calls td_physics_get_position;
  // otherwise it reads from the JS physics world directly.
  function syncFromPhysics(e) {
    if (!e.bodyId) return;
    const p = global.TDEngine.physics;
    if (!p) return;
    e.position = p.getPosition(e.bodyId);
  }

  // Save state for persistence.  Returns a JSON-safe array.
  function serialize() {
    return all().filter(e => !e.dead && e.kind !== 'floor' && e.kind !== 'player').map(e => ({
      name: e.name,
      kind: e.kind,
      position: e.position,
      color: e.color,
      data: e.data,
      // Reconstruct the collider from the physics body so we can respawn it.
      collider: e.bodyId ? p_getColliderDesc(e.bodyId) : null,
    }));
  }

  // (Helper used by serialize; reads collider info from the physics body.)
  function p_getColliderDesc(bodyId) {
    const p = global.TDEngine.physics;
    if (!p || !p.getBody) return null;
    const b = p.getBody(bodyId);
    if (!b || !b.collider) return null;
    return { ...b.collider, restitution: b.restitution, friction: b.friction };
  }

  global.TDSandbox = global.TDSandbox || {};
  global.TDSandbox.ecs = {
    create, destroy, get, all, count, byKind, syncFromPhysics, serialize,
    _entities,
  };
})(typeof window !== 'undefined' ? window : this);
