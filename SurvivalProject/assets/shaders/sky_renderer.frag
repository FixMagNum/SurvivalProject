#version 330 core

out vec4 FragColor;

in vec2 TexCoord;

uniform sampler2D uSunTexture;
uniform sampler2D uMoonTexture;
uniform sampler2D uGlowTexture;

uniform int uCelestialBody;

uniform float uMoonAlpha;

void main()
{
    vec4 color;

    if (uCelestialBody == 0)
        color = texture(uSunTexture, TexCoord);
    else if (uCelestialBody == 1)
        color = texture(uMoonTexture, TexCoord) * vec4(1.0, 1.0, 1.0, 1.0 * uMoonAlpha);
    else if (uCelestialBody == 2)
        color = texture(uGlowTexture, TexCoord) * vec4(1.0, 1.0, 1.0, 0.15);
    else if (uCelestialBody == 3)
        color = texture(uGlowTexture, TexCoord) * vec4(1.0, 1.0, 1.0, 0.05 * uMoonAlpha);

    FragColor = color;
}