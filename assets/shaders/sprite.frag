#version 330 core

in vec2 v_texcoord;
in vec4 v_color;

uniform sampler2D u_texture;

out vec4 FragColor;

void main() {
    vec4 texColor = texture(u_texture, v_texcoord);
    if (texColor.a < 0.01) discard;
    FragColor = texColor * v_color;
}
