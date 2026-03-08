#pragma once
#include <vector>
#include <glm/glm.hpp>
#include <cstdint>
#include "block.h"
#include "aabb.h"

class World;

class Chunk
{
public:
	World* world;  // указатель на мир, к которому принадлежит чанк
    AABB bounds;

	// Размеры чанка
    static const int SIZE_X = 16;
    static const int SIZE_Y = 256;
    static const int SIZE_Z = 16;

    int minY = 0;
    int maxY = SIZE_Y - 1;
        
    Chunk* neighborPX = nullptr;
    Chunk* neighborNX = nullptr;
    Chunk* neighborPZ = nullptr;
    Chunk* neighborNZ = nullptr;

    glm::ivec2 chunkPos;  // позиция в чанковой сетке (x,z)

    Chunk(int chunkX, int chunkZ, World* worldPtr);
    
    BlockType blocks[SIZE_X][SIZE_Y][SIZE_Z];

    void Generate();
    void BuildMesh();
    void Draw();

private:
    bool IsBlockSolid(int x, int y, int z);

    // Добавляет прямоугольный quad (w x h блоков) с нужным тайлом
    void AddQuad(
        glm::vec3 origin,
        glm::vec3 axis1, int w,
        glm::vec3 axis2, int h,
        int tileID, bool flipWinding,
        float ao0, float ao1, float ao2, float ao3);

    unsigned int VAO, VBO, EBO;

    // Считает AO для одной вершины (0..3, где 3 = светло)
    int ComputeAO(int side1, int side2, int corner);

    // AO хранится прямо в вершинах — добавляем 1 float к формату
    // Новый stride будет 8 floats: pos(3) + uv(2) + tileOffset(2) + ao(1)
    std::vector<float> vertices;
    std::vector<uint32_t> indices;
};