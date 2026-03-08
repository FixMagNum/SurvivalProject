#include "camera.h"
#include <GLFW/glfw3.h>

Camera::Camera(glm::vec3 position)
{
    int speedMultiplier = 3;
    
    Position = position;

    WorldUp = glm::vec3(0.0f, 1.0f, 0.0f);

    Yaw = -90.0f;
    Pitch = 0.0f;

    Speed = 3.0f * speedMultiplier;
    Sensitivity = 0.1f;

    UpdateVectors();
}

glm::mat4 Camera::GetViewMatrix()
{
    return glm::lookAt(Position, Position + Front, Up);
}

void Camera::ProcessKeyboard(int key, float deltaTime)
{
    float velocity = Speed * deltaTime;

    // Горизонтальное движение
    glm::vec3 forward = Front;
    forward.y = 0.0f;
    forward = glm::normalize(forward);

    if (key == GLFW_KEY_W)
        Position += forward * velocity;

    if (key == GLFW_KEY_S)
        Position -= forward * velocity;

    if (key == GLFW_KEY_A)
        Position -= Right * velocity;

    if (key == GLFW_KEY_D)
        Position += Right * velocity;

    // Вертикальное движение
    if (key == GLFW_KEY_SPACE)
        Position += WorldUp * velocity;

    if (key == GLFW_KEY_LEFT_SHIFT)
        Position -= WorldUp * velocity;
}

void Camera::ProcessMouse(float xoffset, float yoffset)
{
    xoffset *= Sensitivity;
    yoffset *= Sensitivity;

    Yaw += xoffset;
    Pitch += yoffset;

    if (Pitch > 89.0f)  Pitch = 89.0f;
    if (Pitch < -89.0f) Pitch = -89.0f;

    UpdateVectors();
}

void Camera::UpdateVectors()
{
    glm::vec3 front;
    front.x = cos(glm::radians(Yaw)) * cos(glm::radians(Pitch));
    front.y = sin(glm::radians(Pitch));
    front.z = sin(glm::radians(Yaw)) * cos(glm::radians(Pitch));
    Front = glm::normalize(front);

    Right = glm::normalize(glm::cross(Front, WorldUp));
    Up = glm::normalize(glm::cross(Right, Front));
}