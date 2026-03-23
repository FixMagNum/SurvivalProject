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
    { 0, 0, 0 },                              // AIR
    { Tile(2,0),  Tile(0,0),  Tile(1,0) },    // GRASS
    { Tile(0,0),  Tile(0,0),  Tile(0,0) },    // DIRT
    { Tile(3,0),  Tile(3,0),  Tile(3,0) },    // STONE
    { Tile(5,0),  Tile(5,0),  Tile(4,0) },    // OAK_PLANKS
    { Tile(6,0),  Tile(6,0),  Tile(6,0) },    // GLASS
    { Tile(7,0),  Tile(7,0),  Tile(7,0) },    // WATER
	{ Tile(9,0),  Tile(9,0),  Tile(8,0) },    // OAK_LOG
    { Tile(10,0), Tile(10,0), Tile(10,0) },   // OAK_LEAVES
    { Tile(11,0), Tile(11,0), Tile(11,0) },   // SAND
	{ Tile(12,0), Tile(12,0), Tile(12,0) },   // SNOW
};

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
    VAO_T = 0; VBO_T = 0; EBO_T = 0;
}

static bool IsTransparent(BlockType b) {
    return b == GLASS || b == WATER || b == OAK_LEAVES;
}

static bool IsOcclusionPassable(BlockType b)
{
    return b == AIR || b == GLASS;
}

uint8_t Chunk::ComputeReachableFacesFromCell(int startX, int startY, int startZ) const
{
    if (startX < 0 || startX >= SIZE_X ||
        startY < 0 || startY >= SIZE_Y ||
        startZ < 0 || startZ >= SIZE_Z)
        return 0;

    if (!IsOcclusionPassable(blocks[startX][startY][startZ]))
        return 0;

    bool visited[SIZE_X][SIZE_Y][SIZE_Z] = {};
    struct Cell { uint8_t x, y, z; };
    std::vector<Cell> queue;
    queue.reserve(SIZE_X * SIZE_Y * SIZE_Z / 4);
    queue.push_back({ (uint8_t)startX, (uint8_t)startY, (uint8_t)startZ });
    visited[startX][startY][startZ] = true;

    const int dx[] = { 0, 0, 1,-1, 0, 0 };
    const int dy[] = { 1,-1, 0, 0, 0, 0 };
    const int dz[] = { 0, 0, 0, 0, 1,-1 };

    uint8_t reachableFaces = 0;
    for (int i = 0; i < (int)queue.size(); i++)
    {
        auto [cx, cy, cz] = queue[i];

        if (cy == SIZE_Y - 1) reachableFaces |= (1 << PY);
        if (cy == 0)          reachableFaces |= (1 << NY);
        if (cx == SIZE_X - 1) reachableFaces |= (1 << PX);
        if (cx == 0)          reachableFaces |= (1 << NX);
        if (cz == SIZE_Z - 1) reachableFaces |= (1 << PZ);
        if (cz == 0)          reachableFaces |= (1 << NZ);

        for (int d = 0; d < 6; d++)
        {
            int nx = cx + dx[d];
            int ny = cy + dy[d];
            int nz = cz + dz[d];
            if (nx < 0 || nx >= SIZE_X || ny < 0 || ny >= SIZE_Y || nz < 0 || nz >= SIZE_Z)
                continue;
            if (visited[nx][ny][nz])
                continue;
            if (!IsOcclusionPassable(blocks[nx][ny][nz]))
                continue;
            visited[nx][ny][nz] = true;
            queue.push_back({ (uint8_t)nx, (uint8_t)ny, (uint8_t)nz });
        }
    }

    return reachableFaces;
}

void Chunk::ComputeVisibility()
{
    bool hasAnyOccluder = false;
    openFacesMask = 0;

    auto markFaceOpen = [&](int x, int y, int z) {
        if (!IsOcclusionPassable(blocks[x][y][z]))
        {
            hasAnyOccluder = true;
            return;
        }
        if (y == SIZE_Y - 1) openFacesMask |= (1 << PY);
        if (y == 0)          openFacesMask |= (1 << NY);
        if (x == SIZE_X - 1) openFacesMask |= (1 << PX);
        if (x == 0)          openFacesMask |= (1 << NX);
        if (z == SIZE_Z - 1) openFacesMask |= (1 << PZ);
        if (z == 0)          openFacesMask |= (1 << NZ);
    };

    for (int x = 0; x < SIZE_X; x++)
        for (int y = 0; y < SIZE_Y; y++)
            for (int z = 0; z < SIZE_Z; z++)
            {
                if (!IsOcclusionPassable(blocks[x][y][z]))
                    hasAnyOccluder = true;
                if (x == 0 || x == SIZE_X - 1 || y == 0 || y == SIZE_Y - 1 || z == 0 || z == SIZE_Z - 1)
                    markFaceOpen(x, y, z);
            }

    if (!hasAnyOccluder)
    {
        visibilityMask = 0x7FFF;
        openFacesMask = 0x3F;
        return;
    }

    uint8_t reachable[6] = {};

    for (int startFace = 0; startFace < 6; startFace++)
    {
        if (((openFacesMask >> startFace) & 1) == 0)
            continue;

        bool visited[SIZE_X][SIZE_Y][SIZE_Z] = {};
        struct Cell { uint8_t x, y, z; };
        std::vector<Cell> queue;
        queue.reserve(SIZE_X * SIZE_Y * SIZE_Z / 4);

        auto seedFace = [&](int face) {
            for (int u = 0; u < SIZE_X; u++)
                for (int v = 0; v < SIZE_Z; v++)
                {
                    int x, y, z;
                    switch (face) {
                    case PY: x = u; y = SIZE_Y - 1; z = v; break;
                    case NY: x = u; y = 0;          z = v; break;
                    case PX: x = SIZE_X - 1; y = u; z = v; break;
                    case NX: x = 0;          y = u; z = v; break;
                    case PZ: x = u; y = v; z = SIZE_Z - 1; break;
                    case NZ: x = u; y = v; z = 0;          break;
                    default: x = y = z = 0; break;
                    }
                    if (!IsOcclusionPassable(blocks[x][y][z]) || visited[x][y][z])
                        continue;
                    visited[x][y][z] = true;
                    queue.push_back({ (uint8_t)x,(uint8_t)y,(uint8_t)z });
                }
        };

        seedFace(startFace);
        if (queue.empty())
            continue;

        reachable[startFace] |= (1 << startFace);

        for (int i = 0; i < (int)queue.size(); i++)
        {
            auto [cx, cy, cz] = queue[i];

            if (cy == SIZE_Y - 1) reachable[startFace] |= (1 << PY);
            if (cy == 0)          reachable[startFace] |= (1 << NY);
            if (cx == SIZE_X - 1) reachable[startFace] |= (1 << PX);
            if (cx == 0)          reachable[startFace] |= (1 << NX);
            if (cz == SIZE_Z - 1) reachable[startFace] |= (1 << PZ);
            if (cz == 0)          reachable[startFace] |= (1 << NZ);

            const int dx[] = { 0, 0, 1,-1, 0, 0 };
            const int dy[] = { 1,-1, 0, 0, 0, 0 };
            const int dz[] = { 0, 0, 0, 0, 1,-1 };
            for (int d = 0; d < 6; d++)
            {
                int nx = cx + dx[d];
                int ny = cy + dy[d];
                int nz = cz + dz[d];
                if (nx < 0 || nx >= SIZE_X || ny < 0 || ny >= SIZE_Y || nz < 0 || nz >= SIZE_Z)
                    continue;
                if (visited[nx][ny][nz])
                    continue;
                if (!IsOcclusionPassable(blocks[nx][ny][nz]))
                    continue;
                visited[nx][ny][nz] = true;
                queue.push_back({ (uint8_t)nx,(uint8_t)ny,(uint8_t)nz });
            }
        }
    }

    visibilityMask = 0;
    for (int a = 0; a < 6; a++)
        for (int b = a + 1; b < 6; b++)
            if ((reachable[a] >> b) & 1)
                visibilityMask |= (1 << FacePairBit(a, b));
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

            surfaceY = std::clamp(surfaceY, 1, 500);

            // Заполняем блоки
            for (int y = 0; y < SIZE_Y; y++)
            {
                int worldY = worldChunkY + y; // мировая Y координата блока

                BlockType block = AIR;

                if (worldY == 0)
                    block = STONE;
                else if (worldY < surfaceY - 3)
                {
                    block = STONE;

                    // Пещеры
                    float n1 = caveNoise.GetNoise(worldX, (float)worldY, worldZ);
                    float n2 = caveNoise2.GetNoise(worldX, (float)worldY * 0.5f, worldZ);
                    if (n1 * n1 + n2 * n2 < 0.06f)
                        block = AIR;
                }
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

    ComputeVisibility();
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
    bool transparent)
{
    auto& verts = transparent ? verticesT : vertices;
    auto& inds = transparent ? indicesT : indices;

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
            | ((uint32_t)(corner & 3) << 1);

        uint32_t d1 = (uint32_t)(uint8_t)tileID
            | ((uint32_t)(uint8_t)w << 8)
            | ((uint32_t)(uint8_t)h << 16);

        verts.push_back({ d0, d1 });
        };

    uint32_t base = (uint32_t)verts.size();
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
}

void Chunk::GenerateMeshData()
{
    vertices.clear();
    indices.clear();
    verticesT.clear();
    indicesT.clear();

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

    // +Y (top faces)
    for (int y = 0; y <= SIZE_Y; y++)
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

                bool drawFace = (cur != AIR) &&
                    (above == AIR || (isTransparent(above) && above != cur)) &&
                    !(isTransparent(cur) && isTransparent(above));

                if (drawFace)
                {
                    cell.tileID = getTile(cur, 0);
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

                BlockType cur = getBlock(x, y, z);
                bool trans = isTransparent(cur);
                float topY = y + 1.0f;

                AddQuad(glm::vec3(x, topY, z),
                    glm::vec3(1, 0, 0), dx,
                    glm::vec3(0, 0, 1), dz,
                    ref.tileID, false,
                    ref.ao[0], ref.ao[1], ref.ao[2], ref.ao[3],
                    0, trans);
            }
    }

    // -Y (bottom faces)
    for (int y = 0; y <= SIZE_Y; y++)
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
                if (cur != AIR &&
                    (below == AIR || (isTransparent(below) && below != cur)) &&
                    !(isTransparent(cur) && isTransparent(below)))
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

                BlockType cur = getBlock(x, y, z);
                bool trans = isTransparent(cur);

                AddQuad(glm::vec3(x, y, z),
                    glm::vec3(1, 0, 0), dx,
                    glm::vec3(0, 0, 1), dz,
                    ref.tileID, true,
                    ref.ao[0], ref.ao[1], ref.ao[2], ref.ao[3],
                    1, trans);
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

                if (cur != AIR &&
                    (neighbor == AIR || (isTransparent(neighbor) && neighbor != cur)) &&
                    !(isTransparent(cur) && isTransparent(neighbor)))
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
                bool trans = isTransparent(cur);
                int quadH = dy;

                AddQuad(glm::vec3(x + 1, y, z),
                    glm::vec3(0, 0, 1), dz,
                    glm::vec3(0, 1, 0), quadH,
                    ref.tileID, false,
                    ref.ao[0], ref.ao[1], ref.ao[2], ref.ao[3],
                    2, trans);
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

                if (cur != AIR &&
                    (neighbor == AIR || (isTransparent(neighbor) && neighbor != cur)) &&
                    !(isTransparent(cur) && isTransparent(neighbor)))
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
                bool trans = isTransparent(cur);
                int quadH = dy;

                AddQuad(glm::vec3(x, y, z),
                    glm::vec3(0, 0, 1), dz,
                    glm::vec3(0, 1, 0), quadH,
                    ref.tileID, true,
                    ref.ao[0], ref.ao[1], ref.ao[2], ref.ao[3],
                    3, trans);
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

                if (cur != AIR &&
                    (neighbor == AIR || (isTransparent(neighbor) && neighbor != cur)) &&
                    !(isTransparent(cur) && isTransparent(neighbor)))
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
                bool trans = isTransparent(cur);
                int quadH = dy;

                AddQuad(glm::vec3(x, y, z + 1),
                    glm::vec3(1, 0, 0), dx,
                    glm::vec3(0, 1, 0), quadH,
                    ref.tileID, true,
                    ref.ao[0], ref.ao[1], ref.ao[2], ref.ao[3],
                    4, trans);
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

                if (cur != AIR &&
                    (neighbor == AIR || (isTransparent(neighbor) && neighbor != cur)) &&
                    !(isTransparent(cur) && isTransparent(neighbor)))
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
                bool trans = isTransparent(cur);
                int quadH = dy;

                AddQuad(glm::vec3(x, y, z),
                    glm::vec3(1, 0, 0), dx,
                    glm::vec3(0, 1, 0), quadH,
                    ref.tileID, false,
                    ref.ao[0], ref.ao[1], ref.ao[2], ref.ao[3],
                    5, trans);
            }
    }
}

void Chunk::UploadToGPU(bool isRebuild)
{
    constexpr int STRIDE = sizeof(PackedVertex);

    // Непрозрачный меш
    if (VAO == 0)
    {
        glGenVertexArrays(1, &VAO);
        glGenBuffers(1, &VBO);
        glGenBuffers(1, &EBO);
    }

    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(PackedVertex), vertices.data(), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint32_t), indices.data(), GL_DYNAMIC_DRAW);

    glVertexAttribIPointer(0, 2, GL_UNSIGNED_INT, STRIDE, (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);

    // Прозрачный меш
    if (!verticesT.empty())
    {
        if (VAO_T == 0)
        {
            glGenVertexArrays(1, &VAO_T);
            glGenBuffers(1, &VBO_T);
            glGenBuffers(1, &EBO_T);
        }

        glBindVertexArray(VAO_T);
        glBindBuffer(GL_ARRAY_BUFFER, VBO_T);
        glBufferData(GL_ARRAY_BUFFER, verticesT.size() * sizeof(PackedVertex), verticesT.data(), GL_DYNAMIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO_T);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indicesT.size() * sizeof(uint32_t), indicesT.data(), GL_DYNAMIC_DRAW);

        glVertexAttribIPointer(0, 2, GL_UNSIGNED_INT, STRIDE, (void*)0);
        glEnableVertexAttribArray(0);
        glBindVertexArray(0);
    }

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

void Chunk::BuildMesh()
{
    GenerateMeshData();
    ComputeVisibility(); // пересчитываем после изменения блоков
    UploadToGPU(true);  // rebuild — без fence
}

void Chunk::FreeGPU()
{
    if (uploadFence) { glDeleteSync(uploadFence); uploadFence = nullptr; }
    gpuReady = false;
    if (VAO) { glDeleteVertexArrays(1, &VAO);   VAO = 0; }
    if (VBO) { glDeleteBuffers(1, &VBO);         VBO = 0; }
    if (EBO) { glDeleteBuffers(1, &EBO);         EBO = 0; }
    if (VAO_T) { glDeleteVertexArrays(1, &VAO_T); VAO_T = 0; }
    if (VBO_T) { glDeleteBuffers(1, &VBO_T);       VBO_T = 0; }
    if (EBO_T) { glDeleteBuffers(1, &EBO_T);       EBO_T = 0; }
    vertices.clear();
    indices.clear();
    verticesT.clear();
    indicesT.clear();
    state.store(ChunkState::Empty);
}

void Chunk::Draw()
{
    if (indices.empty()) return;
    glBindVertexArray(VAO);
    glDrawElements(GL_TRIANGLES, (GLsizei)indices.size(), GL_UNSIGNED_INT, 0);
}

void Chunk::DrawTransparent()
{
    if (VAO_T == 0 || indicesT.empty()) return;
    glBindVertexArray(VAO_T);
    glDrawElements(GL_TRIANGLES, (GLsizei)indicesT.size(), GL_UNSIGNED_INT, 0);
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