#version 330 core

layout (location = 0) in vec3 aPosition;
layout (location = 1) in vec2 aTexCoord;

out vec2 TexCoord;

uniform mat4 view;
uniform mat4 projection;

uniform vec3 uSunPosition;
uniform vec3 uMoonPosition;
uniform float uSize;

uniform int uCelestialBody;

void main()
{
    vec3 position;

    if (uCelestialBody == 0)
        position = uSunPosition;
    else if (uCelestialBody == 1)
        position = uMoonPosition;
    else if (uCelestialBody == 2)
        position = uSunPosition;
    else if (uCelestialBody == 3)
        position = uMoonPosition;

    vec3 cameraRight = vec3(view[0][0], view[1][0], view[2][0]);
    vec3 cameraUp    = vec3(view[0][1], view[1][1], view[2][1]);

    vec3 worldPos =
        position
        + cameraRight * aPosition.x * uSize
        + cameraUp * aPosition.y * uSize;

    gl_Position = projection * view * vec4(worldPos, 1.0);

    TexCoord = aTexCoord;
}