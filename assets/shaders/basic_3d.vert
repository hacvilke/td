#version 330 core

layout (location = 0) in vec3 a_position;
layout (location = 1) in vec3 a_normal;
layout (location = 2) in vec2 a_texcoord;

uniform mat4 u_projection;
uniform mat4 u_view;
uniform mat4 u_model;
uniform mat4 u_normalMatrix;

out vec3 v_fragPos;
out vec3 v_normal;
out vec2 v_texcoord;

void main() {
    vec4 worldPos = u_model * vec4(a_position, 1.0);
    v_fragPos = worldPos.xyz;
    v_normal = mat3(u_normalMatrix) * a_normal;
    v_texcoord = a_texcoord;
    gl_Position = u_projection * u_view * worldPos;
}
