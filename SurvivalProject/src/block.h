#pragma once
#include <cstdint>	

enum BlockType
{
	AIR = 0,
	GRASS,
	DIRT,
	STONE,
	OAK_PLANKS,
	GLASS,
	WATER,
	OAK_LOG,
	OAK_LEAVES,
	SAND,
	SNOW,
	BEDROCK,
	COBBLESTONE,
	POOP,
	BASALT,
	TALL_GRASS,
	RED_FLOWER,
};

enum class RenderGroup : uint8_t
{
	Opaque = 0,
	Leaves = 1, // alpha clip
	Water  = 2, // translucent
	Glass  = 3, // translucent
	Count
};

inline RenderGroup GetRenderGroup(BlockType b)
{
	switch (b)
	{
	case OAK_LEAVES: return RenderGroup::Leaves;
	case TALL_GRASS: return RenderGroup::Leaves;
	case RED_FLOWER: return RenderGroup::Leaves;
	case WATER:      return RenderGroup::Water;
	case GLASS:      return RenderGroup::Glass;
	default:         return RenderGroup::Opaque;
	}
}

inline bool IsRenderable(BlockType b)
{
	return b != AIR;
}

struct BlockData
{
	int top;
	int bottom;
	int side;
};