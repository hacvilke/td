/**
 * TD Engine — WebGL2 Renderer + SpriteBatch (TypeScript port of
 * src/renderer/{gl_renderer,sprite_batch}.h/.cpp)
 *
 * Uses the SAME shaders as the C++ engine (assets/shaders/sprite.{vert,frag}),
 * ported to GLSL ES 3.00 (#version 300 es) for WebGL2.
 */

import { Mat4 } from './math';

const SPRITE_VERT_SRC = `#version 300 es
precision highp float;
layout (location = 0) in vec2 a_position;
layout (location = 1) in vec2 a_texcoord;
layout (location = 2) in vec4 a_color;
uniform mat4 u_projection;
uniform mat4 u_view;
out vec2 v_texcoord;
out vec4 v_color;
void main() {
    gl_Position = u_projection * u_view * vec4(a_position, 0.0, 1.0);
    v_texcoord = a_texcoord;
    v_color = a_color;
}
`;

const SPRITE_FRAG_SRC = `#version 300 es
precision highp float;
in vec2 v_texcoord;
in vec4 v_color;
uniform sampler2D u_texture;
out vec4 FragColor;
void main() {
    vec4 texColor = texture(u_texture, v_texcoord);
    if (texColor.a < 0.01) discard;
    FragColor = texColor * v_color;
}
`;

export interface SpriteVertex {
  x: number; y: number;     // position
  u: number; v: number;     // texcoord
  r: number; g: number; b: number; a: number; // color
}

export interface SpriteData {
  x: number;
  y: number;
  width: number;
  height: number;
  u0: number; v0: number; u1: number; v1: number;
  r: number; g: number; b: number; a: number;
  rotation: number;
  originX: number;
  originY: number;
}

const MAX_SPRITES = 10000;
const VERTICES_PER_SPRITE = 4;
const INDICES_PER_SPRITE = 6;
const FLOATS_PER_VERTEX = 8; // x, y, u, v, r, g, b, a
const VERTEX_BUFFER_SIZE = MAX_SPRITES * VERTICES_PER_SPRITE * FLOATS_PER_VERTEX;

function createShader(gl: WebGL2RenderingContext, type: number, src: string): WebGLShader {
  const sh = gl.createShader(type);
  if (!sh) throw new Error('Failed to allocate shader');
  gl.shaderSource(sh, src);
  gl.compileShader(sh);
  if (!gl.getShaderParameter(sh, gl.COMPILE_STATUS)) {
    const log = gl.getShaderInfoLog(sh);
    gl.deleteShader(sh);
    throw new Error(`Shader compile error: ${log}`);
  }
  return sh;
}

function createProgram(gl: WebGL2RenderingContext, vsSrc: string, fsSrc: string): WebGLProgram {
  const vs = createShader(gl, gl.VERTEX_SHADER, vsSrc);
  const fs = createShader(gl, gl.FRAGMENT_SHADER, fsSrc);
  const prog = gl.createProgram();
  if (!prog) throw new Error('Failed to allocate program');
  gl.attachShader(prog, vs);
  gl.attachShader(prog, fs);
  gl.linkProgram(prog);
  if (!gl.getProgramParameter(prog, gl.LINK_STATUS)) {
    const log = gl.getProgramInfoLog(prog);
    gl.deleteProgram(prog);
    throw new Error(`Program link error: ${log}`);
  }
  gl.deleteShader(vs);
  gl.deleteShader(fs);
  return prog;
}

export class SpriteBatch {
  private gl: WebGL2RenderingContext;
  private program: WebGLProgram;
  private vao: WebGLVertexArrayObject;
  private vbo: WebGLBuffer;
  private ibo: WebGLBuffer;
  private vertexData: Float32Array;
  private vertexCount = 0;
  private spriteCount = 0;
  private drawCallCount = 0;
  private currentTexture: WebGLTexture | null = null;
  private whiteTexture: WebGLTexture;
  private projLocation: WebGLUniformLocation;
  private viewLocation: WebGLUniformLocation;
  private texLocation: WebGLUniformLocation;
  private started = false;

  constructor(gl: WebGL2RenderingContext) {
    this.gl = gl;
    this.program = createProgram(gl, SPRITE_VERT_SRC, SPRITE_FRAG_SRC);
    this.projLocation = gl.getUniformLocation(this.program, 'u_projection')!;
    this.viewLocation = gl.getUniformLocation(this.program, 'u_view')!;
    this.texLocation = gl.getUniformLocation(this.program, 'u_texture')!;

    // VAO
    const vao = gl.createVertexArray();
    if (!vao) throw new Error('Failed to create VAO');
    this.vao = vao;
    gl.bindVertexArray(vao);

    // VBO
    const vbo = gl.createBuffer();
    if (!vbo) throw new Error('Failed to create VBO');
    this.vbo = vbo;
    gl.bindBuffer(gl.ARRAY_BUFFER, vbo);
    gl.bufferData(gl.ARRAY_BUFFER, VERTEX_BUFFER_SIZE * 4, gl.DYNAMIC_DRAW);

    const stride = FLOATS_PER_VERTEX * 4;
    gl.enableVertexAttribArray(0);
    gl.vertexAttribPointer(0, 2, gl.FLOAT, false, stride, 0);
    gl.enableVertexAttribArray(1);
    gl.vertexAttribPointer(1, 2, gl.FLOAT, false, stride, 2 * 4);
    gl.enableVertexAttribArray(2);
    gl.vertexAttribPointer(2, 4, gl.FLOAT, false, stride, 4 * 4);

    // IBO
    const ibo = gl.createBuffer();
    if (!ibo) throw new Error('Failed to create IBO');
    this.ibo = ibo;
    gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, ibo);
    const indices = new Uint16Array(MAX_SPRITES * INDICES_PER_SPRITE);
    for (let i = 0; i < MAX_SPRITES; i++) {
      const v = i * VERTICES_PER_SPRITE;
      const o = i * INDICES_PER_SPRITE;
      indices[o] = v;
      indices[o + 1] = v + 1;
      indices[o + 2] = v + 2;
      indices[o + 3] = v + 2;
      indices[o + 4] = v + 3;
      indices[o + 5] = v;
    }
    gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, indices, gl.STATIC_DRAW);

    gl.bindVertexArray(null);

    this.vertexData = new Float32Array(VERTEX_BUFFER_SIZE);

    // 1x1 white texture for untextured quads
    const wt = gl.createTexture();
    if (!wt) throw new Error('Failed to create white texture');
    this.whiteTexture = wt;
    gl.bindTexture(gl.TEXTURE_2D, wt);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, 1, 1, 0, gl.RGBA, gl.UNSIGNED_BYTE, new Uint8Array([255, 255, 255, 255]));
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
  }

  begin(projection: Mat4, view: Mat4 = Mat4.identity()): void {
    if (this.started) {
      console.warn('SpriteBatch.begin() called without end()');
      return;
    }
    this.started = true;
    this.spriteCount = 0;
    this.vertexCount = 0;
    this.drawCallCount = 0;
    this.currentTexture = null;

    this.gl.useProgram(this.program);
    this.gl.uniformMatrix4fv(this.projLocation, false, projection.m);
    this.gl.uniformMatrix4fv(this.viewLocation, false, view.m);
    this.gl.uniform1i(this.texLocation, 0);
    this.gl.activeTexture(this.gl.TEXTURE0);
    this.gl.bindVertexArray(this.vao);
  }

  draw(sprite: SpriteData, texture: WebGLTexture | null): void {
    if (!this.started) return;
    if (this.spriteCount >= MAX_SPRITES) this.flush();

    const tex = texture ?? this.whiteTexture;
    if (tex !== this.currentTexture) {
      this.flush();
      this.currentTexture = tex;
      this.gl.bindTexture(this.gl.TEXTURE_2D, tex);
    }

    this.expandSprite(sprite);
    this.spriteCount++;
  }

  drawQuad(
    x: number, y: number, width: number, height: number,
    r = 1, g = 1, b = 1, a = 1,
    texture: WebGLTexture | null = null,
  ): void {
    this.draw({
      x, y, width, height,
      u0: 0, v0: 0, u1: 1, v1: 1,
      r, g, b, a,
      rotation: 0,
      originX: 0,
      originY: 0,
    }, texture);
  }

  end(): void {
    if (!this.started) return;
    this.flush();
    this.gl.bindVertexArray(null);
    this.started = false;
    this.currentTexture = null;
  }

  private expandSprite(s: SpriteData): void {
    const o = this.vertexCount * FLOATS_PER_VERTEX;

    // Compute the 4 corners, with origin as pivot, then rotate.
    // Origin is normalized [0,1]. 0,0 = top-left of quad, 1,1 = bottom-right.
    const ox = s.x + s.originX * s.width;
    const oy = s.y + s.originY * s.height;

    // Local corner offsets from pivot
    const x0 = s.x - ox;
    const y0 = s.y - oy;
    const x1 = x0 + s.width;
    const y1 = y0 + s.height;

    const c = Math.cos(s.rotation);
    const sn = Math.sin(s.rotation);

    // Rotate each corner around pivot
    const rotate = (lx: number, ly: number): [number, number] => [
      ox + lx * c - ly * sn,
      oy + lx * sn + ly * c,
    ];

    const [p0x, p0y] = rotate(x0, y0); // top-left
    const [p1x, p1y] = rotate(x1, y0); // top-right
    const [p2x, p2y] = rotate(x1, y1); // bottom-right
    const [p3x, p3y] = rotate(x0, y1); // bottom-left

    const d = this.vertexData;

    // vertex 0 (top-left): uv (u0, v0)
    d[o + 0] = p0x; d[o + 1] = p0y; d[o + 2] = s.u0; d[o + 3] = s.v0;
    d[o + 4] = s.r; d[o + 5] = s.g; d[o + 6] = s.b; d[o + 7] = s.a;
    // vertex 1 (top-right): uv (u1, v0)
    d[o + 8] = p1x; d[o + 9] = p1y; d[o + 10] = s.u1; d[o + 11] = s.v0;
    d[o + 12] = s.r; d[o + 13] = s.g; d[o + 14] = s.b; d[o + 15] = s.a;
    // vertex 2 (bottom-right): uv (u1, v1)
    d[o + 16] = p2x; d[o + 17] = p2y; d[o + 18] = s.u1; d[o + 19] = s.v1;
    d[o + 20] = s.r; d[o + 21] = s.g; d[o + 22] = s.b; d[o + 23] = s.a;
    // vertex 3 (bottom-left): uv (u0, v1)
    d[o + 24] = p3x; d[o + 25] = p3y; d[o + 26] = s.u0; d[o + 27] = s.v1;
    d[o + 28] = s.r; d[o + 29] = s.g; d[o + 30] = s.b; d[o + 31] = s.a;

    this.vertexCount += VERTICES_PER_SPRITE;
  }

  flush(): void {
    if (this.spriteCount === 0) return;

    const gl = this.gl;
    gl.bindBuffer(gl.ARRAY_BUFFER, this.vbo);
    gl.bufferSubData(gl.ARRAY_BUFFER, 0, this.vertexData.subarray(0, this.vertexCount * FLOATS_PER_VERTEX));

    if (this.currentTexture) {
      gl.bindTexture(gl.TEXTURE_2D, this.currentTexture);
    } else {
      gl.bindTexture(gl.TEXTURE_2D, this.whiteTexture);
    }

    gl.drawElements(gl.TRIANGLES, this.spriteCount * INDICES_PER_SPRITE, gl.UNSIGNED_SHORT, 0);
    this.drawCallCount++;
    this.spriteCount = 0;
    this.vertexCount = 0;
  }

  getSpriteCount(): number {
    return this.spriteCount;
  }

  getDrawCallCount(): number {
    return this.drawCallCount;
  }

  shutdown(): void {
    const gl = this.gl;
    gl.deleteBuffer(this.vbo);
    gl.deleteBuffer(this.ibo);
    gl.deleteVertexArray(this.vao);
    gl.deleteProgram(this.program);
    gl.deleteTexture(this.whiteTexture);
  }
}

/** Top-level renderer (singleton-style like td::Renderer in C++). */
export class Renderer {
  private gl: WebGL2RenderingContext;
  private spriteBatch: SpriteBatch;

  constructor(gl: WebGL2RenderingContext) {
    this.gl = gl;
    this.spriteBatch = new SpriteBatch(gl);
  }

  init(): void {
    const gl = this.gl;
    gl.enable(gl.BLEND);
    gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);
    gl.disable(gl.DEPTH_TEST);
  }

  clear(r: number, g: number, b: number, a = 1): void {
    const gl = this.gl;
    gl.clearColor(r, g, b, a);
    gl.clear(gl.COLOR_BUFFER_BIT);
  }

  setViewport(x: number, y: number, w: number, h: number): void {
    this.gl.viewport(x, y, w, h);
  }

  getSpriteBatch(): SpriteBatch {
    return this.spriteBatch;
  }

  shutdown(): void {
    this.spriteBatch.shutdown();
  }
}
