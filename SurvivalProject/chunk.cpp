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

    BlockType block = world->GetBlock(worldX, y, worldZ);

    return block != AIR;
}

void Chunk::AddQuad(
    glm::vec3 origin,
    glm::vec3 axis1, int w,
    glm::vec3 axis2, int h,
    int tileID, bool flipWinding)
{
    float worldOffsetX = chunkPos.x * SIZE_X;
    float worldOffsetZ = chunkPos.y * SIZE_Z;

    int tileX = tileID % ATLAS_SIZE;
    int tileY = tileID / ATLAS_SIZE;

    // Смещение тайла в атласе (передаётся в шейдер как TileOffset)
    float u0 = tileX * TILE_SIZE;
    float v0 = tileY * TILE_SIZE;

    // UV в "тайловом" пространстве — просто 0..w и 0..h
    // fract() в шейдере обеспечит повторение
    glm::vec3 p0 = origin;
    glm::vec3 p1 = origin + axis1 * (float)w;
    glm::vec3 p2 = origin + axis1 * (float)w + axis2 * (float)h;
    glm::vec3 p3 = origin + axis2 * (float)h;

    // Формат вершины: x, y, z,  u, v,  tileU, tileV
    // u,v     — тайловые координаты (0..w, 0..h)
    // tileU,V — смещение тайла в атласе
    auto push = [&](glm::vec3 p, float u, float v) {
        vertices.push_back(p.x + worldOffsetX);
        vertices.push_back(p.y);
        vertices.push_back(p.z + worldOffsetZ);
        vertices.push_back(u);
        vertices.push_back(v);
        vertices.push_back(u0);
        vertices.push_back(v0);
        };

    if (!flipWinding)
    {
        push(p0, 0, 0);
        push(p2, w, h);
        push(p1, w, 0);
        push(p0, 0, 0);
        push(p3, 0, h);
        push(p2, w, h);
    }
    else
    {
        push(p0, 0, 0);
        push(p1, w, 0);
        push(p2, w, h);
        push(p0, 0, 0);
        push(p2, w, h);
        push(p3, 0, h);
    }
}


void Chunk::BuildMesh()
{
    vertices.clear();

    float worldOffsetX = chunkPos.x * SIZE_X;
    float worldOffsetZ = chunkPos.y * SIZE_Z;

    // Вспомогательная лямбда: возвращает BlockType внутри чанка или AIR за границей
    auto getBlock = [&](int x, int y, int z) -> BlockType {
        if (y < 0 || y >= SIZE_Y) return AIR;
        if (x >= 0 && x < SIZE_X && z >= 0 && z < SIZE_Z)
            return blocks[x][y][z]; // внутри чанка — берём напрямую
        int worldX = x + chunkPos.x * SIZE_X;
        int worldZ = z + chunkPos.y * SIZE_Z;
        return world->GetBlock(worldX, y, worldZ); // граница — спрашиваем мир
        };

    // Вспомогательная лямбда: возвращает тайл для данного блока и направления
    // direction: 0=+Y(top), 1=-Y(bottom), иначе side
    auto getTile = [&](BlockType type, int direction) -> int {
        if (type == AIR) return -1;
        BlockData& bd = blockDatabase[type];
        if (direction == 0) return bd.top;
        if (direction == 1) return bd.bottom;
        return bd.side;
        };

    // === Ось Y: грани +Y и -Y ===
    // Проходим по каждому слою y, строим XZ-маску
    for (int y = minY; y <= maxY + 1; y++)
    {
        // +Y (смотрит вверх)
        {
            // mask[x][z] = тайл грани, или -1 если грань не видна
            static int mask[SIZE_X][SIZE_Z];
            for (int x = 0; x < SIZE_X; x++)
                for (int z = 0; z < SIZE_Z; z++)
                {
                    BlockType cur = getBlock(x, y, z);
                    BlockType above = getBlock(x, y + 1, z);
                    // Показываем грань +Y блока cur, если cur непрозрачный и above прозрачный
                    if (cur != AIR && above == AIR)
                        mask[x][z] = getTile(cur, 0);
                    else
                        mask[x][z] = -1;
                }

            // Greedy sweep по XZ
            static bool used[SIZE_X][SIZE_Z] = {};
            memset(used, 0, sizeof(used));
            for (int x = 0; x < SIZE_X; x++)
            {
                for (int z = 0; z < SIZE_Z; z++)
                {
                    if (used[x][z] || mask[x][z] < 0) continue;

                    int tileID = mask[x][z];

                    // Растягиваем по Z
                    int dz = 1;
                    while (z + dz < SIZE_Z && !used[x][z + dz] && mask[x][z + dz] == tileID)
                        dz++;

                    // Растягиваем по X
                    int dx = 1;
                    while (x + dx < SIZE_X)
                    {
                        bool rowOk = true;
                        for (int k = 0; k < dz; k++)
                            if (used[x + dx][z + k] || mask[x + dx][z + k] != tileID) { rowOk = false; break; }
                        if (!rowOk) break;
                        dx++;
                    }

                    // Помечаем использованными
                    for (int ix = 0; ix < dx; ix++)
                        for (int iz = 0; iz < dz; iz++)
                            used[x + ix][z + iz] = true;

                    // origin — нижний-левый угол quad'а на верхней грани блока (y+1)
                    glm::vec3 origin((float)x, (float)(y + 1), (float)z);
                    // axis1 вдоль X, axis2 вдоль Z
                    AddQuad(origin,
                        glm::vec3(1, 0, 0), dx,
                        glm::vec3(0, 0, 1), dz,
                        tileID, false);
                }
            }
        }

        // -Y (смотрит вниз)
        {
            static int mask[SIZE_X][SIZE_Z];
            for (int x = 0; x < SIZE_X; x++)
                for (int z = 0; z < SIZE_Z; z++)
                {
                    BlockType cur = getBlock(x, y, z);
                    BlockType below = getBlock(x, y - 1, z);
                    if (cur != AIR && below == AIR)
                        mask[x][z] = getTile(cur, 1);
                    else
                        mask[x][z] = -1;
                }

            static bool used[SIZE_X][SIZE_Z] = {};
            memset(used, 0, sizeof(used));
            for (int x = 0; x < SIZE_X; x++)
            {
                for (int z = 0; z < SIZE_Z; z++)
                {
                    if (used[x][z] || mask[x][z] < 0) continue;

                    int tileID = mask[x][z];

                    int dz = 1;
                    while (z + dz < SIZE_Z && !used[x][z + dz] && mask[x][z + dz] == tileID)
                        dz++;

                    int dx = 1;
                    while (x + dx < SIZE_X)
                    {
                        bool rowOk = true;
                        for (int k = 0; k < dz; k++)
                            if (used[x + dx][z + k] || mask[x + dx][z + k] != tileID) { rowOk = false; break; }
                        if (!rowOk) break;
                        dx++;
                    }

                    for (int ix = 0; ix < dx; ix++)
                        for (int iz = 0; iz < dz; iz++)
                            used[x + ix][z + iz] = true;

                    glm::vec3 origin((float)x, (float)y, (float)z);
                    AddQuad(origin,
                        glm::vec3(1, 0, 0), dx,
                        glm::vec3(0, 0, 1), dz,
                        tileID, true);
                }
            }
        }
    }

    // === Ось X: грани +X и -X ===
    for (int x = 0; x < SIZE_X; x++)
    {
        // +X
        {
            static int mask[SIZE_Y][SIZE_Z];
            for (int y = 0; y < SIZE_Y; y++)
                for (int z = 0; z < SIZE_Z; z++)
                {
                    BlockType cur = getBlock(x, y, z);
                    BlockType next = getBlock(x + 1, y, z);
                    if (cur != AIR && next == AIR)
                        mask[y][z] = getTile(cur, 2); // side
                    else
                        mask[y][z] = -1;
                }

            static bool used[SIZE_Y][SIZE_Z] = {};
            memset(used, 0, sizeof(used));
            for (int y = 0; y < SIZE_Y; y++)
            {
                for (int z = 0; z < SIZE_Z; z++)
                {
                    if (used[y][z] || mask[y][z] < 0) continue;

                    int tileID = mask[y][z];

                    int dz = 1;
                    while (z + dz < SIZE_Z && !used[y][z + dz] && mask[y][z + dz] == tileID)
                        dz++;

                    int dy = 1;
                    while (y + dy < SIZE_Y)
                    {
                        bool rowOk = true;
                        for (int k = 0; k < dz; k++)
                            if (used[y + dy][z + k] || mask[y + dy][z + k] != tileID) { rowOk = false; break; }
                        if (!rowOk) break;
                        dy++;
                    }

                    for (int iy = 0; iy < dy; iy++)
                        for (int iz = 0; iz < dz; iz++)
                            used[y + iy][z + iz] = true;

                    // Грань +X: origin на x+1, растянута по Z и Y
                    glm::vec3 origin((float)(x + 1), (float)y, (float)z);
                    AddQuad(origin,
                        glm::vec3(0, 0, 1), dz,
                        glm::vec3(0, 1, 0), dy,
                        tileID, false);
                }
            }
        }

        // -X
        {
            static int mask[SIZE_Y][SIZE_Z];
            for (int y = 0; y < SIZE_Y; y++)
                for (int z = 0; z < SIZE_Z; z++)
                {
                    BlockType cur = getBlock(x, y, z);
                    BlockType prev = getBlock(x - 1, y, z);
                    if (cur != AIR && prev == AIR)
                        mask[y][z] = getTile(cur, 2);
                    else
                        mask[y][z] = -1;
                }

            static bool used[SIZE_Y][SIZE_Z] = {};
            memset(used, 0, sizeof(used));
            for (int y = 0; y < SIZE_Y; y++)
            {
                for (int z = 0; z < SIZE_Z; z++)
                {
                    if (used[y][z] || mask[y][z] < 0) continue;

                    int tileID = mask[y][z];

                    int dz = 1;
                    while (z + dz < SIZE_Z && !used[y][z + dz] && mask[y][z + dz] == tileID)
                        dz++;

                    int dy = 1;
                    while (y + dy < SIZE_Y)
                    {
                        bool rowOk = true;
                        for (int k = 0; k < dz; k++)
                            if (used[y + dy][z + k] || mask[y + dy][z + k] != tileID) { rowOk = false; break; }
                        if (!rowOk) break;
                        dy++;
                    }

                    for (int iy = 0; iy < dy; iy++)
                        for (int iz = 0; iz < dz; iz++)
                            used[y + iy][z + iz] = true;

                    glm::vec3 origin((float)x, (float)y, (float)z);
                    AddQuad(origin,
                        glm::vec3(0, 0, 1), dz,
                        glm::vec3(0, 1, 0), dy,
                        tileID, true);
                }
            }
        }
    }

    // === Ось Z: грани +Z и -Z ===
    for (int z = 0; z < SIZE_Z; z++)
    {
        // +Z
        {
            static int mask[SIZE_X][SIZE_Y];
            for (int x = 0; x < SIZE_X; x++)
                for (int y = 0; y < SIZE_Y; y++)
                {
                    BlockType cur = getBlock(x, y, z);
                    BlockType next = getBlock(x, y, z + 1);
                    if (cur != AIR && next == AIR)
                        mask[x][y] = getTile(cur, 2);
                    else
                        mask[x][y] = -1;
                }

            static bool used[SIZE_X][SIZE_Y] = {};
            memset(used, 0, sizeof(used));
            for (int x = 0; x < SIZE_X; x++)
            {
                for (int y = 0; y < SIZE_Y; y++)
                {
                    if (used[x][y] || mask[x][y] < 0) continue;

                    int tileID = mask[x][y];

                    int dy = 1;
                    while (y + dy < SIZE_Y && !used[x][y + dy] && mask[x][y + dy] == tileID)
                        dy++;

                    int dx = 1;
                    while (x + dx < SIZE_X)
                    {
                        bool rowOk = true;
                        for (int k = 0; k < dy; k++)
                            if (used[x + dx][y + k] || mask[x + dx][y + k] != tileID) { rowOk = false; break; }
                        if (!rowOk) break;
                        dx++;
                    }

                    for (int ix = 0; ix < dx; ix++)
                        for (int iy = 0; iy < dy; iy++)
                            used[x + ix][y + iy] = true;

                    glm::vec3 origin((float)x, (float)y, (float)(z + 1));
                    AddQuad(origin,
                        glm::vec3(1, 0, 0), dx,
                        glm::vec3(0, 1, 0), dy,
                        tileID, true);
                }
            }
        }

        // -Z
        {
            static int mask[SIZE_X][SIZE_Y];
            for (int x = 0; x < SIZE_X; x++)
                for (int y = 0; y < SIZE_Y; y++)
                {
                    BlockType cur = getBlock(x, y, z);
                    BlockType prev = getBlock(x, y, z - 1);
                    if (cur != AIR && prev == AIR)
                        mask[x][y] = getTile(cur, 2);
                    else
                        mask[x][y] = -1;
                }

            static bool used[SIZE_X][SIZE_Y] = {};
            memset(used, 0, sizeof(used));
            for (int x = 0; x < SIZE_X; x++)
            {
                for (int y = 0; y < SIZE_Y; y++)
                {
                    if (used[x][y] || mask[x][y] < 0) continue;

                    int tileID = mask[x][y];

                    int dy = 1;
                    while (y + dy < SIZE_Y && !used[x][y + dy] && mask[x][y + dy] == tileID)
                        dy++;

                    int dx = 1;
                    while (x + dx < SIZE_X)
                    {
                        bool rowOk = true;
                        for (int k = 0; k < dy; k++)
                            if (used[x + dx][y + k] || mask[x + dx][y + k] != tileID) { rowOk = false; break; }
                        if (!rowOk) break;
                        dx++;
                    }

                    for (int ix = 0; ix < dx; ix++)
                        for (int iy = 0; iy < dy; iy++)
                            used[x + ix][y + iy] = true;

                    glm::vec3 origin((float)x, (float)y, (float)z);
                    AddQuad(origin,
                        glm::vec3(1, 0, 0), dx,
                        glm::vec3(0, 1, 0), dy,
                        tileID, false);
                }
            }
        }
    }

    // Загружаем меш в GPU
    if (VAO == 0)
    {
        glGenVertexArrays(1, &VAO);
        glGenBuffers(1, &VBO);
    }

    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);

    glBufferData(GL_ARRAY_BUFFER,
        vertices.size() * sizeof(float),
        vertices.data(),
        GL_STATIC_DRAW);

    // Теперь stride = 7 floats: pos(3) + uv(2) + tileOffset(2)
    constexpr int STRIDE = 7 * sizeof(float);

    // location 0: позиция
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, STRIDE, (void*)0);
    glEnableVertexAttribArray(0);

    // location 1: тайловые UV (0..w, 0..h)
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, STRIDE, (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    // location 2: смещение тайла в атласе
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, STRIDE, (void*)(5 * sizeof(float)));
    glEnableVertexAttribArray(2);
}

void Chunk::Draw()
{
    glBindVertexArray(VAO);
    glDrawArrays(GL_TRIANGLES, 0, vertices.size() / 7); // было / 5, теперь / 7
}