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

/*
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
*/

void Chunk::AddGreedyFace(int x, int y, int z, int sizeU, int sizeV, int face, int tileID)
{
    int tileX = tileID % ATLAS_SIZE;
    int tileY = tileID / ATLAS_SIZE;

    float worldXBase = chunkPos.x * SIZE_X;
    float worldZBase = chunkPos.y * SIZE_Z;

    struct Vertex {float x, y, z, u, v;};
    Vertex verts[4]{};

    if (face == 2) // +Y
    {
        float yWorld = y + 0.5f;
        verts[0].x = worldXBase + x - 0.5f;
        verts[0].z = worldZBase + z - 0.5f + sizeV;
        verts[1].x = worldXBase + x - 0.5f + sizeU;
        verts[1].z = worldZBase + z - 0.5f + sizeV;
        verts[2].x = worldXBase + x - 0.5f + sizeU;
        verts[2].z = worldZBase + z - 0.5f;
        verts[3].x = worldXBase + x - 0.5f;
        verts[3].z = worldZBase + z - 0.5f;
        
        for (int i = 0; i < 4; i++)
        {
            verts[i].y = yWorld;
            float u = (verts[i].x - (worldXBase + x - 0.5f)) / sizeU;
            float v = (verts[i].z - (worldZBase + z - 0.5f)) / sizeV;
            verts[i].u = (tileX + u) * TILE_SIZE;
            verts[i].v = (tileY + v) * TILE_SIZE;
        }
    }
    else if (face == 3) // -Y
    {
        float yWorld = y - 0.5f;
        verts[0].x = worldXBase + x - 0.5f;
        verts[0].z = worldZBase + z - 0.5f;
        verts[1].x = worldXBase + x - 0.5f + sizeU;
        verts[1].z = worldZBase + z - 0.5f;
        verts[2].x = worldXBase + x - 0.5f + sizeU;
        verts[2].z = worldZBase + z - 0.5f + sizeV;
        verts[3].x = worldXBase + x - 0.5f;
        verts[3].z = worldZBase + z - 0.5f + sizeV;
        
        for (int i = 0; i < 4; i++)
        {
            verts[i].y = yWorld;
            float u = (verts[i].x - (worldXBase + x - 0.5f)) / sizeU;
            float v = (verts[i].z - (worldZBase + z - 0.5f)) / sizeV;
            verts[i].u = (tileX + u) * TILE_SIZE;
            verts[i].v = (tileY + v) * TILE_SIZE;
        }
    }
    else if (face == 0) // +X
    {
        float xWorld = worldXBase + x + 0.5f;
        verts[0].z = worldZBase + z - 0.5f;
        verts[0].y = y - 0.5f;
        verts[1].z = worldZBase + z - 0.5f + sizeU;
        verts[1].y = y - 0.5f;
        verts[2].z = worldZBase + z - 0.5f + sizeU;
        verts[2].y = y - 0.5f + sizeV;
        verts[3].z = worldZBase + z - 0.5f;
        verts[3].y = y - 0.5f + sizeV;
        
        for (int i = 0; i < 4; i++)
        {
            verts[i].x = xWorld;
            float u = (verts[i].z - (worldZBase + z - 0.5f)) / sizeU;
            float v = (verts[i].y - (y - 0.5f)) / sizeV;
            verts[i].u = (tileX + u) * TILE_SIZE;
            verts[i].v = (tileY + v) * TILE_SIZE;
        }
    }
    else if (face == 1) // -X
    {
        float xWorld = worldXBase + x - 0.5f;
        verts[0].z = worldZBase + z - 0.5f;          verts[0].y = y - 0.5f;
        verts[1].z = worldZBase + z - 0.5f + sizeU; verts[1].y = y - 0.5f;
        verts[2].z = worldZBase + z - 0.5f + sizeU; verts[2].y = y - 0.5f + sizeV;
        verts[3].z = worldZBase + z - 0.5f;          verts[3].y = y - 0.5f + sizeV;
        
        for (int i = 0; i < 4; i++)
        {
            verts[i].x = xWorld;
            float u = (verts[i].z - (worldZBase + z - 0.5f)) / sizeU;
            float v = (verts[i].y - (y - 0.5f)) / sizeV;
            verts[i].u = (tileX + u) * TILE_SIZE;
            verts[i].v = (tileY + v) * TILE_SIZE;
        }
    }
    else if (face == 4) // +Z
    {
        float zWorld = worldZBase + z + 0.5f;
        verts[0].x = worldXBase + x - 0.5f;          
        verts[0].y = y - 0.5f;
        verts[1].x = worldXBase + x - 0.5f + sizeU;
        verts[1].y = y - 0.5f;
        verts[2].x = worldXBase + x - 0.5f + sizeU;
        verts[2].y = y - 0.5f + sizeV;
        verts[3].x = worldXBase + x - 0.5f;
        verts[3].y = y - 0.5f + sizeV;
        
        for (int i = 0; i < 4; i++)
        {
            verts[i].z = zWorld;
            float u = (verts[i].x - (worldXBase + x - 0.5f)) / sizeU;
            float v = (verts[i].y - (y - 0.5f)) / sizeV;
            verts[i].u = (tileX + u) * TILE_SIZE;
            verts[i].v = (tileY + v) * TILE_SIZE;
        }
    }
    else if (face == 5) // -Z
    {
        float zWorld = worldZBase + z - 0.5f;
        verts[0].x = worldXBase + x - 0.5f;
        verts[0].y = y - 0.5f;
        verts[1].x = worldXBase + x - 0.5f + sizeU;
        verts[1].y = y - 0.5f;
        verts[2].x = worldXBase + x - 0.5f + sizeU;
        verts[2].y = y - 0.5f + sizeV;
        verts[3].x = worldXBase + x - 0.5f;
        verts[3].y = y - 0.5f + sizeV;
        
        for (int i = 0; i < 4; i++)
        {
            verts[i].z = zWorld;
            float u = (verts[i].x - (worldXBase + x - 0.5f)) / sizeU;
            float v = (verts[i].y - (y - 0.5f)) / sizeV;
            verts[i].u = (tileX + u) * TILE_SIZE;
            verts[i].v = (tileY + v) * TILE_SIZE;
        }
    }
    
    // Выбираем порядок индексов в зависимости от грани
    int indices[6]{};
    if (face == 0 || face == 5)
    {
        // Инвертированный порядок (меняем местами вершины 1 и 2 в каждом треугольнике)
        indices[0] = 0; indices[1] = 2; indices[2] = 1;
        indices[3] = 0; indices[4] = 3; indices[5] = 2;
    }
    else
    {
        // Стандартный порядок (CCW)
        indices[0] = 0; indices[1] = 1; indices[2] = 2;
        indices[3] = 0; indices[4] = 2; indices[5] = 3;
    }

    for (int idx : indices)
    {
        vertices.push_back(verts[idx].x);
        vertices.push_back(verts[idx].y);
        vertices.push_back(verts[idx].z);
        vertices.push_back(verts[idx].u);
        vertices.push_back(verts[idx].v);
    }
}

/*
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
*/

bool Chunk::IsVisible(const glm::mat4& viewProj)
{
    return false;
}

void Chunk::BuildMesh()
{
    vertices.clear();

    /*for (int x = 0; x < SIZE_X; x++)
        for (int y = 0; y < SIZE_Y; y++)
            for (int z = 0; z < SIZE_Z; z++)
                if (blocks[x][y][z] != 0)
                    AddCube(x, y, z);
                    */
    // +Y
    for (int y = 0; y < SIZE_Y; y++)
    {
        int mask[SIZE_X][SIZE_Z]{};
        for (int x = 0; x < SIZE_X; x++)
        {
            for (int z = 0; z < SIZE_Z; z++)
            {
                BlockType type = blocks[x][y][z];
                if (type != AIR && !IsBlockSolid(x, y + 1, z))
                    mask[x][z] = blockDatabase[type].top;
                else
                    mask[x][z] = -1;
            }
        }

        for (int x = 0; x < SIZE_X; x++)
        {
            for (int z = 0; z < SIZE_Z; z++)
            {
                if (mask[x][z] == -1) continue;

                int tile = mask[x][z];
                int w = 1;
                while (x + w < SIZE_X && mask[x + w][z] == tile) w++;

                int h = 1;
                bool canExpand = true;
                while (z + h < SIZE_Z && canExpand)
                {
                    for (int i = 0; i < w; i++)
                        if (mask[x + i][z + h] != tile) { canExpand = false; break; }
                    if (canExpand) h++;
                }

                AddGreedyFace(x, y, z, w, h, 2, tile);

                for (int i = 0; i < w; i++)
                    for (int j = 0; j < h; j++)
                        mask[x + i][z + j] = -1;
            }
        }
    }

    // -Y
    for (int y = 0; y < SIZE_Y; y++)
    {
        int mask[SIZE_X][SIZE_Z]{};
        for (int x = 0; x < SIZE_X; x++)
        {
            for (int z = 0; z < SIZE_Z; z++)
            {
                BlockType type = blocks[x][y][z];
                if (type != AIR && !IsBlockSolid(x, y - 1, z))
                    mask[x][z] = blockDatabase[type].bottom;
                else
                    mask[x][z] = -1;
            }
        }

        for (int x = 0; x < SIZE_X; x++)
        {
            for (int z = 0; z < SIZE_Z; z++)
            {
                if (mask[x][z] == -1) continue;

                int tile = mask[x][z];
                int w = 1;
                while (x + w < SIZE_X && mask[x + w][z] == tile) w++;
                int h = 1;
                bool canExpand = true;
                while (z + h < SIZE_Z && canExpand)
                {
                    for (int i = 0; i < w; i++)
                        if (mask[x + i][z + h] != tile) { canExpand = false; break; }
                    if (canExpand) h++;
                }

                AddGreedyFace(x, y, z, w, h, 3, tile);

                for (int i = 0; i < w; i++)
                    for (int j = 0; j < h; j++)
                        mask[x + i][z + j] = -1;
            }
        }
    }

    // +X
    for (int x = 0; x < SIZE_X; x++)
    {
        int mask[SIZE_Y][SIZE_Z]{};
        for (int y = 0; y < SIZE_Y; y++)
        {
            for (int z = 0; z < SIZE_Z; z++)
            {
                BlockType type = blocks[x][y][z];
                if (type != AIR && !IsBlockSolid(x + 1, y, z))
                    mask[y][z] = blockDatabase[type].side;
                else
                    mask[y][z] = -1;
            }
        }

        for (int y = 0; y < SIZE_Y; y++)
        {
            for (int z = 0; z < SIZE_Z; z++)
            {
                if (mask[y][z] == -1) continue;

                int tile = mask[y][z];
                // ширина по Z
                int w = 1;
                while (z + w < SIZE_Z && mask[y][z + w] == tile) w++;
                // высота по Y
                int h = 1;
                bool canExpand = true;
                while (y + h < SIZE_Y && canExpand)
                {
                    for (int i = 0; i < w; i++)
                        if (mask[y + h][z + i] != tile) { canExpand = false; break; }
                    if (canExpand) h++;
                }

                AddGreedyFace(x, y, z, w, h, 0, tile);

                for (int i = 0; i < w; i++)
                    for (int j = 0; j < h; j++)
                        mask[y + j][z + i] = -1;
            }
        }
    }

    // -X
    for (int x = 0; x < SIZE_X; x++)
    {
        int mask[SIZE_Y][SIZE_Z]{};
        for (int y = 0; y < SIZE_Y; y++)
        {
            for (int z = 0; z < SIZE_Z; z++)
            {
                BlockType type = blocks[x][y][z];
                if (type != AIR && !IsBlockSolid(x - 1, y, z))
                    mask[y][z] = blockDatabase[type].side;
                else
                    mask[y][z] = -1;
            }
        }

        for (int y = 0; y < SIZE_Y; y++)
        {
            for (int z = 0; z < SIZE_Z; z++)
            {
                if (mask[y][z] == -1) continue;

                int tile = mask[y][z];
                int w = 1;
                while (z + w < SIZE_Z && mask[y][z + w] == tile) w++;
                int h = 1;
                bool canExpand = true;
                while (y + h < SIZE_Y && canExpand)
                {
                    for (int i = 0; i < w; i++)
                        if (mask[y + h][z + i] != tile) { canExpand = false; break; }
                    if (canExpand) h++;
                }

                AddGreedyFace(x, y, z, w, h, 1, tile);

                for (int i = 0; i < w; i++)
                    for (int j = 0; j < h; j++)
                        mask[y + j][z + i] = -1;
            }
        }
    }

    // +Z
    for (int z = 0; z < SIZE_Z; z++)
    {
        int mask[SIZE_X][SIZE_Y]{};
        for (int x = 0; x < SIZE_X; x++)
        {
            for (int y = 0; y < SIZE_Y; y++)
            {
                BlockType type = blocks[x][y][z];
                if (type != AIR && !IsBlockSolid(x, y, z + 1))
                    mask[x][y] = blockDatabase[type].side;
                else
                    mask[x][y] = -1;
            }
        }

        for (int x = 0; x < SIZE_X; x++)
        {
            for (int y = 0; y < SIZE_Y; y++)
            {
                if (mask[x][y] == -1) continue;

                int tile = mask[x][y];
                int w = 1; // по X
                while (x + w < SIZE_X && mask[x + w][y] == tile) w++;
                int h = 1; // по Y
                bool canExpand = true;
                while (y + h < SIZE_Y && canExpand)
                {
                    for (int i = 0; i < w; i++)
                        if (mask[x + i][y + h] != tile) { canExpand = false; break; }
                    if (canExpand) h++;
                }

                AddGreedyFace(x, y, z, w, h, 4, tile);

                for (int i = 0; i < w; i++)
                    for (int j = 0; j < h; j++)
                        mask[x + i][y + j] = -1;
            }
        }
    }

    // -Z
    for (int z = 0; z < SIZE_Z; z++)
    {
        int mask[SIZE_X][SIZE_Y]{};
        for (int x = 0; x < SIZE_X; x++)
        {
            for (int y = 0; y < SIZE_Y; y++)
            {
                BlockType type = blocks[x][y][z];
                if (type != AIR && !IsBlockSolid(x, y, z - 1))
                    mask[x][y] = blockDatabase[type].side;
                else
                    mask[x][y] = -1;
            }
        }

        for (int x = 0; x < SIZE_X; x++)
        {
            for (int y = 0; y < SIZE_Y; y++)
            {
                if (mask[x][y] == -1) continue;

                int tile = mask[x][y];
                int w = 1; // по X
                while (x + w < SIZE_X && mask[x + w][y] == tile) w++;
                int h = 1; // по Y
                bool canExpand = true;
                while (y + h < SIZE_Y && canExpand)
                {
                    for (int i = 0; i < w; i++)
                        if (mask[x + i][y + h] != tile) { canExpand = false; break; }
                    if (canExpand) h++;
                }

                AddGreedyFace(x, y, z, w, h, 5, tile);

                for (int i = 0; i < w; i++)
                    for (int j = 0; j < h; j++)
                        mask[x + i][y + j] = -1;
            }
        }
    }

    if (VAO == 0)
    {
        glGenVertexArrays(1, &VAO);
        glGenBuffers(1, &VBO);
    }

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