#pragma once

enum BlockType
{
	AIR = 0,
	GRASS,
	DIRT,
	STONE,
};

struct BlockData
{
	int top;
	int bottom;
	int side;
};