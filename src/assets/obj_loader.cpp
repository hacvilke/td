#include "obj_loader.h"
#include "../core/logger.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace td {

void OBJLoader::processVertex(const char* line) {
    if (m_posCount >= MAX_ELEMENTS) return;
    
    float x, y, z;
    if (sscanf(line, "v %f %f %f", &x, &y, &z) == 3) {
        m_positions[m_posCount * 3] = x;
        m_positions[m_posCount * 3 + 1] = y;
        m_positions[m_posCount * 3 + 2] = z;
        m_posCount++;
    }
}

void OBJLoader::processNormal(const char* line) {
    if (m_normCount >= MAX_ELEMENTS) return;
    
    float x, y, z;
    if (sscanf(line, "vn %f %f %f", &x, &y, &z) == 3) {
        m_normals[m_normCount * 3] = x;
        m_normals[m_normCount * 3 + 1] = y;
        m_normals[m_normCount * 3 + 2] = z;
        m_normCount++;
    }
}

void OBJLoader::processTexcoord(const char* line) {
    if (m_texcoordCount >= MAX_ELEMENTS) return;
    
    float u, v;
    if (sscanf(line, "vt %f %f", &u, &v) >= 2) {
        m_texcoords[m_texcoordCount * 2] = u;
        m_texcoords[m_texcoordCount * 2 + 1] = v;
        m_texcoordCount++;
    }
}

int OBJLoader::parseFaceVertex(const char* str, int* outV, int* outVt, int* outVn) {
    *outV = 0;
    *outVt = 0;
    *outVn = 0;
    
    // Parse formats: v, v/vt, v/vt/vn, v//vn
    int count = 0;
    const char* p = str;
    
    // Read v
    *outV = atoi(p);
    while (*p && *p != '/' && *p != ' ') p++;
    count++;
    
    if (*p == '/') {
        p++;
        if (*p != '/') {
            // vt
            *outVt = atoi(p);
            while (*p && *p != '/' && *p != ' ') p++;
        }
        if (*p == '/') {
            p++;
            // vn
            *outVn = atoi(p);
        }
    }
    
    return count;
}

void OBJLoader::processFace(const char* line) {
    int v[4], vt[4], vn[4];
    int numVerts = 0;
    
    const char* p = line + 2; // Skip "f "
    
    while (*p && numVerts < 4) {
        while (*p == ' ') p++;
        if (!*p) break;
        
        parseFaceVertex(p, &v[numVerts], &vt[numVerts], &vn[numVerts]);
        numVerts++;
        
        while (*p && *p != ' ') p++;
    }
    
    if (numVerts < 3) return;
    
    // Create vertices for this face
    Vertex3D faceVerts[4];
    
    for (int i = 0; i < numVerts; i++) {
        int vi = v[i] - 1;
        int ti = vt[i] - 1;
        int ni = vn[i] - 1;
        
        // Handle negative indices
        if (v[i] < 0) vi = m_posCount + v[i];
        if (vt[i] < 0) ti = m_texcoordCount + vt[i];
        if (vn[i] < 0) ni = m_normCount + vn[i];
        
        if (vi >= 0 && vi < m_posCount) {
            faceVerts[i].position.x = m_positions[vi * 3];
            faceVerts[i].position.y = m_positions[vi * 3 + 1];
            faceVerts[i].position.z = m_positions[vi * 3 + 2];
        }
        
        if (ti >= 0 && ti < m_texcoordCount) {
            faceVerts[i].texcoord.x = m_texcoords[ti * 2];
            faceVerts[i].texcoord.y = m_texcoords[ti * 2 + 1];
        }
        
        if (ni >= 0 && ni < m_normCount) {
            faceVerts[i].normal.x = m_normals[ni * 3];
            faceVerts[i].normal.y = m_normals[ni * 3 + 1];
            faceVerts[i].normal.z = m_normals[ni * 3 + 2];
        }
    }
    
    // Triangulate (first triangle)
    if (m_vertexCount + 3 <= MAX_ELEMENTS * 3) {
        m_vertices[m_vertexCount++] = faceVerts[0];
        m_vertices[m_vertexCount++] = faceVerts[1];
        m_vertices[m_vertexCount++] = faceVerts[2];
        
        m_indices[m_indexCount++] = m_vertexCount - 3;
        m_indices[m_indexCount++] = m_vertexCount - 2;
        m_indices[m_indexCount++] = m_vertexCount - 1;
    }
    
    // Second triangle for quads
    if (numVerts == 4 && m_vertexCount + 3 <= MAX_ELEMENTS * 3) {
        m_vertices[m_vertexCount++] = faceVerts[0];
        m_vertices[m_vertexCount++] = faceVerts[2];
        m_vertices[m_vertexCount++] = faceVerts[3];
        
        m_indices[m_indexCount++] = m_vertexCount - 3;
        m_indices[m_indexCount++] = m_vertexCount - 2;
        m_indices[m_indexCount++] = m_vertexCount - 1;
    }
}

void OBJLoader::computeFlatNormals() {
    // Compute normals for faces that didn't have them
    for (int i = 0; i < m_indexCount; i += 3) {
        Vertex3D& v0 = m_vertices[m_indices[i]];
        Vertex3D& v1 = m_vertices[m_indices[i + 1]];
        Vertex3D& v2 = m_vertices[m_indices[i + 2]];
        
        // Check if normals are zero
        if (v0.normal.lengthSq() < 0.001f) {
            Vec3 edge1 = v1.position - v0.position;
            Vec3 edge2 = v2.position - v0.position;
            Vec3 normal = edge1.cross(edge2).normalized();
            
            v0.normal = normal;
            v1.normal = normal;
            v2.normal = normal;
        }
    }
}

bool OBJLoader::parse(const char* data, int dataSize) {
    m_posCount = 0;
    m_normCount = 0;
    m_texcoordCount = 0;
    m_vertexCount = 0;
    m_indexCount = 0;
    
    // Allocate temporary buffers
    m_vertices = (Vertex3D*)malloc(MAX_ELEMENTS * 3 * sizeof(Vertex3D));
    m_indices = (uint32_t*)malloc(MAX_ELEMENTS * 6 * sizeof(uint32_t));
    
    if (!m_vertices || !m_indices) {
        snprintf(m_error, sizeof(m_error), "Failed to allocate parsing buffers");
        return false;
    }
    
    // Parse line by line
    char line[1024];
    int linePos = 0;
    
    for (int i = 0; i <= dataSize; i++) {
        char c = (i < dataSize) ? data[i] : '\n';
        
        if (c == '\n' || c == '\r') {
            line[linePos] = '\0';
            
            if (linePos > 0) {
                if (line[0] == 'v' && line[1] == ' ') {
                    processVertex(line);
                }
                else if (line[0] == 'v' && line[1] == 'n' && line[2] == ' ') {
                    processNormal(line);
                }
                else if (line[0] == 'v' && line[1] == 't' && line[2] == ' ') {
                    processTexcoord(line);
                }
                else if (line[0] == 'f' && line[1] == ' ') {
                    processFace(line);
                }
            }
            
            linePos = 0;
        }
        else if (linePos < 1023) {
            line[linePos++] = c;
        }
    }
    
    // Compute normals if needed
    if (m_normCount == 0) {
        computeFlatNormals();
    }
    
    return true;
}

bool OBJLoader::load(const char* path, Mesh& outMesh) {
    FILE* file = fopen(path, "rb");
    if (!file) {
        snprintf(m_error, sizeof(m_error), "Failed to open file: %s", path);
        return false;
    }
    
    fseek(file, 0, SEEK_END);
    long fileSize = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    char* data = (char*)malloc(fileSize + 1);
    if (!data) {
        fclose(file);
        snprintf(m_error, sizeof(m_error), "Failed to allocate file buffer");
        return false;
    }
    
    fread(data, 1, fileSize, file);
    data[fileSize] = '\0';
    fclose(file);
    
    if (!parse(data, (int)fileSize)) {
        free(data);
        if (m_vertices) free(m_vertices);
        if (m_indices) free(m_indices);
        return false;
    }
    
    free(data);
    
    if (m_vertexCount == 0) {
        snprintf(m_error, sizeof(m_error), "No vertices found in file");
        free(m_vertices);
        free(m_indices);
        return false;
    }
    
    // Create mesh
    bool result = outMesh.create(m_vertices, m_vertexCount, m_indices, m_indexCount);
    
    free(m_vertices);
    free(m_indices);
    m_vertices = nullptr;
    m_indices = nullptr;
    
    if (result) {
        TD_LOG_INFO("Loaded OBJ: %s (%d vertices, %d indices)", 
                    path, m_vertexCount, m_indexCount);
    }
    
    return result;
}

bool OBJLoader::loadRaw(const char* path, Vertex3D** outVertices, int* vertexCount,
                        uint32_t** outIndices, int* indexCount) {
    FILE* file = fopen(path, "rb");
    if (!file) {
        snprintf(m_error, sizeof(m_error), "Failed to open file: %s", path);
        return false;
    }
    
    fseek(file, 0, SEEK_END);
    long fileSize = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    char* data = (char*)malloc(fileSize + 1);
    if (!data) {
        fclose(file);
        snprintf(m_error, sizeof(m_error), "Failed to allocate file buffer");
        return false;
    }
    
    fread(data, 1, fileSize, file);
    data[fileSize] = '\0';
    fclose(file);
    
    if (!parse(data, (int)fileSize)) {
        free(data);
        if (m_vertices) free(m_vertices);
        if (m_indices) free(m_indices);
        return false;
    }
    
    free(data);
    
    *outVertices = m_vertices;
    *outIndices = m_indices;
    *vertexCount = m_vertexCount;
    *indexCount = m_indexCount;
    
    m_vertices = nullptr;
    m_indices = nullptr;
    
    return true;
}

} // namespace td
