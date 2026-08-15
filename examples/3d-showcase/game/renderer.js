// =============================================================================
// renderer — WebGL2 renderer for the showcase.
// -----------------------------------------------------------------------------
// Demonstrates the TDEngine.renderer.* subsystem (src/renderer/gl_renderer.cpp
// in the C++ engine).  We use WebGL2 directly here because the showcase runs
// without the compiled WASM.  When the WASM is loaded, the C++ renderer takes
// over — but the scene-graph structure (meshes, materials, lights, camera)
// would be identical.
//
// Capabilities:
//   - Mesh upload (box, sphere, capsule, plane, skybox)
//   - Material/shader compilation (from game/shaders.js)
//   - Per-instance model matrix (physics body pose)
//   - Camera (position + yaw/pitch + perspective projection)
//   - Line renderer (for raycast viz + constraints)
// =============================================================================

(function (global) {
  'use strict';

  let _gl = null;
  let _canvas = null;
  let _programs = {};
  let _meshes = {};
  let _camera = {
    pos:  { x: 0, y: 5, z: 12 },
    yaw:  0,           // rotation around Y
    pitch: -0.15,       // rotation around X
    fov:  60 * Math.PI / 180,
    near: 0.1, far: 500,
  };
  let _triCount = 0;

  // ---- Mat4 helpers (column-major) --------------------------------------
  // Hand-rolled to avoid a gl-matrix dependency.  These are the few ops we need.
  const M4 = {
    identity: () => new Float32Array([1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1]),
    perspective: (fovy, aspect, near, far) => {
      const f = 1 / Math.tan(fovy / 2);
      const nf = 1 / (near - far);
      return new Float32Array([
        f / aspect, 0, 0, 0,
        0, f, 0, 0,
        0, 0, (far + near) * nf, -1,
        0, 0, 2 * far * near * nf, 0,
      ]);
    },
    // Build a view matrix from camera position + yaw + pitch (no roll).
    fpsView: (pos, yaw, pitch) => {
      const cy = Math.cos(yaw), sy = Math.sin(yaw);
      const cp = Math.cos(pitch), sp = Math.sin(pitch);
      // Forward: yaw rotates around Y, pitch rotates around X.
      //   yaw=0, pitch=0 → forward = (0, 0, -1) (camera looks down -Z)
      //   pitch > 0 → looking up (+Y)
      //   yaw > 0   → turned right (clockwise viewed from above)
      const fx = sy * cp, fy = sp, fz = -cy * cp;
      const rx = cy, ry = 0, rz = -sy;     // right = (cos(yaw), 0, -sin(yaw))
      // Up = right × forward
      const upX = ry * fz - rz * fy;
      const upY = rz * fx - rx * fz;
      const upZ = rx * fy - ry * fx;
      // View = inverse of camera transform (transpose of rotation, -translate).
      return new Float32Array([
        rx, upX, -fx, 0,
        ry, upY, -fy, 0,
        rz, upZ, -fz, 0,
        -(rx*pos.x + ry*pos.y + rz*pos.z),
        -(upX*pos.x + upY*pos.y + upZ*pos.z),
        -(-fx*pos.x + -fy*pos.y + -fz*pos.z),
        1,
      ]);
    },
    translate: (m, x, y, z) => {
      const out = new Float32Array(m);
      out[12] = m[0]*x + m[4]*y + m[8]*z  + m[12];
      out[13] = m[1]*x + m[5]*y + m[9]*z  + m[13];
      out[14] = m[2]*x + m[6]*y + m[10]*z + m[14];
      out[15] = m[3]*x + m[7]*y + m[11]*z + m[15];
      return out;
    },
    scale: (m, x, y, z) => {
      const out = new Float32Array(m);
      out[0] *= x; out[1] *= x; out[2] *= x; out[3] *= x;
      out[4] *= y; out[5] *= y; out[6] *= y; out[7] *= y;
      out[8] *= z; out[9] *= z; out[10] *= z; out[11] *= z;
      return out;
    },
    multiply: (a, b) => {
      const out = new Float32Array(16);
      for (let i = 0; i < 4; i++) {
        for (let j = 0; j < 4; j++) {
          out[i*4+j] = a[j]*b[i*4] + a[4+j]*b[i*4+1] + a[8+j]*b[i*4+2] + a[12+j]*b[i*4+3];
        }
      }
      return out;
    },
    // Build a TRS (translation + rotation (quat) + scale) matrix.
    trs: (pos, quat, scale) => {
      const { x, y, z, w } = quat;
      const xx = x*x, yy = y*y, zz = z*z;
      const xy = x*y, xz = x*z, yz = y*z;
      const wx = w*x, wy = w*y, wz = w*z;
      const m = new Float32Array([
        1-2*(yy+zz), 2*(xy+wz),   2*(xz-wy),   0,
        2*(xy-wz),   1-2*(xx+zz), 2*(yz+wx),   0,
        2*(xz+wy),   2*(yz-wx),   1-2*(xx+yy), 0,
        0, 0, 0, 1,
      ]);
      const s = M4.scale(M4.identity(), scale.x, scale.y, scale.z);
      const r = M4.multiply(m, s);
      r[12] = pos.x; r[13] = pos.y; r[14] = pos.z;
      return r;
    },
    tr: (pos, scale) => {
      const m = M4.identity();
      m[0] = scale.x; m[5] = scale.y; m[10] = scale.z;
      m[12] = pos.x; m[13] = pos.y; m[14] = pos.z;
      return m;
    },
  };

  // ---- Shader compilation ----------------------------------------------
  function compileShader(type, src) {
    const gl = _gl;
    const sh = gl.createShader(type);
    gl.shaderSource(sh, src);
    gl.compileShader(sh);
    if (!gl.getShaderParameter(sh, gl.COMPILE_STATUS)) {
      const log = gl.getShaderInfoLog(sh);
      gl.deleteShader(sh);
      throw new Error('Shader compile failed: ' + log + '\nSource:\n' + src);
    }
    return sh;
  }

  function linkProgram(vsSrc, fsSrc) {
    const gl = _gl;
    const vs = compileShader(gl.VERTEX_SHADER, vsSrc);
    const fs = compileShader(gl.FRAGMENT_SHADER, fsSrc);
    const prog = gl.createProgram();
    gl.attachShader(prog, vs);
    gl.attachShader(prog, fs);
    gl.linkProgram(prog);
    if (!gl.getProgramParameter(prog, gl.LINK_STATUS)) {
      const log = gl.getProgramInfoLog(prog);
      gl.deleteProgram(prog);
      throw new Error('Program link failed: ' + log);
    }
    gl.deleteShader(vs);
    gl.deleteShader(fs);
    return prog;
  }

  function getProgram(name) {
    if (_programs[name]) return _programs[name];
    const S = global.TDSandbox.shaders;
    let vs, fs;
    if (name === 'floor')   { vs = S.FLOOR_VERT;  fs = S.FLOOR_FRAG;  }
    else if (name === 'solid')  { vs = S.SOLID_VERT;  fs = S.SOLID_FRAG;  }
    else if (name === 'energy') { vs = S.ENERGY_VERT; fs = S.ENERGY_FRAG; }
    else if (name === 'sky')    { vs = S.SKY_VERT;    fs = S.SKY_FRAG;    }
    else if (name === 'line')   { vs = S.LINE_VERT;   fs = S.LINE_FRAG;   }
    else throw new Error('Unknown shader: ' + name);
    const prog = linkProgram(vs, fs);
    // Cache uniform locations.
    prog.uniforms = {};
    ['u_view', 'u_proj', 'u_model', 'u_time', 'u_beatPulse', 'u_cameraPos',
     'u_color', 'u_emissive'].forEach(u => {
      prog.uniforms[u] = _gl.getUniformLocation(prog, u);
    });
    _programs[name] = prog;
    return prog;
  }

  // ---- Mesh construction ------------------------------------------------
  function uploadMesh(verts, normals, indices) {
    const gl = _gl;
    const vao = gl.createVertexArray();
    gl.bindVertexArray(vao);
    const vbo = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, vbo);
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(verts), gl.STATIC_DRAW);
    gl.enableVertexAttribArray(0);
    gl.vertexAttribPointer(0, 3, gl.FLOAT, false, 0, 0);
    const nbo = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, nbo);
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(normals), gl.STATIC_DRAW);
    gl.enableVertexAttribArray(1);
    gl.vertexAttribPointer(1, 3, gl.FLOAT, false, 0, 0);
    const ibo = gl.createBuffer();
    gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, ibo);
    gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, new Uint16Array(indices), gl.STATIC_DRAW);
    gl.bindVertexArray(null);
    return { vao, ibo, indexCount: indices.length };
  }

  function makeBoxMesh(hx, hy, hz) {
    // 24 verts (4 per face), 36 indices.
    const verts = [
      // +X
       hx,-hy,-hz,  hx, hy,-hz,  hx, hy, hz,  hx,-hy, hz,
      // -X
      -hx,-hy, hz, -hx, hy, hz, -hx, hy,-hz, -hx,-hy,-hz,
      // +Y
      -hx, hy,-hz, -hx, hy, hz,  hx, hy, hz,  hx, hy,-hz,
      // -Y
      -hx,-hy, hz, -hx,-hy,-hz,  hx,-hy,-hz,  hx,-hy, hz,
      // +Z
      -hx,-hy, hz,  hx,-hy, hz,  hx, hy, hz, -hx, hy, hz,
      // -Z
       hx,-hy,-hz, -hx,-hy,-hz, -hx, hy,-hz,  hx, hy,-hz,
    ];
    const normals = [
      1,0,0, 1,0,0, 1,0,0, 1,0,0,
      -1,0,0,-1,0,0,-1,0,0,-1,0,0,
      0,1,0, 0,1,0, 0,1,0, 0,1,0,
      0,-1,0,0,-1,0,0,-1,0,0,-1,0,
      0,0,1, 0,0,1, 0,0,1, 0,0,1,
      0,0,-1,0,0,-1,0,0,-1,0,0,-1,
    ];
    const indices = [];
    for (let f = 0; f < 6; f++) {
      const o = f * 4;
      indices.push(o, o+1, o+2, o, o+2, o+3);
    }
    return uploadMesh(verts, normals, indices);
  }

  function makeSphereMesh(radius, segments, rings) {
    const verts = [], normals = [], indices = [];
    for (let r = 0; r <= rings; r++) {
      const v = r / rings;
      const phi = v * Math.PI;
      for (let s = 0; s <= segments; s++) {
        const u = s / segments;
        const theta = u * Math.PI * 2;
        const x = Math.sin(phi) * Math.cos(theta);
        const y = Math.cos(phi);
        const z = Math.sin(phi) * Math.sin(theta);
        verts.push(x * radius, y * radius, z * radius);
        normals.push(x, y, z);
      }
    }
    for (let r = 0; r < rings; r++) {
      for (let s = 0; s < segments; s++) {
        const a = r * (segments + 1) + s;
        const b = a + segments + 1;
        indices.push(a, b, a + 1, b, b + 1, a + 1);
      }
    }
    return uploadMesh(verts, normals, indices);
  }

  function makeCapsuleMesh(radius, height, segments) {
    // Approximate as a sphere stretched along Y — visually fine for the demo.
    // The physics collider is the real capsule; this just needs to look right.
    const totalHeight = height + 2 * radius;
    return makeSphereMesh(radius, segments, segments);
  }

  function makePlaneMesh(size) {
    const h = size / 2;
    const verts = [
      -h, 0, -h,  h, 0, -h,  h, 0,  h, -h, 0,  h,
    ];
    const normals = [0,1,0, 0,1,0, 0,1,0, 0,1,0];
    const indices = [0, 1, 2, 0, 2, 3];
    return uploadMesh(verts, normals, indices);
  }

  function makeSkyMesh() {
    // Unit cube, viewed from inside.
    return makeBoxMesh(1, 1, 1);
  }

  // ---- Init -------------------------------------------------------------
  function init(canvas) {
    _canvas = canvas;
    const gl = canvas.getContext('webgl2', { antialias: true, alpha: false });
    if (!gl) throw new Error('WebGL2 not supported by this browser.');
    _gl = gl;
    // Compile all programs up front.
    getProgram('floor');
    getProgram('solid');
    getProgram('energy');
    getProgram('sky');
    getProgram('line');
    // Build standard meshes (unit sizes; renderer scales per-instance).
    _meshes.box     = makeBoxMesh(0.5, 0.5, 0.5);
    _meshes.sphere  = makeSphereMesh(0.5, 24, 16);
    _meshes.capsule = makeCapsuleMesh(0.5, 1.0, 16);
    _meshes.plane   = makePlaneMesh(200);
    _meshes.sky     = makeSkyMesh();
    // GL state.
    gl.enable(gl.DEPTH_TEST);
    gl.enable(gl.CULL_FACE);
    gl.cullFace(gl.BACK);
    gl.frontFace(gl.CCW);
    gl.clearColor(0.02, 0.03, 0.06, 1.0);
    return gl;
  }

  function resize() {
    if (!_gl || !_canvas) return;
    const dpr = Math.min(window.devicePixelRatio || 1, 2);
    const w = Math.floor(window.innerWidth * dpr);
    const h = Math.floor(window.innerHeight * dpr);
    if (_canvas.width !== w || _canvas.height !== h) {
      _canvas.width = w; _canvas.height = h;
    }
    _gl.viewport(0, 0, w, h);
  }

  // ---- Camera -----------------------------------------------------------
  function setCameraPos(x, y, z) { _camera.pos = { x, y, z }; }
  function getCameraPos() { return _camera.pos; }
  function setCameraYawPitch(yaw, pitch) { _camera.yaw = yaw; _camera.pitch = pitch; }
  function getCameraYaw() { return _camera.yaw; }
  function getCameraPitch() { return _camera.pitch; }
  function moveCamera(dx, dy, dz) {
    _camera.pos.x += dx; _camera.pos.y += dy; _camera.pos.z += dz;
  }

  // ---- Render -----------------------------------------------------------
  function clear() {
    const gl = _gl;
    gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
    _triCount = 0;
  }

  function renderSky(time) {
    const gl = _gl;
    const prog = getProgram('sky');
    gl.useProgram(prog);
    // Disable depth test AND depth mask — sky is the background, always drawn first.
    gl.depthMask(false);
    gl.disable(gl.DEPTH_TEST);
    gl.disable(gl.CULL_FACE);
    const view = M4.fpsView(_camera.pos, _camera.yaw, _camera.pitch);
    const aspect = _canvas.width / _canvas.height;
    const proj = M4.perspective(_camera.fov, aspect, _camera.near, _camera.far);
    gl.uniformMatrix4fv(prog.uniforms['u_view'], false, view);
    gl.uniformMatrix4fv(prog.uniforms['u_proj'], false, proj);
    gl.uniform1f(prog.uniforms['u_time'], time);
    const model = M4.scale(M4.identity(), 100, 100, 100);
    gl.uniformMatrix4fv(prog.uniforms['u_model'], false, model);
    gl.bindVertexArray(_meshes.sky.vao);
    gl.drawElements(gl.TRIANGLES, _meshes.sky.indexCount, gl.UNSIGNED_SHORT, 0);
    gl.bindVertexArray(null);
    gl.enable(gl.CULL_FACE);
    gl.enable(gl.DEPTH_TEST);
    gl.depthMask(true);
    _triCount += _meshes.sky.indexCount / 3;
  }

  function renderFloor(time, beatPulse) {
    const gl = _gl;
    const prog = getProgram('floor');
    gl.useProgram(prog);
    const view = M4.fpsView(_camera.pos, _camera.yaw, _camera.pitch);
    const aspect = _canvas.width / _canvas.height;
    const proj = M4.perspective(_camera.fov, aspect, _camera.near, _camera.far);
    gl.uniformMatrix4fv(prog.uniforms['u_view'], false, view);
    gl.uniformMatrix4fv(prog.uniforms['u_proj'], false, proj);
    gl.uniform1f(prog.uniforms['u_time'], time);
    gl.uniform1f(prog.uniforms['u_beatPulse'], beatPulse);
    gl.uniform3f(prog.uniforms['u_cameraPos'], _camera.pos.x, _camera.pos.y, _camera.pos.z);
    const model = M4.identity();
    gl.uniformMatrix4fv(prog.uniforms['u_model'], false, model);
    gl.bindVertexArray(_meshes.plane.vao);
    gl.drawElements(gl.TRIANGLES, _meshes.plane.indexCount, gl.UNSIGNED_SHORT, 0);
    gl.bindVertexArray(null);
    _triCount += _meshes.plane.indexCount / 3;
  }

  // Render a physics body.  body = { position, collider, color, ... }
  function renderBody(body, time) {
    const gl = _gl;
    if (!body.collider) return;
    const c = body.collider;
    let mesh, scale, progName = 'solid';
    if (c.type === 'sphere') {
      mesh = _meshes.sphere;
      scale = { x: c.radius * 2, y: c.radius * 2, z: c.radius * 2 };
    } else if (c.type === 'box') {
      mesh = _meshes.box;
      scale = { x: c.hx * 2, y: c.hy * 2, z: c.hz * 2 };
    } else if (c.type === 'capsule') {
      mesh = _meshes.capsule;
      scale = { x: c.radius * 2, y: c.radius * 2, z: c.radius * 2 };
    } else if (c.type === 'static-plane') {
      return;  // floor renders separately
    } else {
      return;
    }
    if (body.tag === 'energy') progName = 'energy';
    const prog = getProgram(progName);
    gl.useProgram(prog);
    const view = M4.fpsView(_camera.pos, _camera.yaw, _camera.pitch);
    const aspect = _canvas.width / _canvas.height;
    const proj = M4.perspective(_camera.fov, aspect, _camera.near, _camera.far);
    gl.uniformMatrix4fv(prog.uniforms['u_view'], false, view);
    gl.uniformMatrix4fv(prog.uniforms['u_proj'], false, proj);
    gl.uniform3f(prog.uniforms['u_cameraPos'], _camera.pos.x, _camera.pos.y, _camera.pos.z);
    gl.uniform1f(prog.uniforms['u_time'], time);
    const col = body.color || { r: 0.7, g: 0.7, b: 0.8 };
    gl.uniform3f(prog.uniforms['u_color'], col.r, col.g, col.b);
    gl.uniform1f(prog.uniforms['u_emissive'], body.emissive || 0);
    const model = M4.tr(body.position, scale);
    gl.uniformMatrix4fv(prog.uniforms['u_model'], false, model);
    gl.bindVertexArray(mesh.vao);
    gl.drawElements(gl.TRIANGLES, mesh.indexCount, gl.UNSIGNED_SHORT, 0);
    gl.bindVertexArray(null);
    _triCount += mesh.indexCount / 3;
  }

  // Render a list of line segments (for raycast viz + constraints).
  function renderLines(segments, color) {
    const gl = _gl;
    if (!segments.length) return;
    const verts = [];
    for (const s of segments) {
      verts.push(s.a.x, s.a.y, s.a.z, s.b.x, s.b.y, s.b.z);
    }
    const prog = getProgram('line');
    gl.useProgram(prog);
    const view = M4.fpsView(_camera.pos, _camera.yaw, _camera.pitch);
    const aspect = _canvas.width / _canvas.height;
    const proj = M4.perspective(_camera.fov, aspect, _camera.near, _camera.far);
    gl.uniformMatrix4fv(prog.uniforms['u_view'], false, view);
    gl.uniformMatrix4fv(prog.uniforms['u_proj'], false, proj);
    gl.uniform3f(prog.uniforms['u_color'], color[0], color[1], color[2]);
    const vbo = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, vbo);
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(verts), gl.DYNAMIC_DRAW);
    const vao = gl.createVertexArray();
    gl.bindVertexArray(vao);
    gl.enableVertexAttribArray(0);
    gl.vertexAttribPointer(0, 3, gl.FLOAT, false, 0, 0);
    gl.drawArrays(gl.LINES, 0, verts.length / 3);
    gl.bindVertexArray(null);
    gl.deleteBuffer(vbo);
    gl.deleteVertexArray(vao);
  }

  function triCount() { return _triCount; }

  global.TDSandbox = global.TDSandbox || {};
  global.TDSandbox.renderer = {
    init, resize, clear,
    renderSky, renderFloor, renderBody, renderLines,
    setCameraPos, getCameraPos, setCameraYawPitch, getCameraYaw, getCameraPitch, moveCamera,
    triCount,
    M4,
  };
})(typeof window !== 'undefined' ? window : this);
