#pragma once
#include <cstdint>

namespace td {

enum class TextureFilter { 
    Nearest, 
    Linear,
    NearestMipmap,
    LinearMipmap
};

enum class TextureWrap { 
    Repeat, 
    ClampToEdge,
    MirroredRepeat
};

struct TextureConfig {
    int width = 0;
    int height = 0;
    int channels = 4;
    TextureFilter minFilter = TextureFilter::Linear;
    TextureFilter magFilter = TextureFilter::Linear;
    TextureWrap wrapS = TextureWrap::ClampToEdge;
    TextureWrap wrapT = TextureWrap::ClampToEdge;
    bool generateMipmaps = false;
};

class Texture {
public:
    Texture() = default;
    ~Texture();
    
    bool create(const TextureConfig& config, const unsigned char* pixels);
    bool createEmpty(int width, int height, int channels = 4);
    void destroy();
    
    void bind(int slot = 0) const;
    void unbind() const;
    
    void updateRegion(int x, int y, int width, int height, const unsigned char* pixels);
    void updateFull(const unsigned char* pixels);
    
    int getWidth() const { return m_config.width; }
    int getHeight() const { return m_config.height; }
    int getChannels() const { return m_config.channels; }
    uint32_t getID() const { return m_textureID; }
    bool isValid() const { return m_textureID != 0; }
    
    const TextureConfig& getConfig() const { return m_config; }
    
private:
    uint32_t m_textureID = 0;
    TextureConfig m_config;
};

} // namespace td
