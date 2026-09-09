#pragma once

#include "camera.h"
#include "core/QuadMesh.h"
#include "core/Shader.h"
#include "core/Texture.h"

#include <glm/glm.hpp>

class SkyRenderer
{
public:
	SkyRenderer();

	void Update(const Camera& camera, float timeOfDay);
	void Render(const Camera& camera);

private:
	QuadMesh m_Quad;
	Shader m_Shader;
	Texture m_SunTexture;
	Texture m_MoonTexture;
	Texture m_GlowTexture;

	glm::vec3 m_SunDirection;
	glm::vec3 m_SunPosition;
	glm::vec3 m_MoonDirection;
	glm::vec3 m_MoonPosition;

	static constexpr float OrbitTilt = 0.0f;
	static constexpr float SkyRadius = 500.0f;
};