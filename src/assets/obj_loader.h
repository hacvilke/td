#pragma once
#include "../renderer/mesh.h"
#include <cstdint>

namespace td {

class OBJLoader {
public:
    bool load(const char* path, Mesh& outMesh);
    bool loadRaw(const char* path, Vertex3D** outVertices, int* vertexCount,
                 uint32_t** outIndices, int* indexCount);
    
    const char* getError() const { return m_error; }
    
private:
    bool parse(const char* data, int dataSize);
    void processVertex(const char* line);
    void processNormal(const char* line);
    void processTexcoord(const char* line);
    void processFace(const char* line);
    int parseFaceVertex(const char* str, int* outV, int* outVt, int* outVn);
    void computeFlatNormals();
    
    static const int MAX_ELEMENTS = 100000;
    
    float m_positions[MAX_ELEMENTS * 3];
    float m_normals[MAX_ELEMENTS * 3];
    float m_texcoords[MAX_ELEMENTS * 2];
    
    int m_posCount = 0;
    int m_normCount = 0;
    int m_texcoordCount = 0;
    
    // Final vertex/index data
    Vertex3D* m_vertices = nullptr;
    uint32_t* m_indices = nullptr;
    int m_vertexCount = 0;
    int m_indexCount = 0;
    
    char m_error[256] = {};
};

} // namespace td
