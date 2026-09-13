#pragma once

#include <filesystem>

#include <glad/glad.h>
#include <stb_image.h>

enum class TextureColorSpace {
    Linear,
    SRGB
};

enum class TextureMagFilter : GLenum {
	Nearest = GL_NEAREST,
	Linear = GL_LINEAR
};

class Texture
{
public:
    explicit Texture(const std::filesystem::path& path, TextureColorSpace colorSpace, TextureMagFilter magFilter);
    ~Texture();

    void Bind(unsigned int slot = 0) const;
    void Unbind() const;

private:
    GLuint m_Texture = 0;

	static GLenum GetInternalFormat(int nrChannels, TextureColorSpace colorSpace);
};