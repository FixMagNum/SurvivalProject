#include "world.h"
#include <cmath>

BlockType World::GetBlock(int worldX, int worldY, int worldZ)
{
    if (worldY < 0 || worldY >= Chunk::SIZE_Y)
        return AIR;

    int chunkX = (int)floor((float)worldX / Chunk::SIZE_X);
    int chunkZ = (int)floor((float)worldZ / Chunk::SIZE_Z);

    auto it = chunkMap.find({ chunkX, chunkZ });
    if (it == chunkMap.end()) return AIR;

    int localX = worldX - chunkX * Chunk::SIZE_X;
    int localZ = worldZ - chunkZ * Chunk::SIZE_Z;
    return it->second->blocks[localX][worldY][localZ];
}

void World::SetBlock(int worldX, int worldY, int worldZ, BlockType type)
{
    if (worldY < 0 || worldY >= Chunk::SIZE_Y) return;

    int chunkX = (int)floor((float)worldX / Chunk::SIZE_X);
    int chunkZ = (int)floor((float)worldZ / Chunk::SIZE_Z);

    auto it = chunkMap.find({ chunkX, chunkZ });
    if (it == chunkMap.end()) return;

    int localX = worldX - chunkX * Chunk::SIZE_X;
    int localZ = worldZ - chunkZ * Chunk::SIZE_Z;
    it->second->blocks[localX][worldY][localZ] = type;
}

// DDA (Digital Differential Analyzer) raycast по блокам
// Точный и быстрый — шагает ровно по одному блоку за итерацию
RaycastResult World::Raycast(glm::vec3 origin, glm::vec3 dir, float maxDistance)
{
    RaycastResult result;

    // Нормализуем на всякий случай
    float len = glm::length(dir);
    if (len < 0.0001f) return result;
    dir /= len;

    // Текущий блок
    int x = (int)floor(origin.x);
    int y = (int)floor(origin.y);
    int z = (int)floor(origin.z);

    // Направление шага по каждой оси
    int stepX = (dir.x >= 0) ? 1 : -1;
    int stepY = (dir.y >= 0) ? 1 : -1;
    int stepZ = (dir.z >= 0) ? 1 : -1;

    // tDelta: сколько нужно пройти по лучу чтобы пересечь одну клетку по каждой оси
    float tDeltaX = (dir.x != 0) ? fabs(1.0f / dir.x) : 1e30f;
    float tDeltaY = (dir.y != 0) ? fabs(1.0f / dir.y) : 1e30f;
    float tDeltaZ = (dir.z != 0) ? fabs(1.0f / dir.z) : 1e30f;

    // tMax: расстояние до ближайшей границы по каждой оси
    float tMaxX = (dir.x >= 0)
        ? ((floor(origin.x) + 1 - origin.x) * tDeltaX)
        : ((origin.x - floor(origin.x)) * tDeltaX);
    float tMaxY = (dir.y >= 0)
        ? ((floor(origin.y) + 1 - origin.y) * tDeltaY)
        : ((origin.y - floor(origin.y)) * tDeltaY);
    float tMaxZ = (dir.z >= 0)
        ? ((floor(origin.z) + 1 - origin.z) * tDeltaZ)
        : ((origin.z - floor(origin.z)) * tDeltaZ);

    // Нормаль последней пересечённой грани
    int normX = 0, normY = 0, normZ = 0;

    float t = 0.0f;

    while (t < maxDistance)
    {
        // Проверяем текущий блок
        BlockType block = GetBlock(x, y, z);
        if (block != AIR)
        {
            result.hit = true;
            result.worldX = x;
            result.worldY = y;
            result.worldZ = z;
            result.normalX = normX;
            result.normalY = normY;
            result.normalZ = normZ;
            return result;
        }

        // Шагаем по оси с минимальным tMax
        if (tMaxX < tMaxY && tMaxX < tMaxZ)
        {
            t = tMaxX;
            tMaxX += tDeltaX;
            x += stepX;
            normX = -stepX; normY = 0; normZ = 0;
        }
        else if (tMaxY < tMaxZ)
        {
            t = tMaxY;
            tMaxY += tDeltaY;
            y += stepY;
            normX = 0; normY = -stepY; normZ = 0;
        }
        else
        {
            t = tMaxZ;
            tMaxZ += tDeltaZ;
            z += stepZ;
            normX = 0; normY = 0; normZ = -stepZ;
        }
    }

    return result; // hit = false
}

// Перестраиваем меш чанка где лежит блок.
// Если блок на границе чанка — перестраиваем соседний тоже
// (иначе у соседа останется «дыра» или лишняя грань).
void World::RebuildChunkAt(int worldX, int worldY, int worldZ)
{
    int chunkX = (int)floor((float)worldX / Chunk::SIZE_X);
    int chunkZ = (int)floor((float)worldZ / Chunk::SIZE_Z);

    // Перестраиваем основной чанк
    auto rebuild = [&](int cx, int cz) {
        auto it = chunkMap.find({ cx, cz });
        if (it != chunkMap.end())
            it->second->BuildMesh();
        };

    rebuild(chunkX, chunkZ);

    // Локальная позиция внутри чанка
    int localX = worldX - chunkX * Chunk::SIZE_X;
    int localZ = worldZ - chunkZ * Chunk::SIZE_Z;

    // Если на границе — перестраиваем соседа
    if (localX == 0)                    rebuild(chunkX - 1, chunkZ);
    if (localX == Chunk::SIZE_X - 1)    rebuild(chunkX + 1, chunkZ);
    if (localZ == 0)                    rebuild(chunkX, chunkZ - 1);
    if (localZ == Chunk::SIZE_Z - 1)    rebuild(chunkX, chunkZ + 1);
}