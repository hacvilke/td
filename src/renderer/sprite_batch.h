#pragma once
#include "texture.h"
#include "gl_shader.h"
#include "../core/math/vec2.h"
#include "../core/math/mat4.h"
#include <cstdint>
#include <algorithm>   // std::sort for flushSorted()

namespace td {

struct SpriteData {
    float x, y;                    // Position
    float width, height;           // Size
    float u0, v0, u1, v1;          // Texture UV rectangle
    float r, g, b, a;              // Color tint
    float rotation;                // Rotation in radians
    float originX, originY;        // Origin (pivot) normalized (0-1)
    
    SpriteData() : x(0), y(0), width(32), height(32),
                   u0(0), v0(0), u1(1), v1(1),
                   r(1), g(1), b(1), a(1),
                   rotation(0), originX(0.5f), originY(0.5f) {}
};

struct SpriteVertex {
    float x, y;       // Position
    float u, v;       // Texcoord
    float r, g, b, a; // Color
};

class SpriteBatch {
public:
    static const int MAX_SPRITES = 10000;
    static const int VERTICES_PER_SPRITE = 4;
    static const int INDICES_PER_SPRITE = 6;

    bool init();
    void shutdown();

    void begin(const Mat4& projection, const Mat4& view = Mat4::identity());
    void draw(const SpriteData& sprite, const Texture* texture);
    void drawQuad(float x, float y, float width, float height,
                  float r = 1, float g = 1, float b = 1, float a = 1,
                  const Texture* texture = nullptr);

    // Bulk draw: submit `count` sprites that all share the same texture in
    // one call. Avoids per-sprite function-call overhead and lets the CPU
    // expand vertices into a contiguous buffer with no branches. Designed
    // for particle systems + tilemaps where you have hundreds/thousands of
    // sprites with the same texture per frame.
    //
    // Safety: if count would overflow the buffer, this flushes first.
    void drawBatch(const SpriteData* sprites, int count, const Texture* texture);

    void end();

    // Flush the current batch as one draw call (default behavior).
    void flush();

    // Sort all buffered sprites by texture, then flush — produces one draw
    // call per unique texture instead of one per texture-change. Use this
    // in end() if you queued sprites with mixed textures in arbitrary order.
    //
    // Trade-off: the sort is O(n log n) on the CPU; for scenes where you
    // already draw textures in sorted order, plain flush() is faster.
    void flushSorted();

    int getSpriteCount() const { return m_spriteCount; }
    int getDrawCallCount() const { return m_drawCallCount; }

    // Sort key for a sprite — currently just the texture pointer. Exposed
    // so external code can pre-sort before calling draw() if it wants to
    // avoid the runtime sort in flushSorted().
    static uint64_t sortKey(const Texture* t) {
        return reinterpret_cast<uint64_t>(t);
    }

private:
    void expandSpriteToVertices(const SpriteData& sprite, SpriteVertex* outVerts);

    uint32_t m_vao = 0;
    uint32_t m_vbo = 0;
    uint32_t m_ibo = 0;

    SpriteVertex m_vertices[MAX_SPRITES * VERTICES_PER_SPRITE];
    int m_spriteCount = 0;
    int m_drawCallCount = 0;

    // Per-sprite texture pointers — populated by draw() and drawBatch() so
    // flushSorted() can re-order sprites by texture to minimize draw calls.
    // Lazily populated: only written if flushSorted() is ever called, to
    // avoid the 80KB cost when not needed. Tracked via m_trackTextures.
    const Texture* m_spriteTextures[MAX_SPRITES] = {};
    bool m_trackTextures = false;

    const Texture* m_currentTexture = nullptr;
    Shader m_shader;
    Mat4 m_projection;
    Mat4 m_view;
    bool m_started = false;

    // Default white texture for untextured quads
    Texture m_whiteTexture;
};

} // namespace td
