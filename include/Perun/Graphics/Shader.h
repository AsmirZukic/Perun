#pragma once

#include <string>
#include <unordered_map>
#include <glad/glad.h>

namespace Perun::Graphics {

class Shader {
public:
    Shader(const std::string& vertexSrc, const std::string& fragmentSrc);
    ~Shader();

    void Bind() const;
    void Unbind() const;

    // Uniforms
    void SetInt(const std::string& name, int value);
    void SetFloat(const std::string& name, float value);
    void SetFloat4(const std::string& name, float v0, float v1, float v2, float v3);
    void SetMat4(const std::string& name, const float matrix[16]);

private:
    uint32_t m_RendererID;
    std::unordered_map<std::string, int> m_UniformLocationCache;

    int GetUniformLocation(const std::string& name);
    uint32_t CompileShader(uint32_t type, const std::string& source);
};

} // namespace Perun::Graphics
