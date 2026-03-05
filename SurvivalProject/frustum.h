#pragma once
#include <glm/glm.hpp>

struct Plane
{
	glm::vec3 normal;
	float distance;

	float GetDistance(const glm::vec3& p) const
	{
		return glm::dot(normal, p) + distance;
	}
};

class Frustum
{
public:
	Plane planes[6];

	void Update(const glm::mat4& matrix);

	bool IsBoxVisible(const glm::vec3& min, const glm::vec3& max);
};