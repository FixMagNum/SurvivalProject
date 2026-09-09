#include "Shader.h"
#include "utils/FileUtils.h"

#include <iostream>
#include <stdexcept>

static void CheckShaderCompilation(GLuint shader)
{
	GLint success = 0;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &success);

	if (success) return;

	GLint logLength = 0;
	glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);

	std::string log = "Unknown shader compilation error.";

	if (logLength > 0)
	{
		log.resize(logLength);
		glGetShaderInfoLog(shader, logLength, nullptr, log.data());
	}

	std::cerr << "Shader compilation failed:\n" << log << '\n';

	throw std::runtime_error(log);
}

static void CheckProgramLink(GLuint program)
{
	GLint success = 0;
	glGetProgramiv(program, GL_LINK_STATUS, &success);

	if (success) return;

	GLint logLength = 0;
	glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLength);

	std::string log = "Unknown shader program link error.";

	if (logLength > 0)
	{
		log.resize(logLength);
		glGetProgramInfoLog(program, logLength, nullptr, log.data());
	}

	std::cerr << "Shader program link failed:\n" << log << '\n';

	throw std::runtime_error(log);
}

static GLuint CompileShader(GLenum type, const std::string& source)
{
	const char* shaderCode = source.c_str();

	GLuint shader = glCreateShader(type);

	glShaderSource(shader, 1, &shaderCode, nullptr);
	glCompileShader(shader);

	CheckShaderCompilation(shader);

	return shader;
}

Shader::Shader(const std::filesystem::path& vertexPath, const std::filesystem::path& fragmentPath)
{
	std::string vertexSource = FileUtils::ReadTextFile(vertexPath);
	std::string fragmentSource = FileUtils::ReadTextFile(fragmentPath);

	GLuint vertexShader = CompileShader(GL_VERTEX_SHADER, vertexSource);
	GLuint fragmentShader = CompileShader(GL_FRAGMENT_SHADER, fragmentSource);

	m_Program = glCreateProgram();

	glAttachShader(m_Program, vertexShader);
	glAttachShader(m_Program, fragmentShader);

	glLinkProgram(m_Program);

	CheckProgramLink(m_Program);

	glDeleteShader(vertexShader);
	glDeleteShader(fragmentShader);
}

Shader::~Shader()
{
	glDeleteProgram(m_Program);
}

void Shader::Bind() const
{
	glUseProgram(m_Program);
}

void Shader::Unbind() const
{
	glUseProgram(0);
}

void Shader::SetInt(const std::string& name, int value) const
{
	GLint location = GetUniformLocation(name);

	if (location == -1)
		return;

	glUniform1i(location, value);
}

void Shader::SetFloat(const std::string& name, float value) const
{
	GLint location = GetUniformLocation(name);

	if (location == -1)
		return;

	glUniform1f(location, value);
}

void Shader::SetVec2(const std::string& name, const glm::vec2& value) const
{
	GLint location = GetUniformLocation(name);

	if (location == -1)
		return;

	glUniform2f(location, value.x, value.y);
}

void Shader::SetVec3(const std::string& name, const glm::vec3& value) const
{
	GLint location = GetUniformLocation(name);

	if (location == -1)
		return;

	glUniform3f(location, value.x, value.y, value.z);
}

void Shader::SetVec4(const std::string& name, const glm::vec4& value) const
{
	GLint location = GetUniformLocation(name);

	if (location == -1)
		return;

	glUniform4f(location, value.x, value.y, value.z, value.w);
}

void Shader::SetMat4(const std::string& name, const glm::mat4& value) const
{
	GLint location = GetUniformLocation(name);

	if (location == -1)
		return;

	glUniformMatrix4fv(location, 1, GL_FALSE, &value[0][0]);
}

GLint Shader::GetUniformLocation(const std::string& name) const
{
	auto it = m_UniformLocations.find(name);

	if (it != m_UniformLocations.end())
		return it->second;
	
	GLint location = glGetUniformLocation(m_Program, name.c_str());
	
	if (location == -1)
		std::cerr << "Uniform not found: " << name << '\n';

	m_UniformLocations.emplace(name, location);
	
	return location;
}