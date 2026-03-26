#pragma once

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
};

struct BlockData
{
	int top;
	int bottom;
	int side;
};