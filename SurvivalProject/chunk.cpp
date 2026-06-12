#include <glad/glad.h>
#include "chunk.h"
#include "world.h"
#include "block.h"
#include "FastNoiseLite.h"
#include <algorithm>
#include <cstring>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/norm.hpp>

constexpr int ATLAS_SIZE = 16;
constexpr float TILE_SIZE = 1.0f / ATLAS_SIZE;

static int Tile(int x, int y)
{
    return y * ATLAS_SIZE + x;
}

BlockData blockDatabase[] =
{
	// TOP,        BOTTOM,      SIDE
    { 0, 0, 0 },                                  // AIR
    { Tile(0,15),  Tile(2,15),  Tile(3,15) },     // GRASS
    { Tile(2,15),  Tile(2,15),  Tile(2,15) },     // DIRT
    { Tile(1,15),  Tile(1,15),  Tile(1,15) },     // STONE
    { Tile(4,15),  Tile(4,15),  Tile(4,15) },     // OAK_PLANKS
    { Tile(3,11),  Tile(3,11),  Tile(3,11) },     // GLASS
    { Tile(12,3),  Tile(12,3),  Tile(12,3) },     // WATER
	{ Tile(5,14),  Tile(5,14),  Tile(4,14) },     // OAK_LOG
    { Tile(4,12),  Tile(4,12),  Tile(4,12) },     // OAK_LEAVES
    { Tile(2,14),  Tile(2,14),  Tile(2,14) },     // SAND
	{ Tile(2,11),  Tile(2,11),  Tile(2,11) },     // SNOW
    { Tile(1,6),   Tile(1,6),   Tile(1,6) },      // BEDROCK
    { Tile(0,14),  Tile(0,14),  Tile(0,14) },     // COBBLESTONE
    { Tile(9,0),   Tile(9,0),   Tile(9,0) },      // POOP
	{ Tile(6,15),  Tile(6,15),  Tile(6,15) },     // BASALT
	{ Tile(5,10),  Tile(5,10),  Tile(5,10) },     // TALL_GRASS
};

static inline bool IsTransparentGroup(RenderGroup g)
{
    return g == RenderGroup::Leaves || g == RenderGroup::Water || g == RenderGroup::Glass;
}

static inline bool TransparentFaceVisible(BlockType cur, BlockType neighbor)
{
    if (cur == neighbor)
        return false;

    // Вода + стекло: рисуем у обоих
    if ((cur == WATER && neighbor == GLASS) ||
        (cur == GLASS && neighbor == WATER))
        return true;

    // Листва + вода: рисуем у обоих
    if ((cur == WATER && neighbor == OAK_LEAVES) ||
        (cur == OAK_LEAVES && neighbor == WATER))
        return true;

    // Листва + стекло: рисуем у обоих
    if ((cur == OAK_LEAVES && neighbor == GLASS) ||
        (cur == GLASS && neighbor == OAK_LEAVES))
        return true;

    // По умолчанию для прочих прозрачных пар:
    return true;
}

static inline bool FaceVisible(BlockType cur, BlockType neighbor)
{
    if (cur == AIR || cur == TALL_GRASS) return false;
    if (neighbor == AIR) return true;

    RenderGroup gc = GetRenderGroup(cur);
    RenderGroup gn = GetRenderGroup(neighbor);

    bool curTransparent = IsTransparentGroup(gc);
    bool neighborTransparent = IsTransparentGroup(gn);

    // прозрачный рядом с непрозрачным
    if (curTransparent && !neighborTransparent)
        return false;

    if (!curTransparent && neighborTransparent)
        return true;

    // оба прозрачные
    if (curTransparent && neighborTransparent)
        return TransparentFaceVisible(cur, neighbor);

    // оба непрозрачные
    return gc != gn;
}

static const int SEA_LEVEL = 110;

Chunk::Chunk(int chunkX, int chunkY, int chunkZ, World* worldPtr)
{
    chunkPos = { chunkX, chunkY, chunkZ };
    world = worldPtr;

    float worldX = chunkPos.x * SIZE_X;
    float worldY = chunkPos.y * SIZE_Y;
    float worldZ = chunkPos.z * SIZE_Z;

    bounds.min = glm::vec3(worldX, worldY, worldZ);
    bounds.max = glm::vec3(worldX + SIZE_X, worldY + SIZE_Y, worldZ + SIZE_Z);

    memset(blocks, 0, sizeof(blocks));

    VAO = 0; VBO = 0; EBO = 0;
}

void Chunk::Generate()
{
    FastNoiseLite noise;
    noise.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
    noise.SetFrequency(0.005f);
    noise.SetSeed(1337);

    // Шум для биомов — низкая частота чтобы биомы были большими
    FastNoiseLite biomeNoise;
    biomeNoise.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
    biomeNoise.SetFrequency(0.002f);
    biomeNoise.SetSeed(9999);

    // Пещеры — 3D шум
    FastNoiseLite caveNoise;
    caveNoise.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
    caveNoise.SetFrequency(0.04f);
    caveNoise.SetSeed(2024);

    FastNoiseLite caveNoise2;
    caveNoise2.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
    caveNoise2.SetFrequency(0.04f);
    caveNoise2.SetSeed(9876);

    // Шум для деревьев
    FastNoiseLite treeNoise;
    treeNoise.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
    treeNoise.SetFrequency(0.1f);
    treeNoise.SetSeed(42);

    // Шум для бедрока
    FastNoiseLite bedrockNoise;
    bedrockNoise.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
    bedrockNoise.SetFrequency(0.08f);
    bedrockNoise.SetSeed(777);

    // Шум для базальта
    FastNoiseLite basaltNoise;
    basaltNoise.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
    basaltNoise.SetFrequency(0.03f);
    basaltNoise.SetSeed(5555);

    int worldChunkY = chunkPos.y * SIZE_Y; // нижняя граница чанка в мировых координатах

    for (int x = 0; x < SIZE_X; x++)
    {
        for (int z = 0; z < SIZE_Z; z++)
        {
            float worldX = x + chunkPos.x * SIZE_X;
            float worldZ = z + chunkPos.z * SIZE_Z;

            float biomeVal = biomeNoise.GetNoise(worldX, worldZ);
            float noiseVal = noise.GetNoise(worldX, worldZ);

            // Определяем биом
            // -1.0 .. -0.3 = пустыня
            // -0.3 ..  0.3 = равнина
            //  0.3 ..  0.6 = лес
            //  0.6 ..  1.0 = горы
            enum Biome { DESERT, PLAINS, FOREST, MOUNTAINS };
            Biome biome;
            if      (biomeVal < -0.3f)  biome = DESERT;
            else if (biomeVal <  0.3f)  biome = PLAINS;
            else if (biomeVal <  0.6f)  biome = FOREST;
            else                        biome = MOUNTAINS;

            // Высота рельефа зависит от биома
            int surfaceY;
            if      (biome == DESERT)   surfaceY = (int)(108.0f + noiseVal * 8.0f);   // плоский
            else if (biome == PLAINS)   surfaceY = (int)(120.0f + noiseVal * 20.0f);  // средний
            else if (biome == FOREST)   surfaceY = (int)(120.0f + noiseVal * 25.0f);  // средний
            else                        surfaceY = (int)(140.0f + noiseVal * 60.0f);  // высокий - MOUNTAINS

            float bedrockN = bedrockNoise.GetNoise(worldX, worldZ);
            int bedrockThickness = 1 + (int)std::floor(((bedrockN + 1.0f) * 0.5f) * 4.0f); // 1..5

            float basaltN = basaltNoise.GetNoise(worldX, worldZ);
            int basaltThickness = 50 + (int)std::floor(((basaltN + 1.0f) * 0.5f) * 10.0f); // 50..60

            surfaceY = std::clamp(surfaceY, 1, 500);

            // Заполняем блоки
            for (int y = 0; y < SIZE_Y; y++)
            {
                int worldY = worldChunkY + y; // мировая Y координата блока

                BlockType block = AIR;

                if (worldY <= bedrockThickness - 1)
                    block = BEDROCK;
                else if (worldY <= bedrockThickness + basaltThickness - 1)
                    block = BASALT;
                else if (worldY < surfaceY - 3)
                    block = STONE;
                else if (worldY < surfaceY)
                {
                    if (biome == DESERT) block = SAND;
                    else                 block = DIRT;
                }
                else if (worldY == surfaceY)
                {
                    if (biome == DESERT)                              block = SAND;
                    else if (biome == MOUNTAINS && surfaceY > 170)    block = SNOW;
                    else                                              block = GRASS;
                }
                else if (worldY <= 110 && worldY > surfaceY) // SEA_LEVEL = 110
                    block = WATER;

                blocks[x][y][z] = block;
            }

            // Под водой трава -> грязь
            for (int y = 0; y < SIZE_Y; y++)
            {
                int worldY = worldChunkY + y;
                if (worldY == surfaceY && surfaceY < 110)
                    if (blocks[x][y][z] == GRASS)
                        blocks[x][y][z] = DIRT;
            }
        }
    }

    // Пещеры — отдельный проход после генерации всех блоков
    for (int x = 0; x < SIZE_X; x++)
    {
        for (int z = 0; z < SIZE_Z; z++)
        {
            float worldX = x + chunkPos.x * SIZE_X;
            float worldZ = z + chunkPos.z * SIZE_Z;

            float biomeVal = biomeNoise.GetNoise(worldX, worldZ);
            float noiseVal = noise.GetNoise(worldX, worldZ);

            enum Biome { DESERT, PLAINS, FOREST, MOUNTAINS };
            Biome biome;
            if (biomeVal < -0.3f) biome = DESERT;
            else if (biomeVal < 0.3f) biome = PLAINS;
            else if (biomeVal < 0.6f) biome = FOREST;
            else                       biome = MOUNTAINS;

            int surfaceY;
            if (biome == DESERT)   surfaceY = (int)(108.0f + noiseVal * 8.0f);
            else if (biome == PLAINS)   surfaceY = (int)(120.0f + noiseVal * 20.0f);
            else if (biome == FOREST)   surfaceY = (int)(120.0f + noiseVal * 25.0f);
            else                        surfaceY = (int)(140.0f + noiseVal * 60.0f);

            float bedrockN = bedrockNoise.GetNoise(worldX, worldZ);
            int bedrockThickness = 1 + (int)std::floor(((bedrockN + 1.0f) * 0.5f) * 4.0f); // 1..5

            surfaceY = std::clamp(surfaceY, 1, 500);

            for (int y = 0; y < SIZE_Y; y++)
            {
                int worldY = worldChunkY + y;

                if (worldY <= bedrockThickness - 1)
                    continue;

                if (worldY > surfaceY)
                    continue;

                BlockType& block = blocks[x][y][z];
                if (block == AIR || block == WATER)
                    continue;

                float n1 = caveNoise.GetNoise(worldX, (float)worldY, worldZ);
                float n2 = caveNoise2.GetNoise(worldX, (float)worldY * 0.5f, worldZ);
                float caveValue = n1 * n1 + n2 * n2;

                float threshold = (worldY >= surfaceY - 2) ? 0.012f : 0.006f;
                if (caveValue < threshold)
                    block = AIR;
            }
        }
    }

    for (int x = 0; x < SIZE_X; x++)
    {
        for (int z = 0; z < SIZE_Z; z++)
        {
            float worldX = x + chunkPos.x * SIZE_X;
            float worldZ = z + chunkPos.z * SIZE_Z;

            float biomeVal = biomeNoise.GetNoise(worldX, worldZ);
            float noiseVal = noise.GetNoise(worldX, worldZ);

            enum Biome { DESERT, PLAINS, FOREST, MOUNTAINS };
            Biome biome;
            if (biomeVal < -0.3f) biome = DESERT;
            else if (biomeVal < 0.3f) biome = PLAINS;
            else if (biomeVal < 0.6f) biome = FOREST;
            else                       biome = MOUNTAINS;

            int surfaceY;
            if (biome == DESERT)   surfaceY = (int)(108.0f + noiseVal * 8.0f);
            else if (biome == PLAINS)   surfaceY = (int)(120.0f + noiseVal * 20.0f);
            else if (biome == FOREST)   surfaceY = (int)(120.0f + noiseVal * 25.0f);
            else                        surfaceY = (int)(140.0f + noiseVal * 60.0f);

            surfaceY = std::clamp(surfaceY, 1, 500);

            int localSurface = surfaceY - worldChunkY;
            if (localSurface < 0 || localSurface >= SIZE_Y)
                continue;

            // Если поверхность вскрыта пещерой, делаем верхний блок травой
            if (biome != DESERT && blocks[x][localSurface][z] != AIR)
            {
                if (blocks[x][localSurface][z] == DIRT)
                    blocks[x][localSurface][z] = GRASS;
            }
            else if (biome != DESERT)
            {
                // Если поверхность прорезана, ищем первый твердый блок ниже и делаем его травой
                for (int y = localSurface - 1; y >= 0; y--)
                {
                    if (blocks[x][y][z] == AIR) continue;
                    if (blocks[x][y][z] == DIRT)
                        blocks[x][y][z] = GRASS;
                    break;
                }
            }
        }
    }

    // Деревья — только если чанк содержит поверхность
    for (int x = 2; x < SIZE_X - 2; x++)
    {
        for (int z = 2; z < SIZE_Z - 2; z++)
        {
            float worldX = x + chunkPos.x * SIZE_X;
            float worldZ = z + chunkPos.z * SIZE_Z;

            float biomeVal = biomeNoise.GetNoise(worldX, worldZ);
            if (biomeVal < -0.3f || biomeVal > 0.6f) continue;

            float noiseVal = noise.GetNoise(worldX, worldZ);
            int surfaceY;
            if (biomeVal < 0.3f) surfaceY = (int)(120.0f + noiseVal * 20.0f);
            else                  surfaceY = (int)(120.0f + noiseVal * 25.0f);
            surfaceY = std::clamp(surfaceY, 1, 500);

            // Проверяем что поверхность находится в этом чанке
            int localSurface = surfaceY - worldChunkY;
            if (localSurface < 0 || localSurface >= SIZE_Y) continue;
            if (blocks[x][localSurface][z] != GRASS) continue;
            if (surfaceY <= 110) continue;

            float treeVal = treeNoise.GetNoise(worldX, worldZ);
            float treeThreshold = (biomeVal > 0.3f) ? 0.3f : 0.6f;
            if (treeVal < treeThreshold) continue;

            int trunkHeight = 4 + (int)((treeVal - treeThreshold) * 10.0f);
            trunkHeight = std::clamp(trunkHeight, 4, 6);

            // Проверяем есть ли дерево рядом (радиус 3 блока)
            bool treeNearby = false;
            for (int dx = -3; dx <= 3 && !treeNearby; dx++)
                for (int dz = -3; dz <= 3 && !treeNearby; dz++)
                {
                    int bx = x + dx;
                    int bz = z + dz;
                    if (bx < 0 || bx >= SIZE_X || bz < 0 || bz >= SIZE_Z) continue;
                    for (int y = 0; y < SIZE_Y && !treeNearby; y++)
                        if (blocks[bx][y][bz] == OAK_LOG) treeNearby = true;
                }

            if (treeNearby) continue;

            // Вспомогательная функция: ставит блок по локальным координатам.
            // Если координаты выходят за границу чанка - сохраняет блок как ghost,
            // чтобы он был применён к соседнему чанку когда тот сгенерируется
            auto placeBlock = [&](int lx, int ly, int lz, BlockType type) {
                if (lx >= 0 && lx < SIZE_X && ly >= 0 && ly < SIZE_Y && lz >= 0 && lz < SIZE_Z)
                {
                    if (blocks[lx][ly][lz] == AIR)
                        blocks[lx][ly][lz] = type;
                }
                else
                {
                    int wx = lx + chunkPos.x * SIZE_X;
                    int wy = ly + worldChunkY;
                    int wz = lz + chunkPos.z * SIZE_Z;
                    generatedGhostBlocks.push_back({ wx, wy, wz, type });
                }
            };

            // Ствол
            for (int t = 1; t <= trunkHeight; t++)
                placeBlock(x, localSurface + t, z, OAK_LOG);

            // Листья
            int leafBase = localSurface + trunkHeight - 1;
            for (int ly = leafBase; ly <= leafBase + 2; ly++)
                for (int dx = -2; dx <= 2; dx++)
                    for (int dz = -2; dz <= 2; dz++)
                        placeBlock(x + dx, ly, z + dz, OAK_LEAVES);

            // Верхушка
            for (int dx = -1; dx <= 1; dx++)
                for (int dz = -1; dz <= 1; dz++)
                    placeBlock(x + dx, leafBase + 3, z + dz, OAK_LEAVES);

            placeBlock(x, leafBase + 4, z, OAK_LEAVES);
        }
    }

    // Высокая трава
    FastNoiseLite grassNoise;
    grassNoise.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
    grassNoise.SetFrequency(0.88f);
    grassNoise.SetSeed(2468);

    for (int x = 1; x < SIZE_X - 1; ++x)
    {
        for (int z = 1; z < SIZE_Z - 1; ++z)
        {
            float worldX = x + chunkPos.x * SIZE_X;
            float worldZ = z + chunkPos.z * SIZE_Z;

            // Редкость спавна
            float g = grassNoise.GetNoise(worldX, worldZ);
            if (g < 0.45f)
                continue;

            // Ищем верхний блок в колонке
            for (int y = SIZE_Y - 2; y >= 0; --y)
            {
                if (blocks[x][y][z] == AIR)
                    continue;

                // Ставим только на верхнюю поверхность травы
                if (blocks[x][y][z] == GRASS && blocks[x][y + 1][z] == AIR)
                {
                    blocks[x][y + 1][z] = TALL_GRASS;
                }

                break;
            }
        }
    }
}

int Chunk::ComputeAO(int side1, int side2, int corner)
{
    if (side1 && side2) return 0;
    return 3 - (side1 + side2 + corner);
}

void Chunk::AddQuad(
    glm::vec3 origin,
    glm::vec3 axis1, int w,
    glm::vec3 axis2, int h,
    int tileID, bool flipWinding,
    float ao0, float ao1, float ao2, float ao3,
    int faceId,
    RenderGroup group,
    bool nudge)
{
    auto& bucket = meshGroups[(size_t)group];
    auto& verts = bucket.vertices;
    auto& inds = bucket.indices;

    glm::vec3 p[4] = {
        origin,
        origin + axis1 * (float)w,
        origin + axis1 * (float)w + axis2 * (float)h,
        origin + axis2 * (float)h,
    };

    // ao приходит как float 0..1, конвертируем обратно в 0..3
    auto packAO = [](float ao) -> uint32_t {
        return (uint32_t)roundf(ao * 3.0f) & 3;
        };

    float aoArr[4] = { ao0, ao1, ao2, ao3 };

    auto push = [&](int corner) {
        glm::vec3 pos = p[corner];
        uint8_t x = (uint8_t)roundf(pos.x);
        uint8_t y = (uint8_t)roundf(pos.y);
        uint8_t z = (uint8_t)roundf(pos.z);

        uint32_t d0 = ((uint32_t)x << 24)
            | ((uint32_t)y << 16)
            | ((uint32_t)z << 8)
            | ((uint32_t)(faceId & 7) << 5)
            | (packAO(aoArr[corner]) << 3)
            | ((uint32_t)(corner & 3) << 1)
            | (nudge ? 1u : 0u);

        uint32_t d1 = (uint32_t)(uint8_t)tileID
            | ((uint32_t)(uint8_t)w << 8)
            | ((uint32_t)(uint8_t)h << 16);

        verts.push_back({ d0, d1 });
        };

    uint32_t base = (uint32_t)verts.size();
    uint32_t firstIndex = (uint32_t)inds.size();
    push(0); push(1); push(2); push(3);

    bool flipDiag = (aoArr[1] + aoArr[3]) > (aoArr[0] + aoArr[2]);

    if (!flipWinding)
    {
        if (!flipDiag)
        {
            inds.push_back(base + 0); inds.push_back(base + 2); inds.push_back(base + 1);
            inds.push_back(base + 0); inds.push_back(base + 3); inds.push_back(base + 2);
        }
        else
        {
            inds.push_back(base + 0); inds.push_back(base + 3); inds.push_back(base + 1);
            inds.push_back(base + 1); inds.push_back(base + 3); inds.push_back(base + 2);
        }
    }
    else
    {
        if (!flipDiag)
        {
            inds.push_back(base + 0); inds.push_back(base + 1); inds.push_back(base + 2);
            inds.push_back(base + 0); inds.push_back(base + 2); inds.push_back(base + 3);
        }
        else
        {
            inds.push_back(base + 0); inds.push_back(base + 1); inds.push_back(base + 3);
            inds.push_back(base + 1); inds.push_back(base + 2); inds.push_back(base + 3);
        }
    }

    if (group == RenderGroup::Water || group == RenderGroup::Glass)
    {
        glm::vec3 center = origin + axis1 * (w * 0.5f) + axis2 * (h * 0.5f);
        bucket.sortCmds.push_back({ center, firstIndex, 6 });
    }
}

void Chunk::GenerateMeshData()
{
    for (auto& g : meshGroups)
    {
        g.vertices.clear();
        g.indices.clear();
        g.sortCmds.clear();
    }

    // Проверяем есть ли вообще непустые блоки
    bool hasBlocks = false;
    for (int x = 0; x < SIZE_X && !hasBlocks; x++)
        for (int y = 0; y < SIZE_Y && !hasBlocks; y++)
            for (int z = 0; z < SIZE_Z && !hasBlocks; z++)
                if (blocks[x][y][z] != AIR) hasBlocks = true;

    if (!hasBlocks) return;

    // Border-буфер: [SIZE_X+2][SIZE_Y+2][SIZE_Z+2].
    // borderBuf[x+1][y+1][z+1] == блок в локальных координатах (x, y, z),
    // где x/y/z лежат в диапазоне [-1 .. SIZE-1+1]
    BlockType borderBuf[SIZE_X + 2][SIZE_Y + 2][SIZE_Z + 2]{};

    auto fetchNeighborBlock = [&](int x, int y, int z) -> BlockType {
        Chunk* c = this;
        if (x < 0) { c = c->neighborNX; if (!c) return AIR; x += SIZE_X; }
        else if (x >= SIZE_X) { c = c->neighborPX; if (!c) return AIR; x -= SIZE_X; }
        if (y < 0) { c = c->neighborNY; if (!c) return AIR; y += SIZE_Y; }
        else if (y >= SIZE_Y) { c = c->neighborPY; if (!c) return AIR; y -= SIZE_Y; }
        if (z < 0) { c = c->neighborNZ; if (!c) return AIR; z += SIZE_Z; }
        else if (z >= SIZE_Z) { c = c->neighborPZ; if (!c) return AIR; z -= SIZE_Z; }
        return c->blocks[x][y][z];
        };

    for (int x = -1; x <= SIZE_X; ++x)
        for (int y = -1; y <= SIZE_Y; ++y)
            for (int z = -1; z <= SIZE_Z; ++z)
                borderBuf[x + 1][y + 1][z + 1] = fetchNeighborBlock(x, y, z);

    auto emitTallGrass = [&](int x, int y, int z) {
            if (blocks[x][y][z] != TALL_GRASS)
                return;

            int tileID = blockDatabase[TALL_GRASS].top;
            RenderGroup group = GetRenderGroup(TALL_GRASS);

            // Две скрещенные плоскости, каждая продублирована с обратным winding
            AddQuad(glm::vec3(x, y, z),
                glm::vec3(1, 0, 1), 1,
                glm::vec3(0, 1, 0), 1,
                tileID, false,
                1.f, 1.f, 1.f, 1.f,
                2, group, false);
            AddQuad(glm::vec3(x, y, z),
                glm::vec3(1, 0, 1), 1,
                glm::vec3(0, 1, 0), 1,
                tileID, true,
                1.f, 1.f, 1.f, 1.f,
                2, group, false);

            AddQuad(glm::vec3(x + 1, y, z),
                glm::vec3(-1, 0, 1), 1,
                glm::vec3(0, 1, 0), 1,
                tileID, false,
                1.f, 1.f, 1.f, 1.f,
                4, group, false);
            AddQuad(glm::vec3(x + 1, y, z),
                glm::vec3(-1, 0, 1), 1,
                glm::vec3(0, 1, 0), 1,
                tileID, true,
                1.f, 1.f, 1.f, 1.f,
                4, group, false);
        };

    for (int x = 0; x < SIZE_X; ++x)
        for (int y = 0; y < SIZE_Y; ++y)
            for (int z = 0; z < SIZE_Z; ++z)
                emitTallGrass(x, y, z);

    auto getBlock = [&](int x, int y, int z) -> BlockType {
        return borderBuf[x + 1][y + 1][z + 1];
        };

    auto getTile = [&](BlockType type, int direction) -> int {
        if (type == AIR) return -1;
        BlockData& bd = blockDatabase[type];
        if (direction == 0) return bd.top;
        if (direction == 1) return bd.bottom;
        return bd.side;
        };

    auto isTransparent = [](BlockType b) -> bool {
        return b == GLASS || b == WATER || b == OAK_LEAVES || b == TALL_GRASS;
        };

    auto solid = [&](int x, int y, int z) -> int {
        BlockType b = getBlock(x, y, z);
        return (b != AIR && !isTransparent(b)) ? 1 : 0;
        };

    auto aoVal = [&](int s1, int s2, int c) -> float {
        return ComputeAO(s1, s2, c) / 3.0f;
        };

    // Нужно ли сдвигать грань воды? Только боковые грани, только против стекла/листвы
    auto transparentNudge = [](BlockType cur, BlockType nbr) -> bool {
        RenderGroup gn = GetRenderGroup(nbr);
        if (!IsTransparentGroup(gn)) return false;

        // Вода рядом с любым прозрачным (стекло, листва)
        if (cur == WATER) return true;

        // Стекло рядом с листвой
        if (cur == GLASS && nbr == OAK_LEAVES) return true;

        return false;
        };

    struct MaskCell
    {
        int tileID = -1;
        RenderGroup group = RenderGroup::Opaque;
        float ao[4] = { 1.f, 1.f, 1.f, 1.f };
        bool nudge = false;

        bool operator==(const MaskCell& o) const
        {
            return tileID == o.tileID &&
                group == o.group &&
                ao[0] == o.ao[0] &&
                ao[1] == o.ao[1] &&
                ao[2] == o.ao[2] &&
                ao[3] == o.ao[3] &&
                nudge == o.nudge;
        }

        bool empty() const { return tileID < 0; }
    };

    int gen = 0;
    int usedXZ[SIZE_X][SIZE_Z] = {};
    int usedZY[SIZE_Z][SIZE_Y] = {};
    int usedXY[SIZE_X][SIZE_Y] = {};

    bool columnHasBlock[SIZE_X][SIZE_Z] = {};
    for (int x = 0; x < SIZE_X; x++)
        for (int z = 0; z < SIZE_Z; z++)
            for (int y = 0; y < SIZE_Y; y++)
                if (blocks[x][y][z] != AIR) { columnHasBlock[x][z] = true; break; }

    int minBlockY[SIZE_X][SIZE_Z]{}, maxBlockY[SIZE_X][SIZE_Z]{};
    for (int x = 0; x < SIZE_X; x++)
        for (int z = 0; z < SIZE_Z; z++)
        {
            minBlockY[x][z] = SIZE_Y;
            maxBlockY[x][z] = -1;
            for (int y = 0; y < SIZE_Y; y++)
                if (blocks[x][y][z] != AIR)
                {
                    if (y < minBlockY[x][z]) minBlockY[x][z] = y;
                    if (y > maxBlockY[x][z]) maxBlockY[x][z] = y;
                }
        }

    // +Y (top faces)
    for (int y = 0; y < SIZE_Y; y++)
    {
        MaskCell mask[SIZE_X][SIZE_Z];
        ++gen;

        for (int x = 0; x < SIZE_X; x++)
            for (int z = 0; z < SIZE_Z; z++)
            {
                if (y < minBlockY[x][z] || y > maxBlockY[x][z])
                {
                    mask[x][z].tileID = -1; continue;
                }

                MaskCell& cell = mask[x][z];
                BlockType cur = getBlock(x, y, z);
                BlockType above = getBlock(x, y + 1, z);

                if (FaceVisible(cur, above))
                {
                    cell.tileID = getTile(cur, 0);
                    cell.group = GetRenderGroup(cur);
                    cell.ao[0] = aoVal(solid(x - 1, y + 1, z), solid(x, y + 1, z - 1), solid(x - 1, y + 1, z - 1));
                    cell.ao[1] = aoVal(solid(x + 1, y + 1, z), solid(x, y + 1, z - 1), solid(x + 1, y + 1, z - 1));
                    cell.ao[2] = aoVal(solid(x + 1, y + 1, z), solid(x, y + 1, z + 1), solid(x + 1, y + 1, z + 1));
                    cell.ao[3] = aoVal(solid(x - 1, y + 1, z), solid(x, y + 1, z + 1), solid(x - 1, y + 1, z + 1));
                    cell.nudge = transparentNudge(cur, above);
                }
                else cell.tileID = -1;
            }

        for (int x = 0; x < SIZE_X; x++)
            for (int z = 0; z < SIZE_Z; z++)
            {
                if (usedXZ[x][z] == gen || mask[x][z].empty()) continue;
                MaskCell& ref = mask[x][z];

                int dz = 1;
                while (z + dz < SIZE_Z && usedXZ[x][z + dz] != gen && mask[x][z + dz] == ref) dz++;

                int dx = 1;
                while (x + dx < SIZE_X) {
                    bool ok = true;
                    for (int k = 0; k < dz; k++)
                        if (usedXZ[x + dx][z + k] == gen || !(mask[x + dx][z + k] == ref)) { ok = false; break; }
                    if (!ok) break;
                    dx++;
                }

                for (int ix = 0; ix < dx; ix++)
                    for (int iz = 0; iz < dz; iz++)
                        usedXZ[x + ix][z + iz] = gen;

                BlockType cur = getBlock(x, y, z);
                RenderGroup group = GetRenderGroup(cur);

                AddQuad(glm::vec3(x, y + 1, z),
                    glm::vec3(1, 0, 0), dx,
                    glm::vec3(0, 0, 1), dz,
                    ref.tileID, false,
                    ref.ao[0], ref.ao[1], ref.ao[2], ref.ao[3],
                    0, group, ref.nudge);
            }
    }

    // -Y (bottom faces)
    for (int y = 0; y < SIZE_Y; y++)
    {
        MaskCell mask[SIZE_X][SIZE_Z];
        ++gen;

        for (int x = 0; x < SIZE_X; x++)
            for (int z = 0; z < SIZE_Z; z++)
            {
                if (y < minBlockY[x][z] || y > maxBlockY[x][z])
                {
                    mask[x][z].tileID = -1; continue;
                }

                MaskCell& cell = mask[x][z];
                BlockType cur = getBlock(x, y, z);
                BlockType below = getBlock(x, y - 1, z);

                if (FaceVisible(cur, below))
                {
                    cell.tileID = getTile(cur, 1);
                    cell.group = GetRenderGroup(cur);
                    cell.ao[0] = aoVal(solid(x - 1, y - 1, z), solid(x, y - 1, z - 1), solid(x - 1, y - 1, z - 1));
                    cell.ao[1] = aoVal(solid(x + 1, y - 1, z), solid(x, y - 1, z - 1), solid(x + 1, y - 1, z - 1));
                    cell.ao[2] = aoVal(solid(x + 1, y - 1, z), solid(x, y - 1, z + 1), solid(x + 1, y - 1, z + 1));
                    cell.ao[3] = aoVal(solid(x - 1, y - 1, z), solid(x, y - 1, z + 1), solid(x - 1, y - 1, z + 1));
                    cell.nudge = transparentNudge(cur, below);
                }
                else cell.tileID = -1;
            }

        for (int x = 0; x < SIZE_X; x++)
            for (int z = 0; z < SIZE_Z; z++)
            {
                if (usedXZ[x][z] == gen || mask[x][z].empty()) continue;
                MaskCell& ref = mask[x][z];

                int dz = 1;
                while (z + dz < SIZE_Z && usedXZ[x][z + dz] != gen && mask[x][z + dz] == ref) dz++;

                int dx = 1;
                while (x + dx < SIZE_X) {
                    bool ok = true;
                    for (int k = 0; k < dz; k++)
                        if (usedXZ[x + dx][z + k] == gen || !(mask[x + dx][z + k] == ref)) { ok = false; break; }
                    if (!ok) break;
                    dx++;
                }

                for (int ix = 0; ix < dx; ix++)
                    for (int iz = 0; iz < dz; iz++)
                        usedXZ[x + ix][z + iz] = gen;

                BlockType cur = getBlock(x, y, z);
                RenderGroup group = GetRenderGroup(cur);

                AddQuad(glm::vec3(x, y, z),
                    glm::vec3(1, 0, 0), dx,
                    glm::vec3(0, 0, 1), dz,
                    ref.tileID, true,
                    ref.ao[0], ref.ao[1], ref.ao[2], ref.ao[3],
                    1, group, ref.nudge);
            }
    }

    // +X (right faces)
    for (int x = 0; x < SIZE_X; x++)
    {
        MaskCell mask[SIZE_Z][SIZE_Y];
        ++gen;

        for (int z = 0; z < SIZE_Z; z++)
        {
            if (!columnHasBlock[x][z]) {
                for (int y = 0; y < SIZE_Y; y++) mask[z][y].tileID = -1;
                continue;
            }

            for (int y = 0; y < SIZE_Y; y++)
            {
                MaskCell& cell = mask[z][y];
                BlockType cur = getBlock(x, y, z);
                BlockType neighbor = getBlock(x + 1, y, z);

                if (FaceVisible(cur, neighbor))
                {
                    cell.tileID = getTile(cur, 2);
                    cell.group  = GetRenderGroup(cur);
                    cell.ao[0]  = aoVal(solid(x + 1, y - 1, z), solid(x + 1, y, z - 1), solid(x + 1, y - 1, z - 1));
                    cell.ao[1]  = aoVal(solid(x + 1, y - 1, z), solid(x + 1, y, z + 1), solid(x + 1, y - 1, z + 1));
                    cell.ao[2]  = aoVal(solid(x + 1, y + 1, z), solid(x + 1, y, z + 1), solid(x + 1, y + 1, z + 1));
                    cell.ao[3]  = aoVal(solid(x + 1, y + 1, z), solid(x + 1, y, z - 1), solid(x + 1, y + 1, z - 1));
                    cell.nudge = transparentNudge(cur, neighbor);
                }
                else cell.tileID = -1;
            }
        }

        for (int z = 0; z < SIZE_Z; z++)
            for (int y = 0; y < SIZE_Y; y++)
            {
                if (usedZY[z][y] == gen || mask[z][y].empty()) continue;
                MaskCell& ref = mask[z][y];

                int dy = 1;
                while (y + dy < SIZE_Y && usedZY[z][y + dy] != gen && mask[z][y + dy] == ref) dy++;

                int dz = 1;
                while (z + dz < SIZE_Z) {
                    bool ok = true;
                    for (int k = 0; k < dy; k++)
                        if (usedZY[z + dz][y + k] == gen || !(mask[z + dz][y + k] == ref)) { ok = false; break; }
                    if (!ok) break;
                    dz++;
                }

                for (int iz = 0; iz < dz; iz++)
                    for (int iy = 0; iy < dy; iy++)
                        usedZY[z + iz][y + iy] = gen;

                BlockType cur = getBlock(x, y, z);
                RenderGroup group = GetRenderGroup(cur);
                int quadH = dy;

                AddQuad(glm::vec3(x + 1, y, z),
                    glm::vec3(0, 0, 1), dz,
                    glm::vec3(0, 1, 0), quadH,
                    ref.tileID, false,
                    ref.ao[0], ref.ao[1], ref.ao[2], ref.ao[3],
                    2, group, ref.nudge);
            }
    }

    // -X (left faces)
    for (int x = 0; x < SIZE_X; x++)
    {
        MaskCell mask[SIZE_Z][SIZE_Y];
        ++gen;

        for (int z = 0; z < SIZE_Z; z++)
        {
            if (!columnHasBlock[x][z]) {
                for (int y = 0; y < SIZE_Y; y++) mask[z][y].tileID = -1;
                continue;
            }

            for (int y = 0; y < SIZE_Y; y++)
            {
                MaskCell& cell = mask[z][y];
                BlockType cur = getBlock(x, y, z);
                BlockType neighbor = getBlock(x - 1, y, z);

                if (FaceVisible(cur, neighbor))
                {
                    cell.tileID = getTile(cur, 2);
                    cell.group = GetRenderGroup(cur);
                    cell.ao[0] = aoVal(solid(x - 1, y - 1, z), solid(x - 1, y, z - 1), solid(x - 1, y - 1, z - 1));
                    cell.ao[1] = aoVal(solid(x - 1, y - 1, z), solid(x - 1, y, z + 1), solid(x - 1, y - 1, z + 1));
                    cell.ao[2] = aoVal(solid(x - 1, y + 1, z), solid(x - 1, y, z + 1), solid(x - 1, y + 1, z + 1));
                    cell.ao[3] = aoVal(solid(x - 1, y + 1, z), solid(x - 1, y, z - 1), solid(x - 1, y + 1, z - 1));
                    cell.nudge = transparentNudge(cur, neighbor);
                }
                else cell.tileID = -1;
            }
        }

        for (int z = 0; z < SIZE_Z; z++)
            for (int y = 0; y < SIZE_Y; y++)
            {
                if (usedZY[z][y] == gen || mask[z][y].empty()) continue;
                MaskCell& ref = mask[z][y];

                int dy = 1;
                while (y + dy < SIZE_Y && usedZY[z][y + dy] != gen && mask[z][y + dy] == ref) dy++;

                int dz = 1;
                while (z + dz < SIZE_Z) {
                    bool ok = true;
                    for (int k = 0; k < dy; k++)
                        if (usedZY[z + dz][y + k] == gen || !(mask[z + dz][y + k] == ref)) { ok = false; break; }
                    if (!ok) break;
                    dz++;
                }

                for (int iz = 0; iz < dz; iz++)
                    for (int iy = 0; iy < dy; iy++)
                        usedZY[z + iz][y + iy] = gen;

                BlockType cur = getBlock(x, y, z);
                RenderGroup group = GetRenderGroup(cur);
                int quadH = dy;

                AddQuad(glm::vec3(x, y, z),
                    glm::vec3(0, 0, 1), dz,
                    glm::vec3(0, 1, 0), quadH,
                    ref.tileID, true,
                    ref.ao[0], ref.ao[1], ref.ao[2], ref.ao[3],
                    3, group, ref.nudge);
            }
    }

    // +Z (front faces)
    for (int z = 0; z < SIZE_Z; z++)
    {
        MaskCell mask[SIZE_X][SIZE_Y];
        ++gen;

        for (int x = 0; x < SIZE_X; x++)
        {
            if (!columnHasBlock[x][z]) {
                for (int y = 0; y < SIZE_Y; y++) mask[x][y].tileID = -1;
                continue;
            }

            for (int y = 0; y < SIZE_Y; y++)
            {
                MaskCell& cell = mask[x][y];
                BlockType cur = getBlock(x, y, z);
                BlockType neighbor = getBlock(x, y, z + 1);

                if (FaceVisible(cur, neighbor))
                {
                    cell.tileID = getTile(cur, 2);
                    cell.group = GetRenderGroup(cur);
                    cell.ao[0] = aoVal(solid(x - 1, y, z + 1), solid(x, y - 1, z + 1), solid(x - 1, y - 1, z + 1));
                    cell.ao[1] = aoVal(solid(x + 1, y, z + 1), solid(x, y - 1, z + 1), solid(x + 1, y - 1, z + 1));
                    cell.ao[2] = aoVal(solid(x + 1, y, z + 1), solid(x, y + 1, z + 1), solid(x + 1, y + 1, z + 1));
                    cell.ao[3] = aoVal(solid(x - 1, y, z + 1), solid(x, y + 1, z + 1), solid(x - 1, y + 1, z + 1));
                    cell.nudge = transparentNudge(cur, neighbor);
                }
                else cell.tileID = -1;
            }
        }

        for (int x = 0; x < SIZE_X; x++)
            for (int y = 0; y < SIZE_Y; y++)
            {
                if (usedXY[x][y] == gen || mask[x][y].empty()) continue;
                MaskCell& ref = mask[x][y];

                int dy = 1;
                while (y + dy < SIZE_Y && usedXY[x][y + dy] != gen && mask[x][y + dy] == ref) dy++;

                int dx = 1;
                while (x + dx < SIZE_X) {
                    bool ok = true;
                    for (int k = 0; k < dy; k++)
                        if (usedXY[x + dx][y + k] == gen || !(mask[x + dx][y + k] == ref)) { ok = false; break; }
                    if (!ok) break;
                    dx++;
                }

                for (int ix = 0; ix < dx; ix++)
                    for (int iy = 0; iy < dy; iy++)
                        usedXY[x + ix][y + iy] = gen;

                BlockType cur = getBlock(x, y, z);
                RenderGroup group = GetRenderGroup(cur);
                int quadH = dy;

                AddQuad(glm::vec3(x, y, z + 1),
                    glm::vec3(1, 0, 0), dx,
                    glm::vec3(0, 1, 0), quadH,
                    ref.tileID, true,
                    ref.ao[0], ref.ao[1], ref.ao[2], ref.ao[3],
                    4, group, ref.nudge);
            }
    }

    // -Z (back faces)
    for (int z = 0; z < SIZE_Z; z++)
    {
        MaskCell mask[SIZE_X][SIZE_Y];
        ++gen;

        for (int x = 0; x < SIZE_X; x++)
        {
            if (!columnHasBlock[x][z]) {
                for (int y = 0; y < SIZE_Y; y++) mask[x][y].tileID = -1;
                continue;
            }

            for (int y = 0; y < SIZE_Y; y++)
            {
                MaskCell& cell = mask[x][y];
                BlockType cur = getBlock(x, y, z);
                BlockType neighbor = getBlock(x, y, z - 1);

                if (FaceVisible(cur, neighbor))
                {
                    cell.tileID = getTile(cur, 2);
                    cell.group = GetRenderGroup(cur);
                    cell.ao[0] = aoVal(solid(x - 1, y, z - 1), solid(x, y - 1, z - 1), solid(x - 1, y - 1, z - 1));
                    cell.ao[1] = aoVal(solid(x + 1, y, z - 1), solid(x, y - 1, z - 1), solid(x + 1, y - 1, z - 1));
                    cell.ao[2] = aoVal(solid(x + 1, y, z - 1), solid(x, y + 1, z - 1), solid(x + 1, y + 1, z - 1));
                    cell.ao[3] = aoVal(solid(x - 1, y, z - 1), solid(x, y + 1, z - 1), solid(x - 1, y + 1, z - 1));
                    cell.nudge = transparentNudge(cur, neighbor);
                }
                else cell.tileID = -1;
            }
        }

        for (int x = 0; x < SIZE_X; x++)
            for (int y = 0; y < SIZE_Y; y++)
            {
                if (usedXY[x][y] == gen || mask[x][y].empty()) continue;
                MaskCell& ref = mask[x][y];

                int dy = 1;
                while (y + dy < SIZE_Y && usedXY[x][y + dy] != gen && mask[x][y + dy] == ref) dy++;

                int dx = 1;
                while (x + dx < SIZE_X) {
                    bool ok = true;
                    for (int k = 0; k < dy; k++)
                        if (usedXY[x + dx][y + k] == gen || !(mask[x + dx][y + k] == ref)) { ok = false; break; }
                    if (!ok) break;
                    dx++;
                }

                for (int ix = 0; ix < dx; ix++)
                    for (int iy = 0; iy < dy; iy++)
                        usedXY[x + ix][y + iy] = gen;

                BlockType cur = getBlock(x, y, z);
                RenderGroup group = GetRenderGroup(cur);
                int quadH = dy;

                AddQuad(glm::vec3(x, y, z),
                    glm::vec3(1, 0, 0), dx,
                    glm::vec3(0, 1, 0), quadH,
                    ref.tileID, false,
                    ref.ao[0], ref.ao[1], ref.ao[2], ref.ao[3],
                    5, group, ref.nudge);
            }
    }
}

void Chunk::UploadToGPU(bool isRebuild)
{
    constexpr int STRIDE = sizeof(PackedVertex);

    auto uploadBucket = [](MeshBucket& b)
        {
            if (b.vertices.empty() || b.indices.empty())
            {
                if (b.VAO) { glDeleteVertexArrays(1, &b.VAO); b.VAO = 0; }
                if (b.VBO) { glDeleteBuffers(1, &b.VBO); b.VBO = 0; }
                if (b.EBO) { glDeleteBuffers(1, &b.EBO); b.EBO = 0; }
                return;
            }

            if (b.VAO == 0)
            {
                glGenVertexArrays(1, &b.VAO);
                glGenBuffers(1, &b.VBO);
                glGenBuffers(1, &b.EBO);
            }

            glBindVertexArray(b.VAO);
            glBindBuffer(GL_ARRAY_BUFFER, b.VBO);
            glBufferData(GL_ARRAY_BUFFER, b.vertices.size() * sizeof(PackedVertex), b.vertices.data(), GL_DYNAMIC_DRAW);

            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, b.EBO);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, b.indices.size() * sizeof(uint32_t), b.indices.data(), GL_DYNAMIC_DRAW);

            glVertexAttribIPointer(0, 2, GL_UNSIGNED_INT, sizeof(PackedVertex), (void*)0);
            glEnableVertexAttribArray(0);
            glBindVertexArray(0);
        };

    for (auto& bucket : meshGroups)
        uploadBucket(bucket);

    if (isRebuild)
    {
        // Rebuild вызывается из главного потока синхронно — GPU не рисует этот чанк в данный момент, fence не нужен
        if (uploadFence) { glDeleteSync(uploadFence); uploadFence = nullptr; }
        gpuReady = true;
    }
    else
    {
        // Первичная загрузка при стриминге — ставим fence
        if (uploadFence) glDeleteSync(uploadFence);
        uploadFence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        gpuReady = false;
    }

    state.store(ChunkState::Uploaded);
}

void Chunk::DrawGroup(RenderGroup group, const glm::vec3& cameraPos)
{
    auto& bucket = meshGroups[(size_t)group];
    if (bucket.VAO == 0 || bucket.indices.empty()) return;

    glBindVertexArray(bucket.VAO);

    if (group == RenderGroup::Water || group == RenderGroup::Glass)
    {
        std::vector<const DrawCmd*> order;
        order.reserve(bucket.sortCmds.size());

        for (auto& cmd : bucket.sortCmds)
            order.push_back(&cmd);

        std::sort(order.begin(), order.end(),
            [&cameraPos](const DrawCmd* a, const DrawCmd* b)
            {
                float da = glm::length2(a->center - cameraPos);
                float db = glm::length2(b->center - cameraPos);
                return da > db;
            });

        for (const DrawCmd* cmd : order)
        {
            glDrawElements(
                GL_TRIANGLES,
                (GLsizei)cmd->indexCount,
                GL_UNSIGNED_INT,
                (void*)(uintptr_t)(cmd->firstIndex * sizeof(uint32_t))
            );
        }
    }
    else
    {
        glDrawElements(GL_TRIANGLES, (GLsizei)bucket.indices.size(), GL_UNSIGNED_INT, 0);
    }
}

void Chunk::BuildMesh()
{
    GenerateMeshData();
    UploadToGPU(true);  // rebuild — без fence
}

void Chunk::FreeGPU()
{
    if (uploadFence) { glDeleteSync(uploadFence); uploadFence = nullptr; }
    gpuReady = false;
    if (VAO) { glDeleteVertexArrays(1, &VAO); VAO = 0; }
    if (VBO) { glDeleteBuffers(1, &VBO); VBO = 0; }
    if (EBO) { glDeleteBuffers(1, &EBO); EBO = 0; }
    vertices.clear();
    indices.clear();

    for (auto& b : meshGroups)
    {
        if (b.VAO) { glDeleteVertexArrays(1, &b.VAO); b.VAO = 0; }
        if (b.VBO) { glDeleteBuffers(1, &b.VBO); b.VBO = 0; }
        if (b.EBO) { glDeleteBuffers(1, &b.EBO); b.EBO = 0; }
        b.vertices.clear();
        b.indices.clear();
        b.sortCmds.clear();
    }

    state.store(ChunkState::Empty);
}

void Chunk::CheckFence()
{
    if (gpuReady || !uploadFence) return;

    GLenum result = glClientWaitSync(uploadFence, 0, 0); // таймаут 0 — не блокируем
    if (result == GL_ALREADY_SIGNALED || result == GL_CONDITION_SATISFIED)
    {
        glDeleteSync(uploadFence);
        uploadFence = nullptr;
        gpuReady = true;
    }
}