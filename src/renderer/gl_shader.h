#pragma once
#include <cstdint>

namespace td {

class Shader {
public:
    Shader() = default;
    ~Shader();
    
    bool loadFromMemory(const char* vertSrc, const char* fragSrc);
    bool loadFromFile(const char* vertPath, const char* fragPath);
    
    void bind() const;
    void unbind() const;
    
    // Uniform setters
    void setUniform1f(const char* name, float value);
    void setUniform1i(const char* name, int value);
    void setUniform2f(const char* name, float x, float y);
    void setUniform3f(const char* name, float x, float y, float z);
    void setUniform4f(const char* name, float r, float g, float b, float a);
    void setUniformMat3(const char* name, const float* matrix);
    void setUniformMat4(const char* name, const float* matrix);
    void setUniform1fv(const char* name, const float* values, int count);
    void setUniform3fv(const char* name, const float* values, int count);
    void setUniform4fv(const char* name, const float* values, int count);
    
    uint32_t getID() const { return m_programID; }
    bool isValid() const { return m_programID != 0; }
    const char* getErrorLog() const { return m_errorLog; }
    
    // Get uniform location (cached)
    int getUniformLocation(const char* name);
    
private:
    bool compileShader(uint32_t type, const char* source, uint32_t& outShader);
    
    uint32_t m_programID = 0;
    char m_errorLog[1024] = {};
    
    // Simple uniform location cache
    static const int MAX_CACHED_UNIFORMS = 32;
    struct UniformCache {
        char name[64];
        int location;
    };
    UniformCache m_uniformCache[MAX_CACHED_UNIFORMS];
    int m_uniformCacheCount = 0;
};

} // namespace td
