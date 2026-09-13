#version 330 core

out vec4 FragColor;

in vec2 TexCoord;

uniform sampler2D uSpriteTexture;

uniform float uAlpha;

void main()
{
    vec4 color = texture(uSpriteTexture, TexCoord);

    FragColor = vec4(color.rgb, color.a * uAlpha);
}