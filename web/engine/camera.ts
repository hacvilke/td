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

    const halfW = this.viewportWidth / 2;
    const halfH = this.viewportHeight / 2;

    this._projection = Mat4.orthographic(
      -halfW / this.zoom,
      halfW / this.zoom,
      halfH / this.zoom,
      -halfH / this.zoom,
      -1,
      1,
    );

    // Build view = translate(-pos) * rotate(-rot)
    const t = Mat4.translate(-this.x, -this.y);
    const r = Mat4.rotateZ(-this.rotation);
    this._view = t.multiply(r);
  }
}
