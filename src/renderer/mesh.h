#pragma once
#include "../core/math/vec3.h"
#include "../core/math/vec2.h"
#include <cstdint>

namespace td {

struct Vertex3D {
    Vec3 position;
    Vec3 normal;
    Vec2 texcoord;
    
    Vertex3D() = default;
    Vertex3D(const Vec3& pos, const Vec3& norm, const Vec2& tex)
        : position(pos), normal(norm), texcoord(tex) {}
};

class Mesh {
public:
    Mesh() = default;
    ~Mesh();
    
    // Move semantics
    Mesh(Mesh&& other) noexcept;
    Mesh& operator=(Mesh&& other) noexcept;
    
    // No copying
    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;
    
    bool create(const Vertex3D* vertices, int vertexCount,
                const uint32_t* indices, int indexCount);
    void destroy();
    
    void bind() const;
    void unbind() const;
    void draw() const;
    
    int getVertexCount() const { return m_vertexCount; }
    int getIndexCount() const { return m_indexCount; }
    bool isValid() const { return m_vao != 0; }
    
    // Utility: create primitive meshes
    static Mesh createCube(float size = 1.0f);
    static Mesh createPlane(float width = 1.0f, float depth = 1.0f);
    static Mesh createSphere(float radius = 1.0f, int segments = 32, int rings = 16);
    static Mesh createCylinder(float radius = 0.5f, float height = 1.0f, int segments = 32);
    static Mesh createCone(float radius = 0.5f, float height = 1.0f, int segments = 32);
    static Mesh createQuad(float width = 1.0f, float height = 1.0f);
    
private:
    uint32_t m_vao = 0;
    uint32_t m_vbo = 0;
    uint32_t m_ibo = 0;
    int m_vertexCount = 0;
    int m_indexCount = 0;
};

} // namespace td
