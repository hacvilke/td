#include "mesh.h"
#include "gl_renderer.h"
#include "../core/math/math.h"
#include <cstdlib>
#include <cstring>

namespace td {

Mesh::~Mesh() {
    destroy();
}

Mesh::Mesh(Mesh&& other) noexcept {
    m_vao = other.m_vao;
    m_vbo = other.m_vbo;
    m_ibo = other.m_ibo;
    m_vertexCount = other.m_vertexCount;
    m_indexCount = other.m_indexCount;
    
    other.m_vao = 0;
    other.m_vbo = 0;
    other.m_ibo = 0;
    other.m_vertexCount = 0;
    other.m_indexCount = 0;
}

Mesh& Mesh::operator=(Mesh&& other) noexcept {
    if (this != &other) {
        destroy();
        
        m_vao = other.m_vao;
        m_vbo = other.m_vbo;
        m_ibo = other.m_ibo;
        m_vertexCount = other.m_vertexCount;
        m_indexCount = other.m_indexCount;
        
        other.m_vao = 0;
        other.m_vbo = 0;
        other.m_ibo = 0;
        other.m_vertexCount = 0;
        other.m_indexCount = 0;
    }
    return *this;
}

bool Mesh::create(const Vertex3D* vertices, int vertexCount,
                  const uint32_t* indices, int indexCount) {
    destroy();
    
    m_vertexCount = vertexCount;
    m_indexCount = indexCount;
    
    // Create VAO
    gl.glGenVertexArrays(1, &m_vao);
    gl.glBindVertexArray(m_vao);
    
    // Create VBO
    gl.glGenBuffers(1, &m_vbo);
    gl.glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    gl.glBufferData(GL_ARRAY_BUFFER, vertexCount * sizeof(Vertex3D), 
                    vertices, GL_STATIC_DRAW);
    
    // Set up vertex attributes
    // Position (location 0): 3 floats at offset 0
    gl.glEnableVertexAttribArray(0);
    gl.glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex3D), 
                             (void*)offsetof(Vertex3D, position));
    
    // Normal (location 1): 3 floats at offset 12
    gl.glEnableVertexAttribArray(1);
    gl.glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex3D),
                             (void*)offsetof(Vertex3D, normal));
    
    // Texcoord (location 2): 2 floats at offset 24
    gl.glEnableVertexAttribArray(2);
    gl.glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex3D),
                             (void*)offsetof(Vertex3D, texcoord));
    
    // Create IBO
    if (indexCount > 0 && indices) {
        gl.glGenBuffers(1, &m_ibo);
        gl.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ibo);
        gl.glBufferData(GL_ELEMENT_ARRAY_BUFFER, indexCount * sizeof(uint32_t),
                        indices, GL_STATIC_DRAW);
    }
    
    gl.glBindVertexArray(0);
    
    return true;
}

void Mesh::destroy() {
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
    m_vertexCount = 0;
    m_indexCount = 0;
}

void Mesh::bind() const {
    gl.glBindVertexArray(m_vao);
}

void Mesh::unbind() const {
    gl.glBindVertexArray(0);
}

void Mesh::draw() const {
    gl.glBindVertexArray(m_vao);
    if (m_indexCount > 0) {
        gl.glDrawElements(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, nullptr);
    } else {
        gl.glDrawArrays(GL_TRIANGLES, 0, m_vertexCount);
    }
}

Mesh Mesh::createCube(float size) {
    float h = size * 0.5f;
    
    Vertex3D vertices[24] = {
        // Front face
        {{-h, -h,  h}, {0, 0, 1}, {0, 0}},
        {{ h, -h,  h}, {0, 0, 1}, {1, 0}},
        {{ h,  h,  h}, {0, 0, 1}, {1, 1}},
        {{-h,  h,  h}, {0, 0, 1}, {0, 1}},
        // Back face
        {{ h, -h, -h}, {0, 0, -1}, {0, 0}},
        {{-h, -h, -h}, {0, 0, -1}, {1, 0}},
        {{-h,  h, -h}, {0, 0, -1}, {1, 1}},
        {{ h,  h, -h}, {0, 0, -1}, {0, 1}},
        // Top face
        {{-h,  h,  h}, {0, 1, 0}, {0, 0}},
        {{ h,  h,  h}, {0, 1, 0}, {1, 0}},
        {{ h,  h, -h}, {0, 1, 0}, {1, 1}},
        {{-h,  h, -h}, {0, 1, 0}, {0, 1}},
        // Bottom face
        {{-h, -h, -h}, {0, -1, 0}, {0, 0}},
        {{ h, -h, -h}, {0, -1, 0}, {1, 0}},
        {{ h, -h,  h}, {0, -1, 0}, {1, 1}},
        {{-h, -h,  h}, {0, -1, 0}, {0, 1}},
        // Right face
        {{ h, -h,  h}, {1, 0, 0}, {0, 0}},
        {{ h, -h, -h}, {1, 0, 0}, {1, 0}},
        {{ h,  h, -h}, {1, 0, 0}, {1, 1}},
        {{ h,  h,  h}, {1, 0, 0}, {0, 1}},
        // Left face
        {{-h, -h, -h}, {-1, 0, 0}, {0, 0}},
        {{-h, -h,  h}, {-1, 0, 0}, {1, 0}},
        {{-h,  h,  h}, {-1, 0, 0}, {1, 1}},
        {{-h,  h, -h}, {-1, 0, 0}, {0, 1}},
    };
    
    uint32_t indices[36] = {
        0, 1, 2, 2, 3, 0,       // Front
        4, 5, 6, 6, 7, 4,       // Back
        8, 9, 10, 10, 11, 8,    // Top
        12, 13, 14, 14, 15, 12, // Bottom
        16, 17, 18, 18, 19, 16, // Right
        20, 21, 22, 22, 23, 20  // Left
    };
    
    Mesh mesh;
    mesh.create(vertices, 24, indices, 36);
    return mesh;
}

Mesh Mesh::createPlane(float width, float depth) {
    float hw = width * 0.5f;
    float hd = depth * 0.5f;
    
    Vertex3D vertices[4] = {
        {{-hw, 0, -hd}, {0, 1, 0}, {0, 0}},
        {{ hw, 0, -hd}, {0, 1, 0}, {1, 0}},
        {{ hw, 0,  hd}, {0, 1, 0}, {1, 1}},
        {{-hw, 0,  hd}, {0, 1, 0}, {0, 1}},
    };
    
    uint32_t indices[6] = { 0, 2, 1, 0, 3, 2 };
    
    Mesh mesh;
    mesh.create(vertices, 4, indices, 6);
    return mesh;
}

Mesh Mesh::createSphere(float radius, int segments, int rings) {
    int vertexCount = (rings + 1) * (segments + 1);
    int indexCount = rings * segments * 6;
    
    Vertex3D* vertices = (Vertex3D*)malloc(vertexCount * sizeof(Vertex3D));
    uint32_t* indices = (uint32_t*)malloc(indexCount * sizeof(uint32_t));
    
    int vi = 0;
    for (int r = 0; r <= rings; r++) {
        float phi = TD_PI * (float)r / (float)rings;
        float y = cosF(phi);
        float sinPhi = sinF(phi);
        
        for (int s = 0; s <= segments; s++) {
            float theta = TD_TAU * (float)s / (float)segments;
            float x = sinPhi * cosF(theta);
            float z = sinPhi * sinF(theta);
            
            vertices[vi].position = Vec3(x * radius, y * radius, z * radius);
            vertices[vi].normal = Vec3(x, y, z);
            vertices[vi].texcoord = Vec2((float)s / segments, (float)r / rings);
            vi++;
        }
    }
    
    int ii = 0;
    for (int r = 0; r < rings; r++) {
        for (int s = 0; s < segments; s++) {
            int curr = r * (segments + 1) + s;
            int next = curr + segments + 1;
            
            indices[ii++] = curr;
            indices[ii++] = next;
            indices[ii++] = curr + 1;
            
            indices[ii++] = curr + 1;
            indices[ii++] = next;
            indices[ii++] = next + 1;
        }
    }
    
    Mesh mesh;
    mesh.create(vertices, vertexCount, indices, indexCount);
    
    free(vertices);
    free(indices);
    
    return mesh;
}

Mesh Mesh::createCylinder(float radius, float height, int segments) {
    int vertexCount = (segments + 1) * 4 + 2; // Sides + top/bottom centers
    int indexCount = segments * 12; // Sides + caps
    
    Vertex3D* vertices = (Vertex3D*)malloc(vertexCount * sizeof(Vertex3D));
    uint32_t* indices = (uint32_t*)malloc(indexCount * sizeof(uint32_t));
    
    float halfH = height * 0.5f;
    int vi = 0;
    
    // Side vertices (bottom and top rings, duplicated for normals)
    for (int s = 0; s <= segments; s++) {
        float theta = TD_TAU * (float)s / (float)segments;
        float x = cosF(theta);
        float z = sinF(theta);
        float u = (float)s / segments;
        
        // Bottom ring (side normal)
        vertices[vi++] = {{x * radius, -halfH, z * radius}, {x, 0, z}, {u, 0}};
        // Top ring (side normal)
        vertices[vi++] = {{x * radius, halfH, z * radius}, {x, 0, z}, {u, 1}};
    }
    
    // Cap vertices
    int bottomCenterIdx = vi;
    vertices[vi++] = {{0, -halfH, 0}, {0, -1, 0}, {0.5f, 0.5f}};
    int topCenterIdx = vi;
    vertices[vi++] = {{0, halfH, 0}, {0, 1, 0}, {0.5f, 0.5f}};
    
    int bottomRingStart = vi;
    int topRingStart = vi + segments + 1;
    
    // Bottom and top cap ring vertices (with cap normals)
    for (int s = 0; s <= segments; s++) {
        float theta = TD_TAU * (float)s / (float)segments;
        float x = cosF(theta);
        float z = sinF(theta);
        
        vertices[vi++] = {{x * radius, -halfH, z * radius}, {0, -1, 0}, 
                          {x * 0.5f + 0.5f, z * 0.5f + 0.5f}};
    }
    
    for (int s = 0; s <= segments; s++) {
        float theta = TD_TAU * (float)s / (float)segments;
        float x = cosF(theta);
        float z = sinF(theta);
        
        vertices[vi++] = {{x * radius, halfH, z * radius}, {0, 1, 0},
                          {x * 0.5f + 0.5f, z * 0.5f + 0.5f}};
    }
    
    // Indices
    int ii = 0;
    
    // Side faces
    for (int s = 0; s < segments; s++) {
        int b0 = s * 2;
        int b1 = b0 + 2;
        int t0 = b0 + 1;
        int t1 = b1 + 1;
        
        indices[ii++] = b0;
        indices[ii++] = b1;
        indices[ii++] = t0;
        
        indices[ii++] = t0;
        indices[ii++] = b1;
        indices[ii++] = t1;
    }
    
    // Bottom cap
    for (int s = 0; s < segments; s++) {
        indices[ii++] = bottomCenterIdx;
        indices[ii++] = bottomRingStart + s + 1;
        indices[ii++] = bottomRingStart + s;
    }
    
    // Top cap
    for (int s = 0; s < segments; s++) {
        indices[ii++] = topCenterIdx;
        indices[ii++] = topRingStart + s;
        indices[ii++] = topRingStart + s + 1;
    }
    
    Mesh mesh;
    mesh.create(vertices, vi, indices, ii);
    
    free(vertices);
    free(indices);
    
    return mesh;
}

Mesh Mesh::createCone(float radius, float height, int segments) {
    int vertexCount = segments * 3 + segments + 2;
    int indexCount = segments * 6;
    
    Vertex3D* vertices = (Vertex3D*)malloc(vertexCount * sizeof(Vertex3D));
    uint32_t* indices = (uint32_t*)malloc(indexCount * sizeof(uint32_t));
    
    int vi = 0;
    
    // Apex
    Vec3 apex(0, height, 0);
    
    // Slope angle for normals
    float slope = radius / height;
    float normalY = 1.0f / sqrtF(1.0f + slope * slope);
    float normalScale = slope * normalY;
    
    // Side vertices (separate triangles for flat shading)
    for (int s = 0; s < segments; s++) {
        float theta0 = TD_TAU * (float)s / (float)segments;
        float theta1 = TD_TAU * (float)(s + 1) / (float)segments;
        
        float x0 = cosF(theta0);
        float z0 = sinF(theta0);
        float x1 = cosF(theta1);
        float z1 = sinF(theta1);
        
        // Face normal
        Vec3 v0(x0 * radius, 0, z0 * radius);
        Vec3 v1(x1 * radius, 0, z1 * radius);
        Vec3 edge1 = apex - v0;
        Vec3 edge2 = v1 - v0;
        Vec3 normal = edge1.cross(edge2).normalized();
        
        vertices[vi++] = {apex, normal, {0.5f, 0}};
        vertices[vi++] = {v0, normal, {(float)s / segments, 1}};
        vertices[vi++] = {v1, normal, {(float)(s + 1) / segments, 1}};
    }
    
    // Base center
    int baseCenter = vi;
    vertices[vi++] = {{0, 0, 0}, {0, -1, 0}, {0.5f, 0.5f}};
    
    // Base ring
    int baseRing = vi;
    for (int s = 0; s <= segments; s++) {
        float theta = TD_TAU * (float)s / (float)segments;
        float x = cosF(theta);
        float z = sinF(theta);
        
        vertices[vi++] = {{x * radius, 0, z * radius}, {0, -1, 0},
                          {x * 0.5f + 0.5f, z * 0.5f + 0.5f}};
    }
    
    // Indices
    int ii = 0;
    
    // Side triangles
    for (int s = 0; s < segments; s++) {
        int base = s * 3;
        indices[ii++] = base;
        indices[ii++] = base + 1;
        indices[ii++] = base + 2;
    }
    
    // Base triangles
    for (int s = 0; s < segments; s++) {
        indices[ii++] = baseCenter;
        indices[ii++] = baseRing + s + 1;
        indices[ii++] = baseRing + s;
    }
    
    Mesh mesh;
    mesh.create(vertices, vi, indices, ii);
    
    free(vertices);
    free(indices);
    
    return mesh;
}

Mesh Mesh::createQuad(float width, float height) {
    float hw = width * 0.5f;
    float hh = height * 0.5f;
    
    Vertex3D vertices[4] = {
        {{-hw, -hh, 0}, {0, 0, 1}, {0, 0}},
        {{ hw, -hh, 0}, {0, 0, 1}, {1, 0}},
        {{ hw,  hh, 0}, {0, 0, 1}, {1, 1}},
        {{-hw,  hh, 0}, {0, 0, 1}, {0, 1}},
    };
    
    uint32_t indices[6] = { 0, 1, 2, 2, 3, 0 };
    
    Mesh mesh;
    mesh.create(vertices, 4, indices, 6);
    return mesh;
}

} // namespace td
