#include "texture.h"
#include "gl_renderer.h"
#include "../core/logger.h"

namespace td {

Texture::~Texture() {
    destroy();
}

bool Texture::create(const TextureConfig& config, const unsigned char* pixels) {
    destroy();
    
    m_config = config;
    
    gl.glGenTextures(1, &m_textureID);
    gl.glBindTexture(GL_TEXTURE_2D, m_textureID);
    
    // Set texture parameters
    GLenum minFilter, magFilter;
    
    switch (config.minFilter) {
        case TextureFilter::Nearest:
            minFilter = GL_NEAREST;
            break;
        case TextureFilter::Linear:
            minFilter = GL_LINEAR;
            break;
        case TextureFilter::NearestMipmap:
            minFilter = GL_NEAREST_MIPMAP_LINEAR;
            break;
        case TextureFilter::LinearMipmap:
            minFilter = GL_LINEAR_MIPMAP_LINEAR;
            break;
        default:
            minFilter = GL_LINEAR;
    }
    
    switch (config.magFilter) {
        case TextureFilter::Nearest:
            magFilter = GL_NEAREST;
            break;
        default:
            magFilter = GL_LINEAR;
    }
    
    GLenum wrapS, wrapT;
    
    switch (config.wrapS) {
        case TextureWrap::Repeat:
            wrapS = GL_REPEAT;
            break;
        case TextureWrap::ClampToEdge:
            wrapS = GL_CLAMP_TO_EDGE;
            break;
        case TextureWrap::MirroredRepeat:
            wrapS = 0x8370; // GL_MIRRORED_REPEAT
            break;
        default:
            wrapS = GL_CLAMP_TO_EDGE;
    }
    
    switch (config.wrapT) {
        case TextureWrap::Repeat:
            wrapT = GL_REPEAT;
            break;
        case TextureWrap::ClampToEdge:
            wrapT = GL_CLAMP_TO_EDGE;
            break;
        case TextureWrap::MirroredRepeat:
            wrapT = 0x8370; // GL_MIRRORED_REPEAT
            break;
        default:
            wrapT = GL_CLAMP_TO_EDGE;
    }
    
    gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilter);
    gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magFilter);
    gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapS);
    gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapT);
    
    // Determine format
    GLenum internalFormat, format;
    switch (config.channels) {
        case 1:
            internalFormat = 0x8229; // GL_R8
            format = 0x1903; // GL_RED
            break;
        case 2:
            internalFormat = 0x822B; // GL_RG8
            format = 0x8227; // GL_RG
            break;
        case 3:
            internalFormat = GL_RGB8;
            format = GL_RGB;
            break;
        case 4:
        default:
            internalFormat = GL_RGBA8;
            format = GL_RGBA;
            break;
    }
    
    // Upload texture data
    gl.glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, 
                    config.width, config.height, 0,
                    format, GL_UNSIGNED_BYTE, pixels);
    
    // Generate mipmaps if requested
    if (config.generateMipmaps && gl.glGenerateMipmap) {
        gl.glGenerateMipmap(GL_TEXTURE_2D);
    }
    
    gl.glBindTexture(GL_TEXTURE_2D, 0);
    
    TD_LOG_INFO("Created texture %u (%dx%d, %d channels)", 
                m_textureID, config.width, config.height, config.channels);
    
    return true;
}

bool Texture::createEmpty(int width, int height, int channels) {
    TextureConfig config;
    config.width = width;
    config.height = height;
    config.channels = channels;
    return create(config, nullptr);
}

void Texture::destroy() {
    if (m_textureID) {
        gl.glDeleteTextures(1, &m_textureID);
        m_textureID = 0;
    }
}

void Texture::bind(int slot) const {
    gl.glActiveTexture(GL_TEXTURE0 + slot);
    gl.glBindTexture(GL_TEXTURE_2D, m_textureID);
}

void Texture::unbind() const {
    gl.glBindTexture(GL_TEXTURE_2D, 0);
}

void Texture::updateRegion(int x, int y, int width, int height, const unsigned char* pixels) {
    if (!m_textureID) return;
    
    gl.glBindTexture(GL_TEXTURE_2D, m_textureID);
    
    GLenum format;
    switch (m_config.channels) {
        case 1: format = 0x1903; break; // GL_RED
        case 2: format = 0x8227; break; // GL_RG
        case 3: format = GL_RGB; break;
        case 4: 
        default: format = GL_RGBA; break;
    }
    
    gl.glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, width, height, 
                       format, GL_UNSIGNED_BYTE, pixels);
    
    gl.glBindTexture(GL_TEXTURE_2D, 0);
}

void Texture::updateFull(const unsigned char* pixels) {
    updateRegion(0, 0, m_config.width, m_config.height, pixels);
}

} // namespace td
