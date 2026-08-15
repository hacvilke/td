// =============================================================================
// shaders — Custom GLSL shaders for the showcase.
// -----------------------------------------------------------------------------
// Demonstrates the TDEngine.shaderGraph.* subsystem: the C++ side has a node
// graph compiler (src/renderer/shader_graph.cpp) that produces GLSL from a
// node description.  Here in the showcase we ship hand-written GLSL because
// the WASM isn't required to run — but the code patterns match what the
// shader graph would emit.
//
// Each shader pair is compiled once and cached by the renderer.
// =============================================================================

(function (global) {
  'use strict';

  // ---- Floor grid shader ------------------------------------------------
  // Renders a procedurally-generated grid that pulses to the beat.  The
  // vertex shader passes world position to the fragment shader; the
  // fragment shader computes a grid pattern from XZ coordinates, fades
  // with distance from the camera, and adds a beat-synced pulse.
  const FLOOR_VERT = `#version 300 es
    precision highp float;
    in vec3 a_pos;
    in vec3 a_normal;
    uniform mat4 u_view;
    uniform mat4 u_proj;
    uniform float u_time;
    uniform float u_beatPulse;
    out vec3 v_worldPos;
    out vec3 v_normal;
    void main() {
      v_worldPos = a_pos;
      v_normal = a_normal;
      vec4 viewPos = u_view * vec4(a_pos, 1.0);
      gl_Position = u_proj * viewPos;
    }
  `;
  const FLOOR_FRAG = `#version 300 es
    precision highp float;
    in vec3 v_worldPos;
    in vec3 v_normal;
    uniform float u_time;
    uniform float u_beatPulse;
    uniform vec3  u_cameraPos;
    out vec4 frag;
    void main() {
      // Grid lines every 1 meter.
      vec2 g = abs(fract(v_worldPos.xz) - 0.5);
      float line = smoothstep(0.48, 0.50, max(g.x, g.y));
      // Major lines every 5 meters — brighter.
      vec2 g5 = abs(fract(v_worldPos.xz / 5.0) - 0.5);
      float major = smoothstep(0.49, 0.50, max(g5.x, g5.y));
      // Base color: dark blue-gray.
      vec3 base = vec3(0.04, 0.06, 0.09);
      // Grid color: cyan.
      vec3 gridCol = vec3(0.20, 0.55, 0.65);
      // Beat pulse: brightens the grid on every beat.
      gridCol += vec3(0.30, 0.85, 1.00) * u_beatPulse;
      vec3 color = mix(base, gridCol, line * 0.7 + major * 0.5);
      // Distance fade so the floor fades into the horizon.
      float dist = length(v_worldPos.xz - u_cameraPos.xz);
      float fade = 1.0 - smoothstep(40.0, 80.0, dist);
      color *= fade;
      // Subtle radial vignette around camera.
      float vig = 1.0 - smoothstep(10.0, 40.0, dist) * 0.5;
      color *= vig;
      frag = vec4(color, 1.0);
    }
  `;

  // ---- Solid color shader (for boxes/spheres/capsules) ------------------
  // Simple Lambertian diffuse + a touch of rim lighting.  Used for all
  // physics props with per-entity color.
  const SOLID_VERT = `#version 300 es
    precision highp float;
    in vec3 a_pos;
    in vec3 a_normal;
    uniform mat4 u_model;
    uniform mat4 u_view;
    uniform mat4 u_proj;
    out vec3 v_worldPos;
    out vec3 v_normal;
    void main() {
      vec4 worldPos = u_model * vec4(a_pos, 1.0);
      v_worldPos = worldPos.xyz;
      v_normal = mat3(u_model) * a_normal;
      gl_Position = u_proj * u_view * worldPos;
    }
  `;
  const SOLID_FRAG = `#version 300 es
    precision highp float;
    in vec3 v_worldPos;
    in vec3 v_normal;
    uniform vec3 u_cameraPos;
    uniform vec3 u_color;
    uniform float u_emissive;
    out vec4 frag;
    void main() {
      vec3 N = normalize(v_normal);
      vec3 V = normalize(u_cameraPos - v_worldPos);
      // Single directional light (warm key + cool fill).
      vec3 L1 = normalize(vec3(0.5, 1.0, 0.3));
      vec3 L2 = normalize(vec3(-0.5, 0.6, -0.7));
      float d1 = max(dot(N, L1), 0.0);
      float d2 = max(dot(N, L2), 0.0) * 0.4;
      vec3 warm = vec3(1.0, 0.95, 0.85) * d1;
      vec3 cool = vec3(0.6, 0.7, 1.0) * d2;
      // Rim light.
      float rim = pow(1.0 - max(dot(N, V), 0.0), 3.0);
      vec3 rimCol = vec3(0.4, 0.85, 1.0) * rim * 0.6;
      vec3 color = u_color * (warm + cool) + rimCol;
      // Emissive add (for energy balls).
      color += u_color * u_emissive;
      frag = vec4(color, 1.0);
    }
  `;

  // ---- Energy ball shader (always-on bloom) -----------------------------
  // Renders the energy ball projectile: pulsing emissive sphere with a
  // fresnel rim.  Demonstrates a more complex shader that uses time.
  const ENERGY_VERT = SOLID_VERT;
  const ENERGY_FRAG = `#version 300 es
    precision highp float;
    in vec3 v_worldPos;
    in vec3 v_normal;
    uniform vec3 u_cameraPos;
    uniform vec3 u_color;
    uniform float u_time;
    out vec4 frag;
    void main() {
      vec3 N = normalize(v_normal);
      vec3 V = normalize(u_cameraPos - v_worldPos);
      float fres = pow(1.0 - max(dot(N, V), 0.0), 2.0);
      float pulse = 0.5 + 0.5 * sin(u_time * 8.0);
      vec3 core = u_color * (1.5 + pulse * 0.5);
      vec3 rim = vec3(1.0) * fres * 1.5;
      frag = vec4(core + rim, 1.0);
    }
  `;

  // ---- Skybox shader ----------------------------------------------------
  // Simple gradient sky (procedural, no cubemap needed).
  const SKY_VERT = `#version 300 es
    precision highp float;
    in vec3 a_pos;
    out vec3 v_dir;
    uniform mat4 u_view;
    uniform mat4 u_proj;
    void main() {
      v_dir = a_pos;
      // Strip translation from view matrix (skybox follows camera).
      mat4 viewNoTrans = mat4(mat3(u_view));
      vec4 clipPos = u_proj * viewNoTrans * vec4(a_pos, 1.0);
      // Force depth to 1.0 (far plane) so the sky always renders behind.
      gl_Position = clipPos.xyww;
    }
  `;
  const SKY_FRAG = `#version 300 es
    precision highp float;
    in vec3 v_dir;
    uniform float u_time;
    out vec4 frag;
    void main() {
      vec3 dir = normalize(v_dir);
      float t = max(dir.y * 0.5 + 0.5, 0.0);
      // Top: deep blue.  Bottom: warm purple.  Horizon: pale cyan.
      vec3 top    = vec3(0.02, 0.04, 0.10);
      vec3 horiz  = vec3(0.10, 0.18, 0.28);
      vec3 bottom = vec3(0.18, 0.12, 0.20);
      vec3 color = dir.y > 0.0
        ? mix(horiz, top, smoothstep(0.0, 0.6, dir.y))
        : mix(horiz, bottom, smoothstep(0.0, -0.4, dir.y));
      // Subtle star-like sparkle near zenith.
      float sparkle = sin(dir.x * 80.0 + u_time) * sin(dir.z * 70.0 - u_time * 0.7);
      sparkle = max(smoothstep(0.992, 1.0, sparkle), 0.0);
      color += vec3(0.7, 0.9, 1.0) * sparkle * smoothstep(0.4, 0.9, dir.y);
      frag = vec4(color, 1.0);
    }
  `;

  // ---- Line shader (for raycast visualization + constraints) -----------
  const LINE_VERT = `#version 300 es
    precision highp float;
    in vec3 a_pos;
    uniform mat4 u_view;
    uniform mat4 u_proj;
    void main() {
      gl_Position = u_proj * u_view * vec4(a_pos, 1.0);
    }
  `;
  const LINE_FRAG = `#version 300 es
    precision highp float;
    uniform vec3 u_color;
    out vec4 frag;
    void main() {
      frag = vec4(u_color, 1.0);
    }
  `;

  global.TDSandbox = global.TDSandbox || {};
  global.TDSandbox.shaders = {
    FLOOR_VERT, FLOOR_FRAG,
    SOLID_VERT, SOLID_FRAG,
    ENERGY_VERT, ENERGY_FRAG,
    SKY_VERT,    SKY_FRAG,
    LINE_VERT,   LINE_FRAG,
  };
})(typeof window !== 'undefined' ? window : this);
