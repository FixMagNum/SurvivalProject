#include "Texture.h"

#include <iostream>
#include <stdexcept>

GLenum Texture::GetInternalFormat(int nrChannels, TextureColorSpace colorSpace)
{
	if (nrChannels == 1)
	{
		return GL_R8;
	}
	else if (nrChannels == 3)
	{
		return (colorSpace == TextureColorSpace::Linear) ? GL_RGB8 : GL_SRGB8;
	}
	else if (nrChannels == 4)
	{
		return (colorSpace == TextureColorSpace::Linear) ? GL_RGBA8 : GL_SRGB8_ALPHA8;
	}
	else
		throw std::runtime_error("Unsupported number of texture channels");
}

Texture::Texture(const std::filesystem::path& path, TextureColorSpace colorSpace)
{
	int width, height, nrChannels;

	unsigned char* data = stbi_load(path.string().c_str(), &width, &height, &nrChannels, 0);

	if (!data)
	{
		std::cerr << "Failed to load texture: " << path << '\n';
		return;
	}

	GLenum format = GL_RGBA;

	if (nrChannels == 1)
		format = GL_RED;
	else if (nrChannels == 3)
		format = GL_RGB;
	else if (nrChannels != 4)
	{
		std::cerr << "Unsupported texture format: " << path << '\n';
		stbi_image_free(data);
		return;
	}

	GLenum internalFormat = GetInternalFormat(nrChannels, colorSpace);

	glGenTextures(1, &m_Texture);
	glBindTexture(GL_TEXTURE_2D, m_Texture);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	//glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 4);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, format, GL_UNSIGNED_BYTE, data);
	
	glGenerateMipmap(GL_TEXTURE_2D);

	stbi_image_free(data);
}

Texture::~Texture()
{
	glDeleteTextures(1, &m_Texture);
}

void Texture::Bind(unsigned int slot) const
{
	glActiveTexture(GL_TEXTURE0 + slot);
	glBindTexture(GL_TEXTURE_2D, m_Texture);
}

void Texture::Unbind() const
{
	glBindTexture(GL_TEXTURE_2D, 0);
}