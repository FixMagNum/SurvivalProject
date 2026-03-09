#pragma once

enum BlockType
{
	AIR = 0,
	GRASS,
	DIRT,
	STONE,
	OAK_PLANKS,
};

struct BlockData
{
	int top;
	int bottom;
	int side;
};