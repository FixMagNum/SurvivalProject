#include "world.h"

BlockType World::GetBlock(int worldX, int worldY, int worldZ)
{
    if (worldY < 0 || worldY >= Chunk::SIZE_Y)
        return AIR;

    int chunkX = floor((float)worldX / Chunk::SIZE_X);
    int chunkZ = floor((float)worldZ / Chunk::SIZE_Z);

    auto it = chunkMap.find({ chunkX, chunkZ });
    if (it == chunkMap.end()) return AIR;

    int localX = worldX - chunkX * Chunk::SIZE_X;
    int localZ = worldZ - chunkZ * Chunk::SIZE_Z;
    return it->second->blocks[localX][worldY][localZ];

    for (auto& chunk : chunks)
    {
        if (chunk.chunkPos.x == chunkX &&
            chunk.chunkPos.y == chunkZ)
        {
            int localX = worldX - chunkX * Chunk::SIZE_X;
            int localZ = worldZ - chunkZ * Chunk::SIZE_Z;

            return chunk.blocks[localX][worldY][localZ];
        }
    }

    return AIR;
}