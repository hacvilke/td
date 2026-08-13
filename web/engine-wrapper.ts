/**
 * TD Engine — TypeScript Wrapper (FIXED)
 *
 * Original bug: This wrapper was written for a WASM bridge that never loaded
 * a real WASM module (see web/js_bridge.ts for the diagnosis). All of its
 * methods silently no-op'd and the engine never actually ran in the browser.
 *
 * Fix: Re-export the real working Engine class from web/engine/engine.ts
 * so existing code that imports from `web/engine-wrapper` keeps working,
 * but now actually does something.
 */

export { Engine } from './engine/engine';
export { Renderer, SpriteBatch } from './engine/renderer';
export { Camera2D } from './engine/camera';
export { Input, Key } from './engine/input';
export {
  World,
  ComponentType,
  componentBit,
  INVALID_ENTITY,
  defaultSprite,
  aabbIntersect,
  aabbFromEntity,
} from './engine/ecs';
export type {
  EntityId,
  ComponentMask,
  PositionComponent,
  VelocityComponent,
  SpriteComponent,
  ColliderComponent,
  RigidBodyComponent,
  TagComponent,
  System,
  AABB,
} from './engine/ecs';
export {
  Vec2,
  Vec3,
  Vec4,
  Mat4,
  Color,
  clamp,
  lerp,
  degToRad,
  radToDeg,
  TD_EPSILON,
} from './engine/math';
export type {
  InitCallback,
  UpdateCallback,
  RenderCallback,
  LogCallback,
  ReadyCallback,
  EngineConfig,
} from './engine/engine';

export class EngineError extends Error {
  constructor(message: string) {
    super(message);
    this.name = 'EngineError';
  }
}
