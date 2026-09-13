#include "SkyRenderer.h"

SkyRenderer::SkyRenderer()
	: m_SunTexture("assets/textures/sun.png", TextureColorSpace::SRGB, TextureMagFilter::Linear),
	m_MoonTexture("assets/textures/moon2.png", TextureColorSpace::SRGB, TextureMagFilter::Linear),
	m_GlowTexture("assets/textures/sky_glow.png", TextureColorSpace::SRGB, TextureMagFilter::Linear),
	m_Shader("assets/shaders/sky_renderer.vert", "assets/shaders/sky_renderer.frag"),
	m_SunDirection(0.0f),
	m_SunPosition(0.0f),
	m_MoonDirection(0.0f),
	m_MoonPosition(0.0f)
{
	m_Shader.Bind();
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

struct SkySprite
{
	const Texture* texture;
	glm::vec3 position;
	float size;
	float alpha = 1.0f;
};

static void RenderSprite(const SkySprite& sprite, const int location, const QuadMesh& quad, const Shader& shader)
{
	sprite.texture->Bind(location);
	shader.SetInt("uSpriteTexture", location);
	shader.SetVec3("uSpritePosition", sprite.position);
	shader.SetFloat("uSize", sprite.size);
	shader.SetFloat("uAlpha", sprite.alpha);
	quad.Draw();
	sprite.texture->Unbind();
}

void SkyRenderer::Render(const Camera& camera)
{
	m_Shader.Bind();

	m_Shader.SetMat4("view", camera.GetViewMatrix());
	m_Shader.SetMat4("projection", camera.GetProjectionMatrix());

	float moonAlpha = glm::smoothstep(-0.15f, 0.35f, m_MoonDirection.y);

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE);

	// Sun glow
	RenderSprite({ &m_GlowTexture, m_SunPosition, 240.0f , 0.15f }, 2, m_Quad, m_Shader);

	// Sun
	RenderSprite({ &m_SunTexture, m_SunPosition, 40.0f, 1.0f }, 0, m_Quad, m_Shader);

	if (moonAlpha > 0.0f)
	{
		// Moon glow
		RenderSprite({ &m_GlowTexture, m_MoonPosition, 90.0f, 0.05f * moonAlpha }, 2, m_Quad, m_Shader);

		// Moon
		RenderSprite({ &m_MoonTexture, m_MoonPosition, 30.0f, 1.0f * moonAlpha }, 1, m_Quad, m_Shader);
	}

	glDisable(GL_BLEND);

	m_Shader.Unbind();
}