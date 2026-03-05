#pragma once
#include <vector>
#include "chunk.h"

class World {
public:
	std::vector<Chunk> chunks;

	BlockType GetBlock(int worldX, int worldY, int worldZ);
};