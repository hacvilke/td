/**
 * TD Engine — Camera2D (TypeScript port of src/renderer/camera.h)
 */

import { Mat4, Vec2 } from './math';

export class Camera2D {
  private x = 0;
  private y = 0;
  private zoom = 1;
  private rotation = 0;
  private viewportWidth = 800;
  private viewportHeight = 600;
  private dirty = true;
  private _projection = Mat4.identity();
  private _view = Mat4.identity();

  setViewport(width: number, height: number): void {
    this.viewportWidth = width;
    this.viewportHeight = height;
    this.dirty = true;
  }

  setPosition(x: number, y: number): void {
    this.x = x;
    this.y = y;
    this.dirty = true;
  }

  getPosition(): Vec2 {
    return new Vec2(this.x, this.y);
  }

  setZoom(z: number): void {
    this.zoom = z;
    this.dirty = true;
  }

  setRotation(r: number): void {
    this.rotation = r;
    this.dirty = true;
  }

  getProjection(): Mat4 {
    this.recompute();
    return this._projection;
  }

  getView(): Mat4 {
    this.recompute();
    return this._view;
  }

  private recompute(): void {
    if (!this.dirty) return;
    this.dirty = false;

    // Top-left origin: world (0,0) maps to screen top-left, world
    // (viewportW, viewportH) maps to screen bottom-right. This matches
    // how the game code thinks about coordinates (e.g. pong.ts places
    // paddles at x=40 and x=WIDTH-40, walls at y=WALL_H/2 etc.).
    const w = this.viewportWidth / this.zoom;
    const h = this.viewportHeight / this.zoom;

    this._projection = Mat4.orthographic(
      0,        // left
      w,        // right
      h,        // bottom  (Y axis points DOWN in screen space)
      0,        // top
      -1,
      1,
    );

    // View = translate(-pos) * rotate(-rot). With pos=(0,0) the view
    // is identity, which is what most 2D games want.
    const t = Mat4.translate(-this.x, -this.y);
    const r = Mat4.rotateZ(-this.rotation);
    this._view = t.multiply(r);
  }
}
