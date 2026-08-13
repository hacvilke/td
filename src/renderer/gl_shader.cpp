#include "gl_shader.h"
#include "gl_renderer.h"
#include "../core/logger.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace td {

Shader::~Shader() {
    if (m_programID) {
        gl.glDeleteProgram(m_programID);
        m_programID = 0;
    }
}

bool Shader::compileShader(uint32_t type, const char* source, uint32_t& outShader) {
    outShader = gl.glCreateShader(type);
    
    gl.glShaderSource(outShader, 1, &source, nullptr);
    gl.glCompileShader(outShader);
    
    GLint success;
    gl.glGetShaderiv(outShader, GL_COMPILE_STATUS, &success);
    
    if (!success) {
        gl.glGetShaderInfoLog(outShader, sizeof(m_errorLog), nullptr, m_errorLog);
        
        const char* shaderType = (type == GL_VERTEX_SHADER) ? "vertex" : "fragment";
        TD_LOG_ERROR("Failed to compile %s shader:\n%s", shaderType, m_errorLog);
        
        gl.glDeleteShader(outShader);
        outShader = 0;
        return false;
    }
    
    return true;
}

bool Shader::loadFromMemory(const char* vertSrc, const char* fragSrc) {
    // Clean up any existing program
    if (m_programID) {
        gl.glDeleteProgram(m_programID);
        m_programID = 0;
    }
    
    m_uniformCacheCount = 0;
    memset(m_errorLog, 0, sizeof(m_errorLog));
    
    // Compile vertex shader
    GLuint vertShader;
    if (!compileShader(GL_VERTEX_SHADER, vertSrc, vertShader)) {
        return false;
    }
    
    // Compile fragment shader
    GLuint fragShader;
    if (!compileShader(GL_FRAGMENT_SHADER, fragSrc, fragShader)) {
        gl.glDeleteShader(vertShader);
        return false;
    }
    
    // Create and link program
    m_programID = gl.glCreateProgram();
    gl.glAttachShader(m_programID, vertShader);
    gl.glAttachShader(m_programID, fragShader);
    gl.glLinkProgram(m_programID);
    
    // Check link status
    GLint success;
    gl.glGetProgramiv(m_programID, GL_LINK_STATUS, &success);
    
    if (!success) {
        gl.glGetProgramInfoLog(m_programID, sizeof(m_errorLog), nullptr, m_errorLog);
        TD_LOG_ERROR("Failed to link shader program:\n%s", m_errorLog);
        
        gl.glDeleteShader(vertShader);
        gl.glDeleteShader(fragShader);
        gl.glDeleteProgram(m_programID);
        m_programID = 0;
        return false;
    }
    
    // Clean up shaders (they're linked into the program now)
    gl.glDetachShader(m_programID, vertShader);
    gl.glDetachShader(m_programID, fragShader);
    gl.glDeleteShader(vertShader);
    gl.glDeleteShader(fragShader);
    
    return true;
}

bool Shader::loadFromFile(const char* vertPath, const char* fragPath) {
    // Read vertex shader
    FILE* vertFile = fopen(vertPath, "rb");
    if (!vertFile) {
        snprintf(m_errorLog, sizeof(m_errorLog), "Failed to open vertex shader: %s", vertPath);
        TD_LOG_ERROR("%s", m_errorLog);
        return false;
    }
    
    fseek(vertFile, 0, SEEK_END);
    long vertSize = ftell(vertFile);
    fseek(vertFile, 0, SEEK_SET);
    
    char* vertSrc = (char*)malloc(vertSize + 1);
    fread(vertSrc, 1, vertSize, vertFile);
    vertSrc[vertSize] = '\0';
    fclose(vertFile);
    
    // Read fragment shader
    FILE* fragFile = fopen(fragPath, "rb");
    if (!fragFile) {
        snprintf(m_errorLog, sizeof(m_errorLog), "Failed to open fragment shader: %s", fragPath);
        TD_LOG_ERROR("%s", m_errorLog);
        free(vertSrc);
        return false;
    }
    
    fseek(fragFile, 0, SEEK_END);
    long fragSize = ftell(fragFile);
    fseek(fragFile, 0, SEEK_SET);
    
    char* fragSrc = (char*)malloc(fragSize + 1);
    fread(fragSrc, 1, fragSize, fragFile);
    fragSrc[fragSize] = '\0';
    fclose(fragFile);
    
    // Compile
    bool result = loadFromMemory(vertSrc, fragSrc);
    
    free(vertSrc);
    free(fragSrc);
    
    return result;
}

void Shader::bind() const {
    gl.glUseProgram(m_programID);
}

void Shader::unbind() const {
    gl.glUseProgram(0);
}

int Shader::getUniformLocation(const char* name) {
    // Check cache first
    for (int i = 0; i < m_uniformCacheCount; i++) {
        if (strcmp(m_uniformCache[i].name, name) == 0) {
            return m_uniformCache[i].location;
        }
    }
    
    // Query OpenGL
    int location = gl.glGetUniformLocation(m_programID, name);
    
    // Cache it
    if (m_uniformCacheCount < MAX_CACHED_UNIFORMS) {
        strncpy(m_uniformCache[m_uniformCacheCount].name, name, 63);
        m_uniformCache[m_uniformCacheCount].name[63] = '\0';
        m_uniformCache[m_uniformCacheCount].location = location;
        m_uniformCacheCount++;
    }
    
    return location;
}

void Shader::setUniform1f(const char* name, float value) {
    int loc = getUniformLocation(name);
    if (loc != -1) {
        gl.glUniform1f(loc, value);
    }
}

void Shader::setUniform1i(const char* name, int value) {
    int loc = getUniformLocation(name);
    if (loc != -1) {
        gl.glUniform1i(loc, value);
    }
}

void Shader::setUniform2f(const char* name, float x, float y) {
    int loc = getUniformLocation(name);
    if (loc != -1) {
        gl.glUniform2f(loc, x, y);
    }
}

void Shader::setUniform3f(const char* name, float x, float y, float z) {
    int loc = getUniformLocation(name);
    if (loc != -1) {
        gl.glUniform3f(loc, x, y, z);
    }
}

void Shader::setUniform4f(const char* name, float r, float g, float b, float a) {
    int loc = getUniformLocation(name);
    if (loc != -1) {
        gl.glUniform4f(loc, r, g, b, a);
    }
}

void Shader::setUniformMat3(const char* name, const float* matrix) {
    int loc = getUniformLocation(name);
    if (loc != -1) {
        gl.glUniformMatrix3fv(loc, 1, GL_FALSE, matrix);
    }
}

void Shader::setUniformMat4(const char* name, const float* matrix) {
    int loc = getUniformLocation(name);
    if (loc != -1) {
        gl.glUniformMatrix4fv(loc, 1, GL_FALSE, matrix);
    }
}

void Shader::setUniform1fv(const char* name, const float* values, int count) {
    int loc = getUniformLocation(name);
    if (loc != -1) {
        gl.glUniform1fv(loc, count, values);
    }
}

void Shader::setUniform3fv(const char* name, const float* values, int count) {
    int loc = getUniformLocation(name);
    if (loc != -1) {
        gl.glUniform3fv(loc, count, values);
    }
}

void Shader::setUniform4fv(const char* name, const float* values, int count) {
    int loc = getUniformLocation(name);
    if (loc != -1) {
        gl.glUniform4fv(loc, count, values);
    }
}

} // namespace td
