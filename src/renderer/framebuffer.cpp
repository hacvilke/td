#include "framebuffer.h"
#include "gl_renderer.h"
#include "../core/logger.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace td {

Framebuffer::~Framebuffer() {
    destroy();
}

bool Framebuffer::create(int width, int height, bool hasDepthBuffer) {
    destroy();
    
    m_width = width;
    m_height = height;
    m_hasDepth = hasDepthBuffer;
    
    // Create framebuffer object
    gl.glGenFramebuffers(1, &m_fbo);
    gl.glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    
    // Create attachments
    if (!createAttachments()) {
        destroy();
        return false;
    }
    
    // Check framebuffer status
    GLenum status = gl.glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        TD_LOG_ERROR("Framebuffer incomplete: 0x%X", status);
        destroy();
        return false;
    }
    
    gl.glBindFramebuffer(GL_FRAMEBUFFER, 0);
    
    TD_LOG_INFO("Created framebuffer %u (%dx%d)", m_fbo, width, height);
    return true;
}

bool Framebuffer::createAttachments() {
    // Create color texture
    gl.glGenTextures(1, &m_colorTexture);
    gl.glBindTexture(GL_TEXTURE_2D, m_colorTexture);
    gl.glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_width, m_height, 0, 
                    GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    // Attach color texture
    gl.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, 
                              GL_TEXTURE_2D, m_colorTexture, 0);
    
    // Create depth buffer
    if (m_hasDepth) {
        if (m_useDepthTexture) {
            // Create depth texture
            gl.glGenTextures(1, &m_depthTexture);
            gl.glBindTexture(GL_TEXTURE_2D, m_depthTexture);
            gl.glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, 
                            m_width, m_height, 0,
                            GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
            gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            
            gl.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                      GL_TEXTURE_2D, m_depthTexture, 0);
        } else {
            // Create depth renderbuffer
            gl.glGenRenderbuffers(1, &m_depthRenderbuffer);
            gl.glBindRenderbuffer(GL_RENDERBUFFER, m_depthRenderbuffer);
            gl.glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, 
                                     m_width, m_height);
            gl.glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                         GL_RENDERBUFFER, m_depthRenderbuffer);
        }
    }
    
    gl.glBindTexture(GL_TEXTURE_2D, 0);
    
    return true;
}

void Framebuffer::destroyAttachments() {
    if (m_colorTexture) {
        gl.glDeleteTextures(1, &m_colorTexture);
        m_colorTexture = 0;
    }
    if (m_depthTexture) {
        gl.glDeleteTextures(1, &m_depthTexture);
        m_depthTexture = 0;
    }
    if (m_depthRenderbuffer) {
        gl.glDeleteRenderbuffers(1, &m_depthRenderbuffer);
        m_depthRenderbuffer = 0;
    }
}

void Framebuffer::destroy() {
    destroyAttachments();
    
    if (m_fbo) {
        gl.glDeleteFramebuffers(1, &m_fbo);
        m_fbo = 0;
    }
    
    m_width = 0;
    m_height = 0;
}

void Framebuffer::bind() const {
    gl.glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    gl.glViewport(0, 0, m_width, m_height);
}

void Framebuffer::unbind() const {
    gl.glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

bool Framebuffer::resize(int width, int height) {
    if (width == m_width && height == m_height) {
        return true;
    }
    
    m_width = width;
    m_height = height;
    
    gl.glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    
    destroyAttachments();
    
    if (!createAttachments()) {
        return false;
    }
    
    GLenum status = gl.glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        TD_LOG_ERROR("Framebuffer incomplete after resize: 0x%X", status);
        return false;
    }
    
    gl.glBindFramebuffer(GL_FRAMEBUFFER, 0);
    
    return true;
}

void Framebuffer::readPixels(int x, int y, int width, int height,
                              unsigned char* outPixels) const {
    gl.glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    
    // Use glReadPixels - need to add this to GLFunctions
    typedef void (*PFNGLREADPIXELSPROC)(GLint, GLint, GLsizei, GLsizei, 
                                         GLenum, GLenum, void*);
    static PFNGLREADPIXELSPROC glReadPixels = nullptr;
    if (!glReadPixels) {
        glReadPixels = (PFNGLREADPIXELSPROC)GetProcAddress(
            GetModuleHandle("opengl32.dll"), "glReadPixels");
    }
    
    if (glReadPixels) {
        glReadPixels(x, y, width, height, GL_RGBA, GL_UNSIGNED_BYTE, outPixels);
    }
    
    gl.glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

} // namespace td
