#include "SkyRenderer.h"

SkyRenderer::SkyRenderer()
	: m_SunTexture("assets/textures/sun.png", TextureColorSpace::SRGB),
	m_MoonTexture("assets/textures/moon2.png", TextureColorSpace::SRGB),
	m_GlowTexture("assets/textures/sky_glow.png", TextureColorSpace::SRGB),
	m_Shader("assets/shaders/sky_renderer.vert", "assets/shaders/sky_renderer.frag"),
	m_SunDirection(0.0f),
	m_SunPosition(0.0f),
	m_MoonDirection(0.0f),
	m_MoonPosition(0.0f)
{
	m_Shader.Bind();
	m_Shader.SetInt("uSunTexture", 0);
	m_Shader.SetInt("uMoonTexture", 1);
	m_Shader.SetInt("uGlowTexture", 2);
	m_Shader.Unbind();
}

void SkyRenderer::Update(const Camera& camera, float timeOfDay)
{
	float sunAngle = timeOfDay * glm::two_pi<float>();

	m_SunDirection = glm::normalize(glm::vec3(
		cos(sunAngle),
		sin(sunAngle),
		OrbitTilt
	));

	m_SunPosition = camera.Position + m_SunDirection * SkyRadius;

	float moonAngle = sunAngle + glm::pi<float>();

	m_MoonDirection = glm::normalize(glm::vec3(
		cos(moonAngle),
		sin(moonAngle),
		OrbitTilt
	));

	m_MoonPosition = camera.Position + m_MoonDirection * SkyRadius;
}

void SkyRenderer::Render(const Camera& camera)
{
	m_Shader.Bind();

	m_Shader.SetMat4("view", camera.GetViewMatrix());
	m_Shader.SetMat4("projection", camera.GetProjectionMatrix());

	m_SunTexture.Bind(0);
	m_MoonTexture.Bind(1);
	m_GlowTexture.Bind(2);

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE);

	// Sun rendering
	m_Shader.SetVec3("uSunPosition", m_SunPosition);

	// Sun glow
	m_Shader.SetInt("uCelestialBody", 2);
	m_Shader.SetFloat("uSize", 240.0f);
	m_Quad.Draw();

	// Sun
	m_Shader.SetInt("uCelestialBody", 0);
	m_Shader.SetFloat("uSize", 40.0f);
	m_Quad.Draw();

	// Moon rendering
	float moonAlpha = glm::smoothstep(-0.15f, 0.35f, m_MoonDirection.y);

	m_Shader.SetFloat("uMoonAlpha", moonAlpha);
	m_Shader.SetVec3("uMoonPosition", m_MoonPosition);

	if (moonAlpha > 0.0f)
	{
		// Moon glow
		m_Shader.SetInt("uCelestialBody", 3);
		m_Shader.SetFloat("uSize", 90.0f);
		m_Quad.Draw();

		// Moon
		m_Shader.SetInt("uCelestialBody", 1);
		m_Shader.SetFloat("uSize", 30.0f);
		m_Quad.Draw();
	}

	glDisable(GL_BLEND);

	m_SunTexture.Unbind();
	m_MoonTexture.Unbind();
	m_Shader.Unbind();
}