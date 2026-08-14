// Minimal Mesh stub for the voxel test on Linux.
//
// The real mesh.cpp calls into the GLFunctions struct (defined in
// gl_renderer.cpp, which on non-Emscripten builds pulls in <windows.h>).
// That's not available on Linux CI. The voxel tests don't actually upload
// anything to GL — they read VoxelMesh.vertices / .indices directly — so we
// link against this stub instead.
//
// The stub provides the same Mesh symbols so voxel.cpp's VoxelMesh::toMesh()
// (which calls Mesh::create) links cleanly. The stub's create() returns
// false; callers that try to actually render would get a no-op Mesh.

#include "renderer/mesh.h"

namespace td {

Mesh::~Mesh() {
    m_vao = m_vbo = m_ibo = 0;
    m_vertexCount = m_indexCount = 0;
}

Mesh::Mesh(Mesh&& other) noexcept {
    m_vao = other.m_vao; m_vbo = other.m_vbo; m_ibo = other.m_ibo;
    m_vertexCount = other.m_vertexCount; m_indexCount = other.m_indexCount;
    other.m_vao = other.m_vbo = other.m_ibo = 0;
    other.m_vertexCount = other.m_indexCount = 0;
}

Mesh& Mesh::operator=(Mesh&& other) noexcept {
    if (this != &other) {
        m_vao = other.m_vao; m_vbo = other.m_vbo; m_ibo = other.m_ibo;
        m_vertexCount = other.m_vertexCount; m_indexCount = other.m_indexCount;
        other.m_vao = other.m_vbo = other.m_ibo = 0;
        other.m_vertexCount = other.m_indexCount = 0;
    }
    return *this;
}

bool Mesh::create(const Vertex3D* /*vertices*/, int /*vertexCount*/,
                  const uint32_t* /*indices*/, int /*indexCount*/) {
    // Stub: no GL. Real impl in src/renderer/mesh.cpp.
    return false;
}

void Mesh::destroy() {
    m_vao = m_vbo = m_ibo = 0;
    m_vertexCount = m_indexCount = 0;
}

void Mesh::bind() const   {}
void Mesh::unbind() const {}
void Mesh::draw() const   {}

} // namespace td
