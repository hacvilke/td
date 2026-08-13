#include "gl_renderer.h"
#include "../core/logger.h"
#include <cstring>

#ifdef __EMSCRIPTEN__
// WebAssembly build: Emscripten provides GL functions as statically-linked
// symbols from <GLES3/gl3.h> when the project is linked with -s USE_WEBGL2=1.
// We populate the GLFunctions struct by taking the address of each symbol.
#include <GLES3/gl3.h>
#else
// Desktop (Win32) build: load GL function pointers at runtime via
// wglGetProcAddress / GetProcAddress(opengl32.dll).
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <GL/gl.h>
#endif

namespace td {

GLFunctions gl;
Renderer Renderer::s_instance;

Renderer& Renderer::get() {
    return s_instance;
}

#ifdef __EMSCRIPTEN__
// -----------------------------------------------------------------------------
// WebAssembly: GL functions are real symbols provided by Emscripten's GL shim.
// We just take their addresses. No runtime loading needed.
// -----------------------------------------------------------------------------
#define EM_ASSIGN(name) gl.name = (decltype(gl.name))::name;

bool Renderer::loadGLFunctions() {
    // Basic
    EM_ASSIGN(glViewport);
    EM_ASSIGN(glClear);
    EM_ASSIGN(glClearColor);
    EM_ASSIGN(glEnable);
    EM_ASSIGN(glDisable);
    EM_ASSIGN(glBlendFunc);
    EM_ASSIGN(glDepthFunc);
    EM_ASSIGN(glCullFace);
    EM_ASSIGN(glFrontFace);
    EM_ASSIGN(glDepthMask);
    // glPolygonMode is not available in WebGL - leave null (setWireframe no-ops).
    gl.glPolygonMode = nullptr;
    EM_ASSIGN(glLineWidth);
    EM_ASSIGN(glGetString);
    EM_ASSIGN(glGetIntegerv);
    EM_ASSIGN(glGetError);

    // Textures
    EM_ASSIGN(glGenTextures);
    EM_ASSIGN(glDeleteTextures);
    EM_ASSIGN(glBindTexture);
    EM_ASSIGN(glTexImage2D);
    EM_ASSIGN(glTexSubImage2D);
    EM_ASSIGN(glTexParameteri);
    EM_ASSIGN(glTexParameterfv);
    EM_ASSIGN(glActiveTexture);
    EM_ASSIGN(glGenerateMipmap);

    // Buffers
    EM_ASSIGN(glGenBuffers);
    EM_ASSIGN(glDeleteBuffers);
    EM_ASSIGN(glBindBuffer);
    EM_ASSIGN(glBufferData);
    EM_ASSIGN(glBufferSubData);
    // glMapBuffer / glUnmapBuffer are not available in WebGL 1; in WebGL 2 they
    // exist as glMapBufferRange / glUnmapBuffer. Leave them null for now - the
    // engine doesn't use them on the sprite/mesh paths.
    gl.glMapBuffer   = nullptr;
    gl.glUnmapBuffer = nullptr;

    // Vertex Arrays
    EM_ASSIGN(glGenVertexArrays);
    EM_ASSIGN(glDeleteVertexArrays);
    EM_ASSIGN(glBindVertexArray);
    EM_ASSIGN(glEnableVertexAttribArray);
    EM_ASSIGN(glDisableVertexAttribArray);
    EM_ASSIGN(glVertexAttribPointer);
    EM_ASSIGN(glVertexAttribIPointer);

    // Drawing
    EM_ASSIGN(glDrawArrays);
    EM_ASSIGN(glDrawElements);
    EM_ASSIGN(glDrawArraysInstanced);
    EM_ASSIGN(glDrawElementsInstanced);

    // Shaders
    EM_ASSIGN(glCreateShader);
    EM_ASSIGN(glDeleteShader);
    EM_ASSIGN(glShaderSource);
    EM_ASSIGN(glCompileShader);
    EM_ASSIGN(glGetShaderiv);
    EM_ASSIGN(glGetShaderInfoLog);

    // Programs
    EM_ASSIGN(glCreateProgram);
    EM_ASSIGN(glDeleteProgram);
    EM_ASSIGN(glAttachShader);
    EM_ASSIGN(glDetachShader);
    EM_ASSIGN(glLinkProgram);
    EM_ASSIGN(glUseProgram);
    EM_ASSIGN(glGetProgramiv);
    EM_ASSIGN(glGetProgramInfoLog);

    // Uniforms
    EM_ASSIGN(glGetUniformLocation);
    EM_ASSIGN(glUniform1f);
    EM_ASSIGN(glUniform2f);
    EM_ASSIGN(glUniform3f);
    EM_ASSIGN(glUniform4f);
    EM_ASSIGN(glUniform1i);
    EM_ASSIGN(glUniform2i);
    EM_ASSIGN(glUniform3i);
    EM_ASSIGN(glUniform4i);
    EM_ASSIGN(glUniform1fv);
    EM_ASSIGN(glUniform2fv);
    EM_ASSIGN(glUniform3fv);
    EM_ASSIGN(glUniform4fv);
    EM_ASSIGN(glUniformMatrix3fv);
    EM_ASSIGN(glUniformMatrix4fv);

    // Framebuffers
    EM_ASSIGN(glGenFramebuffers);
    EM_ASSIGN(glDeleteFramebuffers);
    EM_ASSIGN(glBindFramebuffer);
    EM_ASSIGN(glFramebufferTexture2D);
    EM_ASSIGN(glCheckFramebufferStatus);

    // Renderbuffers
    EM_ASSIGN(glGenRenderbuffers);
    EM_ASSIGN(glDeleteRenderbuffers);
    EM_ASSIGN(glBindRenderbuffer);
    EM_ASSIGN(glRenderbufferStorage);
    EM_ASSIGN(glFramebufferRenderbuffer);

    // Verify critical functions loaded
    if (!gl.glGenBuffers || !gl.glGenVertexArrays || !gl.glCreateShader || !gl.glCreateProgram) {
        TD_LOG_ERROR("Failed to load required OpenGL functions");
        return false;
    }
    return true;
}

#undef EM_ASSIGN

#else // !__EMSCRIPTEN__

// Helper macro for loading GL functions (Win32 desktop)
#define LOAD_GL_FUNC(name) \
    gl.name = (decltype(gl.name))wglGetProcAddress(#name); \
    if (!gl.name) { \
        gl.name = (decltype(gl.name))GetProcAddress(GetModuleHandle("opengl32.dll"), #name); \
    }

bool Renderer::loadGLFunctions() {
    // Basic GL functions (some are in opengl32.dll directly)
    gl.glViewport = (decltype(gl.glViewport))GetProcAddress(GetModuleHandle("opengl32.dll"), "glViewport");
    gl.glClear = (decltype(gl.glClear))GetProcAddress(GetModuleHandle("opengl32.dll"), "glClear");
    gl.glClearColor = (decltype(gl.glClearColor))GetProcAddress(GetModuleHandle("opengl32.dll"), "glClearColor");
    gl.glEnable = (decltype(gl.glEnable))GetProcAddress(GetModuleHandle("opengl32.dll"), "glEnable");
    gl.glDisable = (decltype(gl.glDisable))GetProcAddress(GetModuleHandle("opengl32.dll"), "glDisable");
    gl.glBlendFunc = (decltype(gl.glBlendFunc))GetProcAddress(GetModuleHandle("opengl32.dll"), "glBlendFunc");
    gl.glDepthFunc = (decltype(gl.glDepthFunc))GetProcAddress(GetModuleHandle("opengl32.dll"), "glDepthFunc");
    gl.glCullFace = (decltype(gl.glCullFace))GetProcAddress(GetModuleHandle("opengl32.dll"), "glCullFace");
    gl.glFrontFace = (decltype(gl.glFrontFace))GetProcAddress(GetModuleHandle("opengl32.dll"), "glFrontFace");
    gl.glDepthMask = (decltype(gl.glDepthMask))GetProcAddress(GetModuleHandle("opengl32.dll"), "glDepthMask");
    gl.glPolygonMode = (decltype(gl.glPolygonMode))GetProcAddress(GetModuleHandle("opengl32.dll"), "glPolygonMode");
    gl.glLineWidth = (decltype(gl.glLineWidth))GetProcAddress(GetModuleHandle("opengl32.dll"), "glLineWidth");
    gl.glGetString = (decltype(gl.glGetString))GetProcAddress(GetModuleHandle("opengl32.dll"), "glGetString");
    gl.glGetIntegerv = (decltype(gl.glGetIntegerv))GetProcAddress(GetModuleHandle("opengl32.dll"), "glGetIntegerv");
    gl.glGetError = (decltype(gl.glGetError))GetProcAddress(GetModuleHandle("opengl32.dll"), "glGetError");
    
    // Textures
    gl.glGenTextures = (decltype(gl.glGenTextures))GetProcAddress(GetModuleHandle("opengl32.dll"), "glGenTextures");
    gl.glDeleteTextures = (decltype(gl.glDeleteTextures))GetProcAddress(GetModuleHandle("opengl32.dll"), "glDeleteTextures");
    gl.glBindTexture = (decltype(gl.glBindTexture))GetProcAddress(GetModuleHandle("opengl32.dll"), "glBindTexture");
    gl.glTexImage2D = (decltype(gl.glTexImage2D))GetProcAddress(GetModuleHandle("opengl32.dll"), "glTexImage2D");
    gl.glTexSubImage2D = (decltype(gl.glTexSubImage2D))GetProcAddress(GetModuleHandle("opengl32.dll"), "glTexSubImage2D");
    gl.glTexParameteri = (decltype(gl.glTexParameteri))GetProcAddress(GetModuleHandle("opengl32.dll"), "glTexParameteri");
    gl.glTexParameterfv = (decltype(gl.glTexParameterfv))GetProcAddress(GetModuleHandle("opengl32.dll"), "glTexParameterfv");
    
    // Extension functions via wglGetProcAddress
    LOAD_GL_FUNC(glActiveTexture);
    LOAD_GL_FUNC(glGenerateMipmap);
    
    // Buffers
    LOAD_GL_FUNC(glGenBuffers);
    LOAD_GL_FUNC(glDeleteBuffers);
    LOAD_GL_FUNC(glBindBuffer);
    LOAD_GL_FUNC(glBufferData);
    LOAD_GL_FUNC(glBufferSubData);
    LOAD_GL_FUNC(glMapBuffer);
    LOAD_GL_FUNC(glUnmapBuffer);
    
    // Vertex Arrays
    LOAD_GL_FUNC(glGenVertexArrays);
    LOAD_GL_FUNC(glDeleteVertexArrays);
    LOAD_GL_FUNC(glBindVertexArray);
    LOAD_GL_FUNC(glEnableVertexAttribArray);
    LOAD_GL_FUNC(glDisableVertexAttribArray);
    LOAD_GL_FUNC(glVertexAttribPointer);
    LOAD_GL_FUNC(glVertexAttribIPointer);
    
    // Drawing
    gl.glDrawArrays = (decltype(gl.glDrawArrays))GetProcAddress(GetModuleHandle("opengl32.dll"), "glDrawArrays");
    gl.glDrawElements = (decltype(gl.glDrawElements))GetProcAddress(GetModuleHandle("opengl32.dll"), "glDrawElements");
    LOAD_GL_FUNC(glDrawArraysInstanced);
    LOAD_GL_FUNC(glDrawElementsInstanced);
    
    // Shaders
    LOAD_GL_FUNC(glCreateShader);
    LOAD_GL_FUNC(glDeleteShader);
    LOAD_GL_FUNC(glShaderSource);
    LOAD_GL_FUNC(glCompileShader);
    LOAD_GL_FUNC(glGetShaderiv);
    LOAD_GL_FUNC(glGetShaderInfoLog);
    
    // Programs
    LOAD_GL_FUNC(glCreateProgram);
    LOAD_GL_FUNC(glDeleteProgram);
    LOAD_GL_FUNC(glAttachShader);
    LOAD_GL_FUNC(glDetachShader);
    LOAD_GL_FUNC(glLinkProgram);
    LOAD_GL_FUNC(glUseProgram);
    LOAD_GL_FUNC(glGetProgramiv);
    LOAD_GL_FUNC(glGetProgramInfoLog);
    
    // Uniforms
    LOAD_GL_FUNC(glGetUniformLocation);
    LOAD_GL_FUNC(glUniform1f);
    LOAD_GL_FUNC(glUniform2f);
    LOAD_GL_FUNC(glUniform3f);
    LOAD_GL_FUNC(glUniform4f);
    LOAD_GL_FUNC(glUniform1i);
    LOAD_GL_FUNC(glUniform2i);
    LOAD_GL_FUNC(glUniform3i);
    LOAD_GL_FUNC(glUniform4i);
    LOAD_GL_FUNC(glUniform1fv);
    LOAD_GL_FUNC(glUniform2fv);
    LOAD_GL_FUNC(glUniform3fv);
    LOAD_GL_FUNC(glUniform4fv);
    LOAD_GL_FUNC(glUniformMatrix3fv);
    LOAD_GL_FUNC(glUniformMatrix4fv);
    
    // Framebuffers
    LOAD_GL_FUNC(glGenFramebuffers);
    LOAD_GL_FUNC(glDeleteFramebuffers);
    LOAD_GL_FUNC(glBindFramebuffer);
    LOAD_GL_FUNC(glFramebufferTexture2D);
    LOAD_GL_FUNC(glCheckFramebufferStatus);
    
    // Renderbuffers
    LOAD_GL_FUNC(glGenRenderbuffers);
    LOAD_GL_FUNC(glDeleteRenderbuffers);
    LOAD_GL_FUNC(glBindRenderbuffer);
    LOAD_GL_FUNC(glRenderbufferStorage);
    LOAD_GL_FUNC(glFramebufferRenderbuffer);
    
    // Verify critical functions loaded
    if (!gl.glGenBuffers || !gl.glGenVertexArrays || !gl.glCreateShader || !gl.glCreateProgram) {
        TD_LOG_ERROR("Failed to load required OpenGL functions");
        return false;
    }
    
    return true;
}

#endif // __EMSCRIPTEN__

bool Renderer::init() {
    if (m_initialized) {
        return true;
    }
    
    // Load GL functions
    if (!loadGLFunctions()) {
        return false;
    }
    
    // Log OpenGL info
    const char* version = (const char*)gl.glGetString(0x1F02); // GL_VERSION
    const char* renderer = (const char*)gl.glGetString(0x1F01); // GL_RENDERER
    TD_LOG_INFO("OpenGL Version: %s", version ? version : "unknown");
    TD_LOG_INFO("OpenGL Renderer: %s", renderer ? renderer : "unknown");
    
    // Create default VAO
    gl.glGenVertexArrays(1, &m_defaultVAO);
    gl.glBindVertexArray(m_defaultVAO);
    
    // Set default OpenGL state
    gl.glEnable(GL_DEPTH_TEST);
    gl.glDepthFunc(GL_LESS);
    
    gl.glEnable(GL_BLEND);
    gl.glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    gl.glEnable(GL_CULL_FACE);
    gl.glCullFace(GL_BACK);
    gl.glFrontFace(GL_CCW);
    
    gl.glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    
    m_initialized = true;
    TD_LOG_INFO("Renderer initialized");
    
    return true;
}

void Renderer::shutdown() {
    if (!m_initialized) {
        return;
    }
    
    if (m_defaultVAO) {
        gl.glDeleteVertexArrays(1, &m_defaultVAO);
        m_defaultVAO = 0;
    }
    
    m_initialized = false;
    TD_LOG_INFO("Renderer shutdown");
}

void Renderer::clear(float r, float g, float b, float a) {
    gl.glClearColor(r, g, b, a);
    gl.glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void Renderer::setViewport(int x, int y, int w, int h) {
    m_viewportWidth = w;
    m_viewportHeight = h;
    gl.glViewport(x, y, w, h);
}

void Renderer::setBlendMode(bool enabled) {
    if (enabled) {
        gl.glEnable(GL_BLEND);
    } else {
        gl.glDisable(GL_BLEND);
    }
}

void Renderer::setDepthTest(bool enabled) {
    if (enabled) {
        gl.glEnable(GL_DEPTH_TEST);
    } else {
        gl.glDisable(GL_DEPTH_TEST);
    }
}

void Renderer::setCullFace(bool enabled) {
    if (enabled) {
        gl.glEnable(GL_CULL_FACE);
    } else {
        gl.glDisable(GL_CULL_FACE);
    }
}

void Renderer::setWireframe(bool enabled) {
    if (enabled) {
        gl.glPolygonMode(GL_FRONT_AND_BACK, 0x1B01); // GL_LINE
    } else {
        gl.glPolygonMode(GL_FRONT_AND_BACK, 0x1B02); // GL_FILL
    }
}

void Renderer::drawTriangles(int vertexCount, int offset) {
    gl.glDrawArrays(GL_TRIANGLES, offset, vertexCount);
}

void Renderer::drawTrianglesIndexed(int indexCount, int offset) {
    gl.glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, (void*)(offset * sizeof(unsigned int)));
}

void Renderer::drawLines(int vertexCount, int offset) {
    gl.glDrawArrays(GL_LINES, offset, vertexCount);
}

void Renderer::drawPoints(int vertexCount, int offset) {
    gl.glDrawArrays(GL_POINTS, offset, vertexCount);
}

} // namespace td
