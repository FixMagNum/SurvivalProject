#version 330 core

layout (location = 0) in vec3 aPosition;
layout (location = 1) in vec2 aTexCoord;

out vec2 TexCoord;

uniform vec2 uScreenSize;
uniform vec2 uCrosshairSize;

void main()
{
	vec2 pixelPosition = aPosition.xy * uCrosshairSize;

	vec2 ndcPosition = (pixelPosition / uScreenSize) * 2.0;

	gl_Position = vec4(ndcPosition, 0.0, 1.0);

	TexCoord = aTexCoord;
}