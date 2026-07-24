#include "frustum.h"

void Frustum::Update(const glm::mat4& projview)
{
    // Транспонируем - так удобнее доставать строки для плоскостей
    glm::mat4 m = glm::transpose(projview);

    m_planes[Left] = m[3] + m[0];
    m_planes[Right] = m[3] - m[0];
    m_planes[Bottom] = m[3] + m[1];
    m_planes[Top] = m[3] - m[1];
    m_planes[Near] = m[3] + m[2];
    m_planes[Far] = m[3] - m[2];

    // Попарные векторные произведения нормалей плоскостей —
    // нужны для нахождения 8 угловых точек фрустума
    glm::vec3 crosses[Combinations] = {
        glm::cross(glm::vec3(m_planes[Left]),   glm::vec3(m_planes[Right])),
        glm::cross(glm::vec3(m_planes[Left]),   glm::vec3(m_planes[Bottom])),
        glm::cross(glm::vec3(m_planes[Left]),   glm::vec3(m_planes[Top])),
        glm::cross(glm::vec3(m_planes[Left]),   glm::vec3(m_planes[Near])),
        glm::cross(glm::vec3(m_planes[Left]),   glm::vec3(m_planes[Far])),
        glm::cross(glm::vec3(m_planes[Right]),  glm::vec3(m_planes[Bottom])),
        glm::cross(glm::vec3(m_planes[Right]),  glm::vec3(m_planes[Top])),
        glm::cross(glm::vec3(m_planes[Right]),  glm::vec3(m_planes[Near])),
        glm::cross(glm::vec3(m_planes[Right]),  glm::vec3(m_planes[Far])),
        glm::cross(glm::vec3(m_planes[Bottom]), glm::vec3(m_planes[Top])),
        glm::cross(glm::vec3(m_planes[Bottom]), glm::vec3(m_planes[Near])),
        glm::cross(glm::vec3(m_planes[Bottom]), glm::vec3(m_planes[Far])),
        glm::cross(glm::vec3(m_planes[Top]),    glm::vec3(m_planes[Near])),
        glm::cross(glm::vec3(m_planes[Top]),    glm::vec3(m_planes[Far])),
        glm::cross(glm::vec3(m_planes[Near]),   glm::vec3(m_planes[Far])),
    };

    m_points[0] = intersection<Left, Bottom, Near>(crosses);
    m_points[1] = intersection<Left, Top, Near>(crosses);
    m_points[2] = intersection<Right, Bottom, Near>(crosses);
    m_points[3] = intersection<Right, Top, Near>(crosses);
    m_points[4] = intersection<Left, Bottom, Far>(crosses);
    m_points[5] = intersection<Left, Top, Far>(crosses);
    m_points[6] = intersection<Right, Bottom, Far>(crosses);
    m_points[7] = intersection<Right, Top, Far>(crosses);
}

bool Frustum::IsBoxVisible(const glm::vec3& minp, const glm::vec3& maxp) const
{
    // Проверка 1: все 8 углов AABB находятся за одной из плоскостей фрустума
    // → AABB точно снаружи
    for (int i = 0; i < Count; i++)
    {
        if (glm::dot(m_planes[i], glm::vec4(minp.x, minp.y, minp.z, 1.f)) >= 0.f) continue;
        if (glm::dot(m_planes[i], glm::vec4(maxp.x, minp.y, minp.z, 1.f)) >= 0.f) continue;
        if (glm::dot(m_planes[i], glm::vec4(minp.x, maxp.y, minp.z, 1.f)) >= 0.f) continue;
        if (glm::dot(m_planes[i], glm::vec4(maxp.x, maxp.y, minp.z, 1.f)) >= 0.f) continue;
        if (glm::dot(m_planes[i], glm::vec4(minp.x, minp.y, maxp.z, 1.f)) >= 0.f) continue;
        if (glm::dot(m_planes[i], glm::vec4(maxp.x, minp.y, maxp.z, 1.f)) >= 0.f) continue;
        if (glm::dot(m_planes[i], glm::vec4(minp.x, maxp.y, maxp.z, 1.f)) >= 0.f) continue;
        if (glm::dot(m_planes[i], glm::vec4(maxp.x, maxp.y, maxp.z, 1.f)) >= 0.f) continue;
        return false;
    }

    // Проверка 2 (обратная): все 8 углов фрустума находятся по одну сторону
    // от AABB по одной из осей → фрустум полностью «мимо» бокса
    // Это устраняет false positives при длинных диагональных взглядах
    int out;
    out = 0; for (int i = 0; i < 8; i++) out += (m_points[i].x > maxp.x) ? 1 : 0;
    if (out == 8) return false;
    out = 0; for (int i = 0; i < 8; i++) out += (m_points[i].x < minp.x) ? 1 : 0;
    if (out == 8) return false;
    out = 0; for (int i = 0; i < 8; i++) out += (m_points[i].y > maxp.y) ? 1 : 0;
    if (out == 8) return false;
    out = 0; for (int i = 0; i < 8; i++) out += (m_points[i].y < minp.y) ? 1 : 0;
    if (out == 8) return false;
    out = 0; for (int i = 0; i < 8; i++) out += (m_points[i].z > maxp.z) ? 1 : 0;
    if (out == 8) return false;
    out = 0; for (int i = 0; i < 8; i++) out += (m_points[i].z < minp.z) ? 1 : 0;
    if (out == 8) return false;

    return true;
}

// Вычисляет точку пересечения трёх плоскостей фрустума
template<Frustum::Planes a, Frustum::Planes b, Frustum::Planes c>
glm::vec3 Frustum::intersection(const glm::vec3* crosses) const
{
    float D = glm::dot(glm::vec3(m_planes[a]), crosses[ij2k<b, c>::k]);
    glm::vec3 res = glm::mat3(
        crosses[ij2k<b, c>::k],
        -crosses[ij2k<a, c>::k],
        crosses[ij2k<a, b>::k]
    ) * glm::vec3(m_planes[a].w, m_planes[b].w, m_planes[c].w);
    return res * (-1.f / D);
}