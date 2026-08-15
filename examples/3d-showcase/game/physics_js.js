// =============================================================================
// physics_js — Pure-JS reference physics engine.
// -----------------------------------------------------------------------------
// This is the fallback when the compiled WASM (with the real C++ physics
// engine in src/physics/) isn't loaded.  It implements the SAME API surface
// as TDEngine.physics.* (init, addBody, setSphereCollider, setBoxCollider,
// setPosition, applyImpulse, raycast, step, etc.) so the game code never
// branches on backend.
//
// It's a real rigid body simulation: semi-implicit Euler integration, sphere/
// box/AABB collision detection, impulse-based response with restitution and
// Coulomb friction, sleeping, distance constraints, raycast.  Not as robust
// as the C++ engine (no GJK/EPA, no contact manifolds), but enough for the
// showcase to be physically convincing.
//
// When WASM is loaded, TDEngine.physics.* points at the C++ engine and this
// module is never used.  When WASM is absent, this module REPLACES
// TDEngine.physics.* with itself.
// =============================================================================

(function (global) {
  'use strict';

  // ---- Vector helpers (Vec3) --------------------------------------------
  const V3 = {
    add:  (a, b) => ({ x: a.x + b.x, y: a.y + b.y, z: a.z + b.z }),
    sub:  (a, b) => ({ x: a.x - b.x, y: a.y - b.y, z: a.z - b.z }),
    scale:(a, s) => ({ x: a.x * s, y: a.y * s, z: a.z * s }),
    dot:  (a, b) => a.x * b.x + a.y * b.y + a.z * b.z,
    cross:(a, b) => ({
      x: a.y * b.z - a.z * b.y,
      y: a.z * b.x - a.x * b.z,
      z: a.x * b.y - a.y * b.x,
    }),
    len:  (a) => Math.sqrt(a.x * a.x + a.y * a.y + a.z * a.z),
    norm: (a) => {
      const l = V3.len(a);
      return l > 1e-9 ? { x: a.x / l, y: a.y / l, z: a.z / l } : { x: 0, y: 0, z: 0 };
    },
  };

  // ---- Rigid body -------------------------------------------------------
  let _nextBodyId = 1;
  function makeBody(mass, x, y, z, isStatic) {
    return {
      id: _nextBodyId++,
      mass: isStatic ? 0 : mass,
      invMass: isStatic ? 0 : 1 / mass,
      isStatic: !!isStatic,
      position: { x, y, z },
      prevPosition: { x, y, z },
      velocity: { x: 0, y: 0, z: 0 },
      force:    { x: 0, y: 0, z: 0 },
      // Collider: { type: 'sphere'|'box'|'capsule', ... }
      collider: null,
      // AABB cache (computed each broadphase).
      aabb: { min: { x: 0, y: 0, z: 0 }, max: { x: 0, y: 0, z: 0 } },
      // Material.
      restitution: 0.3,
      friction: 0.5,
      gravityScale: 1.0,
      useGravity: true,
      // Sleeping.
      sleeping: false,
      sleepTimer: 0,
      // Visual color (used by renderer).
      color: { r: 0.7, g: 0.7, b: 0.8 },
      // Tag (game code can set this for filtering).
      tag: '',
    };
  }

  // ---- World ------------------------------------------------------------
  const _world = {
    gravity: { x: 0, y: -9.81, z: 0 },
    bodies: new Map(),
    constraints: [],
    contactCount: 0,
    sleepThreshold: 0.05,
    sleepTimeRequired: 0.6,
  };

  // ---- Collider helpers -------------------------------------------------
  function computeAABB(body) {
    const c = body.collider;
    if (!c) {
      body.aabb.min = { x: body.position.x, y: body.position.y, z: body.position.z };
      body.aabb.max = { x: body.position.x, y: body.position.y, z: body.position.z };
      return;
    }
    const p = body.position;
    if (c.type === 'sphere') {
      body.aabb.min = { x: p.x - c.radius, y: p.y - c.radius, z: p.z - c.radius };
      body.aabb.max = { x: p.x + c.radius, y: p.y + c.radius, z: p.z + c.radius };
    } else if (c.type === 'box') {
      body.aabb.min = { x: p.x - c.hx, y: p.y - c.hy, z: p.z - c.hz };
      body.aabb.max = { x: p.x + c.hx, y: p.y + c.hy, z: p.z + c.hz };
    } else if (c.type === 'capsule') {
      const r = c.radius, h = c.height / 2;
      body.aabb.min = { x: p.x - r, y: p.y - h - r, z: p.z - r };
      body.aabb.max = { x: p.x + r, y: p.y + h + r, z: p.z + r };
    } else if (c.type === 'static-plane') {
      // Infinite plane at y = p.y (for the floor).
      body.aabb.min = { x: -1e6, y: p.y - 0.01, z: -1e6 };
      body.aabb.max = { x:  1e6, y: p.y + 0.01, z:  1e6 };
    }
  }

  // ---- Public API: world lifecycle -------------------------------------
  function init(gx, gy, gz) {
    _world.gravity = { x: gx || 0, y: gy || -9.81, z: gz || 0 };
    _world.bodies.clear();
    _world.constraints.length = 0;
    _world.contactCount = 0;
    return 1;  // worldId, matches the C++ side's return value
  }

  function shutdown() {
    _world.bodies.clear();
    _world.constraints.length = 0;
  }

  function step(dt) {
    // 1) Apply gravity + integrate velocities (semi-implicit Euler).
    for (const body of _world.bodies.values()) {
      if (body.isStatic || body.sleeping) continue;
      if (body.useGravity) {
        body.force.x += _world.gravity.x * body.mass * body.gravityScale;
        body.force.y += _world.gravity.y * body.mass * body.gravityScale;
        body.force.z += _world.gravity.z * body.mass * body.gravityScale;
      }
      body.velocity.x += body.force.x * body.invMass * dt;
      body.velocity.y += body.force.y * body.invMass * dt;
      body.velocity.z += body.force.z * body.invMass * dt;
      // Damping (air resistance).
      const damp = Math.pow(0.998, dt * 60);
      body.velocity.x *= damp;
      body.velocity.y *= damp;
      body.velocity.z *= damp;
      body.force.x = 0; body.force.y = 0; body.force.z = 0;
    }

    // 2) Integrate positions.
    for (const body of _world.bodies.values()) {
      if (body.isStatic || body.sleeping) continue;
      body.prevPosition = { ...body.position };
      body.position.x += body.velocity.x * dt;
      body.position.y += body.velocity.y * dt;
      body.position.z += body.velocity.z * dt;
    }

    // 3) Broadphase + narrowphase + solve.
    let contacts = 0;
    const bodies = Array.from(_world.bodies.values());
    // Update AABBs.
    for (const b of bodies) computeAABB(b);
    // O(n^2) pairwise — fine for the showcase's ~30 bodies.
    for (let i = 0; i < bodies.length; i++) {
      for (let j = i + 1; j < bodies.length; j++) {
        const a = bodies[i], b = bodies[j];
        if (a.isStatic && b.isStatic) continue;
        if (a.sleeping && b.sleeping) continue;
        if (!aabbOverlap(a.aabb, b.aabb)) continue;
        const c = narrowphase(a, b);
        if (c) {
          contacts++;
          resolveContact(a, b, c);
        }
      }
    }
    _world.contactCount = contacts;

    // 4) Solve distance constraints.
    for (const con of _world.constraints) {
      solveDistanceConstraint(con, dt);
    }

    // 5) Sleeping.
    for (const body of _world.bodies.values()) {
      if (body.isStatic) continue;
      const v = body.velocity;
      const speed2 = v.x * v.x + v.y * v.y + v.z * v.z;
      if (speed2 < _world.sleepThreshold * _world.sleepThreshold) {
        body.sleepTimer += dt;
        if (body.sleepTimer > _world.sleepTimeRequired) {
          body.sleeping = true;
          body.velocity.x = 0; body.velocity.y = 0; body.velocity.z = 0;
        }
      } else {
        body.sleepTimer = 0;
        body.sleeping = false;
      }
    }
  }

  function aabbOverlap(a, b) {
    return !(a.max.x < b.min.x || a.min.x > b.max.x ||
             a.max.y < b.min.y || a.min.y > b.max.y ||
             a.max.z < b.min.z || a.min.z > b.max.z);
  }

  // Narrowphase: returns {normal, depth, point} or null.
  function narrowphase(a, b) {
    const ca = a.collider, cb = b.collider;
    if (!ca || !cb) return null;
    // Dispatch on collider type pairs.
    // Convention: primitive(arg1, arg2) returns normal pointing from arg2 to arg1.
    // resolveContact expects normal pointing from a to b.
    // So when we call primitive(b, a) (swapped), the normal points from a to b → correct.
    // When we call primitive(a, b) directly, the normal points from b to a → flip.
    if (ca.type === 'sphere' && cb.type === 'sphere') {
      const c = sphereSphere(a, b);
      if (c) c.normal = V3.scale(c.normal, -1);
      return c;
    }
    if (ca.type === 'sphere' && cb.type === 'box') {
      const c = sphereBox(a, b);
      if (c) c.normal = V3.scale(c.normal, -1);
      return c;
    }
    if (ca.type === 'box'    && cb.type === 'sphere') return sphereBox(b, a);
    if (ca.type === 'sphere' && cb.type === 'static-plane') {
      const c = spherePlane(a, b);
      if (c) c.normal = V3.scale(c.normal, -1);
      return c;
    }
    if (ca.type === 'static-plane' && cb.type === 'sphere') return spherePlane(b, a);
    if (ca.type === 'box' && cb.type === 'box') {
      const c = boxBox(a, b);
      if (c) c.normal = V3.scale(c.normal, -1);
      return c;
    }
    if (ca.type === 'box' && cb.type === 'static-plane') {
      const c = boxPlane(a, b);
      if (c) c.normal = V3.scale(c.normal, -1);
      return c;
    }
    if (ca.type === 'static-plane' && cb.type === 'box') return boxPlane(b, a);
    if (ca.type === 'capsule' && cb.type === 'static-plane') {
      const c = capsulePlane(a, b);
      if (c) c.normal = V3.scale(c.normal, -1);
      return c;
    }
    if (ca.type === 'static-plane' && cb.type === 'capsule') return capsulePlane(b, a);
    // Fallback: treat both as AABBs.
    return aabbContact(a, b);
  }

  // Convention: each primitive returns the contact normal pointing FROM
  // arg2 TO arg1 (i.e., towards arg1, away from arg2).  This means when
  // narrowphase dispatches primitive(a, b), the returned normal points from
  // b to a — which is the OPPOSITE of what resolveContact expects (it wants
  // normal from a to b).  So the dispatches that call primitive(a, b)
  // directly must flip the result; the dispatches that call primitive(b, a)
  // (swapped) get the correct direction for free.
  function sphereSphere(a, b) {
    const ca = a.collider, cb = b.collider;
    const d = V3.sub(a.position, b.position);  // from b to a
    const r = ca.radius + cb.radius;
    const dist = V3.len(d);
    if (dist >= r) return null;
    const normal = dist > 1e-9 ? V3.scale(d, 1 / dist) : { x: 0, y: 1, z: 0 };
    const depth = r - dist;
    const point = V3.add(b.position, V3.scale(normal, cb.radius));
    return { normal, depth, point };
  }

  // Normal points from plane (arg2) to sphere (arg1) — UP if sphere is above.
  function spherePlane(sphere, plane) {
    const r = sphere.collider.radius;
    const py = plane.position.y;
    if (sphere.position.y - r >= py) return null;
    return {
      normal: { x: 0, y: 1, z: 0 },
      depth:  py - (sphere.position.y - r),
      point:  { x: sphere.position.x, y: py, z: sphere.position.z },
    };
  }

  // Normal points from box (arg2) to sphere (arg1) — UP if sphere is above.
  function sphereBox(sphere, box) {
    const r = sphere.collider.radius;
    const bh = box.collider;
    const sp = sphere.position;
    const bp = box.position;
    // Closest point on box to sphere.
    const cx = Math.max(bp.x - bh.hx, Math.min(sp.x, bp.x + bh.hx));
    const cy = Math.max(bp.y - bh.hy, Math.min(sp.y, bp.y + bh.hy));
    const cz = Math.max(bp.z - bh.hz, Math.min(sp.z, bp.z + bh.hz));
    // d points from box's closest point to sphere center (UP if sphere is above).
    const d = { x: sp.x - cx, y: sp.y - cy, z: sp.z - cz };
    const dist2 = d.x * d.x + d.y * d.y + d.z * d.z;
    if (dist2 >= r * r) return null;
    const dist = Math.sqrt(dist2);
    let normal;
    if (dist > 1e-9) {
      normal = V3.scale(d, 1 / dist);  // from box to sphere
    } else {
      // Sphere center inside box — push out along the smallest penetration axis.
      const dx = Math.min(bp.x + bh.hx - sp.x, sp.x - (bp.x - bh.hx));
      const dy = Math.min(bp.y + bh.hy - sp.y, sp.y - (bp.y - bh.hy));
      const dz = Math.min(bp.z + bh.hz - sp.z, sp.z - (bp.z - bh.hz));
      if (dx < dy && dx < dz) normal = { x: Math.sign(sp.x - bp.x) || 1, y: 0, z: 0 };
      else if (dy < dz)       normal = { x: 0, y: Math.sign(sp.y - bp.y) || 1, z: 0 };
      else                    normal = { x: 0, y: 0, z: Math.sign(sp.z - bp.z) || 1 };
    }
    const depth = r - dist;
    return { normal, depth, point: { x: cx, y: cy, z: cz } };
  }

  function boxBox(a, b) {
    const ca = a.collider, cb = b.collider;
    const dx = a.position.x - b.position.x;  // from b to a
    const px = (ca.hx + cb.hx) - Math.abs(dx);
    if (px <= 0) return null;
    const dy = a.position.y - b.position.y;
    const py = (ca.hy + cb.hy) - Math.abs(dy);
    if (py <= 0) return null;
    const dz = a.position.z - b.position.z;
    const pz = (ca.hz + cb.hz) - Math.abs(dz);
    if (pz <= 0) return null;
    // Smallest penetration axis = contact normal (pointing from b to a).
    let normal, depth;
    if (px < py && px < pz) {
      normal = { x: dx < 0 ? -1 : 1, y: 0, z: 0 };
      depth = px;
    } else if (py < pz) {
      normal = { x: 0, y: dy < 0 ? -1 : 1, z: 0 };
      depth = py;
    } else {
      normal = { x: 0, y: 0, z: dz < 0 ? -1 : 1 };
      depth = pz;
    }
    const point = {
      x: (a.position.x + b.position.x) / 2,
      y: (a.position.y + b.position.y) / 2,
      z: (a.position.z + b.position.z) / 2,
    };
    return { normal, depth, point };
  }

  // Normal points from plane (arg2) to box (arg1) — UP if box is above.
  function boxPlane(box, plane) {
    const bh = box.collider;
    const py = plane.position.y;
    if (box.position.y - bh.hy >= py) return null;
    return {
      normal: { x: 0, y: 1, z: 0 },
      depth:  py - (box.position.y - bh.hy),
      point:  { x: box.position.x, y: py, z: box.position.z },
    };
  }

  // Normal points from plane (arg2) to capsule (arg1) — UP if capsule is above.
  function capsulePlane(capsule, plane) {
    const c = capsule.collider;
    const r = c.radius, h = c.height / 2;
    const py = plane.position.y;
    if (capsule.position.y - h - r >= py) return null;
    return {
      normal: { x: 0, y: 1, z: 0 },
      depth:  py - (capsule.position.y - h - r),
      point:  { x: capsule.position.x, y: py, z: capsule.position.z },
    };
  }

  function aabbContact(a, b) {
    // Reuse boxBox logic on AABB.  boxBox returns normal pointing from arg2
    // to arg1, so we need to flip to get from a to b.
    const c = boxBox(
      { position: a.position, collider: { hx: (a.aabb.max.x - a.aabb.min.x) / 2, hy: (a.aabb.max.y - a.aabb.min.y) / 2, hz: (a.aabb.max.z - a.aabb.min.z) / 2 } },
      { position: b.position, collider: { hx: (b.aabb.max.x - b.aabb.min.x) / 2, hy: (b.aabb.max.y - b.aabb.min.y) / 2, hz: (b.aabb.max.z - b.aabb.min.z) / 2 } }
    );
    if (c) c.normal = V3.scale(c.normal, -1);
    return c;
  }

  // Impulse-based contact resolution (no rotations — sphere/box treated as
  // point masses for simplicity.  The C++ engine does the full rotational
  // solve with inertia tensors).
  function resolveContact(a, b, c) {
    const n = c.normal;
    // Positional correction (Baumgarte-ish, slop to avoid jitter).
    const SLOP = 0.005;
    const PERCENT = 0.4;
    const correction = Math.max(c.depth - SLOP, 0) * PERCENT;
    const totalInv = a.invMass + b.invMass;
    if (totalInv > 0) {
      const move = V3.scale(n, correction / totalInv);
      if (!a.isStatic) {
        a.position.x -= move.x * a.invMass;
        a.position.y -= move.y * a.invMass;
        a.position.z -= move.z * a.invMass;
      }
      if (!b.isStatic) {
        b.position.x += move.x * b.invMass;
        b.position.y += move.y * b.invMass;
        b.position.z += move.z * b.invMass;
      }
    }
    // Velocity correction (restitution).
    const rv = V3.sub(b.velocity, a.velocity);
    const velAlongNormal = V3.dot(rv, n);
    if (velAlongNormal > 0) return;  // separating
    const e = Math.min(a.restitution, b.restitution);
    const j = -(1 + e) * velAlongNormal / totalInv;
    const impulse = V3.scale(n, j);
    if (!a.isStatic) {
      a.velocity.x -= impulse.x * a.invMass;
      a.velocity.y -= impulse.y * a.invMass;
      a.velocity.z -= impulse.z * a.invMass;
      a.sleeping = false; a.sleepTimer = 0;
    }
    if (!b.isStatic) {
      b.velocity.x += impulse.x * b.invMass;
      b.velocity.y += impulse.y * b.invMass;
      b.velocity.z += impulse.z * b.invMass;
      b.sleeping = false; b.sleepTimer = 0;
    }
    // Friction (Coulomb, simplified — single tangent direction).
    const t = V3.norm(V3.sub(rv, V3.scale(n, velAlongNormal)));
    const jt = -V3.dot(rv, t) / totalInv;
    const mu = Math.sqrt(a.friction * b.friction);
    let frictionImpulse;
    if (Math.abs(jt) < j * mu) frictionImpulse = V3.scale(t, jt);
    else                        frictionImpulse = V3.scale(t, -j * mu);
    if (!a.isStatic) {
      a.velocity.x -= frictionImpulse.x * a.invMass;
      a.velocity.y -= frictionImpulse.y * a.invMass;
      a.velocity.z -= frictionImpulse.z * a.invMass;
    }
    if (!b.isStatic) {
      b.velocity.x += frictionImpulse.x * b.invMass;
      b.velocity.y += frictionImpulse.y * b.invMass;
      b.velocity.z += frictionImpulse.z * b.invMass;
    }
  }

  // ---- Distance constraint (rope / pendulum) ----------------------------
  function solveDistanceConstraint(con, dt) {
    const a = _world.bodies.get(con.bodyA);
    const b = _world.bodies.get(con.bodyB);
    if (!a || !b) return;
    const d = V3.sub(b.position, a.position);
    const dist = V3.len(d);
    if (dist < 1e-9) return;
    const n = V3.scale(d, 1 / dist);
    const C = dist - con.targetDistance;
    const totalInv = a.invMass + b.invMass;
    if (totalInv === 0) return;
    const correction = V3.scale(n, -C / totalInv);
    if (!a.isStatic) {
      a.position.x -= correction.x * a.invMass;
      a.position.y -= correction.y * a.invMass;
      a.position.z -= correction.z * a.invMass;
    }
    if (!b.isStatic) {
      b.position.x += correction.x * b.invMass;
      b.position.y += correction.y * b.invMass;
      b.position.z += correction.z * b.invMass;
    }
  }

  // ---- Public API: bodies ----------------------------------------------
  function addBody(mass, x, y, z, isStatic) {
    const b = makeBody(mass, x, y, z, isStatic);
    _world.bodies.set(b.id, b);
    return b.id;
  }
  function bodyCount() { return _world.bodies.size; }
  function contactCount() { return _world.contactCount; }
  function getBody(id) { return _world.bodies.get(id); }
  function allBodies() { return Array.from(_world.bodies.values()); }
  // Marks a body for removal.  We actually delete it from the Map (the JS
  // fallback doesn't need index stability the way the C++ engine does —
  // bodyIds are arbitrary numbers, not array indices).  Any future calls
  // with this bodyId are no-ops.
  function removeBody(id) {
    const b = _world.bodies.get(id);
    if (!b) return;
    // Also remove any constraints that reference this body.
    _world.constraints = _world.constraints.filter(c => c.bodyA !== id && c.bodyB !== id);
    _world.bodies.delete(id);
  }

  function setSphereCollider(id, radius) {
    const b = _world.bodies.get(id);
    if (b) b.collider = { type: 'sphere', radius };
  }
  function setBoxCollider(id, hx, hy, hz) {
    const b = _world.bodies.get(id);
    if (b) b.collider = { type: 'box', hx, hy, hz };
  }
  function setCapsuleCollider(id, radius, height) {
    const b = _world.bodies.get(id);
    if (b) b.collider = { type: 'capsule', radius, height };
  }
  function setStaticPlaneCollider(id) {
    const b = _world.bodies.get(id);
    if (b) b.collider = { type: 'static-plane' };
  }
  function setPosition(id, x, y, z) {
    const b = _world.bodies.get(id);
    if (b) { b.position = { x, y, z }; b.sleeping = false; b.sleepTimer = 0; }
  }
  function setVelocity(id, vx, vy, vz) {
    const b = _world.bodies.get(id);
    if (b) { b.velocity = { x: vx, y: vy, z: vz }; b.sleeping = false; b.sleepTimer = 0; }
  }
  function getPosition(id) {
    const b = _world.bodies.get(id);
    return b ? { ...b.position } : { x: 0, y: 0, z: 0 };
  }
  function getVelocity(id) {
    const b = _world.bodies.get(id);
    return b ? { ...b.velocity } : { x: 0, y: 0, z: 0 };
  }
  function getOrientation(id) {
    // JS fallback doesn't simulate rotation — return identity quaternion.
    return { x: 0, y: 0, z: 0, w: 1 };
  }
  function applyForce(id, fx, fy, fz) {
    const b = _world.bodies.get(id);
    if (b) { b.force.x += fx; b.force.y += fy; b.force.z += fz; b.sleeping = false; }
  }
  function applyImpulse(id, ix, iy, iz) {
    const b = _world.bodies.get(id);
    if (b && !b.isStatic) {
      b.velocity.x += ix * b.invMass;
      b.velocity.y += iy * b.invMass;
      b.velocity.z += iz * b.invMass;
      b.sleeping = false; b.sleepTimer = 0;
    }
  }
  function applyTorque(id, tx, ty, tz) { /* JS fallback: no rotation */ }
  function setRestitution(id, e) {
    const b = _world.bodies.get(id);
    if (b) b.restitution = e;
  }
  function setFriction(id, f) {
    const b = _world.bodies.get(id);
    if (b) b.friction = f;
  }
  function setGravityScale(id, s) {
    const b = _world.bodies.get(id);
    if (b) b.gravityScale = s;
  }
  function setUseGravity(id, g) {
    const b = _world.bodies.get(id);
    if (b) b.useGravity = g;
  }
  function setColor(id, r, g, b) {
    const body = _world.bodies.get(id);
    if (body) body.color = { r, g, b };
  }
  function setTag(id, tag) {
    const body = _world.bodies.get(id);
    if (body) body.tag = tag;
  }
  function addDistanceConstraint(aId, bId, dist) {
    const con = { bodyA: aId, bodyB: bId, targetDistance: dist };
    _world.constraints.push(con);
    return _world.constraints.length;
  }
  function addHingeConstraint(aId, bId, ax, ay, az) {
    // JS fallback treats hinge as a distance constraint with 0 length.
    return addDistanceConstraint(aId, bId, 0);
  }
  function raycast(ox, oy, oz, dx, dy, dz, maxDist) {
    const dir = V3.norm({ x: dx, y: dy, z: dz });
    let bestT = maxDist;
    let bestBody = null;
    let bestPoint = null;
    let bestNormal = null;
    for (const body of _world.bodies.values()) {
      if (!body.collider) continue;
      const c = body.collider;
      let t = -1;
      let normal = null;
      if (c.type === 'sphere') {
        const oc = V3.sub({ x: ox, y: oy, z: oz }, body.position);
        const a = V3.dot(dir, dir);
        const b = 2 * V3.dot(oc, dir);
        const cc = V3.dot(oc, oc) - c.radius * c.radius;
        const disc = b * b - 4 * a * cc;
        if (disc < 0) continue;
        const sq = Math.sqrt(disc);
        const t0 = (-b - sq) / (2 * a);
        const t1 = (-b + sq) / (2 * a);
        t = t0 >= 0 ? t0 : (t1 >= 0 ? t1 : -1);
        if (t < 0 || t >= bestT) continue;
        bestT = t;
        bestBody = body;
        bestPoint = { x: ox + dir.x * t, y: oy + dir.y * t, z: oz + dir.z * t };
        bestNormal = V3.norm(V3.sub(bestPoint, body.position));
      } else if (c.type === 'box') {
        // Slab method.
        const p = body.position;
        let tmin = -Infinity, tmax = Infinity;
        let hitAxis = -1, hitSign = 1;
        const o = { x: ox, y: oy, z: oz };
        const h = [c.hx, c.hy, c.hz];
        const dArr = [dir.x, dir.y, dir.z];
        const oArr = [o.x, o.y, o.z];
        const pArr = [p.x, p.y, p.z];
        for (let i = 0; i < 3; i++) {
          if (Math.abs(dArr[i]) < 1e-9) {
            if (oArr[i] < pArr[i] - h[i] || oArr[i] > pArr[i] + h[i]) { tmin = Infinity; break; }
            continue;
          }
          const t1 = (pArr[i] - h[i] - oArr[i]) / dArr[i];
          const t2 = (pArr[i] + h[i] - oArr[i]) / dArr[i];
          const sign = t1 > t2 ? 1 : -1;
          const tn = Math.min(t1, t2), tf = Math.max(t1, t2);
          if (tn > tmin) { tmin = tn; hitAxis = i; hitSign = -sign; }
          if (tf < tmax) tmax = tf;
          if (tmin > tmax) { tmin = Infinity; break; }
        }
        if (tmin < 0 || tmin >= bestT || !isFinite(tmin)) continue;
        bestT = tmin;
        bestBody = body;
        bestPoint = { x: ox + dir.x * tmin, y: oy + dir.y * tmin, z: oz + dir.z * tmin };
        const n = [0, 0, 0];
        n[hitAxis] = hitSign;
        bestNormal = { x: n[0], y: n[1], z: n[2] };
      } else if (c.type === 'static-plane') {
        // Plane at y = body.position.y, normal = +Y.
        if (Math.abs(dir.y) < 1e-9) continue;
        const t = (body.position.y - oy) / dir.y;
        if (t < 0 || t >= bestT) continue;
        bestT = t;
        bestBody = body;
        bestPoint = { x: ox + dir.x * t, y: oy + dir.y * t, z: oz + dir.z * t };
        bestNormal = { x: 0, y: 1, z: 0 };
      }
    }
    if (!bestBody) return null;
    return { bodyId: bestBody.id, point: bestPoint, normal: bestNormal };
  }

  // Expose the JS fallback as a complete TDEngine.physics-compatible API.
  // The game's main module installs it as TDEngine.physics when WASM is absent.
  const Physics = {
    init, shutdown, step,
    addBody, bodyCount, contactCount, getBody, allBodies, removeBody,
    setSphereCollider, setBoxCollider, setCapsuleCollider, setStaticPlaneCollider,
    setPosition, setVelocity, getPosition, getVelocity, getOrientation,
    applyForce, applyImpulse, applyTorque,
    setRestitution, setFriction, setGravityScale, setUseGravity,
    setColor, setTag,
    addDistanceConstraint, addHingeConstraint,
    raycast,
    // Extra (non-TDEngine) helpers for the showcase.
    _bodies: _world.bodies,
    _constraints: _world.constraints,
    _gravity: _world.gravity,
  };

  global.TDSandbox = global.TDSandbox || {};
  global.TDSandbox.physicsJs = Physics;
})(typeof window !== 'undefined' ? window : this);
