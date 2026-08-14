// =============================================================================
// TD Engine - Voxel System Implementation (Tier 2.3, wave1-voxel)
//
// Implements the real meshers + worldgen + raycast + streaming declared in
// voxel.h. The skeleton chunk.h stays byte-identical; everything new lives
// here. See voxel.h for the high-level architecture comment.
// =============================================================================
#include "voxel.h"
#include <cmath>
#include <cstring>
#include <algorithm>

namespace td {

// ============================================================================
// Face geometry tables.
//
// For each of 6 face directions, define:
//   - outward normal
//   - 4 corner positions (CCW when viewed from outside the cube), as offsets
//     from the voxel's local (0,0,0) corner. Each component is 0 or 1.
//   - 4 UVs (one per corner) — standard quad UVs.
//
// Corner ordering matches the standard "0,1,2, 0,2,3" triangulation. Winding
// is right-handed (CCW front face), so a face is "front-facing" when its
// normal points toward the viewer.
// ============================================================================

static const Vec3 FACE_NORMALS[6] = {
    { 1.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f},
    { 0.0f, 1.0f, 0.0f}, { 0.0f,-1.0f, 0.0f},
    { 0.0f, 0.0f, 1.0f}, { 0.0f, 0.0f,-1.0f}
};

// 4 corners per face (CCW from outside), as (dx, dy, dz) in {0,1}^3.
// Verified by cross-product check: cross(c1-c0, c2-c0) == normal.
static const Vec3 FACE_CORNERS[6][4] = {
    // +X (right) — plane x=1
    {{1,0,0}, {1,1,0}, {1,1,1}, {1,0,1}},
    // -X (left) — plane x=0
    {{0,0,1}, {0,1,1}, {0,1,0}, {0,0,0}},
    // +Y (top) — plane y=1
    {{0,1,0}, {0,1,1}, {1,1,1}, {1,1,0}},
    // -Y (bottom) — plane y=0
    {{0,0,0}, {1,0,0}, {1,0,1}, {0,0,1}},
    // +Z (front) — plane z=1
    {{0,0,1}, {1,0,1}, {1,1,1}, {0,1,1}},
    // -Z (back) — plane z=0
    {{1,0,0}, {0,0,0}, {0,1,0}, {1,1,0}}
};

static const Vec2 FACE_UVS[4] = {
    {0.0f, 0.0f},
    {0.0f, 1.0f},
    {1.0f, 1.0f},
    {1.0f, 0.0f}
};

// ============================================================================
// Block sampling helpers — work for both chunk-local (no world) and
// cross-chunk (with world) queries.
// ============================================================================

static inline BlockId sampleBlock(const Chunk& c, int lx, int ly, int lz,
                                  const VoxelWorld* world) {
    if (lx >= 0 && lx < VOXEL_CHUNK_SIZE &&
        ly >= 0 && ly < VOXEL_CHUNK_SIZE &&
        lz >= 0 && lz < VOXEL_CHUNK_SIZE) {
        return c.getBlock(lx, ly, lz);
    }
    // Out of chunk: query world if available, else treat as air.
    if (world) {
        int wx = c.cx * VOXEL_CHUNK_SIZE + lx;
        int wy = c.cy * VOXEL_CHUNK_SIZE + ly;
        int wz = c.cz * VOXEL_CHUNK_SIZE + lz;
        return world->getVoxel(wx, wy, wz);
    }
    return BLOCK_AIR;
}

static inline int sampleSolid(const Chunk& c, int lx, int ly, int lz,
                              const VoxelWorld* world) {
    return isSolidBlock(sampleBlock(c, lx, ly, lz, world)) ? 1 : 0;
}

// ============================================================================
// AO sampling for a single face of a single voxel (used by the naive + culled
// meshers). For each corner, sample the 3 voxels on the OUTSIDE of the face
// (in the face normal direction) that share an edge or diagonal with the
// corner.
//
// Returns the 4 AO levels (one per corner, in FACE_CORNERS order).
// ============================================================================

static void computeFaceAO(const Chunk& c, int lx, int ly, int lz,
                          int faceIdx, const VoxelWorld* world,
                          int aoOut[4]) {
    Vec3 n = FACE_NORMALS[faceIdx];
    // The two axes perpendicular to the face normal.
    int axis1, axis2;
    if (n.x != 0)      { axis1 = 1; axis2 = 2; }  // X face: tangential Y, Z
    else if (n.y != 0) { axis1 = 0; axis2 = 2; }  // Y face: tangential X, Z
    else               { axis1 = 0; axis2 = 1; }  // Z face: tangential X, Y

    // The neighbor voxel position (on the outside of the face).
    int nbX = lx + (int)n.x;
    int nbY = ly + (int)n.y;
    int nbZ = lz + (int)n.z;

    for (int corner = 0; corner < 4; corner++) {
        Vec3 cp = FACE_CORNERS[faceIdx][corner];
        // Tangential offset for axis1: +1 if corner is at the high end (cp[axis1]==1), else -1.
        int t1Dir = (cp[axis1] == 1.0f) ? 1 : -1;
        int t2Dir = (cp[axis2] == 1.0f) ? 1 : -1;

        int side1X = nbX, side1Y = nbY, side1Z = nbZ;
        int side2X = nbX, side2Y = nbY, side2Z = nbZ;
        int corX   = nbX, corY   = nbY, corZ   = nbZ;

        // Apply t1 offset to side1, t2 to side2, both to corner.
        switch (axis1) {
            case 0: side1X += t1Dir; corX += t1Dir; break;
            case 1: side1Y += t1Dir; corY += t1Dir; break;
            case 2: side1Z += t1Dir; corZ += t1Dir; break;
        }
        switch (axis2) {
            case 0: side2X += t2Dir; corX += t2Dir; break;
            case 1: side2Y += t2Dir; corY += t2Dir; break;
            case 2: side2Z += t2Dir; corZ += t2Dir; break;
        }

        int s1 = sampleSolid(c, side1X, side1Y, side1Z, world);
        int s2 = sampleSolid(c, side2X, side2Y, side2Z, world);
        int sc = sampleSolid(c, corX,   corY,   corZ,   world);

        aoOut[corner] = vertexAOLevel(s1, s2, sc);
    }
}

// ============================================================================
// AO sampling for a greedy-merged face (rectangle of size w×h starting at
// (uu, vv) on the slice). The corner index is 0..3 (bottom-left, bottom-right,
// top-right, top-left in (u,v) face coords).
// ============================================================================

static int computeGreedyCornerAO(const Chunk& c,
                                 int axis, int sign,
                                 int slice, int uu, int vv, int w, int h,
                                 int cornerIdx,
                                 const VoxelWorld* world) {
    // neighbor_axis_pos: the axis coord of the voxel on the outside of the face.
    int nbAxis = slice + sign;  // sign is +1 or -1

    // The 2 tangential axes.
    int uAxis = (axis + 1) % 3;
    int vAxis = (axis + 2) % 3;

    // Corner (cu, cv) in face coords.
    int cu = (cornerIdx == 0 || cornerIdx == 3) ? uu : (uu + w);
    int cv = (cornerIdx == 0 || cornerIdx == 1) ? vv : (vv + h);

    // Face cell at this corner.
    int fcu = (cu == uu) ? uu : (uu + w - 1);
    int fcv = (cv == vv) ? vv : (vv + h - 1);

    int uDir = (cu == uu) ? -1 : +1;
    int vDir = (cv == vv) ? -1 : +1;

    // Build 3D position for side1, side2, corner.
    int p1[3], p2[3], pc[3];
    p1[axis] = nbAxis; p1[uAxis] = fcu + uDir; p1[vAxis] = fcv;
    p2[axis] = nbAxis; p2[uAxis] = fcu;        p2[vAxis] = fcv + vDir;
    pc[axis] = nbAxis; pc[uAxis] = fcu + uDir; pc[vAxis] = fcv + vDir;

    int s1 = sampleSolid(c, p1[0], p1[1], p1[2], world);
    int s2 = sampleSolid(c, p2[0], p2[1], p2[2], world);
    int sc = sampleSolid(c, pc[0], pc[1], pc[2], world);

    return vertexAOLevel(s1, s2, sc);
}

// ============================================================================
// Naive mesher — one quad per exposed face, with AO.
// ============================================================================

static void emitNaiveQuad(VoxelMesh& mesh, const Chunk& c,
                          int lx, int ly, int lz,
                          int faceIdx, const VoxelWorld* world) {
    int baseIdx = mesh.vertexCount();
    Vec3 normal = FACE_NORMALS[faceIdx];

    int ao[4];
    computeFaceAO(c, lx, ly, lz, faceIdx, world, ao);

    for (int corner = 0; corner < 4; corner++) {
        Vec3 cp = FACE_CORNERS[faceIdx][corner];
        VoxelVertex v;
        v.position = Vec3((float)(lx + (int)cp.x),
                          (float)(ly + (int)cp.y),
                          (float)(lz + (int)cp.z));
        v.normal   = normal;
        v.texcoord = FACE_UVS[corner];
        v.ao       = aoLevelToBrightness(ao[corner]);
        mesh.vertices.push_back(v);
    }

    // Anti-anisotropy: if a0+a2 > a1+a3, the standard triangulation produces
    // a "smooth" gradient; otherwise flip to avoid the asymmetric shadow.
    // This is the standard Minecraft trick (see 0fps.net AO article).
    float a0 = mesh.vertices[baseIdx + 0].ao;
    float a1 = mesh.vertices[baseIdx + 1].ao;
    float a2 = mesh.vertices[baseIdx + 2].ao;
    float a3 = mesh.vertices[baseIdx + 3].ao;

    if (a0 + a2 < a1 + a3) {
        // Flipped triangulation: 0,1,3, 1,2,3
        mesh.indices.push_back(baseIdx + 0);
        mesh.indices.push_back(baseIdx + 1);
        mesh.indices.push_back(baseIdx + 3);
        mesh.indices.push_back(baseIdx + 1);
        mesh.indices.push_back(baseIdx + 2);
        mesh.indices.push_back(baseIdx + 3);
    } else {
        // Standard: 0,1,2, 0,2,3
        mesh.indices.push_back(baseIdx + 0);
        mesh.indices.push_back(baseIdx + 1);
        mesh.indices.push_back(baseIdx + 2);
        mesh.indices.push_back(baseIdx + 0);
        mesh.indices.push_back(baseIdx + 2);
        mesh.indices.push_back(baseIdx + 3);
    }
}

VoxelMesh meshChunkNaive(const Chunk& c, const VoxelWorld* world) {
    VoxelMesh mesh;
    // Upper bound: 4096 voxels * 6 faces * 4 verts = 98304 verts.
    // Typical terrain chunk: ~3000-6000 faces. Reserve a reasonable amount.
    mesh.vertices.reserve(8192);
    mesh.indices.reserve(12288);

    for (int ly = 0; ly < VOXEL_CHUNK_SIZE; ly++) {
        for (int lz = 0; lz < VOXEL_CHUNK_SIZE; lz++) {
            for (int lx = 0; lx < VOXEL_CHUNK_SIZE; lx++) {
                BlockId b = c.getBlock(lx, ly, lz);
                if (!isSolidBlock(b)) continue;

                for (int face = 0; face < 6; face++) {
                    Vec3 n = FACE_NORMALS[face];
                    int nx = lx + (int)n.x;
                    int ny = ly + (int)n.y;
                    int nz = lz + (int)n.z;
                    BlockId neighbor = sampleBlock(c, nx, ny, nz, world);
                    if (isSolidBlock(neighbor)) continue;  // face occluded

                    emitNaiveQuad(mesh, c, lx, ly, lz, face, world);
                }
            }
        }
    }
    return mesh;
}

// ============================================================================
// Culled mesher — same as naive but skips AO (faster, slightly worse-looking).
// Useful when AO is too expensive or for debug views.
// ============================================================================

VoxelMesh meshChunkCulled(const Chunk& c, const VoxelWorld* world) {
    VoxelMesh mesh;
    mesh.vertices.reserve(8192);
    mesh.indices.reserve(12288);

    for (int ly = 0; ly < VOXEL_CHUNK_SIZE; ly++) {
        for (int lz = 0; lz < VOXEL_CHUNK_SIZE; lz++) {
            for (int lx = 0; lx < VOXEL_CHUNK_SIZE; lx++) {
                BlockId b = c.getBlock(lx, ly, lz);
                if (!isSolidBlock(b)) continue;

                for (int face = 0; face < 6; face++) {
                    Vec3 n = FACE_NORMALS[face];
                    int nx = lx + (int)n.x;
                    int ny = ly + (int)n.y;
                    int nz = lz + (int)n.z;
                    BlockId neighbor = sampleBlock(c, nx, ny, nz, world);
                    if (isSolidBlock(neighbor)) continue;

                    int baseIdx = mesh.vertexCount();
                    for (int corner = 0; corner < 4; corner++) {
                        Vec3 cp = FACE_CORNERS[face][corner];
                        VoxelVertex v;
                        v.position = Vec3((float)(lx + (int)cp.x),
                                          (float)(ly + (int)cp.y),
                                          (float)(lz + (int)cp.z));
                        v.normal   = n;
                        v.texcoord = FACE_UVS[corner];
                        v.ao       = 1.0f;  // no AO for culled
                        mesh.vertices.push_back(v);
                    }
                    mesh.indices.push_back(baseIdx + 0);
                    mesh.indices.push_back(baseIdx + 1);
                    mesh.indices.push_back(baseIdx + 2);
                    mesh.indices.push_back(baseIdx + 0);
                    mesh.indices.push_back(baseIdx + 2);
                    mesh.indices.push_back(baseIdx + 3);
                }
            }
        }
    }
    return mesh;
}

// ============================================================================
// Greedy mesher — Minecraft-style face merging.
//
// For each of 6 face directions (3 axes × 2 signs):
//   For each slice along the axis:
//     Build a 16×16 mask of visible faces (block ID, or 0 if not visible).
//     Greedily merge same-block-ID cells into the largest possible rectangles.
//     Emit each rectangle as a single quad (with per-corner AO).
// ============================================================================

static void emitGreedyQuad(VoxelMesh& mesh, const Chunk& c,
                           int axis, int sign,
                           int slice, int uu, int vv, int w, int h,
                           BlockId blockType, const VoxelWorld* world) {
    (void)blockType;  // blockType reserved for future per-block UV mapping.
    int uAxis = (axis + 1) % 3;
    int vAxis = (axis + 2) % 3;

    // Build the 4 corner positions in 3D chunk coords.
    // Face plane is at axis_pos = slice + (sign > 0 ? 1 : 0).
    int axisPos = slice + (sign > 0 ? 1 : 0);

    // For sign > 0 (e.g. +X face), outward normal points +axis. The face's
    // corners (CCW from outside) are:
    //   0: (axisPos, uu,   vv)
    //   1: (axisPos, uu,   vv+h)
    //   2: (axisPos, uu+w, vv+h)
    //   3: (axisPos, uu+w, vv)
    // For sign < 0, the winding reverses (we still want CCW from outside).
    //
    // To keep winding correct, we'll define the 4 corners in (u, v) order and
    // flip the triangulation if sign < 0.
    int cornerUV[4][2] = {
        {uu,   vv},
        {uu,   vv + h},
        {uu + w, vv + h},
        {uu + w, vv}
    };

    int baseIdx = mesh.vertexCount();
    Vec3 normal;
    float ncomp[3] = {0, 0, 0};
    ncomp[axis] = (float)sign;
    normal = Vec3(ncomp[0], ncomp[1], ncomp[2]);

    for (int i = 0; i < 4; i++) {
        int pos[3];
        pos[axis] = axisPos;
        pos[uAxis] = cornerUV[i][0];
        pos[vAxis] = cornerUV[i][1];

        VoxelVertex v;
        v.position = Vec3((float)pos[0], (float)pos[1], (float)pos[2]);
        v.normal   = normal;
        v.texcoord = Vec2((float)(cornerUV[i][0] - uu),
                          (float)(cornerUV[i][1] - vv));
        // AO per corner
        int ao = computeGreedyCornerAO(c, axis, sign, slice,
                                        uu, vv, w, h, i, world);
        v.ao = aoLevelToBrightness(ao);
        mesh.vertices.push_back(v);
    }

    // Anti-anisotropy flip
    float a0 = mesh.vertices[baseIdx + 0].ao;
    float a1 = mesh.vertices[baseIdx + 1].ao;
    float a2 = mesh.vertices[baseIdx + 2].ao;
    float a3 = mesh.vertices[baseIdx + 3].ao;

    bool flip = (a0 + a2 < a1 + a3);
    // For -direction faces, the CCW-from-outside winding inverts, so flip
    // the triangulation default.
    if (sign < 0) flip = !flip;

    if (flip) {
        mesh.indices.push_back(baseIdx + 0);
        mesh.indices.push_back(baseIdx + 1);
        mesh.indices.push_back(baseIdx + 3);
        mesh.indices.push_back(baseIdx + 1);
        mesh.indices.push_back(baseIdx + 2);
        mesh.indices.push_back(baseIdx + 3);
    } else {
        mesh.indices.push_back(baseIdx + 0);
        mesh.indices.push_back(baseIdx + 1);
        mesh.indices.push_back(baseIdx + 2);
        mesh.indices.push_back(baseIdx + 0);
        mesh.indices.push_back(baseIdx + 2);
        mesh.indices.push_back(baseIdx + 3);
    }
}

VoxelMesh meshChunkGreedy(const Chunk& c, const VoxelWorld* world) {
    VoxelMesh mesh;
    mesh.vertices.reserve(4096);
    mesh.indices.reserve(6144);

    // For each of 6 face directions.
    for (int faceDir = 0; faceDir < 6; faceDir++) {
        int axis = faceDir / 2;          // 0=X, 1=Y, 2=Z
        int sign = (faceDir % 2 == 0) ? 1 : -1;

        int uAxis = (axis + 1) % 3;
        int vAxis = (axis + 2) % 3;

        // For each slice along the axis.
        for (int slice = 0; slice < VOXEL_CHUNK_SIZE; slice++) {
            // The voxel whose face we're considering is at axis coord = slice.
            // Its neighbor (in the face direction) is at slice + sign.
            int voxelAxisPos = slice;
            int neighborAxisPos = slice + sign;

            // Build 16×16 mask of visible faces.
            static BlockId mask[VOXEL_CHUNK_SIZE * VOXEL_CHUNK_SIZE];
            for (int vv = 0; vv < VOXEL_CHUNK_SIZE; vv++) {
                for (int uu = 0; uu < VOXEL_CHUNK_SIZE; uu++) {
                    int vpos[3], npos[3];
                    vpos[axis] = voxelAxisPos;
                    vpos[uAxis] = uu;
                    vpos[vAxis] = vv;
                    npos[axis] = neighborAxisPos;
                    npos[uAxis] = uu;
                    npos[vAxis] = vv;
                    BlockId voxel    = sampleBlock(c, vpos[0], vpos[1], vpos[2], world);
                    BlockId neighbor = sampleBlock(c, npos[0], npos[1], npos[2], world);
                    if (isSolidBlock(voxel) && !isSolidBlock(neighbor)) {
                        mask[vv * VOXEL_CHUNK_SIZE + uu] = voxel;
                    } else {
                        mask[vv * VOXEL_CHUNK_SIZE + uu] = BLOCK_AIR;
                    }
                }
            }

            // Greedily merge.
            for (int vv = 0; vv < VOXEL_CHUNK_SIZE; vv++) {
                for (int uu = 0; uu < VOXEL_CHUNK_SIZE; ) {
                    BlockId bt = mask[vv * VOXEL_CHUNK_SIZE + uu];
                    if (bt == BLOCK_AIR) { uu++; continue; }

                    // Find max width (along u).
                    int w = 1;
                    while (uu + w < VOXEL_CHUNK_SIZE &&
                           mask[vv * VOXEL_CHUNK_SIZE + uu + w] == bt) {
                        w++;
                    }

                    // Find max height (along v) — all `w` cells must match in each row.
                    int h = 1;
                    while (vv + h < VOXEL_CHUNK_SIZE) {
                        bool ok = true;
                        for (int k = 0; k < w; k++) {
                            if (mask[(vv + h) * VOXEL_CHUNK_SIZE + uu + k] != bt) {
                                ok = false;
                                break;
                            }
                        }
                        if (!ok) break;
                        h++;
                    }

                    emitGreedyQuad(mesh, c, axis, sign, slice,
                                   uu, vv, w, h, bt, world);

                    // Zero out consumed cells.
                    for (int l = 0; l < h; l++) {
                        for (int k = 0; k < w; k++) {
                            mask[(vv + l) * VOXEL_CHUNK_SIZE + uu + k] = BLOCK_AIR;
                        }
                    }

                    uu += w;
                }
            }
        }
    }
    return mesh;
}

// ============================================================================
// meshChunk dispatcher
// ============================================================================

VoxelMesh meshChunk(const Chunk& c, MeshingAlgorithm algo,
                    const VoxelWorld* world) {
    switch (algo) {
        case MeshingAlgorithm::NAIVE:  return meshChunkNaive(c, world);
        case MeshingAlgorithm::GREEDY: return meshChunkGreedy(c, world);
        case MeshingAlgorithm::CULLED: return meshChunkCulled(c, world);
    }
    return VoxelMesh();
}

// ============================================================================
// VoxelMesh::toMesh — upload to a td::Mesh.
//
// NOTE: This is a basic uploader. The engine's Mesh::create() takes Vertex3D
// (no AO attribute), so AO is dropped here. Gameplay code that wants AO in
// the shader should write a custom uploader using the VoxelVertex data
// directly with a 4-attribute VAO (position, normal, uv, ao). See
// meshChunkGreedy / meshChunkNaive above for the data layout.
// ============================================================================

Mesh VoxelMesh::toMesh() const {
    Mesh mesh;
    if (vertices.empty() || indices.empty()) return mesh;

    // Convert VoxelVertex → Vertex3D (drops AO; see note above).
    std::vector<Vertex3D> v3d(vertices.size());
    for (size_t i = 0; i < vertices.size(); i++) {
        v3d[i].position = vertices[i].position;
        v3d[i].normal   = vertices[i].normal;
        v3d[i].texcoord = vertices[i].texcoord;
    }
    mesh.create(v3d.data(), (int)v3d.size(),
                indices.data(), (int)indices.size());
    return mesh;
}

// ============================================================================
// SimplexNoise — 2D + 3D, from scratch (Gustavson's reference impl).
// Output range: roughly [-1, 1].
// ============================================================================

static const float GRAD3[12][3] = {
    { 1, 1, 0}, {-1, 1, 0}, { 1,-1, 0}, {-1,-1, 0},
    { 1, 0, 1}, {-1, 0, 1}, { 1, 0,-1}, {-1, 0,-1},
    { 0, 1, 1}, { 0,-1, 1}, { 0, 1,-1}, { 0,-1,-1}
};

static const float SIMPLEX_F2 = 0.366025403f;  // 0.5 * (sqrt(3) - 1)
static const float SIMPLEX_G2 = 0.211324865f;  // (3 - sqrt(3)) / 6
static const float SIMPLEX_F3 = 1.0f / 3.0f;
static const float SIMPLEX_G3 = 1.0f / 6.0f;

SimplexNoise::SimplexNoise(int seed) : m_seed(seed) {
    int p[256];
    for (int i = 0; i < 256; i++) p[i] = i;

    // Seedable LCG for deterministic permutation shuffling.
    uint32_t state = (uint32_t)seed;
    if (state == 0) state = 1;  // LCG needs non-zero seed
    auto nextRand = [&state]() {
        state = state * 1664525u + 1013904223u;
        return state;
    };

    // Fisher-Yates shuffle.
    for (int i = 255; i > 0; i--) {
        int j = (int)(nextRand() % (uint32_t)(i + 1));
        int tmp = p[i]; p[i] = p[j]; p[j] = tmp;
    }

    for (int i = 0; i < 512; i++) {
        m_perm[i] = p[i & 255];
        m_permMod12[i] = m_perm[i] % 12;
    }
}

float SimplexNoise::noise2D(float xin, float yin) const {
    float n0, n1, n2;

    float s = (xin + yin) * SIMPLEX_F2;
    int i = (int)floorF(xin + s);
    int j = (int)floorF(yin + s);
    float t = (float)(i + j) * SIMPLEX_G2;
    float X0 = (float)i - t;
    float Y0 = (float)j - t;
    float x0 = xin - X0;
    float y0 = yin - Y0;

    int i1, j1;
    if (x0 > y0) { i1 = 1; j1 = 0; }
    else         { i1 = 0; j1 = 1; }

    float x1 = x0 - (float)i1 + SIMPLEX_G2;
    float y1 = y0 - (float)j1 + SIMPLEX_G2;
    float x2 = x0 - 1.0f + 2.0f * SIMPLEX_G2;
    float y2 = y0 - 1.0f + 2.0f * SIMPLEX_G2;

    int ii = i & 255;
    int jj = j & 255;
    int gi0 = m_permMod12[ii + m_perm[jj]];
    int gi1 = m_permMod12[ii + i1 + m_perm[jj + j1]];
    int gi2 = m_permMod12[ii + 1 + m_perm[jj + 1]];

    float t0 = 0.5f - x0*x0 - y0*y0;
    if (t0 < 0.0f) n0 = 0.0f;
    else { t0 *= t0; n0 = t0 * t0 * (GRAD3[gi0][0]*x0 + GRAD3[gi0][1]*y0); }

    float t1 = 0.5f - x1*x1 - y1*y1;
    if (t1 < 0.0f) n1 = 0.0f;
    else { t1 *= t1; n1 = t1 * t1 * (GRAD3[gi1][0]*x1 + GRAD3[gi1][1]*y1); }

    float t2 = 0.5f - x2*x2 - y2*y2;
    if (t2 < 0.0f) n2 = 0.0f;
    else { t2 *= t2; n2 = t2 * t2 * (GRAD3[gi2][0]*x2 + GRAD3[gi2][1]*y2); }

    return 70.0f * (n0 + n1 + n2);  // output ~[-1, 1]
}

float SimplexNoise::noise3D(float xin, float yin, float zin) const {
    float n0, n1, n2, n3;

    float s = (xin + yin + zin) * SIMPLEX_F3;
    int i = (int)floorF(xin + s);
    int j = (int)floorF(yin + s);
    int k = (int)floorF(zin + s);
    float t = (float)(i + j + k) * SIMPLEX_G3;
    float X0 = (float)i - t;
    float Y0 = (float)j - t;
    float Z0 = (float)k - t;
    float x0 = xin - X0;
    float y0 = yin - Y0;
    float z0 = zin - Z0;

    int i1, j1, k1;
    int i2, j2, k2;
    if (x0 >= y0) {
        if (y0 >= z0)      { i1=1; j1=0; k1=0; i2=1; j2=1; k2=0; }
        else if (x0 >= z0) { i1=1; j1=0; k1=0; i2=1; j2=0; k2=1; }
        else               { i1=0; j1=0; k1=1; i2=1; j2=0; k2=1; }
    } else {
        if (y0 < z0)       { i1=0; j1=0; k1=1; i2=0; j2=1; k2=1; }
        else if (x0 < z0)  { i1=0; j1=1; k1=0; i2=0; j2=1; k2=1; }
        else               { i1=0; j1=1; k1=0; i2=1; j2=1; k2=0; }
    }

    float x1 = x0 - (float)i1 + SIMPLEX_G3;
    float y1 = y0 - (float)j1 + SIMPLEX_G3;
    float z1 = z0 - (float)k1 + SIMPLEX_G3;
    float x2 = x0 - (float)i2 + 2.0f * SIMPLEX_G3;
    float y2 = y0 - (float)j2 + 2.0f * SIMPLEX_G3;
    float z2 = z0 - (float)k2 + 2.0f * SIMPLEX_G3;
    float x3 = x0 - 1.0f + 3.0f * SIMPLEX_G3;
    float y3 = y0 - 1.0f + 3.0f * SIMPLEX_G3;
    float z3 = z0 - 1.0f + 3.0f * SIMPLEX_G3;

    int ii = i & 255;
    int jj = j & 255;
    int kk = k & 255;
    int gi0 = m_permMod12[ii     + m_perm[jj     + m_perm[kk    ]]];
    int gi1 = m_permMod12[ii + i1 + m_perm[jj + j1 + m_perm[kk + k1]]];
    int gi2 = m_permMod12[ii + i2 + m_perm[jj + j2 + m_perm[kk + k2]]];
    int gi3 = m_permMod12[ii + 1 + m_perm[jj + 1 + m_perm[kk + 1]]];

    float t0 = 0.6f - x0*x0 - y0*y0 - z0*z0;
    if (t0 < 0.0f) n0 = 0.0f;
    else { t0 *= t0; n0 = t0 * t0 * (GRAD3[gi0][0]*x0 + GRAD3[gi0][1]*y0 + GRAD3[gi0][2]*z0); }

    float t1 = 0.6f - x1*x1 - y1*y1 - z1*z1;
    if (t1 < 0.0f) n1 = 0.0f;
    else { t1 *= t1; n1 = t1 * t1 * (GRAD3[gi1][0]*x1 + GRAD3[gi1][1]*y1 + GRAD3[gi1][2]*z1); }

    float t2 = 0.6f - x2*x2 - y2*y2 - z2*z2;
    if (t2 < 0.0f) n2 = 0.0f;
    else { t2 *= t2; n2 = t2 * t2 * (GRAD3[gi2][0]*x2 + GRAD3[gi2][1]*y2 + GRAD3[gi2][2]*z2); }

    float t3 = 0.6f - x3*x3 - y3*y3 - z3*z3;
    if (t3 < 0.0f) n3 = 0.0f;
    else { t3 *= t3; n3 = t3 * t3 * (GRAD3[gi3][0]*x3 + GRAD3[gi3][1]*y3 + GRAD3[gi3][2]*z3); }

    return 32.0f * (n0 + n1 + n2 + n3);  // output ~[-1, 1]
}

float SimplexNoise::fractal2D(float x, float y, int octaves,
                              float persistence, float lacunarity) const {
    float total = 0.0f;
    float freq = 1.0f;
    float amp = 1.0f;
    float maxAmp = 0.0f;
    for (int i = 0; i < octaves; i++) {
        total += noise2D(x * freq, y * freq) * amp;
        maxAmp += amp;
        freq *= lacunarity;
        amp *= persistence;
    }
    if (maxAmp < TD_EPSILON) return 0.0f;
    return total / maxAmp;  // normalize to ~[-1, 1]
}

float SimplexNoise::fractal3D(float x, float y, float z, int octaves,
                              float persistence, float lacunarity) const {
    float total = 0.0f;
    float freq = 1.0f;
    float amp = 1.0f;
    float maxAmp = 0.0f;
    for (int i = 0; i < octaves; i++) {
        total += noise3D(x * freq, y * freq, z * freq) * amp;
        maxAmp += amp;
        freq *= lacunarity;
        amp *= persistence;
    }
    if (maxAmp < TD_EPSILON) return 0.0f;
    return total / maxAmp;
}

// ============================================================================
// Worldgen — simplex heightmap terrain.
// ============================================================================

Worldgen::Worldgen(int seed, int seaLevel, int baseHeight, int hillHeight)
    : m_noise2D(seed),
      m_noise3D(seed * 7 + 13),
      m_seed(seed),
      m_seaLevel(seaLevel),
      m_baseHeight(baseHeight),
      m_hillHeight(hillHeight),
      m_stoneStart(10) {}

int Worldgen::heightAt(int worldX, int worldZ) const {
    // 4-octave fractal simplex noise → heightmap.
    float n = m_noise2D.fractal2D((float)worldX, (float)worldZ, 4, 0.5f, 2.0f);
    // n is ~[-1, 1]. Map to [base - hill, base + hill].
    int h = m_baseHeight + (int)(n * (float)m_hillHeight);
    if (h < 0) h = 0;
    if (h > 255) h = 255;
    return h;
}

BlockId Worldgen::blockAt(int worldX, int worldY, int worldZ) const {
    int h = heightAt(worldX, worldZ);

    if (worldY > h) {
        // Above terrain: water below sea level, else air.
        if (worldY < m_seaLevel) return BLOCK_WATER;
        return BLOCK_AIR;
    }
    if (worldY == h) {
        // Top block: sand near water, grass otherwise.
        if (h <= m_seaLevel) return BLOCK_SAND;     // beach / underwater
        return BLOCK_GRASS;
    }
    // Below top.
    if (worldY < m_stoneStart) return BLOCK_STONE;  // deep stone
    return BLOCK_DIRT;                              // dirt layer
}

BlockId Worldgen::maybeOre(int worldX, int worldY, int worldZ,
                            BlockId baseBlock) const {
    if (!m_oresEnabled || baseBlock != BLOCK_STONE) return baseBlock;
    // Sparse 3D noise threshold → ore veins.
    float n = m_noise3D.noise3D((float)worldX * 0.1f,
                                 (float)worldY * 0.1f,
                                 (float)worldZ * 0.1f);
    if (n > 0.78f) return BLOCK_DIRT;   // coal-like vein placeholder
    return baseBlock;
}

void Worldgen::placeTree(Chunk& c, int lx, int ly, int lz,
                         int worldX, int worldZ) const {
    if (!m_treesEnabled) return;
    if (ly < 0 || ly + 6 >= VOXEL_CHUNK_SIZE) return;

    // Deterministic tree placement: ~1 in 64 grass columns gets a tree.
    uint32_t h = (uint32_t)(worldX * 73856093) ^
                 (uint32_t)(worldZ * 19349663) ^
                 (uint32_t)(m_seed * 83492791u);
    if ((h & 0x3F) != 0) return;

    int trunkH = 4 + (int)((h >> 6) & 0x3);  // 4..7
    for (int i = 1; i <= trunkH; i++) {
        if (ly + i < VOXEL_CHUNK_SIZE) {
            c.setBlock(lx, ly + i, lz, BLOCK_WOOD);
        }
    }

    // Leaves: 3×3×2 blob on top, plus a single block above.
    int topY = ly + trunkH;
    for (int dy = -1; dy <= 1; dy++) {
        int r = (dy < 1) ? 2 : 1;
        for (int dx = -r; dx <= r; dx++) {
            for (int dz = -r; dz <= r; dz++) {
                if (dx == 0 && dz == 0 && dy <= 0) continue;  // don't clobber trunk
                int lx2 = lx + dx, ly2 = topY + dy, lz2 = lz + dz;
                if (lx2 < 0 || lx2 >= VOXEL_CHUNK_SIZE) continue;
                if (ly2 < 0 || ly2 >= VOXEL_CHUNK_SIZE) continue;
                if (lz2 < 0 || lz2 >= VOXEL_CHUNK_SIZE) continue;
                if (c.getBlock(lx2, ly2, lz2) == BLOCK_AIR) {
                    c.setBlock(lx2, ly2, lz2, BLOCK_LEAVES);
                }
            }
        }
    }
    // Top leaf.
    if (topY + 1 < VOXEL_CHUNK_SIZE) {
        c.setBlock(lx, topY + 1, lz, BLOCK_LEAVES);
    }
}

void Worldgen::generate(Chunk& c) {
    // Pass 1: heightmap terrain.
    for (int lz = 0; lz < VOXEL_CHUNK_SIZE; lz++) {
        for (int lx = 0; lx < VOXEL_CHUNK_SIZE; lx++) {
            int worldX = c.cx * VOXEL_CHUNK_SIZE + lx;
            int worldZ = c.cz * VOXEL_CHUNK_SIZE + lz;
            for (int ly = 0; ly < VOXEL_CHUNK_SIZE; ly++) {
                int worldY = c.cy * VOXEL_CHUNK_SIZE + ly;
                BlockId b = blockAt(worldX, worldY, worldZ);
                if (b == BLOCK_STONE) {
                    b = maybeOre(worldX, worldY, worldZ, b);
                }
                c.blocks[(ly * VOXEL_CHUNK_SIZE + lz) * VOXEL_CHUNK_SIZE + lx] = b;
            }
        }
    }

    // Pass 2: trees. Place on topmost grass in each column.
    if (m_treesEnabled) {
        for (int lz = 0; lz < VOXEL_CHUNK_SIZE; lz++) {
            for (int lx = 0; lx < VOXEL_CHUNK_SIZE; lx++) {
                for (int ly = VOXEL_CHUNK_SIZE - 1; ly >= 0; ly--) {
                    BlockId b = c.getBlock(lx, ly, lz);
                    if (b == BLOCK_GRASS) {
                        int worldX = c.cx * VOXEL_CHUNK_SIZE + lx;
                        int worldZ = c.cz * VOXEL_CHUNK_SIZE + lz;
                        placeTree(c, lx, ly, lz, worldX, worldZ);
                        break;
                    }
                    // Skip air/water/leaves but keep looking down for grass.
                    if (b != BLOCK_AIR && b != BLOCK_WATER && b != BLOCK_LEAVES) break;
                }
            }
        }
    }

    c.generated = true;
    c.dirty = true;
}

// ============================================================================
// VoxelWorld
// ============================================================================

VoxelWorld::VoxelWorld() {}
VoxelWorld::~VoxelWorld() {}

BlockId VoxelWorld::getVoxel(int x, int y, int z) const {
    int32_t cx = (int32_t)floorF((float)x / (float)VOXEL_CHUNK_SIZE);
    int32_t cy = (int32_t)floorF((float)y / (float)VOXEL_CHUNK_SIZE);
    int32_t cz = (int32_t)floorF((float)z / (float)VOXEL_CHUNK_SIZE);
    // const_cast: ChunkMap::get is non-const in the frozen chunk.h, but
    // semantically it's a pure lookup.
    Chunk* c = const_cast<ChunkMap&>(m_chunks).get(cx, cy, cz);
    if (!c) return BLOCK_AIR;
    int lx = x - cx * VOXEL_CHUNK_SIZE;
    int ly = y - cy * VOXEL_CHUNK_SIZE;
    int lz = z - cz * VOXEL_CHUNK_SIZE;
    return c->getBlock(lx, ly, lz);
}

void VoxelWorld::setVoxel(int x, int y, int z, BlockId b) {
    Chunk* c = getOrCreateChunkAt(x, y, z);
    if (!c) return;
    int32_t cx = (int32_t)floorF((float)x / (float)VOXEL_CHUNK_SIZE);
    int32_t cy = (int32_t)floorF((float)y / (float)VOXEL_CHUNK_SIZE);
    int32_t cz = (int32_t)floorF((float)z / (float)VOXEL_CHUNK_SIZE);
    int lx = x - cx * VOXEL_CHUNK_SIZE;
    int ly = y - cy * VOXEL_CHUNK_SIZE;
    int lz = z - cz * VOXEL_CHUNK_SIZE;
    c->setBlock(lx, ly, lz, b);  // marks dirty
    m_editCount++;
}

Chunk* VoxelWorld::getChunkAt(int x, int y, int z) const {
    int32_t cx = (int32_t)floorF((float)x / (float)VOXEL_CHUNK_SIZE);
    int32_t cy = (int32_t)floorF((float)y / (float)VOXEL_CHUNK_SIZE);
    int32_t cz = (int32_t)floorF((float)z / (float)VOXEL_CHUNK_SIZE);
    return const_cast<ChunkMap&>(m_chunks).get(cx, cy, cz);
}

Chunk* VoxelWorld::getOrCreateChunkAt(int x, int y, int z) {
    int32_t cx = (int32_t)floorF((float)x / (float)VOXEL_CHUNK_SIZE);
    int32_t cy = (int32_t)floorF((float)y / (float)VOXEL_CHUNK_SIZE);
    int32_t cz = (int32_t)floorF((float)z / (float)VOXEL_CHUNK_SIZE);
    Chunk* c = m_chunks.getOrCreate(cx, cy, cz);
    if (c && !c->generated && m_generator) {
        m_generator->generate(*c);
    }
    return c;
}

void VoxelWorld::markDirty(int x, int y, int z) {
    Chunk* c = getChunkAt(x, y, z);
    if (c) c->dirty = true;
}

// ============================================================================
// Raycast — DDA voxel traversal (Amanatides & Woo, 1987).
// ============================================================================

RayHit raycastChunk(const Chunk& c, Vec3 chunkOrigin,
                    Vec3 origin, Vec3 dir, float maxDist) {
    RayHit hit;

    // Transform origin to chunk-local coords.
    Vec3 o = origin - chunkOrigin;
    Vec3 d = dir;
    float dlen = d.length();
    if (dlen < TD_EPSILON) return hit;
    d = d / dlen;  // normalize

    int ix = (int)floorF(o.x);
    int iy = (int)floorF(o.y);
    int iz = (int)floorF(o.z);

    int stepX = (d.x > 0) ? 1 : (d.x < 0 ? -1 : 0);
    int stepY = (d.y > 0) ? 1 : (d.y < 0 ? -1 : 0);
    int stepZ = (d.z > 0) ? 1 : (d.z < 0 ? -1 : 0);

    // Distance to the next voxel boundary along each axis.
    float tMaxX = (stepX != 0)
        ? ((stepX > 0 ? ((float)(ix + 1) - o.x) : (o.x - (float)ix)) / absF(d.x))
        : 1e30f;
    float tMaxY = (stepY != 0)
        ? ((stepY > 0 ? ((float)(iy + 1) - o.y) : (o.y - (float)iy)) / absF(d.y))
        : 1e30f;
    float tMaxZ = (stepZ != 0)
        ? ((stepZ > 0 ? ((float)(iz + 1) - o.z) : (o.z - (float)iz)) / absF(d.z))
        : 1e30f;

    float tDeltaX = (stepX != 0) ? 1.0f / absF(d.x) : 1e30f;
    float tDeltaY = (stepY != 0) ? 1.0f / absF(d.y) : 1e30f;
    float tDeltaZ = (stepZ != 0) ? 1.0f / absF(d.z) : 1e30f;

    int face = -1;  // 0=X, 1=Y, 2=Z (last face stepped through)
    float t = 0.0f;

    // Check starting voxel first (ray might begin inside a solid block).
    if (ix >= 0 && ix < VOXEL_CHUNK_SIZE &&
        iy >= 0 && iy < VOXEL_CHUNK_SIZE &&
        iz >= 0 && iz < VOXEL_CHUNK_SIZE) {
        BlockId b = c.getBlock(ix, iy, iz);
        if (isSolidBlock(b)) {
            hit.hit = true;
            hit.voxelX = ix + (int)chunkOrigin.x;
            hit.voxelY = iy + (int)chunkOrigin.y;
            hit.voxelZ = iz + (int)chunkOrigin.z;
            hit.distance = 0.0f;
            hit.normal = Vec3(0, 0, 0);  // started inside; no entry face
            return hit;
        }
    }

    while (t < maxDist) {
        // Step to the next voxel.
        if (tMaxX < tMaxY) {
            if (tMaxX < tMaxZ) {
                ix += stepX; t = tMaxX; tMaxX += tDeltaX; face = 0;
            } else {
                iz += stepZ; t = tMaxZ; tMaxZ += tDeltaZ; face = 2;
            }
        } else {
            if (tMaxY < tMaxZ) {
                iy += stepY; t = tMaxY; tMaxY += tDeltaY; face = 1;
            } else {
                iz += stepZ; t = tMaxZ; tMaxZ += tDeltaZ; face = 2;
            }
        }

        if (t > maxDist) break;

        // Out-of-chunk voxels are treated as air — keep stepping until the ray
        // either enters the chunk and hits something, or exceeds maxDist.
        if (ix < 0 || ix >= VOXEL_CHUNK_SIZE ||
            iy < 0 || iy >= VOXEL_CHUNK_SIZE ||
            iz < 0 || iz >= VOXEL_CHUNK_SIZE) {
            continue;
        }

        BlockId b = c.getBlock(ix, iy, iz);
        if (isSolidBlock(b)) {
            hit.hit = true;
            hit.voxelX = ix + (int)chunkOrigin.x;
            hit.voxelY = iy + (int)chunkOrigin.y;
            hit.voxelZ = iz + (int)chunkOrigin.z;
            hit.distance = t;
            // Normal points opposite to the step direction on the entry face.
            if      (face == 0) hit.normal = Vec3((float)(-stepX), 0.0f, 0.0f);
            else if (face == 1) hit.normal = Vec3(0.0f, (float)(-stepY), 0.0f);
            else                hit.normal = Vec3(0.0f, 0.0f, (float)(-stepZ));
            return hit;
        }
    }
    return hit;
}

RayHit raycastVoxel(const VoxelWorld& world, Vec3 origin, Vec3 dir, float maxDist) {
    RayHit hit;

    Vec3 d = dir;
    float dlen = d.length();
    if (dlen < TD_EPSILON) return hit;
    d = d / dlen;

    int ix = (int)floorF(origin.x);
    int iy = (int)floorF(origin.y);
    int iz = (int)floorF(origin.z);

    int stepX = (d.x > 0) ? 1 : (d.x < 0 ? -1 : 0);
    int stepY = (d.y > 0) ? 1 : (d.y < 0 ? -1 : 0);
    int stepZ = (d.z > 0) ? 1 : (d.z < 0 ? -1 : 0);

    float tMaxX = (stepX != 0)
        ? ((stepX > 0 ? ((float)(ix + 1) - origin.x) : (origin.x - (float)ix)) / absF(d.x))
        : 1e30f;
    float tMaxY = (stepY != 0)
        ? ((stepY > 0 ? ((float)(iy + 1) - origin.y) : (origin.y - (float)iy)) / absF(d.y))
        : 1e30f;
    float tMaxZ = (stepZ != 0)
        ? ((stepZ > 0 ? ((float)(iz + 1) - origin.z) : (origin.z - (float)iz)) / absF(d.z))
        : 1e30f;

    float tDeltaX = (stepX != 0) ? 1.0f / absF(d.x) : 1e30f;
    float tDeltaY = (stepY != 0) ? 1.0f / absF(d.y) : 1e30f;
    float tDeltaZ = (stepZ != 0) ? 1.0f / absF(d.z) : 1e30f;

    int face = -1;
    float t = 0.0f;

    // Check starting voxel.
    BlockId b0 = world.getVoxel(ix, iy, iz);
    if (isSolidBlock(b0)) {
        hit.hit = true;
        hit.voxelX = ix;
        hit.voxelY = iy;
        hit.voxelZ = iz;
        hit.distance = 0.0f;
        hit.normal = Vec3(0, 0, 0);
        return hit;
    }

    while (t < maxDist) {
        if (tMaxX < tMaxY) {
            if (tMaxX < tMaxZ) {
                ix += stepX; t = tMaxX; tMaxX += tDeltaX; face = 0;
            } else {
                iz += stepZ; t = tMaxZ; tMaxZ += tDeltaZ; face = 2;
            }
        } else {
            if (tMaxY < tMaxZ) {
                iy += stepY; t = tMaxY; tMaxY += tDeltaY; face = 1;
            } else {
                iz += stepZ; t = tMaxZ; tMaxZ += tDeltaZ; face = 2;
            }
        }
        if (t > maxDist) break;

        BlockId b = world.getVoxel(ix, iy, iz);
        if (isSolidBlock(b)) {
            hit.hit = true;
            hit.voxelX = ix;
            hit.voxelY = iy;
            hit.voxelZ = iz;
            hit.distance = t;
            if      (face == 0) hit.normal = Vec3((float)(-stepX), 0.0f, 0.0f);
            else if (face == 1) hit.normal = Vec3(0.0f, (float)(-stepY), 0.0f);
            else                hit.normal = Vec3(0.0f, 0.0f, (float)(-stepZ));
            return hit;
        }
    }
    return hit;
}

// ============================================================================
// Frustum — 6-plane extraction from view-projection matrix (Gribb-Hartmann).
// ============================================================================

Frustum Frustum::fromViewProj(const Mat4& vp) {
    Frustum f;
    // m[col * 4 + row]
    // plane = row_a - row_b (where row is (a, b, c, d) plane eq)
    // Left:   row3 + row0  (a=m3+m0, b=m7+m4, c=m11+m8, d=m15+m12)
    // Right:  row3 - row0
    // Bottom: row3 + row1
    // Top:    row3 - row1
    // Near:   row3 + row2
    // Far:    row3 - row2
    //
    // In this codebase m is column-major: m[col*4 + row]. row 0 = (m[0], m[4], m[8],  m[12])
    //                                                          row 1 = (m[1], m[5], m[9],  m[13])
    //                                                          row 2 = (m[2], m[6], m[10], m[14])
    //                                                          row 3 = (m[3], m[7], m[11], m[15])
    const float* m = vp.m;
    // Left = row3 + row0
    f.planes[0] = Vec4(m[3]+m[0], m[7]+m[4], m[11]+m[8],  m[15]+m[12]);
    // Right = row3 - row0
    f.planes[1] = Vec4(m[3]-m[0], m[7]-m[4], m[11]-m[8],  m[15]-m[12]);
    // Bottom = row3 + row1
    f.planes[2] = Vec4(m[3]+m[1], m[7]+m[5], m[11]+m[9],  m[15]+m[13]);
    // Top = row3 - row1
    f.planes[3] = Vec4(m[3]-m[1], m[7]-m[5], m[11]-m[9],  m[15]-m[13]);
    // Near = row3 + row2
    f.planes[4] = Vec4(m[3]+m[2], m[7]+m[6], m[11]+m[10], m[15]+m[14]);
    // Far = row3 - row2
    f.planes[5] = Vec4(m[3]-m[2], m[7]-m[6], m[11]-m[10], m[15]-m[14]);

    // Normalize each plane (so dot(p.xyz, q) + p.w >= 0 means inside).
    for (int i = 0; i < 6; i++) {
        Vec3 n(f.planes[i].x, f.planes[i].y, f.planes[i].z);
        float len = n.length();
        if (len > TD_EPSILON) {
            float inv = 1.0f / len;
            f.planes[i].x *= inv;
            f.planes[i].y *= inv;
            f.planes[i].z *= inv;
            f.planes[i].w *= inv;
        }
    }
    return f;
}

bool Frustum::intersectsPoint(const Vec3& p) const {
    for (int i = 0; i < 6; i++) {
        const Vec4& pl = planes[i];
        if (pl.x * p.x + pl.y * p.y + pl.z * p.z + pl.w < 0.0f) {
            return false;
        }
    }
    return true;
}

bool Frustum::intersectsAABB(const Vec3& min, const Vec3& max) const {
    // Conservative test: for each plane, find the AABB's "positive vertex"
    // (the corner furthest in the plane's normal direction). If that corner
    // is outside the plane, the whole AABB is outside.
    for (int i = 0; i < 6; i++) {
        const Vec4& pl = planes[i];
        Vec3 pv;
        pv.x = (pl.x >= 0.0f) ? max.x : min.x;
        pv.y = (pl.y >= 0.0f) ? max.y : min.y;
        pv.z = (pl.z >= 0.0f) ? max.z : min.z;
        if (pl.x * pv.x + pl.y * pv.y + pl.z * pv.z + pl.w < 0.0f) {
            return false;
        }
    }
    return true;
}

// ============================================================================
// VoxelStreamer — distance-ordered load queue + per-frame budget + unload.
// ============================================================================

VoxelStreamer::VoxelStreamer(int viewDistance, int unloadDistance)
    : m_viewDistance(viewDistance), m_unloadDistance(unloadDistance) {
    m_meshCache.reserve(256);
}

VoxelStreamer::~VoxelStreamer() = default;

void VoxelStreamer::enqueue(int cx, int cy, int cz) {
    for (const auto& e : m_loadQueue) {
        if (e.cx == cx && e.cy == cy && e.cz == cz) return;  // already queued
    }
    // Also skip if already in mesh cache.
    if (findMesh(cx, cy, cz) >= 0) return;
    m_loadQueue.push_back({cx, cy, cz, 0});
}

int VoxelStreamer::findMesh(int cx, int cy, int cz) const {
    for (size_t i = 0; i < m_meshCache.size(); i++) {
        if (m_meshCache[i].cx == cx &&
            m_meshCache[i].cy == cy &&
            m_meshCache[i].cz == cz) {
            return (int)i;
        }
    }
    return -1;
}

void VoxelStreamer::rebuildMesh(MeshEntry& e, MeshingAlgorithm algo) {
    if (!m_world) return;
    Chunk* c = m_world->chunks().get(e.cx, e.cy, e.cz);
    if (!c) return;
    e.mesh = meshChunk(*c, algo, m_world);
    e.dirty = false;
}

void VoxelStreamer::sortQueue(int ccx, int ccy, int ccz) {
    for (auto& e : m_loadQueue) {
        int dx = e.cx - ccx;
        int dy = e.cy - ccy;
        int dz = e.cz - ccz;
        e.distSq = dx*dx + dy*dy + dz*dz;
    }
    std::sort(m_loadQueue.begin(), m_loadQueue.end(),
              [](const LoadEntry& a, const LoadEntry& b) {
                  return a.distSq < b.distSq;
              });
}

int VoxelStreamer::update(float camX, float camY, float camZ, int budget) {
    if (!m_world) return 0;

    int32_t ccx = (int32_t)floorF(camX / (float)VOXEL_CHUNK_SIZE);
    int32_t ccy = (int32_t)floorF(camY / (float)VOXEL_CHUNK_SIZE);
    int32_t ccz = (int32_t)floorF(camZ / (float)VOXEL_CHUNK_SIZE);

    int loaded = 0;

    // 1. Enqueue chunks within viewDistance.
    int rd = m_viewDistance;
    int rd2 = rd * rd;
    for (int dz = -rd; dz <= rd; dz++) {
        for (int dy = -rd; dy <= rd; dy++) {
            for (int dx = -rd; dx <= rd; dx++) {
                int d2 = dx*dx + dy*dy + dz*dz;
                if (d2 > rd2) continue;
                int32_t cx = ccx + dx, cy = ccy + dy, cz = ccz + dz;
                // Skip if already in mesh cache.
                if (findMesh(cx, cy, cz) >= 0) continue;
                // Skip if already queued.
                bool queued = false;
                for (const auto& e : m_loadQueue) {
                    if (e.cx == cx && e.cy == cy && e.cz == cz) {
                        queued = true; break;
                    }
                }
                if (queued) continue;
                m_loadQueue.push_back({cx, cy, cz, d2});
            }
        }
    }

    // 2. Sort queue by distance to camera (closest first).
    sortQueue(ccx, ccy, ccz);

    // 3. Process up to `budget` from the front.
    int toLoad = (int)m_loadQueue.size();
    if (toLoad > budget) toLoad = budget;
    for (int i = 0; i < toLoad; i++) {
        const LoadEntry& e = m_loadQueue[i];
        // Ensure chunk is generated.
        Chunk* c = m_world->getOrCreateChunkAt(
            e.cx * VOXEL_CHUNK_SIZE, e.cy * VOXEL_CHUNK_SIZE, e.cz * VOXEL_CHUNK_SIZE);
        (void)c;
        // Add to mesh cache.
        MeshEntry me;
        me.cx = e.cx; me.cy = e.cy; me.cz = e.cz;
        me.dirty = true;
        m_meshCache.push_back(std::move(me));
        rebuildMesh(m_meshCache.back(), MeshingAlgorithm::GREEDY);
        loaded++;
    }
    // Erase processed entries.
    if (toLoad > 0) {
        m_loadQueue.erase(m_loadQueue.begin(), m_loadQueue.begin() + toLoad);
    }

    // 4. Unload chunks beyond unloadDistance (with 1-chunk hysteresis).
    int ud = m_unloadDistance;
    int ud2 = ud * ud;
    for (size_t i = 0; i < m_meshCache.size(); ) {
        int dx = m_meshCache[i].cx - ccx;
        int dy = m_meshCache[i].cy - ccy;
        int dz = m_meshCache[i].cz - ccz;
        if (dx*dx + dy*dy + dz*dz > ud2) {
            m_meshCache.erase(m_meshCache.begin() + (int)i);
        } else {
            i++;
        }
    }

    return loaded;
}

const VoxelMesh* VoxelStreamer::getChunkMesh(int cx, int cy, int cz,
                                              MeshingAlgorithm algo) {
    int idx = findMesh(cx, cy, cz);
    if (idx < 0) return nullptr;
    MeshEntry& e = m_meshCache[idx];
    if (e.dirty) {
        rebuildMesh(e, algo);
    }
    return &e.mesh;
}

// ============================================================================
// Lighting — basic skylight + BFS block light.
// ============================================================================

void computeSkylight(const Chunk& c, ChunkLighting& out) {
    // For each (x, z) column, march top-down. Voxels above the topmost solid
    // block get sky=15. Solid blocks get sky=0. The first solid block from
    // the top (its top face is exposed to sky) also gets sky=15 to avoid a
    // hard black line at the surface; below it, sky=0.
    for (int lx = 0; lx < VOXEL_CHUNK_SIZE; lx++) {
        for (int lz = 0; lz < VOXEL_CHUNK_SIZE; lz++) {
            bool exposed = true;
            for (int ly = VOXEL_CHUNK_SIZE - 1; ly >= 0; ly--) {
                BlockId b = c.getBlock(lx, ly, lz);
                if (exposed) {
                    out.setSky(lx, ly, lz, 15);
                } else {
                    out.setSky(lx, ly, lz, 0);
                }
                if (isSolidBlock(b)) {
                    exposed = false;
                }
            }
        }
    }
}

void propagateBlockLight(const Chunk& c, ChunkLighting& out) {
    // BFS from torches.
    struct Pos { int x, y, z; };
    std::vector<Pos> queue;

    // Seed: find all torches (light emitters).
    for (int ly = 0; ly < VOXEL_CHUNK_SIZE; ly++) {
        for (int lz = 0; lz < VOXEL_CHUNK_SIZE; lz++) {
            for (int lx = 0; lx < VOXEL_CHUNK_SIZE; lx++) {
                BlockId b = c.getBlock(lx, ly, lz);
                if (b == BLOCK_TORCH) {
                    out.setBlock(lx, ly, lz, 15);
                    queue.push_back({lx, ly, lz});
                }
            }
        }
    }

    // BFS: each step, propagate to 6 neighbors with level-1.
    static const int offsets[6][3] = {
        { 1, 0, 0}, {-1, 0, 0},
        { 0, 1, 0}, { 0,-1, 0},
        { 0, 0, 1}, { 0, 0,-1}
    };
    size_t head = 0;
    while (head < queue.size()) {
        Pos p = queue[head++];
        int level = out.getBlock(p.x, p.y, p.z);
        if (level <= 1) continue;
        for (int i = 0; i < 6; i++) {
            int nx = p.x + offsets[i][0];
            int ny = p.y + offsets[i][1];
            int nz = p.z + offsets[i][2];
            if (nx < 0 || nx >= VOXEL_CHUNK_SIZE) continue;
            if (ny < 0 || ny >= VOXEL_CHUNK_SIZE) continue;
            if (nz < 0 || nz >= VOXEL_CHUNK_SIZE) continue;
            // Don't propagate into solid blocks (except the torch source).
            BlockId nb = c.getBlock(nx, ny, nz);
            if (isSolidBlock(nb) && nb != BLOCK_TORCH) continue;
            int cur = out.getBlock(nx, ny, nz);
            if (cur + 2 <= level) {
                out.setBlock(nx, ny, nz, level - 1);
                queue.push_back({nx, ny, nz});
            }
        }
    }
}

} // namespace td
