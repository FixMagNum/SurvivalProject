#pragma once
#include <vector>
#include <glm/glm.hpp>
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
        int tileID, bool flipWinding);

    unsigned int VAO, VBO;
    std::vector<float> vertices;
};