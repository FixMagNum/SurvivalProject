#pragma once
#include <glm/glm.hpp>

class Frustum
{
public:
    void Update(const glm::mat4& projview);
    bool IsBoxVisible(const glm::vec3& min, const glm::vec3& max) const;

private:
    enum Planes { Left = 0, Right, Bottom, Top, Near, Far, Count };
    static constexpr int Combinations = Count * (Count - 1) / 2;

    template<Planes i, Planes j>
    struct ij2k { enum { k = i * (9 - i) / 2 + j - 1 }; };

    template<Planes a, Planes b, Planes c>
    glm::vec3 intersection(const glm::vec3* crosses) const;

    glm::vec4 m_planes[Count];
    glm::vec3 m_points[8];   // 8 углов фрустума в мировом пространстве
};