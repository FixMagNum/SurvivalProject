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

class World {
public:
	std::vector<Chunk> chunks;
	std::unordered_map<ChunkKey, Chunk*, ChunkKeyHash> chunkMap; // для быстрого поиска

	BlockType GetBlock(int worldX, int worldY, int worldZ);
};