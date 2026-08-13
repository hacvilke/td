#pragma once
#include <cstdint>

namespace td {

class Framebuffer {
public:
    Framebuffer() = default;
    ~Framebuffer();
    
    bool create(int width, int height, bool hasDepthBuffer = true);
    void destroy();
    
    void bind() const;
    void unbind() const; // Binds default framebuffer (0)
    
    uint32_t getTextureID() const { return m_colorTexture; }
    uint32_t getDepthTextureID() const { return m_depthTexture; }
    uint32_t getFBO() const { return m_fbo; }
    
    int getWidth() const { return m_width; }
    int getHeight() const { return m_height; }
    
    bool resize(int width, int height);
    bool isValid() const { return m_fbo != 0; }
    
    // Read pixels from framebuffer
    void readPixels(int x, int y, int width, int height, 
                    unsigned char* outPixels) const;
    
private:
    bool createAttachments();
    void destroyAttachments();
    
    uint32_t m_fbo = 0;
    uint32_t m_colorTexture = 0;
    uint32_t m_depthTexture = 0;
    uint32_t m_depthRenderbuffer = 0;
    int m_width = 0;
    int m_height = 0;
    bool m_hasDepth = true;
    bool m_useDepthTexture = false;
};

} // namespace td
