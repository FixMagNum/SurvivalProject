#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>

struct BillboardVertex
{
    glm::vec3 Position;
    glm::vec2 UV;
};

class QuadMesh
{
public:
    QuadMesh();
    ~QuadMesh();

    void Draw() const;

private:
    GLuint m_VAO = 0;
    GLuint m_VBO = 0;
    GLuint m_EBO = 0;
};