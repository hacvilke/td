#version 330 core

in vec3 v_fragPos;
in vec3 v_normal;
in vec2 v_texcoord;

uniform sampler2D u_texture;
uniform vec3 u_objectColor;
uniform float u_ambientStrength;
uniform vec3 u_lightPos;
uniform vec3 u_lightColor;
uniform vec3 u_viewPos;
uniform float u_shininess;
uniform bool u_useTexture;

// Second light
uniform vec3 u_lightPos2;
uniform vec3 u_lightColor2;

out vec4 FragColor;

void main() {
    vec3 norm = normalize(v_normal);
    vec3 objectColor = u_useTexture ? texture(u_texture, v_texcoord).rgb : u_objectColor;

    // Ambient
    vec3 ambient = u_ambientStrength * u_lightColor;

    // Diffuse (light 1)
    vec3 lightDir1 = normalize(u_lightPos - v_fragPos);
    float diff1 = max(dot(norm, lightDir1), 0.0);
    vec3 diffuse1 = diff1 * u_lightColor;

    // Specular (light 1)
    vec3 viewDir = normalize(u_viewPos - v_fragPos);
    vec3 reflectDir1 = reflect(-lightDir1, norm);
    float spec1 = pow(max(dot(viewDir, reflectDir1), 0.0), u_shininess);
    vec3 specular1 = 0.5 * spec1 * u_lightColor;

    // Light 2 calculations
    vec3 lightDir2 = normalize(u_lightPos2 - v_fragPos);
    float diff2 = max(dot(norm, lightDir2), 0.0);
    vec3 diffuse2 = diff2 * u_lightColor2;
    vec3 reflectDir2 = reflect(-lightDir2, norm);
    float spec2 = pow(max(dot(viewDir, reflectDir2), 0.0), u_shininess);
    vec3 specular2 = 0.3 * spec2 * u_lightColor2;

    vec3 result = (ambient + diffuse1 + specular1 + diffuse2 + specular2) * objectColor;
    FragColor = vec4(result, 1.0);
}
