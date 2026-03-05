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

    glm::ivec2 chunkPos;  // позиция в чанковой сетке (x,z)

    Chunk(int chunkX, int chunkZ, World* worldPtr);
    BlockType blocks[SIZE_X][SIZE_Y][SIZE_Z];

    void Generate();
    void BuildMesh();
    void Draw();

private:
    bool IsBlockSolid(int x, int y, int z);
    void AddFace(int x, int y, int z, int face, int tileID);
	bool IsVisible(const glm::mat4& viewProj);

    unsigned int VAO, VBO;
    std::vector<float> vertices;

    void AddCube(int x, int y, int z);
};