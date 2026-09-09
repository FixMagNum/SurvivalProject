#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>

#include <glad/glad.h>
#include <glm/glm.hpp>

class Shader
{
public:
    Shader(const std::filesystem::path& vertexPath, const std::filesystem::path& fragmentPath);
    ~Shader();

    void Bind() const;
    void Unbind() const;

    void SetInt(const std::string& name, int value) const;
    void SetFloat(const std::string& name, float value) const;

    void SetVec2(const std::string& name, const glm::vec2& value) const;
    void SetVec3(const std::string& name, const glm::vec3& value) const;
    void SetVec4(const std::string& name, const glm::vec4& value) const;

    void SetMat4(const std::string& name, const glm::mat4& value) const;

private:
    GLuint m_Program = 0;

    GLint GetUniformLocation(const std::string& name) const;

    mutable std::unordered_map<std::string, GLint> m_UniformLocations;
};