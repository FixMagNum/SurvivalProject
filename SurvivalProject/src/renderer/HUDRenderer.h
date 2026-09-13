#pragma once

#include "core/QuadMesh.h"
#include "core/Shader.h"
#include "core/Texture.h"

#include <glm/ext/matrix_clip_space.hpp>

class HUDRenderer
{
public:
	HUDRenderer();

	void Render(float screenWidth, float screenHeight);

private:
	QuadMesh m_Quad;

	Shader m_Shader;

	Texture m_CrosshairTexture;
};