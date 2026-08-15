// Stub for td::SpriteBatch methods that the UI test exercises.
//
// The real SpriteBatch in src/renderer/sprite_batch.cpp uses OpenGL function
// pointers (via the global `td::GLFunctions gl;`) which are not available on
// Linux CI without a GL context. The UI test exercises layout / hit-testing /
// input dispatch only — it never actually renders. These stubs let the test
// link without dragging in the GL renderer.
#include "../src/renderer/sprite_batch.h"

namespace td {

bool SpriteBatch::init() { return true; }
void SpriteBatch::shutdown() {}
void SpriteBatch::begin(const Mat4&, const Mat4&) {}
void SpriteBatch::draw(const SpriteData&, const Texture*) {}
void SpriteBatch::drawBatch(const SpriteData*, int, const Texture*) {}
void SpriteBatch::drawQuad(float, float, float, float,
                           float, float, float, float,
                           const Texture*) {}
void SpriteBatch::end() {}
void SpriteBatch::flush() {}
void SpriteBatch::flushSorted() {}

void SpriteBatch::expandSpriteToVertices(const SpriteData&, SpriteVertex*) {}

} // namespace td
