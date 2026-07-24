#pragma once
#include <glm/glm.hpp>
#include "camera.h"

class SkyRenderer
{
public:
	void Update(const Camera& camera, float timeOfDay);
	void Render();

private:
	glm::vec3 m_SunDirection;
	glm::vec3 m_SunPosition;

	static constexpr float OrbitTilt = 0.3f;
	static constexpr float SkyRadius = 500.0f;
};