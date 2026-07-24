#include "SkyRenderer.h"

void SkyRenderer::Update(const Camera& camera, float timeOfDay)
{
	float angle = timeOfDay * glm::two_pi<float>();

	m_SunDirection = glm::normalize(glm::vec3(
		cos(angle),
		sin(angle),
		OrbitTilt
	));

	m_SunPosition = camera.Position + m_SunDirection * SkyRadius;
};

void SkyRenderer::Render()
{
};