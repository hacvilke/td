/**
 * TD Engine — ECS (TypeScript port of src/ecs/{entity,component,system,world}.h/.cpp)
 * Same component layout, same bit-mask queries, same SoA storage as the C++ engine.
 */

import { Color } from './math';

export type EntityId = number;
export const INVALID_ENTITY: EntityId = 0;

export enum ComponentType {
  Position = 0,
  Velocity,
  Sprite,
  RigidBody,
  Collider,
  Light,
  Camera,
  AudioSource,
  Transform3D,
  MeshRenderer,
  Script,
  Tag,
  COUNT,
}

export type ComponentMask = number;

export function componentBit(type: ComponentType): number {
  return 1 << type;
}

export const TD_MAX_ENTITIES = 10000;

// ---- Components (mirror src/ecs/component.h) ----

export interface PositionComponent {
  x: number;
  y: number;
  prevX: number;
  prevY: number;
}

export interface VelocityComponent {
  vx: number;
  vy: number;
  ax: number;
  ay: number;
}

export interface SpriteComponent {
  texture: WebGLTexture | null;
  width: number;
  height: number;
  u0: number;
  v0: number;
  u1: number;
  v1: number;
  r: number;
  g: number;
  b: number;
  a: number;
  rotation: number;
  originX: number;
  originY: number;
  layer: number;
  visible: boolean;
  flipX: boolean;
  flipY: boolean;
}

export interface ColliderComponent {
  type: 'AABB' | 'Circle';
  offsetX: number;
  offsetY: number;
  width: number;
  height: number;
  radius: number;
  colliding: boolean;
  normalX: number;
  normalY: number;
  collidingWith: EntityId;
}

export interface RigidBodyComponent {
  mass: number;
  friction: number;
  restitution: number;
  linearDamping: number;
  gravityScale: number;
  useGravity: boolean;
  isStatic: boolean;
  isKinematic: boolean;
  isTrigger: boolean;
}

export interface TagComponent {
  name: string;
  tag: string;
  enabled: boolean;
}

// ---- Entity record ----

interface EntityRecord {
  id: EntityId;
  mask: ComponentMask;
  active: boolean;
  name: string;
  tag: string;
  // component storage (sparse SoA-style)
  position?: PositionComponent;
  velocity?: VelocityComponent;
  sprite?: SpriteComponent;
  collider?: ColliderComponent;
  rigidBody?: RigidBodyComponent;
  tag_?: TagComponent;
}

// ---- System interface ----

export interface System {
  onUpdate(world: World, dt: number): void;
}

/**
 * World — port of td::World.
 * Uses Map for entity storage (sparse, easier in JS) but keeps the
 * same bit-mask query API as the C++ engine.
 */
export class World {
  private entities = new Map<EntityId, EntityRecord>();
  private nextId: EntityId = 1;
  private systems: System[] = [];

  createEntity(name: string = 'Entity'): EntityId {
    const id = this.nextId++;
    this.entities.set(id, {
      id,
      mask: 0,
      active: true,
      name,
      tag: 'Untagged',
    });
    return id;
  }

  destroyEntity(id: EntityId): void {
    this.entities.delete(id);
  }

  entityExists(id: EntityId): boolean {
    return this.entities.has(id);
  }

  setEntityEnabled(id: EntityId, enabled: boolean): void {
    const r = this.entities.get(id);
    if (r) r.active = enabled;
  }

  isEntityEnabled(id: EntityId): boolean {
    const r = this.entities.get(id);
    return r ? r.active : false;
  }

  setEntityName(id: EntityId, name: string): void {
    const r = this.entities.get(id);
    if (r) r.name = name;
  }

  getEntityName(id: EntityId): string {
    return this.entities.get(id)?.name ?? '';
  }

  setEntityTag(id: EntityId, tag: string): void {
    const r = this.entities.get(id);
    if (r) r.tag = tag;
  }

  getEntityTag(id: EntityId): string {
    return this.entities.get(id)?.tag ?? '';
  }

  findEntityByName(name: string): EntityId {
    for (const [id, r] of this.entities) {
      if (r.name === name) return id;
    }
    return INVALID_ENTITY;
  }

  findEntitiesByTag(tag: string, maxResults: number): EntityId[] {
    const out: EntityId[] = [];
    for (const [id, r] of this.entities) {
      if (r.tag === tag) {
        out.push(id);
        if (out.length >= maxResults) break;
      }
    }
    return out;
  }

  private getRecord(id: EntityId): EntityRecord | undefined {
    return this.entities.get(id);
  }

  addPosition(id: EntityId, x = 0, y = 0): PositionComponent {
    const r = this.getRecord(id);
    if (!r) throw new Error(`Entity ${id} does not exist`);
    r.position = { x, y, prevX: x, prevY: y };
    r.mask |= componentBit(ComponentType.Position);
    return r.position;
  }

  addVelocity(id: EntityId, vx = 0, vy = 0): VelocityComponent {
    const r = this.getRecord(id);
    if (!r) throw new Error(`Entity ${id} does not exist`);
    r.velocity = { vx, vy, ax: 0, ay: 0 };
    r.mask |= componentBit(ComponentType.Velocity);
    return r.velocity;
  }

  addSprite(opts: Partial<SpriteComponent> = {}): (id: EntityId) => SpriteComponent {
    return (id: EntityId) => {
      const r = this.getRecord(id);
      if (!r) throw new Error(`Entity ${id} does not exist`);
      r.sprite = {
        texture: null,
        width: 32,
        height: 32,
        u0: 0, v0: 0, u1: 1, v1: 1,
        r: 1, g: 1, b: 1, a: 1,
        rotation: 0,
        originX: 0.5,
        originY: 0.5,
        layer: 0,
        visible: true,
        flipX: false,
        flipY: false,
        ...opts,
      };
      r.mask |= componentBit(ComponentType.Sprite);
      return r.sprite;
    };
  }

  addCollider(opts: Partial<ColliderComponent> = {}): (id: EntityId) => ColliderComponent {
    return (id: EntityId) => {
      const r = this.getRecord(id);
      if (!r) throw new Error(`Entity ${id} does not exist`);
      r.collider = {
        type: 'AABB',
        offsetX: 0,
        offsetY: 0,
        width: 32,
        height: 32,
        radius: 16,
        colliding: false,
        normalX: 0,
        normalY: 0,
        collidingWith: INVALID_ENTITY,
        ...opts,
      };
      r.mask |= componentBit(ComponentType.Collider);
      return r.collider;
    };
  }

  addRigidBody(opts: Partial<RigidBodyComponent> = {}): (id: EntityId) => RigidBodyComponent {
    return (id: EntityId) => {
      const r = this.getRecord(id);
      if (!r) throw new Error(`Entity ${id} does not exist`);
      r.rigidBody = {
        mass: 1,
        friction: 0.3,
        restitution: 0.2,
        linearDamping: 0.01,
        gravityScale: 1,
        useGravity: false,
        isStatic: false,
        isKinematic: false,
        isTrigger: false,
        ...opts,
      };
      r.mask |= componentBit(ComponentType.RigidBody);
      return r.rigidBody;
    };
  }

  getPosition(id: EntityId): PositionComponent | undefined {
    return this.getRecord(id)?.position;
  }

  getVelocity(id: EntityId): VelocityComponent | undefined {
    return this.getRecord(id)?.velocity;
  }

  getSprite(id: EntityId): SpriteComponent | undefined {
    return this.getRecord(id)?.sprite;
  }

  getCollider(id: EntityId): ColliderComponent | undefined {
    return this.getRecord(id)?.collider;
  }

  getRigidBody(id: EntityId): RigidBodyComponent | undefined {
    return this.getRecord(id)?.rigidBody;
  }

  hasComponent(id: EntityId, type: ComponentType): boolean {
    const r = this.getRecord(id);
    return r ? (r.mask & componentBit(type)) !== 0 : false;
  }

  removeComponent(id: EntityId, type: ComponentType): void {
    const r = this.getRecord(id);
    if (!r) return;
    r.mask &= ~componentBit(type);
    switch (type) {
      case ComponentType.Position: r.position = undefined; break;
      case ComponentType.Velocity: r.velocity = undefined; break;
      case ComponentType.Sprite: r.sprite = undefined; break;
      case ComponentType.Collider: r.collider = undefined; break;
      case ComponentType.RigidBody: r.rigidBody = undefined; break;
    }
  }

  addSystem(sys: System): void {
    this.systems.push(sys);
  }

  updateSystems(dt: number): void {
    for (const sys of this.systems) sys.onUpdate(this, dt);
  }

  query(mask: ComponentMask): EntityId[] {
    const out: EntityId[] = [];
    for (const [id, r] of this.entities) {
      if ((r.mask & mask) === mask) out.push(id);
    }
    return out;
  }

  queryActive(mask: ComponentMask): EntityId[] {
    const out: EntityId[] = [];
    for (const [id, r] of this.entities) {
      if (r.active && (r.mask & mask) === mask) out.push(id);
    }
    return out;
  }

  getEntityCount(): number {
    return this.entities.size;
  }

  getActiveEntityCount(): number {
    let n = 0;
    for (const r of this.entities.values()) if (r.active) n++;
    return n;
  }

  clear(): void {
    this.entities.clear();
    this.systems.length = 0;
    this.nextId = 1;
  }

  // Iteration helper for renderer
  forEachActiveWith(mask: ComponentMask, fn: (id: EntityId, r: EntityRecord) => void): void {
    for (const [id, r] of this.entities) {
      if (r.active && (r.mask & mask) === mask) fn(id, r);
    }
  }
}

// ---- AABB collision helpers (mirror src/physics/aabb.h) ----

export interface AABB {
  x: number;
  y: number;
  width: number;
  height: number;
}

export function aabbFromEntity(
  pos: PositionComponent,
  col: ColliderComponent,
): AABB {
  return {
    x: pos.x - col.width / 2 + col.offsetX,
    y: pos.y - col.height / 2 + col.offsetY,
    width: col.width,
    height: col.height,
  };
}

export function aabbIntersect(a: AABB, b: AABB): boolean {
  return (
    a.x < b.x + b.width &&
    a.x + a.width > b.x &&
    a.y < b.y + b.height &&
    a.y + a.height > b.y
  );
}

export function defaultSprite(
  width: number,
  height: number,
  color: Color,
): Partial<SpriteComponent> {
  return {
    width,
    height,
    r: color.r,
    g: color.g,
    b: color.b,
    a: color.a,
  };
}
