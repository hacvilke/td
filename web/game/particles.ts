/**
 * TD Engine — Particle system (built on the engine's ECS + SpriteBatch).
 * Reused across game scenes for hit bursts, trail effects, etc.
 */

import type { World, EntityId, SpriteComponent } from '../engine/ecs';
import { ComponentType, componentBit } from '../engine/ecs';
import type { SpriteBatch } from '../engine/renderer';

export interface ParticleBurstConfig {
  count: number;
  x: number;
  y: number;
  speedMin: number;
  speedMax: number;
  angleMin: number; // radians
  angleMax: number;
  sizeMin: number;
  sizeMax: number;
  r: number;
  g: number;
  b: number;
  lifeMin: number; // seconds
  lifeMax: number;
  gravity?: number;
  drag?: number;
}

interface ParticleRuntime {
  entity: EntityId;
  vx: number;
  vy: number;
  life: number;
  maxLife: number;
  size: number;
  startR: number;
  startG: number;
  startB: number;
  startA: number;
  gravity: number;
  drag: number;
}

export class ParticleSystem {
  private world: World;
  private batch: SpriteBatch;
  private particles: ParticleRuntime[] = [];
  private queryMask = componentBit(ComponentType.Position) | componentBit(ComponentType.Sprite);

  constructor(world: World, batch: SpriteBatch) {
    this.world = world;
    this.batch = batch;
  }

  burst(cfg: ParticleBurstConfig): void {
    const { count, x, y, speedMin, speedMax, angleMin, angleMax,
            sizeMin, sizeMax, r, g, b, lifeMin, lifeMax,
            gravity = 0, drag = 0.92 } = cfg;

    for (let i = 0; i < count; i++) {
      const id = this.world.createEntity('Particle');
      const pos = this.world.addPosition(id, x, y);

      const angle = angleMin + Math.random() * (angleMax - angleMin);
      const speed = speedMin + Math.random() * (speedMax - speedMin);
      const size = sizeMin + Math.random() * (sizeMax - sizeMin);
      const life = lifeMin + Math.random() * (lifeMax - lifeMin);
      const alpha = 0.7 + Math.random() * 0.3;

      pos.x = x;
      pos.y = y;

      const sprite = this.world.addSprite({
        width: size, height: size,
        r, g, b, a: alpha,
        visible: true,
      })(id);

      void sprite; // sprite auto-registered

      this.particles.push({
        entity: id,
        vx: Math.cos(angle) * speed,
        vy: Math.sin(angle) * speed,
        life,
        maxLife: life,
        size,
        startR: r, startG: g, startB: b, startA: alpha,
        gravity,
        drag,
      });

      void pos; void sprite;
    }
  }

  update(dt: number): void {
    for (let i = this.particles.length - 1; i >= 0; i--) {
      const p = this.particles[i];
      p.life -= dt;

      if (p.life <= 0) {
        this.world.destroyEntity(p.entity);
        this.particles.splice(i, 1);
        continue;
      }

      const pos = this.world.getPosition(p.entity);
      const sprite = this.world.getSprite(p.entity);
      if (!pos || !sprite) {
        this.world.destroyEntity(p.entity);
        this.particles.splice(i, 1);
        continue;
      }

      p.vy += p.gravity * dt;
      p.vx *= p.drag;
      p.vy *= p.drag;
      pos.x += p.vx * dt;
      pos.y += p.vy * dt;

      const t = p.life / p.maxLife;
      sprite.a = p.startA * t;
      // slight fade toward darker
      sprite.r = p.startR * (0.5 + 0.5 * t);
      sprite.g = p.startG * (0.5 + 0.5 * t);
      sprite.b = p.startB * (0.5 + 0.5 * t);
    }
  }

  /** Called inside a SpriteBatch.begin()/end() pair — draws all live particles. */
  draw(): void {
    const ids = this.world.queryActive(this.queryMask);
    for (const id of ids) {
      const pos = this.world.getPosition(id);
      const sprite = this.world.getSprite(id) as SpriteComponent | undefined;
      if (!pos || !sprite || !sprite.visible) continue;
      this.batch.draw({
        x: pos.x - sprite.width * sprite.originX,
        y: pos.y - sprite.height * sprite.originY,
        width: sprite.width, height: sprite.height,
        u0: 0, v0: 0, u1: 1, v1: 1,
        r: sprite.r, g: sprite.g, b: sprite.b, a: sprite.a,
        rotation: 0, originX: 0, originY: 0,
      }, sprite.texture);
    }
  }

  clear(): void {
    for (const p of this.particles) this.world.destroyEntity(p.entity);
    this.particles.length = 0;
  }

  count(): number {
    return this.particles.length;
  }
}
