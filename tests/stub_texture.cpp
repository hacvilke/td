// Stub for td::Texture methods.
//
// The real Texture in src/renderer/texture.cpp uses GL function pointers.
// The UI test never actually uploads any pixel data — it only exercises
// layout/hit-test/input. These stubs let the test link without GL.
#include "../src/renderer/texture.h"

namespace td {

Texture::~Texture() = default;

bool Texture::create(const TextureConfig& config, const unsigned char*) {
    m_config = config;
    m_textureID = 1;  // fake non-zero so isValid() returns true
    return true;
}

bool Texture::createEmpty(int w, int h, int c) {
    m_config.width = w;
    m_config.height = h;
    m_config.channels = c;
    m_textureID = 1;
    return true;
}

void Texture::destroy() { m_textureID = 0; }
void Texture::bind(int) const {}
void Texture::unbind() const {}
void Texture::updateRegion(int, int, int, int, const unsigned char*) {}
void Texture::updateFull(const unsigned char*) {}

} // namespace td
