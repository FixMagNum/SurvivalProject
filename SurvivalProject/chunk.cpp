#include <glad/glad.h>
#include "chunk.h"
#include "world.h"
#include "block.h"
#include "FastNoiseLite.h"
#include <algorithm>
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
    { 0, 0, 0 },                              // AIR
    { Tile(2,0),  Tile(0,0),  Tile(1,0) },    // GRASS
    { Tile(0,0),  Tile(0,0),  Tile(0,0) },    // DIRT
    { Tile(3,0),  Tile(3,0),  Tile(3,0) },    // STONE
    { Tile(4,0),  Tile(4,0),  Tile(4,0) },    // OAK_PLANKS
    { Tile(5,0),  Tile(5,0),  Tile(5,0) },    // GLASS
    { Tile(6,0),  Tile(6,0),  Tile(6,0) },    // WATER
	{ Tile(8,0),  Tile(8,0),  Tile(7,0) },    // OAK_LOG
    { Tile(9,0),  Tile(9,0),  Tile(9,0) },    // OAK_LEAVES
    { Tile(10,0), Tile(10,0), Tile(10,0) },   // SAND
	{ Tile(11,0), Tile(11,0), Tile(11,0) },   // SNOW
    { Tile(12,0), Tile(12,0), Tile(12,0) },   // BEDROCK
    { Tile(13,0), Tile(13,0), Tile(13,0) },   // COBBLESTONE
    { Tile(14,0), Tile(14,0), Tile(14,0) },   // POOP
	{ Tile(15,0), Tile(15,0), Tile(15,0) },   // BASALT
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
    return false;
}

static inline bool FaceVisible(BlockType cur, BlockType neighbor)
{
    if (cur == AIR) return false;
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
    basaltNoise.SetFrequency(0.03f);   // подбери по вкусу
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

            float basaltN = basaltNoise.GetNoise(worldX, worldZ);
            int basaltThickness = 50 + (int)std::floor(((basaltN + 1.0f) * 0.5f) * 10.0f); // 50..60

            surfaceY = std::clamp(surfaceY, 1, 500);

            for (int y = 0; y < SIZE_Y; y++)
            {
                int worldY = worldChunkY + y;

                if (worldY <= bedrockThickness + basaltThickness - 1)
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

            // Временно: ограничиваем высоту дерева чтобы листья не вышли за границу чанка
            int maxTrunk = SIZE_Y - localSurface - 6; // 6 блоков запас под листья
            trunkHeight = std::min(trunkHeight, maxTrunk);
            if (trunkHeight < 3) continue; // слишком мало места — не сажаем

            // Проверяем есть ли дерево рядом (радиус 3 блока)
            bool treeNearby = false;
            for (int dx = -3; dx <= 3 && !treeNearby; dx++)
                for (int dz = -3; dz <= 3 && !treeNearby; dz++)
                {
                    int bx = x + dx;
                    int bz = z + dz;
                    if (bx < 0 || bx >= SIZE_X || bz < 0 || bz >= SIZE_Z) continue;
                    int localCheck = surfaceY - worldChunkY + 1;
                    if (localCheck >= 0 && localCheck < SIZE_Y)
                        if (blocks[bx][localCheck][bz] == OAK_LOG) treeNearby = true;
                }

            if (treeNearby) continue;

            // Ствол
            for (int t = 1; t <= trunkHeight; t++)
            {
                int localY = localSurface + t;
                if (localY >= SIZE_Y) break;
                blocks[x][localY][z] = OAK_LOG;
            }

            // Листья
            int leafBase = localSurface + trunkHeight - 1;
            for (int ly = leafBase; ly <= leafBase + 2; ly++)
                for (int lx = -2; lx <= 2; lx++)
                    for (int lz = -2; lz <= 2; lz++)
                    {
                        int bx = x + lx, bz = z + lz;
                        if (bx < 0 || bx >= SIZE_X || bz < 0 || bz >= SIZE_Z) continue;
                        if (ly < 0 || ly >= SIZE_Y) continue;
                        if (blocks[bx][ly][bz] == AIR)
                            blocks[bx][ly][bz] = OAK_LEAVES;
                    }

            // Верхушка
            for (int lx = -1; lx <= 1; lx++)
                for (int lz = -1; lz <= 1; lz++)
                {
                    int bx = x + lx, bz = z + lz;
                    if (bx < 0 || bx >= SIZE_X || bz < 0 || bz >= SIZE_Z) continue;
                    int ly = leafBase + 3;
                    if (ly >= SIZE_Y) continue;
                    if (blocks[bx][ly][bz] == AIR)
                        blocks[bx][ly][bz] = OAK_LEAVES;
                }

            int apex = leafBase + 4;
            if (apex < SIZE_Y && blocks[x][apex][z] == AIR)
                blocks[x][apex][z] = OAK_LEAVES;
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

    if (!flipWinding)
    {
        inds.push_back(base + 0); inds.push_back(base + 2); inds.push_back(base + 1);
        inds.push_back(base + 0); inds.push_back(base + 3); inds.push_back(base + 2);
    }
    else
    {
        inds.push_back(base + 0); inds.push_back(base + 1); inds.push_back(base + 2);
        inds.push_back(base + 0); inds.push_back(base + 2); inds.push_back(base + 3);
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

    auto getBlock = [&](int x, int y, int z) -> BlockType {
        if (x >= 0 && x < SIZE_X && y >= 0 && y < SIZE_Y && z >= 0 && z < SIZE_Z)
            return blocks[x][y][z];

        if (x < 0 && neighborNX) {
            int lx = x + SIZE_X;
            if (lx >= 0 && lx < SIZE_X && y >= 0 && y < SIZE_Y && z >= 0 && z < SIZE_Z)
                return neighborNX->blocks[lx][y][z];
        }
        if (x >= SIZE_X && neighborPX) {
            int lx = x - SIZE_X;
            if (lx >= 0 && lx < SIZE_X && y >= 0 && y < SIZE_Y && z >= 0 && z < SIZE_Z)
                return neighborPX->blocks[lx][y][z];
        }
        if (y < 0 && neighborNY) {
            int ly = y + SIZE_Y;
            if (ly >= 0 && ly < SIZE_Y && x >= 0 && x < SIZE_X && z >= 0 && z < SIZE_Z)
                return neighborNY->blocks[x][ly][z];
        }
        if (y >= SIZE_Y && neighborPY) {
            int ly = y - SIZE_Y;
            if (ly >= 0 && ly < SIZE_Y && x >= 0 && x < SIZE_X && z >= 0 && z < SIZE_Z)
                return neighborPY->blocks[x][ly][z];
        }
        if (z < 0 && neighborNZ) {
            int lz = z + SIZE_Z;
            if (lz >= 0 && lz < SIZE_Z && x >= 0 && x < SIZE_X && y >= 0 && y < SIZE_Y)
                return neighborNZ->blocks[x][y][lz];
        }
        if (z >= SIZE_Z && neighborPZ) {
            int lz = z - SIZE_Z;
            if (lz >= 0 && lz < SIZE_Z && x >= 0 && x < SIZE_X && y >= 0 && y < SIZE_Y)
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

    auto isTransparent = [](BlockType b) -> bool {
        return b == GLASS || b == WATER || b == OAK_LEAVES;
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

    // +Y (top faces)
    for (int y = 0; y < SIZE_Y; y++)
    {
        MaskCell mask[SIZE_X][SIZE_Z];
        bool     used[SIZE_X][SIZE_Z];
        memset(used, 0, sizeof(used));

        for (int x = 0; x < SIZE_X; x++)
            for (int z = 0; z < SIZE_Z; z++)
            {
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
        bool     used[SIZE_X][SIZE_Z];
        memset(used, 0, sizeof(used));

        for (int x = 0; x < SIZE_X; x++)
            for (int z = 0; z < SIZE_Z; z++)
            {
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
        bool     used[SIZE_Z][SIZE_Y];
        memset(used, 0, sizeof(used));

        for (int z = 0; z < SIZE_Z; z++)
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

        for (int z = 0; z < SIZE_Z; z++)
            for (int y = 0; y < SIZE_Y; y++)
            {
                if (used[z][y] || mask[z][y].empty()) continue;
                MaskCell& ref = mask[z][y];

                int dy = 1;
                while (y + dy < SIZE_Y && !used[z][y + dy] && mask[z][y + dy] == ref) dy++;

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
        bool     used[SIZE_Z][SIZE_Y];
        memset(used, 0, sizeof(used));

        for (int z = 0; z < SIZE_Z; z++)
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

        for (int z = 0; z < SIZE_Z; z++)
            for (int y = 0; y < SIZE_Y; y++)
            {
                if (used[z][y] || mask[z][y].empty()) continue;
                MaskCell& ref = mask[z][y];

                int dy = 1;
                while (y + dy < SIZE_Y && !used[z][y + dy] && mask[z][y + dy] == ref) dy++;

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
        bool     used[SIZE_X][SIZE_Y];
        memset(used, 0, sizeof(used));

        for (int x = 0; x < SIZE_X; x++)
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

        for (int x = 0; x < SIZE_X; x++)
            for (int y = 0; y < SIZE_Y; y++)
            {
                if (used[x][y] || mask[x][y].empty()) continue;
                MaskCell& ref = mask[x][y];

                int dy = 1;
                while (y + dy < SIZE_Y && !used[x][y + dy] && mask[x][y + dy] == ref) dy++;

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
        bool     used[SIZE_X][SIZE_Y];
        memset(used, 0, sizeof(used));

        for (int x = 0; x < SIZE_X; x++)
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

        for (int x = 0; x < SIZE_X; x++)
            for (int y = 0; y < SIZE_Y; y++)
            {
                if (used[x][y] || mask[x][y].empty()) continue;
                MaskCell& ref = mask[x][y];

                int dy = 1;
                while (y + dy < SIZE_Y && !used[x][y + dy] && mask[x][y + dy] == ref) dy++;

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