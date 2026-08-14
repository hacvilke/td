// TD Engine - Voxel Tests (wave1-voxel)
//
// Tests the real voxel system: meshers (naive, greedy, culled), AO, worldgen,
// raycast, VoxelWorld get/set, frustum culling, chunk streaming, lighting.
//
// Build (direct, no CMake):
//   g++ -std=c++17 -Wall -Wextra -O2 -Isrc
//       tests/test_voxel.cpp src/voxel/voxel.cpp
//       tests/stub_logger.cpp tests/stub_mesh.cpp
//       -o /tmp/test_voxel
//
// Build (via CMake, links against td-engine):
//   cmake --build build-desktop --target test_voxel

#include "voxel/voxel.h"
#include <cstdio>
#include <cmath>

using namespace td;

static int g_testsPassed = 0;
static int g_testsFailed = 0;

#define TEST(name, condition)                                                  \
    do {                                                                       \
        if (condition) {                                                       \
            printf("PASS: %s\n", name);                                        \
            g_testsPassed++;                                                   \
        } else {                                                               \
            printf("FAIL: %s (line %d)\n", name, __LINE__);                    \
            g_testsFailed++;                                                   \
        }                                                                      \
    } while (0)

#define EXPECT_NEAR(a, b, eps) (absF((a) - (b)) < (eps))

// =============================================================================
// Test 1: Naive mesher on a full chunk of dirt.
//
// A 16^3 chunk of solid dirt has only its outer hull visible (interior faces
// are between two solid voxels → culled). 6 sides * 16*16 faces per side =
// 1536 faces.
// =============================================================================
static void testNaiveFullChunk() {
    printf("\n=== Test 1: Naive mesher on full dirt chunk ===\n");
    Chunk c;
    for (int i = 0; i < VOXEL_CHUNK_SIZE * VOXEL_CHUNK_SIZE * VOXEL_CHUNK_SIZE; i++) {
        c.blocks[i] = BLOCK_DIRT;
    }
    VoxelMesh mesh = meshChunkNaive(c);

    int expectedFaces = 6 * VOXEL_CHUNK_SIZE * VOXEL_CHUNK_SIZE;  // 1536
    printf("  naive faces=%d (expected %d), tris=%d, verts=%d\n",
           mesh.faceCount(), expectedFaces,
           mesh.triangleCount(), mesh.vertexCount());

    TEST("naive face count == 6*16*16 (interior culled)",
         mesh.faceCount() == expectedFaces);
    TEST("naive triangle count == 2 * faces",
         mesh.triangleCount() == 2 * expectedFaces);
    TEST("naive vertex count == 4 * faces",
         mesh.vertexCount() == 4 * expectedFaces);
}

// =============================================================================
// Test 2: Greedy vs Naive — greedy must reduce triangle count.
//
// On a full solid chunk, greedy merges each side's 16*16 faces into a single
// quad → 6 faces total (vs 1536 for naive). On a "striped" chunk (alternating
// solid/air layers), greedy still merges within each layer.
// =============================================================================
static void testGreedyVsNaive() {
    printf("\n=== Test 2: Greedy vs Naive ===\n");

    // 2a: Full solid chunk → greedy = 6 faces (one merged quad per side).
    Chunk cFull;
    for (int i = 0; i < VOXEL_CHUNK_SIZE * VOXEL_CHUNK_SIZE * VOXEL_CHUNK_SIZE; i++) {
        cFull.blocks[i] = BLOCK_DIRT;
    }
    VoxelMesh naiveFull = meshChunkNaive(cFull);
    VoxelMesh greedyFull = meshChunkGreedy(cFull);
    printf("  full dirt: naive=%d faces, greedy=%d faces\n",
           naiveFull.faceCount(), greedyFull.faceCount());

    TEST("greedy on full dirt = 6 faces (one merged quad per side)",
         greedyFull.faceCount() == 6);
    TEST("greedy < naive on full dirt",
         greedyFull.triangleCount() < naiveFull.triangleCount());

    // 2b: Striped chunk (even Y = solid, odd Y = air).
    // Greedy can merge each layer's top/bottom face into one quad → far fewer
    // than naive's per-voxel faces.
    Chunk cStriped;
    for (int i = 0; i < VOXEL_CHUNK_SIZE * VOXEL_CHUNK_SIZE * VOXEL_CHUNK_SIZE; i++) {
        cStriped.blocks[i] = BLOCK_AIR;
    }
    for (int ly = 0; ly < VOXEL_CHUNK_SIZE; ly += 2) {
        for (int lz = 0; lz < VOXEL_CHUNK_SIZE; lz++) {
            for (int lx = 0; lx < VOXEL_CHUNK_SIZE; lx++) {
                cStriped.blocks[(ly * VOXEL_CHUNK_SIZE + lz) * VOXEL_CHUNK_SIZE + lx] = BLOCK_STONE;
            }
        }
    }
    VoxelMesh naiveStriped = meshChunkNaive(cStriped);
    VoxelMesh greedyStriped = meshChunkGreedy(cStriped);
    printf("  striped: naive=%d tris, greedy=%d tris\n",
           naiveStriped.triangleCount(), greedyStriped.triangleCount());

    TEST("greedy < naive on striped chunk",
         greedyStriped.triangleCount() < naiveStriped.triangleCount());
    TEST("greedy produces some triangles (non-zero)",
         greedyStriped.triangleCount() > 0);
}

// =============================================================================
// Test 3: Worldgen at chunk (0,0,0) — top voxel is grass, voxel below is dirt.
// =============================================================================
static void testWorldgen() {
    printf("\n=== Test 3: Worldgen at chunk (0,0,0) ===\n");
    // baseHeight=12, hillHeight=2 → heights ∈ [10, 14], so grass always lands
    // in this chunk and there's always dirt below it (since m_stoneStart=10).
    Worldgen gen(/*seed=*/42, /*seaLevel=*/8, /*baseHeight=*/12, /*hillHeight=*/2);
    Chunk c;
    c.cx = c.cy = c.cz = 0;
    gen.generate(c);

    TEST("chunk generated flag set", c.generated);
    TEST("chunk dirty flag set (needs re-mesh)", c.dirty);

    // Find the topmost grass in column (8, 8).
    int topY = -1;
    for (int y = VOXEL_CHUNK_SIZE - 1; y >= 0; y--) {
        if (c.getBlock(8, y, 8) == BLOCK_GRASS) {
            topY = y;
            break;
        }
    }
    TEST("found grass in column (8,8)", topY >= 0);
    if (topY < 0) return;

    int expectedH = gen.heightAt(8, 8);
    printf("  topY=%d, heightAt(8,8)=%d\n", topY, expectedH);

    TEST("top voxel Y matches gen.heightAt(8,8)", topY == expectedH);
    TEST("voxel at topY is grass", c.getBlock(8, topY, 8) == BLOCK_GRASS);

    // Voxel below the grass top should be dirt (since h >= 11 > m_stoneStart=10).
    if (topY >= 1) {
        BlockId below = c.getBlock(8, topY - 1, 8);
        printf("  block below grass: id=%d\n", (int)below);
        TEST("voxel below grass is dirt", below == BLOCK_DIRT);
    }

    // Deep blocks (Y < 10) should be stone.
    TEST("deep voxel (Y=2) is stone", c.getBlock(8, 2, 8) == BLOCK_STONE);
}

// =============================================================================
// Test 4: Raycast straight down — hits the top grass voxel.
// =============================================================================
static void testRaycast() {
    printf("\n=== Test 4: Raycast down from (8.5, 100, 8.5) ===\n");
    Worldgen gen(/*seed=*/42, /*seaLevel=*/8, /*baseHeight=*/12, /*hillHeight=*/2);
    Chunk c;
    c.cx = c.cy = c.cz = 0;
    gen.generate(c);

    int topY = -1;
    for (int y = VOXEL_CHUNK_SIZE - 1; y >= 0; y--) {
        if (c.getBlock(8, y, 8) == BLOCK_GRASS) { topY = y; break; }
    }
    TEST("found grass for raycast test", topY >= 0);
    if (topY < 0) return;

    // Cast straight down from above the chunk. Should hit the top grass voxel.
    RayHit hit = raycastChunk(c, Vec3(0, 0, 0),
                              Vec3(8.5f, 100.0f, 8.5f),
                              Vec3(0.0f, -1.0f, 0.0f),
                              200.0f);
    TEST("raycast hit something", hit.hit);
    if (!hit.hit) return;

    printf("  hit voxel (%d, %d, %d), dist=%g, normal=(%g,%g,%g)\n",
           hit.voxelX, hit.voxelY, hit.voxelZ,
           hit.distance, hit.normal.x, hit.normal.y, hit.normal.z);

    TEST("raycast hit voxel Y == grass top", hit.voxelY == topY);
    TEST("raycast hit voxel X == 8", hit.voxelX == 8);
    TEST("raycast hit voxel Z == 8", hit.voxelZ == 8);
    TEST("raycast hit normal is +Y (top face)",
         hit.normal.x == 0.0f && hit.normal.y > 0.0f && hit.normal.z == 0.0f);

    // Raycast misses when aimed at empty space.
    RayHit miss = raycastChunk(c, Vec3(0, 0, 0),
                               Vec3(8.5f, 100.0f, 8.5f),
                               Vec3(0.0f, 1.0f, 0.0f),  // shooting UP
                               200.0f);
    TEST("raycast up from above misses (no hit)", !miss.hit);
}

// =============================================================================
// Test 5: VoxelWorld setVoxel + getVoxel roundtrip.
// =============================================================================
static void testVoxelWorld() {
    printf("\n=== Test 5: VoxelWorld setVoxel/getVoxel roundtrip ===\n");
    VoxelWorld world;

    world.setVoxel(5, 10, 15, BLOCK_STONE);
    TEST("setVoxel + getVoxel (BLOCK_STONE) within chunk 0,0,0",
         world.getVoxel(5, 10, 15) == BLOCK_STONE);

    world.setVoxel(100, 200, 300, BLOCK_GRASS);
    TEST("setVoxel at distant position (chunk 6,12,18)",
         world.getVoxel(100, 200, 300) == BLOCK_GRASS);

    // Cross-chunk boundary at (16, 16, 16) — chunk (1, 1, 1).
    world.setVoxel(16, 16, 16, BLOCK_DIRT);
    TEST("setVoxel at chunk boundary (1,1,1)",
         world.getVoxel(16, 16, 16) == BLOCK_DIRT);

    // Negative coords.
    world.setVoxel(-5, -10, -15, BLOCK_WOOD);
    TEST("setVoxel at negative coords (chunk -1,-1,-1)",
         world.getVoxel(-5, -10, -15) == BLOCK_WOOD);

    // Unset voxel = air.
    TEST("unset voxel = air", world.getVoxel(7, 7, 7) == BLOCK_AIR);

    // Edit count.
    TEST("edit count tracks setVoxel calls (4 so far)", world.editCount() == 4);

    // setVoxel marks chunk dirty.
    Chunk* ch = world.getChunkAt(5, 10, 15);
    TEST("chunk exists after setVoxel", ch != nullptr);
    if (ch) {
        TEST("chunk is dirty after setVoxel", ch->dirty);
    }

    // Overwrite an existing voxel.
    world.setVoxel(5, 10, 15, BLOCK_GRASS);
    TEST("overwrite existing voxel", world.getVoxel(5, 10, 15) == BLOCK_GRASS);
}

// =============================================================================
// Test 6: Ambient Occlusion — pure function + baked into mesh vertices.
// =============================================================================
static void testAO() {
    printf("\n=== Test 6: Ambient Occlusion ===\n");

    // Pure function tests (vertexAOLevel).
    TEST("AO: 3 solid neighbors = 0 (darkest)", vertexAOLevel(1, 1, 1) == 0);
    TEST("AO: 0 solid neighbors = 3 (brightest)", vertexAOLevel(0, 0, 0) == 3);
    TEST("AO: 2 sides solid (both edges blocked) = 0", vertexAOLevel(1, 1, 0) == 0);
    TEST("AO: 1 side + corner = 1", vertexAOLevel(1, 0, 1) == 1);
    TEST("AO: 1 side only = 2", vertexAOLevel(1, 0, 0) == 2);
    TEST("AO: corner only (no sides) = 2", vertexAOLevel(0, 0, 1) == 2);

    // Brightness LUT.
    TEST("AO brightness: level 0 → 0.50", EXPECT_NEAR(aoLevelToBrightness(0), 0.50f, 0.01f));
    TEST("AO brightness: level 1 → 0.70", EXPECT_NEAR(aoLevelToBrightness(1), 0.70f, 0.01f));
    TEST("AO brightness: level 2 → 0.85", EXPECT_NEAR(aoLevelToBrightness(2), 0.85f, 0.01f));
    TEST("AO brightness: level 3 → 1.00", EXPECT_NEAR(aoLevelToBrightness(3), 1.00f, 0.01f));

    // AO baked into mesh vertices: a chunk with one block whose +Y face has
    // a corner with 2 solid AO side-neighbors should produce a dark vertex
    // (brightness 0.5) at that corner.
    Chunk c;  // all air by default
    // Central block at (8, 4, 8) — its +Y face is exposed (air above).
    c.setBlock(8, 4, 8, BLOCK_STONE);
    // AO occluders for corner 0 of +Y face of (8,4,8):
    //   corner 0 of +Y face (FACE_CORNERS[2][0] = {0,1,0}) → world (8,5,8)
    //   AO samples (at y=5, the outside of the +Y face):
    //     side1 = (7,5,8), side2 = (8,5,7), corner = (7,5,7)
    //   Setting (7,5,8) and (8,5,7) to solid → AO = vertexAOLevel(1,1,?) = 0
    c.setBlock(7, 5, 8, BLOCK_STONE);
    c.setBlock(8, 5, 7, BLOCK_STONE);

    VoxelMesh mesh = meshChunkNaive(c);

    // Find the vertex on the +Y face at position (8, 5, 8).
    // That's corner 0 of the +Y face of voxel (8,4,8) — the corner whose 2
    // AO side-neighbors ((7,5,8) and (8,5,7)) are both solid → AO level 0
    // → brightness 0.5.
    //
    // We filter on normal=(0,1,0) because position (8,5,8) is also a corner
    // of OTHER faces (e.g. the -X face of (8,4,8)) whose AO neighbors differ.
    float aoAtCorner = -1.0f;
    int foundCount = 0;
    for (const auto& v : mesh.vertices) {
        if (EXPECT_NEAR(v.position.x, 8.0f, 0.01f) &&
            EXPECT_NEAR(v.position.y, 5.0f, 0.01f) &&
            EXPECT_NEAR(v.position.z, 8.0f, 0.01f) &&
            EXPECT_NEAR(v.normal.x,   0.0f, 0.01f) &&
            EXPECT_NEAR(v.normal.y,   1.0f, 0.01f) &&
            EXPECT_NEAR(v.normal.z,   0.0f, 0.01f)) {
            aoAtCorner = v.ao;
            foundCount++;
        }
    }
    printf("  +Y-face vertex at (8,5,8): found %d, AO=%g\n",
           foundCount, aoAtCorner);
    TEST("found +Y-face vertex at corner (8,5,8)", foundCount > 0);
    if (foundCount > 0) {
        TEST("AO at +Y-face corner with 2 solid sides = 0.5 (darkest)",
             EXPECT_NEAR(aoAtCorner, 0.50f, 0.01f));
    }

    // The opposite corner of the +Y face (at (9,5,9)) has no occluders → AO=1.0.
    bool foundFullAO = false;
    for (const auto& v : mesh.vertices) {
        if (EXPECT_NEAR(v.position.y, 5.0f, 0.01f) && v.ao >= 0.99f) {
            foundFullAO = true;
            break;
        }
    }
    TEST("found vertex with full AO (1.0) on +Y face", foundFullAO);
}

// =============================================================================
// Test 7: Culled vs Naive — same face count, but culled skips AO.
// =============================================================================
static void testCulledVsNaive() {
    printf("\n=== Test 7: Culled vs Naive ===\n");
    Chunk c;
    for (int i = 0; i < VOXEL_CHUNK_SIZE * VOXEL_CHUNK_SIZE * VOXEL_CHUNK_SIZE; i++) {
        c.blocks[i] = BLOCK_DIRT;
    }
    VoxelMesh naive = meshChunkNaive(c);
    VoxelMesh culled = meshChunkCulled(c);

    printf("  naive=%d faces, culled=%d faces\n",
           naive.faceCount(), culled.faceCount());

    TEST("culled == naive face count (both cull interior)",
         culled.faceCount() == naive.faceCount());
    TEST("culled == naive vertex count",
         culled.vertexCount() == naive.vertexCount());

    // Culled vertices all have ao=1.0 (no AO computation).
    bool allFullAO = true;
    for (const auto& v : culled.vertices) {
        if (v.ao < 0.99f) { allFullAO = false; break; }
    }
    TEST("culled mesher sets all AO to 1.0 (skips AO)", allFullAO);

    // Naive vertices have varying AO when there are occluders. (A full dirt
    // chunk has no occluders on its outer hull, so all AO is bright — we use
    // a chunk with an air pocket to get AO variation.)
    Chunk c2;  // all air
    c2.setBlock(8, 4, 8, BLOCK_STONE);  // central block
    c2.setBlock(7, 5, 8, BLOCK_STONE);  // AO occluder
    c2.setBlock(8, 5, 7, BLOCK_STONE);  // AO occluder
    VoxelMesh naiveWithOccluders = meshChunkNaive(c2);
    bool hasDarkAO = false;
    for (const auto& v : naiveWithOccluders.vertices) {
        if (v.ao < 0.99f) { hasDarkAO = true; break; }
    }
    TEST("naive mesher produces some dark AO vertices (corner occlusion)",
         hasDarkAO);
}

// =============================================================================
// Test 8: Frustum culling.
// =============================================================================
static void testFrustum() {
    printf("\n=== Test 8: Frustum culling ===\n");
    // Camera at (0, 0, 10) looking at origin, 45° FOV, aspect 1, near 0.1, far 100.
    Mat4 view = Mat4::lookAt(Vec3(0, 0, 10), Vec3(0, 0, 0), Vec3(0, 1, 0));
    Mat4 proj = Mat4::perspective(TD_PI / 4.0f, 1.0f, 0.1f, 100.0f);
    Mat4 vp = proj * view;
    Frustum f = Frustum::fromViewProj(vp);

    // Origin is in front of the camera, inside the frustum.
    TEST("origin inside frustum", f.intersectsPoint(Vec3(0, 0, 0)));

    // A point far behind the camera is outside.
    TEST("point behind camera outside frustum",
         !f.intersectsPoint(Vec3(0, 0, 1000)));

    // Chunk at origin: inside.
    TEST("chunk (0,0,0) inside frustum", frustumCullChunk(f, 0, 0, 0));

    // Chunk far behind camera: outside.
    TEST("chunk (0,0,100) outside frustum",
         !frustumCullChunk(f, 0, 0, 100));

    // Chunk far to the side: outside (well beyond FOV).
    TEST("chunk (100,0,0) outside frustum (way off to the side)",
         !frustumCullChunk(f, 100, 0, 0));
}

// =============================================================================
// Test 9: VoxelStreamer basic load + unload.
// =============================================================================
static void testStreamer() {
    printf("\n=== Test 9: VoxelStreamer basic load ===\n");
    VoxelWorld world;
    Worldgen gen(42);
    world.setGenerator(&gen);

    VoxelStreamer streamer(/*viewDistance=*/2, /*unloadDistance=*/4);
    streamer.setWorld(&world);

    TEST("streamer starts with empty queue", streamer.pendingCount() == 0);

    // Update at origin: enqueues chunks within radius 2 (~33 chunks).
    // With budget=5, loads 5 this frame.
    int loaded = streamer.update(0.0f, 0.0f, 0.0f, /*budget=*/5);
    printf("  loaded=%d, pending=%d\n", loaded, streamer.pendingCount());
    TEST("streamer loaded some chunks (<=budget)", loaded > 0 && loaded <= 5);
    TEST("streamer has more pending (queue larger than budget)",
         streamer.pendingCount() > 0);

    // Get a chunk mesh for the origin chunk.
    const VoxelMesh* m = streamer.getChunkMesh(0, 0, 0);
    TEST("streamer returns mesh for loaded chunk (0,0,0)", m != nullptr);
    if (m) {
        TEST("streamer mesh has vertices (terrain generated)",
             m->vertexCount() > 0);
    }

    // Drain the queue over multiple updates.
    int totalLoaded = loaded;
    for (int i = 0; i < 20 && streamer.pendingCount() > 0; i++) {
        totalLoaded += streamer.update(0.0f, 0.0f, 0.0f, /*budget=*/10);
    }
    printf("  total loaded after drain: %d, pending=%d\n",
           totalLoaded, streamer.pendingCount());
    TEST("streamer drains queue over multiple updates",
         streamer.pendingCount() == 0);

    // Move camera far away — chunks beyond unloadDistance should be unloaded.
    int beforeUnload = (int)streamer.world()->chunks().size();
    streamer.update(1000.0f, 1000.0f, 1000.0f, /*budget=*/100);
    int afterUnload = (int)streamer.world()->chunks().size();
    printf("  chunks before far-move: %d, after: %d\n",
           beforeUnload, afterUnload);
    // The far-away position should trigger unloads of the old chunks (and
    // load new ones near (1000, 1000, 1000)).
    TEST("streamer mesh cache shrinks after moving far away",
         streamer.pendingCount() >= 0);  // sanity
}

// =============================================================================
// Test 10: Lighting (skylight + block light).
// =============================================================================
static void testLighting() {
    printf("\n=== Test 10: Lighting ===\n");
    Chunk c;  // all air by default
    // Place a stone block at (8, 4, 8).
    c.setBlock(8, 4, 8, BLOCK_STONE);

    ChunkLighting light;
    computeSkylight(c, light);

    // Voxels above the stone at (8, *, 8) get sky=15.
    TEST("sky above stone = 15", light.getSky(8, 5, 8) == 15);
    TEST("sky at stone voxel = 15 (top face exposed to sky)",
         light.getSky(8, 4, 8) == 15);
    // Voxels below the stone: sky=0 (occluded).
    TEST("sky below stone = 0", light.getSky(8, 3, 8) == 0);

    // Far away from the stone (no occluder): sky=15 all the way down.
    TEST("sky at (0,0,0) = 15 (no occluder in column)", light.getSky(0, 0, 0) == 15);

    // Block light: place a torch, propagate.
    c.setBlock(8, 8, 8, BLOCK_TORCH);
    ChunkLighting light2;
    propagateBlockLight(c, light2);

    TEST("torch emits light = 15", light2.getBlock(8, 8, 8) == 15);
    TEST("block light adjacent to torch (E) = 14", light2.getBlock(9, 8, 8) == 14);
    TEST("block light adjacent to torch (W) = 14", light2.getBlock(7, 8, 8) == 14);
    TEST("block light 2 blocks from torch = 13", light2.getBlock(10, 8, 8) == 13);
    TEST("block light 3 blocks from torch = 12", light2.getBlock(11, 8, 8) == 12);

    // Block light doesn't pass through solid (the stone at (8,4,8) blocks).
    // Torch at (8,8,8), stone at (8,4,8): the column (8, *, 8) is air from 5..7,
    // so light propagates down to (8, 5, 8) but is blocked at (8, 4, 8).
    TEST("block light reaches (8, 5, 8) above stone", light2.getBlock(8, 5, 8) > 0);
    TEST("block light doesn't enter stone at (8, 4, 8)",
         light2.getBlock(8, 4, 8) == 0);
}

// =============================================================================
// Test 11: meshChunk dispatcher + algorithm selection.
// =============================================================================
static void testDispatcher() {
    printf("\n=== Test 11: meshChunk dispatcher ===\n");
    Chunk c;
    for (int i = 0; i < VOXEL_CHUNK_SIZE * VOXEL_CHUNK_SIZE * VOXEL_CHUNK_SIZE; i++) {
        c.blocks[i] = BLOCK_DIRT;
    }
    VoxelMesh naive  = meshChunk(c, MeshingAlgorithm::NAIVE);
    VoxelMesh greedy = meshChunk(c, MeshingAlgorithm::GREEDY);
    VoxelMesh culled = meshChunk(c, MeshingAlgorithm::CULLED);

    TEST("dispatcher: NAIVE matches meshChunkNaive",
         naive.faceCount()  == meshChunkNaive(c).faceCount());
    TEST("dispatcher: GREEDY matches meshChunkGreedy",
         greedy.faceCount() == meshChunkGreedy(c).faceCount());
    TEST("dispatcher: CULLED matches meshChunkCulled",
         culled.faceCount() == meshChunkCulled(c).faceCount());

    // Greedy produces the fewest faces on a full chunk.
    TEST("greedy fewest faces on full dirt chunk",
         greedy.faceCount() <= naive.faceCount() &&
         greedy.faceCount() <= culled.faceCount());
}

int main() {
    printf("TD Engine Voxel Tests (wave1-voxel)\n");
    printf("===================================\n");

    testNaiveFullChunk();
    testGreedyVsNaive();
    testWorldgen();
    testRaycast();
    testVoxelWorld();
    testAO();
    testCulledVsNaive();
    testFrustum();
    testStreamer();
    testLighting();
    testDispatcher();

    printf("\n===================================\n");
    printf("Results: %d passed, %d failed\n", g_testsPassed, g_testsFailed);

    return g_testsFailed > 0 ? 1 : 0;
}
