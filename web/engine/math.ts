/**
 * TD Engine — Math library (TypeScript port of src/core/math/*.h)
 * Mirrors the C++ Vec2/Vec3/Vec4/Mat4 API exactly so game code reads the same.
 */

export const TD_EPSILON = 1e-6;

export function degToRad(deg: number): number {
  return (deg * Math.PI) / 180;
}

export function radToDeg(rad: number): number {
  return (rad * 180) / Math.PI;
}

export function clamp(v: number, lo: number, hi: number): number {
  return v < lo ? lo : v > hi ? hi : v;
}

export function lerp(a: number, b: number, t: number): number {
  return a + (b - a) * t;
}

export function absF(v: number): number {
  return v < 0 ? -v : v;
}

export function sinF(v: number): number {
  return Math.sin(v);
}

export function cosF(v: number): number {
  return Math.cos(v);
}

export function tanF(v: number): number {
  return Math.tan(v);
}

export function sqrtF(v: number): number {
  return Math.sqrt(v);
}

export class Vec2 {
  constructor(
    public x: number = 0,
    public y: number = 0,
  ) {}

  static zero(): Vec2 {
    return new Vec2(0, 0);
  }

  static one(): Vec2 {
    return new Vec2(1, 1);
  }

  static up(): Vec2 {
    return new Vec2(0, -1); // screen-space y-down
  }

  static down(): Vec2 {
    return new Vec2(0, 1);
  }

  static left(): Vec2 {
    return new Vec2(-1, 0);
  }

  static right(): Vec2 {
    return new Vec2(1, 0);
  }

  set(x: number, y: number): this {
    this.x = x;
    this.y = y;
    return this;
  }

  clone(): Vec2 {
    return new Vec2(this.x, this.y);
  }

  add(v: Vec2): Vec2 {
    return new Vec2(this.x + v.x, this.y + v.y);
  }

  sub(v: Vec2): Vec2 {
    return new Vec2(this.x - v.x, this.y - v.y);
  }

  scale(s: number): Vec2 {
    return new Vec2(this.x * s, this.y * s);
  }

  dot(v: Vec2): number {
    return this.x * v.x + this.y * v.y;
  }

  length(): number {
    return Math.sqrt(this.x * this.x + this.y * this.y);
  }

  lengthSq(): number {
    return this.x * this.x + this.y * this.y;
  }

  normalized(): Vec2 {
    const l = this.length();
    if (l < TD_EPSILON) return new Vec2(0, 0);
    return new Vec2(this.x / l, this.y / l);
  }

  perp(): Vec2 {
    return new Vec2(-this.y, this.x);
  }

  static lerp(a: Vec2, b: Vec2, t: number): Vec2 {
    return new Vec2(lerp(a.x, b.x, t), lerp(a.y, b.y, t));
  }
}

export class Vec3 {
  constructor(
    public x: number = 0,
    public y: number = 0,
    public z: number = 0,
  ) {}

  static zero(): Vec3 {
    return new Vec3(0, 0, 0);
  }

  static one(): Vec3 {
    return new Vec3(1, 1, 1);
  }

  static up(): Vec3 {
    return new Vec3(0, 1, 0);
  }

  static forward(): Vec3 {
    return new Vec3(0, 0, -1);
  }

  clone(): Vec3 {
    return new Vec3(this.x, this.y, this.z);
  }

  set(x: number, y: number, z: number): this {
    this.x = x;
    this.y = y;
    this.z = z;
    return this;
  }

  add(v: Vec3): Vec3 {
    return new Vec3(this.x + v.x, this.y + v.y, this.z + v.z);
  }

  sub(v: Vec3): Vec3 {
    return new Vec3(this.x - v.x, this.y - v.y, this.z - v.z);
  }

  scale(s: number): Vec3 {
    return new Vec3(this.x * s, this.y * s, this.z * s);
  }

  dot(v: Vec3): number {
    return this.x * v.x + this.y * v.y + this.z * v.z;
  }

  cross(v: Vec3): Vec3 {
    return new Vec3(
      this.y * v.z - this.z * v.y,
      this.z * v.x - this.x * v.z,
      this.x * v.y - this.y * v.x,
    );
  }

  length(): number {
    return Math.sqrt(this.x * this.x + this.y * this.y + this.z * this.z);
  }

  lengthSq(): number {
    return this.x * this.x + this.y * this.y + this.z * this.z;
  }

  normalized(): Vec3 {
    const l = this.length();
    if (l < TD_EPSILON) return new Vec3(0, 0, 0);
    return new Vec3(this.x / l, this.y / l, this.z / l);
  }
}

export class Vec4 {
  constructor(
    public x: number = 0,
    public y: number = 0,
    public z: number = 0,
    public w: number = 0,
  ) {}

  perspectiveDivide(): Vec3 {
    if (absF(this.w) < TD_EPSILON) return new Vec3(0, 0, 0);
    return new Vec3(this.x / this.w, this.y / this.w, this.z / this.w);
  }
}

/**
 * Column-major 4x4 matrix for WebGL2 — exactly mirrors src/core/math/mat4.h.
 * m[col * 4 + row]
 */
export class Mat4 {
  m: Float32Array;

  constructor() {
    this.m = new Float32Array(16);
  }

  static identity(): Mat4 {
    const r = new Mat4();
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1;
    return r;
  }

  static orthographic(
    left: number,
    right: number,
    bottom: number,
    top: number,
    near: number,
    far: number,
  ): Mat4 {
    const r = new Mat4();
    const rl = right - left;
    const tb = top - bottom;
    const fn = far - near;
    r.m[0] = 2 / rl;
    r.m[5] = 2 / tb;
    r.m[10] = -2 / fn;
    r.m[12] = -(right + left) / rl;
    r.m[13] = -(top + bottom) / tb;
    r.m[14] = -(far + near) / fn;
    r.m[15] = 1;
    return r;
  }

  static perspective(
    fovRadians: number,
    aspect: number,
    near: number,
    far: number,
  ): Mat4 {
    const r = new Mat4();
    const tanHalfFov = Math.tan(fovRadians * 0.5);
    const fn = far - near;
    r.m[0] = 1 / (aspect * tanHalfFov);
    r.m[5] = 1 / tanHalfFov;
    r.m[10] = -(far + near) / fn;
    r.m[11] = -1;
    r.m[14] = -(2 * far * near) / fn;
    return r;
  }

  static translate(x: number, y: number, z: number = 0): Mat4 {
    const r = Mat4.identity();
    r.m[12] = x;
    r.m[13] = y;
    r.m[14] = z;
    return r;
  }

  static scale(sx: number, sy: number, sz: number = 1): Mat4 {
    const r = new Mat4();
    r.m[0] = sx;
    r.m[5] = sy;
    r.m[10] = sz;
    r.m[15] = 1;
    return r;
  }

  static rotateZ(radians: number): Mat4 {
    const r = Mat4.identity();
    const c = Math.cos(radians);
    const s = Math.sin(radians);
    r.m[0] = c;
    r.m[1] = s;
    r.m[4] = -s;
    r.m[5] = c;
    return r;
  }

  multiply(other: Mat4): Mat4 {
    const r = new Mat4();
    const a = this.m;
    const b = other.m;
    const out = r.m;
    for (let col = 0; col < 4; col++) {
      for (let row = 0; row < 4; row++) {
        let sum = 0;
        for (let k = 0; k < 4; k++) {
          sum += a[k * 4 + row] * b[col * 4 + k];
        }
        out[col * 4 + row] = sum;
      }
    }
    return r;
  }
}

export class Color {
  constructor(
    public r: number = 1,
    public g: number = 1,
    public b: number = 1,
    public a: number = 1,
  ) {}

  static fromHex(hex: number): Color {
    const r = ((hex >> 16) & 0xff) / 255;
    const g = ((hex >> 8) & 0xff) / 255;
    const b = (hex & 0xff) / 255;
    return new Color(r, g, b, 1);
  }

  static rgb(r: number, g: number, b: number): Color {
    return new Color(r / 255, g / 255, b / 255, 1);
  }

  static rgba(r: number, g: number, b: number, a: number): Color {
    return new Color(r / 255, g / 255, b / 255, a);
  }

  withAlpha(a: number): Color {
    return new Color(this.r, this.g, this.b, a);
  }

  toArray(): [number, number, number, number] {
    return [this.r, this.g, this.b, this.a];
  }
}
