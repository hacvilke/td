#pragma once
#include "../platform/platform.h"
#include <cstdint>

// OpenGL type definitions
typedef unsigned int GLenum;
typedef unsigned char GLboolean;
typedef unsigned int GLbitfield;
typedef signed char GLbyte;
typedef short GLshort;
typedef int GLint;
typedef int GLsizei;
typedef unsigned char GLubyte;
typedef unsigned short GLushort;
typedef unsigned int GLuint;
typedef float GLfloat;
typedef float GLclampf;
typedef double GLdouble;
typedef double GLclampd;
typedef void GLvoid;
typedef char GLchar;
typedef ptrdiff_t GLintptr;
typedef ptrdiff_t GLsizeiptr;

// OpenGL constants
#define GL_FALSE                          0
#define GL_TRUE                           1
#define GL_NONE                           0
#define GL_ZERO                           0
#define GL_ONE                            1

#define GL_DEPTH_BUFFER_BIT               0x00000100
#define GL_STENCIL_BUFFER_BIT             0x00000400
#define GL_COLOR_BUFFER_BIT               0x00004000

#define GL_POINTS                         0x0000
#define GL_LINES                          0x0001
#define GL_LINE_LOOP                      0x0002
#define GL_LINE_STRIP                     0x0003
#define GL_TRIANGLES                      0x0004
#define GL_TRIANGLE_STRIP                 0x0005
#define GL_TRIANGLE_FAN                   0x0006

#define GL_NEVER                          0x0200
#define GL_LESS                           0x0201
#define GL_EQUAL                          0x0202
#define GL_LEQUAL                         0x0203
#define GL_GREATER                        0x0204
#define GL_NOTEQUAL                       0x0205
#define GL_GEQUAL                         0x0206
#define GL_ALWAYS                         0x0207

#define GL_SRC_COLOR                      0x0300
#define GL_ONE_MINUS_SRC_COLOR            0x0301
#define GL_SRC_ALPHA                      0x0302
#define GL_ONE_MINUS_SRC_ALPHA            0x0303
#define GL_DST_ALPHA                      0x0304
#define GL_ONE_MINUS_DST_ALPHA            0x0305
#define GL_DST_COLOR                      0x0306
#define GL_ONE_MINUS_DST_COLOR            0x0307
#define GL_SRC_ALPHA_SATURATE             0x0308

#define GL_FRONT                          0x0404
#define GL_BACK                           0x0405
#define GL_FRONT_AND_BACK                 0x0408

#define GL_CW                             0x0900
#define GL_CCW                            0x0901

#define GL_CULL_FACE                      0x0B44
#define GL_DEPTH_TEST                     0x0B71
#define GL_BLEND                          0x0BE2

#define GL_TEXTURE_2D                     0x0DE1
#define GL_TEXTURE_3D                     0x806F

#define GL_UNSIGNED_BYTE                  0x1401
#define GL_UNSIGNED_SHORT                 0x1403
#define GL_UNSIGNED_INT                   0x1405
#define GL_FLOAT                          0x1406

#define GL_RGB                            0x1907
#define GL_RGBA                           0x1908

#define GL_NEAREST                        0x2600
#define GL_LINEAR                         0x2601
#define GL_NEAREST_MIPMAP_NEAREST         0x2700
#define GL_LINEAR_MIPMAP_NEAREST          0x2701
#define GL_NEAREST_MIPMAP_LINEAR          0x2702
#define GL_LINEAR_MIPMAP_LINEAR           0x2703

#define GL_TEXTURE_MAG_FILTER             0x2800
#define GL_TEXTURE_MIN_FILTER             0x2801
#define GL_TEXTURE_WRAP_S                 0x2802
#define GL_TEXTURE_WRAP_T                 0x2803

#define GL_REPEAT                         0x2901
#define GL_CLAMP_TO_EDGE                  0x812F

#define GL_TEXTURE0                       0x84C0
#define GL_TEXTURE1                       0x84C1
#define GL_TEXTURE2                       0x84C2
#define GL_TEXTURE3                       0x84C3

#define GL_ARRAY_BUFFER                   0x8892
#define GL_ELEMENT_ARRAY_BUFFER           0x8893

#define GL_STREAM_DRAW                    0x88E0
#define GL_STATIC_DRAW                    0x88E4
#define GL_DYNAMIC_DRAW                   0x88E8

#define GL_FRAGMENT_SHADER                0x8B30
#define GL_VERTEX_SHADER                  0x8B31
#define GL_COMPILE_STATUS                 0x8B81
#define GL_LINK_STATUS                    0x8B82
#define GL_INFO_LOG_LENGTH                0x8B84

#define GL_RGB8                           0x8051
#define GL_RGBA8                          0x8058

#define GL_DEPTH_COMPONENT                0x1902
#define GL_DEPTH_COMPONENT16              0x81A5
#define GL_DEPTH_COMPONENT24              0x81A6
#define GL_DEPTH_COMPONENT32              0x81A7

#define GL_FRAMEBUFFER                    0x8D40
#define GL_RENDERBUFFER                   0x8D41
#define GL_COLOR_ATTACHMENT0              0x8CE0
#define GL_DEPTH_ATTACHMENT               0x8D00
#define GL_STENCIL_ATTACHMENT             0x8D20
#define GL_DEPTH_STENCIL_ATTACHMENT       0x821A
#define GL_FRAMEBUFFER_COMPLETE           0x8CD5

namespace td {

// OpenGL function pointers
struct GLFunctions {
    // Basic
    void (*glViewport)(GLint x, GLint y, GLsizei width, GLsizei height);
    void (*glClear)(GLbitfield mask);
    void (*glClearColor)(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
    void (*glEnable)(GLenum cap);
    void (*glDisable)(GLenum cap);
    void (*glBlendFunc)(GLenum sfactor, GLenum dfactor);
    void (*glDepthFunc)(GLenum func);
    void (*glCullFace)(GLenum mode);
    void (*glFrontFace)(GLenum mode);
    void (*glDepthMask)(GLboolean flag);
    void (*glPolygonMode)(GLenum face, GLenum mode);
    void (*glLineWidth)(GLfloat width);
    const GLubyte* (*glGetString)(GLenum name);
    void (*glGetIntegerv)(GLenum pname, GLint* data);
    GLenum (*glGetError)();
    
    // Textures
    void (*glGenTextures)(GLsizei n, GLuint* textures);
    void (*glDeleteTextures)(GLsizei n, const GLuint* textures);
    void (*glBindTexture)(GLenum target, GLuint texture);
    void (*glTexImage2D)(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const void* data);
    void (*glTexSubImage2D)(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const void* data);
    void (*glTexParameteri)(GLenum target, GLenum pname, GLint param);
    void (*glTexParameterfv)(GLenum target, GLenum pname, const GLfloat* params);
    void (*glActiveTexture)(GLenum texture);
    void (*glGenerateMipmap)(GLenum target);
    
    // Buffers
    void (*glGenBuffers)(GLsizei n, GLuint* buffers);
    void (*glDeleteBuffers)(GLsizei n, const GLuint* buffers);
    void (*glBindBuffer)(GLenum target, GLuint buffer);
    void (*glBufferData)(GLenum target, GLsizeiptr size, const void* data, GLenum usage);
    void (*glBufferSubData)(GLenum target, GLintptr offset, GLsizeiptr size, const void* data);
    void* (*glMapBuffer)(GLenum target, GLenum access);
    GLboolean (*glUnmapBuffer)(GLenum target);
    
    // Vertex Arrays
    void (*glGenVertexArrays)(GLsizei n, GLuint* arrays);
    void (*glDeleteVertexArrays)(GLsizei n, const GLuint* arrays);
    void (*glBindVertexArray)(GLuint array);
    void (*glEnableVertexAttribArray)(GLuint index);
    void (*glDisableVertexAttribArray)(GLuint index);
    void (*glVertexAttribPointer)(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void* pointer);
    void (*glVertexAttribIPointer)(GLuint index, GLint size, GLenum type, GLsizei stride, const void* pointer);
    
    // Drawing
    void (*glDrawArrays)(GLenum mode, GLint first, GLsizei count);
    void (*glDrawElements)(GLenum mode, GLsizei count, GLenum type, const void* indices);
    void (*glDrawArraysInstanced)(GLenum mode, GLint first, GLsizei count, GLsizei primcount);
    void (*glDrawElementsInstanced)(GLenum mode, GLsizei count, GLenum type, const void* indices, GLsizei primcount);
    
    // Shaders
    GLuint (*glCreateShader)(GLenum type);
    void (*glDeleteShader)(GLuint shader);
    void (*glShaderSource)(GLuint shader, GLsizei count, const GLchar** string, const GLint* length);
    void (*glCompileShader)(GLuint shader);
    void (*glGetShaderiv)(GLuint shader, GLenum pname, GLint* params);
    void (*glGetShaderInfoLog)(GLuint shader, GLsizei maxLength, GLsizei* length, GLchar* infoLog);
    
    // Programs
    GLuint (*glCreateProgram)();
    void (*glDeleteProgram)(GLuint program);
    void (*glAttachShader)(GLuint program, GLuint shader);
    void (*glDetachShader)(GLuint program, GLuint shader);
    void (*glLinkProgram)(GLuint program);
    void (*glUseProgram)(GLuint program);
    void (*glGetProgramiv)(GLuint program, GLenum pname, GLint* params);
    void (*glGetProgramInfoLog)(GLuint program, GLsizei maxLength, GLsizei* length, GLchar* infoLog);
    
    // Uniforms
    GLint (*glGetUniformLocation)(GLuint program, const GLchar* name);
    void (*glUniform1f)(GLint location, GLfloat v0);
    void (*glUniform2f)(GLint location, GLfloat v0, GLfloat v1);
    void (*glUniform3f)(GLint location, GLfloat v0, GLfloat v1, GLfloat v2);
    void (*glUniform4f)(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3);
    void (*glUniform1i)(GLint location, GLint v0);
    void (*glUniform2i)(GLint location, GLint v0, GLint v1);
    void (*glUniform3i)(GLint location, GLint v0, GLint v1, GLint v2);
    void (*glUniform4i)(GLint location, GLint v0, GLint v1, GLint v2, GLint v3);
    void (*glUniform1fv)(GLint location, GLsizei count, const GLfloat* value);
    void (*glUniform2fv)(GLint location, GLsizei count, const GLfloat* value);
    void (*glUniform3fv)(GLint location, GLsizei count, const GLfloat* value);
    void (*glUniform4fv)(GLint location, GLsizei count, const GLfloat* value);
    void (*glUniformMatrix3fv)(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value);
    void (*glUniformMatrix4fv)(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value);
    
    // Framebuffers
    void (*glGenFramebuffers)(GLsizei n, GLuint* framebuffers);
    void (*glDeleteFramebuffers)(GLsizei n, const GLuint* framebuffers);
    void (*glBindFramebuffer)(GLenum target, GLuint framebuffer);
    void (*glFramebufferTexture2D)(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level);
    GLenum (*glCheckFramebufferStatus)(GLenum target);
    
    // Renderbuffers
    void (*glGenRenderbuffers)(GLsizei n, GLuint* renderbuffers);
    void (*glDeleteRenderbuffers)(GLsizei n, const GLuint* renderbuffers);
    void (*glBindRenderbuffer)(GLenum target, GLuint renderbuffer);
    void (*glRenderbufferStorage)(GLenum target, GLenum internalformat, GLsizei width, GLsizei height);
    void (*glFramebufferRenderbuffer)(GLenum target, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer);
};

// Global GL functions
extern GLFunctions gl;

class Renderer {
public:
    bool init();
    void shutdown();
    void clear(float r, float g, float b, float a = 1.0f);
    void setViewport(int x, int y, int w, int h);
    void setBlendMode(bool enabled);
    void setDepthTest(bool enabled);
    void setCullFace(bool enabled);
    void setWireframe(bool enabled);
    void drawTriangles(int vertexCount, int offset = 0);
    void drawTrianglesIndexed(int indexCount, int offset = 0);
    void drawLines(int vertexCount, int offset = 0);
    void drawPoints(int vertexCount, int offset = 0);
    
    int getViewportWidth() const { return m_viewportWidth; }
    int getViewportHeight() const { return m_viewportHeight; }
    
    static Renderer& get();
    
private:
    Renderer() = default;
    bool loadGLFunctions();
    
    GLuint m_defaultVAO = 0;
    int m_viewportWidth = 800;
    int m_viewportHeight = 600;
    bool m_initialized = false;
    
    static Renderer s_instance;
};

} // namespace td
