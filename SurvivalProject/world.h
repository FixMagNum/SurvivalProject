#pragma once
#include <vector>
#include <unordered_map>
#include "chunk.h"

struct ChunkKey {
    int x, z;
    bool operator==(const ChunkKey& o) const { return x == o.x && z == o.z; }
};

struct ChunkKeyHash {
    size_t operator()(const ChunkKey& k) const {
        return std::hash<int>()(k.x) ^ (std::hash<int>()(k.z) << 16);
    }
};

// Результат рейкаста
struct RaycastResult {
    bool  hit = false;
    int   worldX, worldY, worldZ;   // координаты блока который hit
    int   normalX, normalY, normalZ; // нормаль грани (куда ставить блок)
};

class World {
public:
    std::vector<Chunk> chunks;
    std::unordered_map<ChunkKey, Chunk*, ChunkKeyHash> chunkMap;

    BlockType GetBlock(int worldX, int worldY, int worldZ);
    void      SetBlock(int worldX, int worldY, int worldZ, BlockType type);

    // DDA raycast: origin + direction, maxDistance в блоках
    RaycastResult Raycast(glm::vec3 origin, glm::vec3 direction, float maxDistance);

    // Перестроить меш чанка и его соседей если блок на границе
    void RebuildChunkAt(int worldX, int worldY, int worldZ);
};