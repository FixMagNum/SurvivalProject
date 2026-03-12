#include <glad/glad.h>
#include "chunk.h"
#include "world.h"
#include "block.h"
#include "FastNoiseLite.h"
#include <algorithm>

constexpr int ATLAS_SIZE = 16;
constexpr float TILE_SIZE = 1.0f / ATLAS_SIZE;

static int Tile(int x, int y)
{
    return y * ATLAS_SIZE + x;
}

BlockData blockDatabase[] =
{
    { 0, 0, 0 },                            // AIR
    { Tile(2,0), Tile(0,0), Tile(1,0) },    // GRASS
    { Tile(0,0), Tile(0,0), Tile(0,0) },    // DIRT
    { Tile(3,0), Tile(3,0), Tile(3,0) },    // STONE
	{ Tile(5,0), Tile(5,0), Tile(4,0) },    // OAK_PLANKS
	{ Tile(6,0), Tile(6,0), Tile(6,0) },    // GLASS
};

Chunk::Chunk(int chunkX, int chunkZ, World* worldPtr)
{
    chunkPos = { chunkX, chunkZ };
    world = worldPtr;

    float worldX = chunkPos.x * SIZE_X;
    float worldZ = chunkPos.y * SIZE_Z;

    bounds.min = glm::vec3(worldX, 0, worldZ);
    bounds.max = glm::vec3(worldX + SIZE_X, SIZE_Y, worldZ + SIZE_Z);

    for (int x = 0; x < SIZE_X; x++)
        for (int y = 0; y < SIZE_Y; y++)
            for (int z = 0; z < SIZE_Z; z++)
                blocks[x][y][z] = AIR;

    VAO = 0; VBO = 0; EBO = 0;
}

void Chunk::Generate()
{
    FastNoiseLite noise;
    noise.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
    noise.SetFrequency(0.005f);
    noise.SetSeed(1337);

    for (int x = 0; x < SIZE_X; x++)
    {
        for (int z = 0; z < SIZE_Z; z++)
        {
            float worldX = x + chunkPos.x * SIZE_X;
            float worldZ = z + chunkPos.y * SIZE_Z;

            float noiseVal = noise.GetNoise(worldX, worldZ);
            int surfaceY = (int)(60.0f + noiseVal * 60.0f);
            surfaceY = std::clamp(surfaceY, 1, SIZE_Y - 2);

            for (int y = 0; y < SIZE_Y; y++)
            {
                if (y == 0)                blocks[x][y][z] = STONE;
                else if (y < surfaceY - 3) blocks[x][y][z] = STONE;
                else if (y < surfaceY)     blocks[x][y][z] = DIRT;
                else if (y == surfaceY)    blocks[x][y][z] = GRASS;
                else                       blocks[x][y][z] = AIR;
            }
        }
    }

    minY = SIZE_Y; maxY = 0;
    for (int x = 0; x < SIZE_X; x++)
        for (int z = 0; z < SIZE_Z; z++)
            for (int y = 0; y < SIZE_Y; y++)
                if (blocks[x][y][z] != AIR) {
                    minY = std::min(minY, y);
                    maxY = std::max(maxY, y);
                }
}

bool Chunk::IsBlockSolid(int x, int y, int z)
{
    int worldX = x + chunkPos.x * SIZE_X;
    int worldZ = z + chunkPos.y * SIZE_Z;
    return world->GetBlock(worldX, y, worldZ) != AIR;
}

int Chunk::ComputeAO(int side1, int side2, int corner)
{
    if (side1 && side2) return 0;
    return 3 - (side1 + side2 + corner);
}

// Формат вершины: pos(3) + uv(2) + tileOffset(2) + ao(1) = 8 floats
void Chunk::AddQuad(
    glm::vec3 origin,
    glm::vec3 axis1, int w,
    glm::vec3 axis2, int h,
    int tileID, bool flipWinding,
    float ao0, float ao1, float ao2, float ao3)
{
    float worldOffsetX = chunkPos.x * SIZE_X;
    float worldOffsetZ = chunkPos.y * SIZE_Z;

    int tileX = tileID % ATLAS_SIZE;
    int tileY = tileID / ATLAS_SIZE;
    float u0 = tileX * TILE_SIZE;
    float v0 = tileY * TILE_SIZE;

    glm::vec3 p0 = origin;
    glm::vec3 p1 = origin + axis1 * (float)w;
    glm::vec3 p2 = origin + axis1 * (float)w + axis2 * (float)h;
    glm::vec3 p3 = origin + axis2 * (float)h;

    auto push = [&](glm::vec3 p, float u, float v, float ao) {
        vertices.push_back(p.x + worldOffsetX);
        vertices.push_back(p.y);
        vertices.push_back(p.z + worldOffsetZ);
        vertices.push_back(u);
        vertices.push_back(v);
        vertices.push_back(u0);
        vertices.push_back(v0);
        vertices.push_back(ao);
        };

    uint32_t base = (uint32_t)(vertices.size() / 8);

    push(p0, 0, 0, ao0);
    push(p1, (float)w, 0, ao1);
    push(p2, (float)w, (float)h, ao2);
    push(p3, 0, (float)h, ao3);

    if (!flipWinding)
    {
        indices.push_back(base + 0);
        indices.push_back(base + 2);
        indices.push_back(base + 1);
        indices.push_back(base + 0);
        indices.push_back(base + 3);
        indices.push_back(base + 2);
    }
    else
    {
        indices.push_back(base + 0);
        indices.push_back(base + 1);
        indices.push_back(base + 2);
        indices.push_back(base + 0);
        indices.push_back(base + 2);
        indices.push_back(base + 3);
    }
}

// GenerateMeshData — только CPU работа, можно вызывать из рабочего потока
void Chunk::GenerateMeshData()
{
    vertices.clear();
    indices.clear();

    // Пересчитываем minY/maxY — они могли устареть после SetBlock
    minY = SIZE_Y;
    maxY = 0;
    for (int x = 0; x < SIZE_X; x++)
        for (int z = 0; z < SIZE_Z; z++)
            for (int y = 0; y < SIZE_Y; y++)
                if (blocks[x][y][z] != AIR) {
                    minY = std::min(minY, y);
                    maxY = std::max(maxY, y);
                }
    if (minY > maxY) return; // чанк пустой

    // getBlock с поддержкой соседних чанков
    auto getBlock = [&](int x, int y, int z) -> BlockType {
        if (y < 0 || y >= SIZE_Y) return AIR;
        if (x >= 0 && x < SIZE_X && z >= 0 && z < SIZE_Z)
            return blocks[x][y][z];
        if (x < 0 && neighborNX) {
            int lx = x + SIZE_X;
            if (lx >= 0 && lx < SIZE_X && z >= 0 && z < SIZE_Z)
                return neighborNX->blocks[lx][y][z];
        }
        if (x >= SIZE_X && neighborPX) {
            int lx = x - SIZE_X;
            if (lx >= 0 && lx < SIZE_X && z >= 0 && z < SIZE_Z)
                return neighborPX->blocks[lx][y][z];
        }
        if (z < 0 && neighborNZ) {
            int lz = z + SIZE_Z;
            if (lz >= 0 && lz < SIZE_Z && x >= 0 && x < SIZE_X)
                return neighborNZ->blocks[x][y][lz];
        }
        if (z >= SIZE_Z && neighborPZ) {
            int lz = z - SIZE_Z;
            if (lz >= 0 && lz < SIZE_Z && x >= 0 && x < SIZE_X)
                return neighborPZ->blocks[x][y][lz];
        }
        return AIR;
        };

    auto getTile = [&](BlockType type, int direction) -> int {
        if (type == AIR) return -1;
        BlockData& bd = blockDatabase[type];
        if (direction == 0) return bd.top;
        if (direction == 1) return bd.bottom;
        return bd.side;
        };

    auto solid = [&](int x, int y, int z) -> int {
        return getBlock(x, y, z) != AIR ? 1 : 0;
        };

    auto aoVal = [&](int s1, int s2, int c) -> float {
        return ComputeAO(s1, s2, c) / 3.0f;
        };

    // Ключевая структура: ячейка маски содержит tileID + AO
    // Greedy meshing объединяет блоки ТОЛЬКО если ячейки полностью совпадают.
    // Благодаря этому AO не растягивается на весь quad.
    struct MaskCell {
        int   tileID = -1;
        float ao[4] = { 1.f, 1.f, 1.f, 1.f };

        bool operator==(const MaskCell& o) const {
            if (tileID != o.tileID) return false;
            return ao[0] == o.ao[0] && ao[1] == o.ao[1] &&
                ao[2] == o.ao[2] && ao[3] == o.ao[3];
        }
        bool empty() const { return tileID < 0; }
    };

    // +Y  (top faces)
    for (int y = minY; y <= maxY + 1; y++)
    {
        MaskCell mask[SIZE_X][SIZE_Z];
        bool     used[SIZE_X][SIZE_Z];
        memset(used, 0, sizeof(used));

        for (int x = 0; x < SIZE_X; x++)
            for (int z = 0; z < SIZE_Z; z++)
            {
                MaskCell& cell = mask[x][z];
                BlockType cur = getBlock(x, y, z);
                if (cur != AIR && getBlock(x, y + 1, z) == AIR)
                {
                    cell.tileID = getTile(cur, 0);
                    // Вершины quad'а в Y+1 плоскости (по часовой: -x-z, +x-z, +x+z, -x+z)
                    cell.ao[0] = aoVal(solid(x - 1, y + 1, z), solid(x, y + 1, z - 1), solid(x - 1, y + 1, z - 1));
                    cell.ao[1] = aoVal(solid(x + 1, y + 1, z), solid(x, y + 1, z - 1), solid(x + 1, y + 1, z - 1));
                    cell.ao[2] = aoVal(solid(x + 1, y + 1, z), solid(x, y + 1, z + 1), solid(x + 1, y + 1, z + 1));
                    cell.ao[3] = aoVal(solid(x - 1, y + 1, z), solid(x, y + 1, z + 1), solid(x - 1, y + 1, z + 1));
                }
                else cell.tileID = -1;
            }

        for (int x = 0; x < SIZE_X; x++)
            for (int z = 0; z < SIZE_Z; z++)
            {
                if (used[x][z] || mask[x][z].empty()) continue;
                MaskCell& ref = mask[x][z];

                int dz = 1;
                while (z + dz < SIZE_Z && !used[x][z + dz] && mask[x][z + dz] == ref) dz++;

                int dx = 1;
                while (x + dx < SIZE_X) {
                    bool ok = true;
                    for (int k = 0; k < dz; k++)
                        if (used[x + dx][z + k] || !(mask[x + dx][z + k] == ref)) { ok = false; break; }
                    if (!ok) break;
                    dx++;
                }

                for (int ix = 0; ix < dx; ix++)
                    for (int iz = 0; iz < dz; iz++)
                        used[x + ix][z + iz] = true;

                AddQuad(glm::vec3(x, y + 1, z),
                    glm::vec3(1, 0, 0), dx,
                    glm::vec3(0, 0, 1), dz,
                    ref.tileID, false,
                    ref.ao[0], ref.ao[1], ref.ao[2], ref.ao[3]);
            }
    }

    // -Y  (bottom faces)
    for (int y = minY; y <= maxY + 1; y++)
    {
        MaskCell mask[SIZE_X][SIZE_Z];
        bool     used[SIZE_X][SIZE_Z];
        memset(used, 0, sizeof(used));

        for (int x = 0; x < SIZE_X; x++)
            for (int z = 0; z < SIZE_Z; z++)
            {
                MaskCell& cell = mask[x][z];
                BlockType cur = getBlock(x, y, z);
                if (cur != AIR && getBlock(x, y - 1, z) == AIR)
                {
                    cell.tileID = getTile(cur, 1);
                    cell.ao[0] = aoVal(solid(x - 1, y - 1, z), solid(x, y - 1, z - 1), solid(x - 1, y - 1, z - 1));
                    cell.ao[1] = aoVal(solid(x + 1, y - 1, z), solid(x, y - 1, z - 1), solid(x + 1, y - 1, z - 1));
                    cell.ao[2] = aoVal(solid(x + 1, y - 1, z), solid(x, y - 1, z + 1), solid(x + 1, y - 1, z + 1));
                    cell.ao[3] = aoVal(solid(x - 1, y - 1, z), solid(x, y - 1, z + 1), solid(x - 1, y - 1, z + 1));
                }
                else cell.tileID = -1;
            }

        for (int x = 0; x < SIZE_X; x++)
            for (int z = 0; z < SIZE_Z; z++)
            {
                if (used[x][z] || mask[x][z].empty()) continue;
                MaskCell& ref = mask[x][z];

                int dz = 1;
                while (z + dz < SIZE_Z && !used[x][z + dz] && mask[x][z + dz] == ref) dz++;

                int dx = 1;
                while (x + dx < SIZE_X) {
                    bool ok = true;
                    for (int k = 0; k < dz; k++)
                        if (used[x + dx][z + k] || !(mask[x + dx][z + k] == ref)) { ok = false; break; }
                    if (!ok) break;
                    dx++;
                }

                for (int ix = 0; ix < dx; ix++)
                    for (int iz = 0; iz < dz; iz++)
                        used[x + ix][z + iz] = true;

                AddQuad(glm::vec3(x, y, z),
                    glm::vec3(1, 0, 0), dx,
                    glm::vec3(0, 0, 1), dz,
                    ref.tileID, true,
                    ref.ao[0], ref.ao[1], ref.ao[2], ref.ao[3]);
            }
    }

    // +X  (right faces)
    for (int x = 0; x < SIZE_X; x++)
    {
        MaskCell mask[SIZE_Z][SIZE_Y];
        bool     used[SIZE_Z][SIZE_Y];
        memset(used, 0, sizeof(used));

        for (int z = 0; z < SIZE_Z; z++)
            for (int y = minY; y <= maxY; y++)
            {
                MaskCell& cell = mask[z][y];
                BlockType cur = getBlock(x, y, z);
                if (cur != AIR && getBlock(x + 1, y, z) == AIR)
                {
                    cell.tileID = getTile(cur, 2);
                    cell.ao[0] = aoVal(solid(x + 1, y - 1, z), solid(x + 1, y, z - 1), solid(x + 1, y - 1, z - 1));
                    cell.ao[1] = aoVal(solid(x + 1, y - 1, z), solid(x + 1, y, z + 1), solid(x + 1, y - 1, z + 1));
                    cell.ao[2] = aoVal(solid(x + 1, y + 1, z), solid(x + 1, y, z + 1), solid(x + 1, y + 1, z + 1));
                    cell.ao[3] = aoVal(solid(x + 1, y + 1, z), solid(x + 1, y, z - 1), solid(x + 1, y + 1, z - 1));
                }
                else cell.tileID = -1;
            }

        for (int z = 0; z < SIZE_Z; z++)
            for (int y = minY; y <= maxY; y++)
            {
                if (used[z][y] || mask[z][y].empty()) continue;
                MaskCell& ref = mask[z][y];

                int dy = 1;
                while (y + dy <= maxY && !used[z][y + dy] && mask[z][y + dy] == ref) dy++;

                int dz = 1;
                while (z + dz < SIZE_Z) {
                    bool ok = true;
                    for (int k = 0; k < dy; k++)
                        if (used[z + dz][y + k] || !(mask[z + dz][y + k] == ref)) { ok = false; break; }
                    if (!ok) break;
                    dz++;
                }

                for (int iz = 0; iz < dz; iz++)
                    for (int iy = 0; iy < dy; iy++)
                        used[z + iz][y + iy] = true;

                AddQuad(glm::vec3(x + 1, y, z),
                    glm::vec3(0, 0, 1), dz,
                    glm::vec3(0, 1, 0), dy,
                    ref.tileID, false,
                    ref.ao[0], ref.ao[1], ref.ao[2], ref.ao[3]);
            }
    }

    // -X  (left faces)
    for (int x = 0; x < SIZE_X; x++)
    {
        MaskCell mask[SIZE_Z][SIZE_Y];
        bool     used[SIZE_Z][SIZE_Y];
        memset(used, 0, sizeof(used));

        for (int z = 0; z < SIZE_Z; z++)
            for (int y = minY; y <= maxY; y++)
            {
                MaskCell& cell = mask[z][y];
                BlockType cur = getBlock(x, y, z);
                if (cur != AIR && getBlock(x - 1, y, z) == AIR)
                {
                    cell.tileID = getTile(cur, 2);
                    cell.ao[0] = aoVal(solid(x - 1, y - 1, z), solid(x - 1, y, z - 1), solid(x - 1, y - 1, z - 1));
                    cell.ao[1] = aoVal(solid(x - 1, y - 1, z), solid(x - 1, y, z + 1), solid(x - 1, y - 1, z + 1));
                    cell.ao[2] = aoVal(solid(x - 1, y + 1, z), solid(x - 1, y, z + 1), solid(x - 1, y + 1, z + 1));
                    cell.ao[3] = aoVal(solid(x - 1, y + 1, z), solid(x - 1, y, z - 1), solid(x - 1, y + 1, z - 1));
                }
                else cell.tileID = -1;
            }

        for (int z = 0; z < SIZE_Z; z++)
            for (int y = minY; y <= maxY; y++)
            {
                if (used[z][y] || mask[z][y].empty()) continue;
                MaskCell& ref = mask[z][y];

                int dy = 1;
                while (y + dy <= maxY && !used[z][y + dy] && mask[z][y + dy] == ref) dy++;

                int dz = 1;
                while (z + dz < SIZE_Z) {
                    bool ok = true;
                    for (int k = 0; k < dy; k++)
                        if (used[z + dz][y + k] || !(mask[z + dz][y + k] == ref)) { ok = false; break; }
                    if (!ok) break;
                    dz++;
                }

                for (int iz = 0; iz < dz; iz++)
                    for (int iy = 0; iy < dy; iy++)
                        used[z + iz][y + iy] = true;

                AddQuad(glm::vec3(x, y, z),
                    glm::vec3(0, 0, 1), dz,
                    glm::vec3(0, 1, 0), dy,
                    ref.tileID, true,
                    ref.ao[0], ref.ao[1], ref.ao[2], ref.ao[3]);
            }
    }

    // +Z  (front faces)
    for (int z = 0; z < SIZE_Z; z++)
    {
        MaskCell mask[SIZE_X][SIZE_Y];
        bool     used[SIZE_X][SIZE_Y];
        memset(used, 0, sizeof(used));

        for (int x = 0; x < SIZE_X; x++)
            for (int y = minY; y <= maxY; y++)
            {
                MaskCell& cell = mask[x][y];
                BlockType cur = getBlock(x, y, z);
                if (cur != AIR && getBlock(x, y, z + 1) == AIR)
                {
                    cell.tileID = getTile(cur, 2);
                    cell.ao[0] = aoVal(solid(x - 1, y, z + 1), solid(x, y - 1, z + 1), solid(x - 1, y - 1, z + 1));
                    cell.ao[1] = aoVal(solid(x + 1, y, z + 1), solid(x, y - 1, z + 1), solid(x + 1, y - 1, z + 1));
                    cell.ao[2] = aoVal(solid(x + 1, y, z + 1), solid(x, y + 1, z + 1), solid(x + 1, y + 1, z + 1));
                    cell.ao[3] = aoVal(solid(x - 1, y, z + 1), solid(x, y + 1, z + 1), solid(x - 1, y + 1, z + 1));
                }
                else cell.tileID = -1;
            }

        for (int x = 0; x < SIZE_X; x++)
            for (int y = minY; y <= maxY; y++)
            {
                if (used[x][y] || mask[x][y].empty()) continue;
                MaskCell& ref = mask[x][y];

                int dy = 1;
                while (y + dy <= maxY && !used[x][y + dy] && mask[x][y + dy] == ref) dy++;

                int dx = 1;
                while (x + dx < SIZE_X) {
                    bool ok = true;
                    for (int k = 0; k < dy; k++)
                        if (used[x + dx][y + k] || !(mask[x + dx][y + k] == ref)) { ok = false; break; }
                    if (!ok) break;
                    dx++;
                }

                for (int ix = 0; ix < dx; ix++)
                    for (int iy = 0; iy < dy; iy++)
                        used[x + ix][y + iy] = true;

                AddQuad(glm::vec3(x, y, z + 1),
                    glm::vec3(1, 0, 0), dx,
                    glm::vec3(0, 1, 0), dy,
                    ref.tileID, true,
                    ref.ao[0], ref.ao[1], ref.ao[2], ref.ao[3]);
            }
    }

    // -Z  (back faces)
    for (int z = 0; z < SIZE_Z; z++)
    {
        MaskCell mask[SIZE_X][SIZE_Y];
        bool     used[SIZE_X][SIZE_Y];
        memset(used, 0, sizeof(used));

        for (int x = 0; x < SIZE_X; x++)
            for (int y = minY; y <= maxY; y++)
            {
                MaskCell& cell = mask[x][y];
                BlockType cur = getBlock(x, y, z);
                if (cur != AIR && getBlock(x, y, z - 1) == AIR)
                {
                    cell.tileID = getTile(cur, 2);
                    cell.ao[0] = aoVal(solid(x - 1, y, z - 1), solid(x, y - 1, z - 1), solid(x - 1, y - 1, z - 1));
                    cell.ao[1] = aoVal(solid(x + 1, y, z - 1), solid(x, y - 1, z - 1), solid(x + 1, y - 1, z - 1));
                    cell.ao[2] = aoVal(solid(x + 1, y, z - 1), solid(x, y + 1, z - 1), solid(x + 1, y + 1, z - 1));
                    cell.ao[3] = aoVal(solid(x - 1, y, z - 1), solid(x, y + 1, z - 1), solid(x - 1, y + 1, z - 1));
                }
                else cell.tileID = -1;
            }

        for (int x = 0; x < SIZE_X; x++)
            for (int y = minY; y <= maxY; y++)
            {
                if (used[x][y] || mask[x][y].empty()) continue;
                MaskCell& ref = mask[x][y];

                int dy = 1;
                while (y + dy <= maxY && !used[x][y + dy] && mask[x][y + dy] == ref) dy++;

                int dx = 1;
                while (x + dx < SIZE_X) {
                    bool ok = true;
                    for (int k = 0; k < dy; k++)
                        if (used[x + dx][y + k] || !(mask[x + dx][y + k] == ref)) { ok = false; break; }
                    if (!ok) break;
                    dx++;
                }

                for (int ix = 0; ix < dx; ix++)
                    for (int iy = 0; iy < dy; iy++)
                        used[x + ix][y + iy] = true;

                AddQuad(glm::vec3(x, y, z),
                    glm::vec3(1, 0, 0), dx,
                    glm::vec3(0, 1, 0), dy,
                    ref.tileID, false,
                    ref.ao[0], ref.ao[1], ref.ao[2], ref.ao[3]);
            }
    }
    // (конец GenerateMeshData — только CPU данные, GPU ещё не трогаем)
}

// UploadToGPU — ТОЛЬКО из главного потока (OpenGL не thread-safe)
void Chunk::UploadToGPU()
{
    if (VAO == 0)
    {
        glGenVertexArrays(1, &VAO);
        glGenBuffers(1, &VBO);
        glGenBuffers(1, &EBO);
    }

    glBindVertexArray(VAO);

    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_DYNAMIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint32_t), indices.data(), GL_DYNAMIC_DRAW);

    constexpr int STRIDE = 8 * sizeof(float);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, STRIDE, (void*)0);
    glEnableVertexAttribArray(0);

    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, STRIDE, (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, STRIDE, (void*)(5 * sizeof(float)));
    glEnableVertexAttribArray(2);

    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, STRIDE, (void*)(7 * sizeof(float)));
    glEnableVertexAttribArray(3);

    glBindVertexArray(0);

    state.store(ChunkState::Uploaded);
}

// BuildMesh — совместимый метод для rebuild после break/place блока.
// Вызывается только из главного потока.
void Chunk::BuildMesh()
{
    GenerateMeshData();
    UploadToGPU();
}

// FreeGPU — освобождает OpenGL объекты (только из главного потока)
void Chunk::FreeGPU()
{
    if (VAO) { glDeleteVertexArrays(1, &VAO); VAO = 0; }
    if (VBO) { glDeleteBuffers(1, &VBO);      VBO = 0; }
    if (EBO) { glDeleteBuffers(1, &EBO);      EBO = 0; }
    vertices.clear();
    indices.clear();
    state.store(ChunkState::Empty);
}

void Chunk::Draw()
{
    glBindVertexArray(VAO);
    glDrawElements(GL_TRIANGLES, (GLsizei)indices.size(), GL_UNSIGNED_INT, 0);
}