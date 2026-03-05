#include <glad/glad.h>
#include "chunk.h"
#include "world.h"
#include "block.h"

constexpr int ATLAS_SIZE = 16;
constexpr float TILE_SIZE = 1.0f / ATLAS_SIZE;

// Получаем ID тайла в атласе по координатам
static int Tile(int x, int y)
{
    return y * ATLAS_SIZE + x;
}

BlockData blockDatabase[] =
{
    // AIR
    {0,0,0},

    // GRASS
    { Tile(2,0), Tile(0,0), Tile(1,0) },

    // DIRT
    { Tile(0,0), Tile(0,0), Tile(0,0) },

    // STONE
    { Tile(3,0), Tile(3,0), Tile(3,0) }
};

Chunk::Chunk(int chunkX, int chunkZ, World* worldPtr)
{
	chunkPos = {chunkX, chunkZ};
	world = worldPtr;

    float worldX = chunkPos.x * SIZE_X;
    float worldZ = chunkPos.y * SIZE_Z;

    bounds.min = glm::vec3(worldX, 0, worldZ);
    bounds.max = glm::vec3(worldX + SIZE_X, SIZE_Y, worldZ + SIZE_Z);
    
    // Обнуляем массив блоков
    for (int x = 0; x < SIZE_X; x++)
        for (int y = 0; y < SIZE_Y; y++)
            for (int z = 0; z < SIZE_Z; z++)
                blocks[x][y][z] = AIR;

    VAO = 0;
    VBO = 0;
}

void Chunk::Generate()
{
    for (int x = 0; x < SIZE_X; x++)
        for (int z = 0; z < SIZE_Z; z++)
			for (int y = 0; y < SIZE_Y; y++)
                if (y < 255)
                    blocks[x][y][z] = DIRT;
                else if (y == 255)
                    blocks[x][y][z] = GRASS;
                else
                    blocks[x][y][z] = AIR;
}

bool Chunk::IsBlockSolid(int x, int y, int z)
{
    int worldX = x + chunkPos.x * SIZE_X;
    int worldZ = z + chunkPos.y * SIZE_Z;

    BlockType block = world->GetBlock(worldX, y, worldZ);

    return block != AIR;
}

void Chunk::AddCube(int x, int y, int z)
{
    BlockType type = blocks[x][y][z];
    BlockData data = blockDatabase[type];

    // +X
    if (!IsBlockSolid(x + 1, y, z))
        AddFace(x, y, z, 0, data.side);

    // -X
    if (!IsBlockSolid(x - 1, y, z))
        AddFace(x, y, z, 1, data.side);

    // +Y
    if (!IsBlockSolid(x, y + 1, z))
        AddFace(x, y, z, 2, data.top);

    // -Y
    if (!IsBlockSolid(x, y - 1, z))
        AddFace(x, y, z, 3, data.bottom);

    // +Z
    if (!IsBlockSolid(x, y, z + 1))
        AddFace(x, y, z, 4, data.side);

    // -Z
    if (!IsBlockSolid(x, y, z - 1))
        AddFace(x, y, z, 5, data.side);
}

void Chunk::AddFace(int x, int y, int z, int face, int tileID)
{
    int tileX = tileID % ATLAS_SIZE;
    int tileY = tileID / ATLAS_SIZE;
    tileID = tileY * ATLAS_SIZE + tileX;

    static const float faces[6][30] =
    {
        // +X
        {
            0.5f,-0.5f,-0.5f,0.0f,0.0f,
            0.5f, 0.5f,-0.5f,0.0f,1.0f,
            0.5f, 0.5f, 0.5f,1.0f,1.0f,
            0.5f, 0.5f, 0.5f,1.0f,1.0f,
            0.5f,-0.5f, 0.5f,1.0f,0.0f,
            0.5f,-0.5f,-0.5f,0.0f,0.0f
        },

        // -X
        {
            -0.5f,-0.5f,-0.5f,0.0f,0.0f,
            -0.5f,-0.5f, 0.5f,1.0f,0.0f,
            -0.5f, 0.5f, 0.5f,1.0f,1.0f,
            -0.5f, 0.5f, 0.5f,1.0f,1.0f,
            -0.5f, 0.5f,-0.5f,0.0f,1.0f,
            -0.5f,-0.5f,-0.5f,0.0f,0.0f
        },

        // +Y
        {
            -0.5f,0.5f,-0.5f,0.0f,0.0f,
            -0.5f,0.5f, 0.5f,0.0f,1.0f,
             0.5f,0.5f, 0.5f,1.0f,1.0f,
             0.5f,0.5f, 0.5f,1.0f,1.0f,
             0.5f,0.5f,-0.5f,1.0f,0.0f,
            -0.5f,0.5f,-0.5f,0.0f,0.0f
        },

        // -Y
        {
            -0.5f,-0.5f,-0.5f,0.0f,0.0f,
             0.5f,-0.5f,-0.5f,1.0f,0.0f,
             0.5f,-0.5f, 0.5f,1.0f,1.0f,
             0.5f,-0.5f, 0.5f,1.0f,1.0f,
            -0.5f,-0.5f, 0.5f,0.0f,1.0f,
            -0.5f,-0.5f,-0.5f,0.0f,0.0f
        },

        // +Z
        {
            -0.5f,-0.5f,0.5f,0.0f,0.0f,
             0.5f,-0.5f,0.5f,1.0f,0.0f,
             0.5f, 0.5f,0.5f,1.0f,1.0f,
             0.5f, 0.5f,0.5f,1.0f,1.0f,
            -0.5f, 0.5f,0.5f,0.0f,1.0f,
            -0.5f,-0.5f,0.5f,0.0f,0.0f
        },

        // -Z
        {
            -0.5f,-0.5f,-0.5f,0.0f,0.0f,
            -0.5f, 0.5f,-0.5f,0.0f,1.0f,
             0.5f, 0.5f,-0.5f,1.0f,1.0f,
             0.5f, 0.5f,-0.5f,1.0f,1.0f,
             0.5f,-0.5f,-0.5f,1.0f,0.0f,
            -0.5f,-0.5f,-0.5f,0.0f,0.0f
        }
    };

    for (int i = 0; i < 30; i += 5)
    {
        float baseU = faces[face][i + 3];
        float baseV = faces[face][i + 4];

        float u = (tileX + baseU) * TILE_SIZE;
        float v = (tileY + baseV) * TILE_SIZE;

        float worldX = x + chunkPos.x * SIZE_X;
        float worldZ = z + chunkPos.y * SIZE_Z;

        vertices.push_back(faces[face][i + 0] + worldX);
        vertices.push_back(faces[face][i + 1] + y);
        vertices.push_back(faces[face][i + 2] + worldZ);
        vertices.push_back(u);
        vertices.push_back(v);
    }
}

bool Chunk::IsVisible(const glm::mat4& viewProj)
{
    return false;
}

void Chunk::BuildMesh()
{
    vertices.clear();

    for (int x = 0; x < SIZE_X; x++)
        for (int y = 0; y < SIZE_Y; y++)
            for (int z = 0; z < SIZE_Z; z++)
                if (blocks[x][y][z] != 0)
                    AddCube(x, y, z);

    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);

    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);

    glBufferData(GL_ARRAY_BUFFER,
        vertices.size() * sizeof(float),
        vertices.data(),
        GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
	glEnableVertexAttribArray(1);
}

void Chunk::Draw() {
    glBindVertexArray(VAO);
    glDrawArrays(GL_TRIANGLES, 0, vertices.size() / 5);
}