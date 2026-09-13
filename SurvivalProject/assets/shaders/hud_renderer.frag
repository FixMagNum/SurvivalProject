#version 330 core

out vec4 FragColor;

in vec2 TexCoord;

uniform sampler2D uCrosshairTexture;

void main()
{
	vec4 color = texture(uCrosshairTexture, TexCoord);

	FragColor = pow(color, vec4(1.0 / 2.2));
}