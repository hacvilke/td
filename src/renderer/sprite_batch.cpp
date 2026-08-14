#include "sprite_batch.h"
#include "gl_renderer.h"
#include "../core/logger.h"
#include "../core/math/math.h"
#include <cstring>

namespace td {

// Embedded sprite shader source
//
// Two variants are kept in sync:
//   - Desktop OpenGL 3.3+ : #version 330 core, layout(location=...) syntax
//   - WebGL2 / GLSL ES 3.0: #version 300 es  + precision qualifiers
//
// The desktop GLSL `#version 330 core` directive is rejected by WebGL2's
// GLSL ES compiler ('core' is an invalid version directive). Conversely,
// `#version 300 es` is invalid on desktop GL 3.3 contexts. So we select
// the correct source at compile time via #ifdef __EMSCRIPTEN__.
//
// IMPORTANT: GLSL requires the #version directive to be the literal first
// characters of the shader source — no leading newline, no comment, no
// whitespace. We use a custom raw-string delimiter R"GLSL_ES(...)GLSL_ES"
// (instead of the bare R"(...)") so the #version line can sit immediately
// after the opening paren with no inserted newline. The default R"( form
// always starts the string with a newline after the (, which causes:
//   "ERROR: 0:2: #version directive must occur on the first line of the shader"
#ifdef __EMSCRIPTEN__
static const char* SPRITE_VERT_SRC = R"GLSL_ES(#version 300 es
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
)GLSL_ES";

static const char* SPRITE_FRAG_SRC = R"GLSL_ES(#version 300 es
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
)GLSL_ES";
#else
static const char* SPRITE_VERT_SRC = R"GLSL(#version 330 core
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
)GLSL";

static const char* SPRITE_FRAG_SRC = R"GLSL(#version 330 core
in vec2 v_texcoord;
in vec4 v_color;

uniform sampler2D u_texture;

out vec4 FragColor;

void main() {
    vec4 texColor = texture(u_texture, v_texcoord);
    if (texColor.a < 0.01) discard;
    FragColor = texColor * v_color;
}
)GLSL";
#endif

bool SpriteBatch::init() {
    // Create shader from embedded source
    if (!m_shader.loadFromMemory(SPRITE_VERT_SRC, SPRITE_FRAG_SRC)) {
        TD_LOG_ERROR("Failed to create sprite batch shader");
        return false;
    }
    
    // Create VAO
    gl.glGenVertexArrays(1, &m_vao);
    gl.glBindVertexArray(m_vao);
    
    // Create VBO for vertex data (dynamic)
    gl.glGenBuffers(1, &m_vbo);
    gl.glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    gl.glBufferData(GL_ARRAY_BUFFER, 
                    sizeof(SpriteVertex) * MAX_SPRITES * VERTICES_PER_SPRITE,
                    nullptr, GL_DYNAMIC_DRAW);
    
    // Set up vertex attributes
    // Position (2 floats)
    gl.glEnableVertexAttribArray(0);
    gl.glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(SpriteVertex), (void*)0);
    
    // Texcoord (2 floats)
    gl.glEnableVertexAttribArray(1);
    gl.glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(SpriteVertex), 
                             (void*)(2 * sizeof(float)));
    
    // Color (4 floats)
    gl.glEnableVertexAttribArray(2);
    gl.glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(SpriteVertex), 
                             (void*)(4 * sizeof(float)));
    
    // Create IBO with static indices
    // NOTE: This array is MAX_SPRITES * INDICES_PER_SPRITE * sizeof(uint32_t)
    // = 10000 * 6 * 4 = 240 KB. That's far too large to put on the WASM
    // stack (default 64KB, even 8MB stack is wasteful for a one-shot alloc).
    // Heap-allocate it instead so we don't risk a stack overflow.
    uint32_t* indices = new uint32_t[MAX_SPRITES * INDICES_PER_SPRITE];
    for (int i = 0; i < MAX_SPRITES; i++) {
        int vertBase = i * 4;
        int idxBase = i * 6;

        // Two triangles per quad
        indices[idxBase + 0] = vertBase + 0;
        indices[idxBase + 1] = vertBase + 1;
        indices[idxBase + 2] = vertBase + 2;
        indices[idxBase + 3] = vertBase + 2;
        indices[idxBase + 4] = vertBase + 3;
        indices[idxBase + 5] = vertBase + 0;
    }

    gl.glGenBuffers(1, &m_ibo);
    gl.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ibo);
    gl.glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                    sizeof(uint32_t) * MAX_SPRITES * INDICES_PER_SPRITE,
                    indices, GL_STATIC_DRAW);
    delete[] indices;
    
    gl.glBindVertexArray(0);
    
    // Create 1x1 white texture
    unsigned char whitePixel[4] = { 255, 255, 255, 255 };
    TextureConfig whiteConfig;
    whiteConfig.width = 1;
    whiteConfig.height = 1;
    whiteConfig.channels = 4;
    m_whiteTexture.create(whiteConfig, whitePixel);
    
    TD_LOG_INFO("SpriteBatch initialized (max %d sprites)", MAX_SPRITES);
    return true;
}

void SpriteBatch::shutdown() {
    if (m_ibo) {
        gl.glDeleteBuffers(1, &m_ibo);
        m_ibo = 0;
    }
    if (m_vbo) {
        gl.glDeleteBuffers(1, &m_vbo);
        m_vbo = 0;
    }
    if (m_vao) {
        gl.glDeleteVertexArrays(1, &m_vao);
        m_vao = 0;
    }
    m_whiteTexture.destroy();
}

void SpriteBatch::begin(const Mat4& projection, const Mat4& view) {
    m_projection = projection;
    m_view = view;
    m_spriteCount = 0;
    m_drawCallCount = 0;
    m_currentTexture = nullptr;
    m_started = true;
}

void SpriteBatch::expandSpriteToVertices(const SpriteData& sprite, SpriteVertex* outVerts) {
    // Calculate sprite corners relative to origin
    float ox = sprite.width * sprite.originX;
    float oy = sprite.height * sprite.originY;
    
    // Corner positions before rotation
    float x0 = -ox;
    float y0 = -oy;
    float x1 = sprite.width - ox;
    float y1 = sprite.height - oy;
    
    // Apply rotation
    float cosR = cosF(sprite.rotation);
    float sinR = sinF(sprite.rotation);
    
    // Rotate corners and translate
    float rx0 = x0 * cosR - y0 * sinR + sprite.x + ox;
    float ry0 = x0 * sinR + y0 * cosR + sprite.y + oy;
    
    float rx1 = x1 * cosR - y0 * sinR + sprite.x + ox;
    float ry1 = x1 * sinR + y0 * cosR + sprite.y + oy;
    
    float rx2 = x1 * cosR - y1 * sinR + sprite.x + ox;
    float ry2 = x1 * sinR + y1 * cosR + sprite.y + oy;
    
    float rx3 = x0 * cosR - y1 * sinR + sprite.x + ox;
    float ry3 = x0 * sinR + y1 * cosR + sprite.y + oy;
    
    // Bottom-left
    outVerts[0].x = rx0;
    outVerts[0].y = ry0;
    outVerts[0].u = sprite.u0;
    outVerts[0].v = sprite.v1;
    outVerts[0].r = sprite.r;
    outVerts[0].g = sprite.g;
    outVerts[0].b = sprite.b;
    outVerts[0].a = sprite.a;
    
    // Bottom-right
    outVerts[1].x = rx1;
    outVerts[1].y = ry1;
    outVerts[1].u = sprite.u1;
    outVerts[1].v = sprite.v1;
    outVerts[1].r = sprite.r;
    outVerts[1].g = sprite.g;
    outVerts[1].b = sprite.b;
    outVerts[1].a = sprite.a;
    
    // Top-right
    outVerts[2].x = rx2;
    outVerts[2].y = ry2;
    outVerts[2].u = sprite.u1;
    outVerts[2].v = sprite.v0;
    outVerts[2].r = sprite.r;
    outVerts[2].g = sprite.g;
    outVerts[2].b = sprite.b;
    outVerts[2].a = sprite.a;
    
    // Top-left
    outVerts[3].x = rx3;
    outVerts[3].y = ry3;
    outVerts[3].u = sprite.u0;
    outVerts[3].v = sprite.v0;
    outVerts[3].r = sprite.r;
    outVerts[3].g = sprite.g;
    outVerts[3].b = sprite.b;
    outVerts[3].a = sprite.a;
}

void SpriteBatch::draw(const SpriteData& sprite, const Texture* texture) {
    if (!m_started) {
        TD_LOG_WARN("SpriteBatch::draw called without begin()");
        return;
    }
    
    // Use white texture if none provided
    if (!texture) {
        texture = &m_whiteTexture;
    }
    
    // Flush if texture changed or batch is full
    if (texture != m_currentTexture || m_spriteCount >= MAX_SPRITES) {
        flush();
        m_currentTexture = texture;
    }
    
    // Expand sprite to 4 vertices
    expandSpriteToVertices(sprite, &m_vertices[m_spriteCount * VERTICES_PER_SPRITE]);
    m_spriteCount++;
}

void SpriteBatch::drawQuad(float x, float y, float width, float height,
                           float r, float g, float b, float a,
                           const Texture* texture) {
    SpriteData sprite;
    sprite.x = x;
    sprite.y = y;
    sprite.width = width;
    sprite.height = height;
    sprite.r = r;
    sprite.g = g;
    sprite.b = b;
    sprite.a = a;
    sprite.originX = 0;
    sprite.originY = 0;
    draw(sprite, texture);
}

void SpriteBatch::flush() {
    if (m_spriteCount == 0) {
        return;
    }
    
    // Upload vertex data
    gl.glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    gl.glBufferSubData(GL_ARRAY_BUFFER, 0, 
                       sizeof(SpriteVertex) * m_spriteCount * VERTICES_PER_SPRITE,
                       m_vertices);
    
    // Bind shader and set uniforms
    m_shader.bind();
    m_shader.setUniformMat4("u_projection", m_projection.data());
    m_shader.setUniformMat4("u_view", m_view.data());
    m_shader.setUniform1i("u_texture", 0);
    
    // Bind texture
    if (m_currentTexture) {
        m_currentTexture->bind(0);
    }
    
    // Draw
    gl.glBindVertexArray(m_vao);
    gl.glDrawElements(GL_TRIANGLES, m_spriteCount * INDICES_PER_SPRITE, 
                      GL_UNSIGNED_INT, nullptr);
    
    m_drawCallCount++;
    m_spriteCount = 0;
}

void SpriteBatch::end() {
    if (!m_started) {
        return;
    }
    
    flush();
    m_started = false;
}

} // namespace td
