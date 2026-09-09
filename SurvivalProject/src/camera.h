#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

class Camera
{
public:
    glm::vec3 Position;
    glm::vec3 Front;
    glm::vec3 Up;
    glm::vec3 Right;
    glm::vec3 WorldUp;

    float Yaw;
    float Pitch;

    float Speed;
    float Sensitivity;

    Camera(glm::vec3 position);

    glm::mat4 GetViewMatrix() const;
	glm::mat4 GetProjectionMatrix() const;

    void SetAspectRatio(float aspectRatio);

    void ProcessKeyboard(int key, float deltaTime);
    void ProcessMouse(float xoffset, float yoffset);

private:
    void UpdateVectors();

	float m_Fov = 75.0f;
	float m_AspectRatio;
    float m_NearPlane = 0.1f;
	float m_FarPlane = 1000.0f;
};