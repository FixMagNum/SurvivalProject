#include "renderer/HUDRenderer.h"

HUDRenderer::HUDRenderer()
	: m_Quad(),
	m_Shader("assets/shaders/hud_renderer.vert", "assets/shaders/hud_renderer.frag"),
	m_CrosshairTexture("assets/textures/crosshair.png", TextureColorSpace::SRGB, TextureMagFilter::Nearest)
{
	m_Shader.Bind();
	m_Shader.SetInt("uCrosshairTexture", 0);
	m_Shader.Unbind();
}

void HUDRenderer::Render(float screenWidth, float screenHeight)
{
	m_Shader.Bind();
	m_CrosshairTexture.Bind(0);

	m_Shader.SetVec2("uScreenSize", glm::vec2(screenWidth, screenHeight));
	m_Shader.SetVec2("uCrosshairSize", glm::vec2(27.0f, 27.0f));

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	m_Quad.Draw();

	glDisable(GL_BLEND);

	m_Shader.Unbind();
	m_CrosshairTexture.Unbind();
}