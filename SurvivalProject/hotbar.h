#pragma once
#include <glad/glad.h>
#include <glm/glm.hpp>
#include "block.h"

class Hotbar
{
public:
    static const int SLOTS = 9;

    BlockType slots[SLOTS] = {
        GRASS, DIRT, STONE, OAK_PLANKS, GLASS, AIR, AIR, AIR, AIR
    };

    int activeSlot = 0;

    Hotbar();
    ~Hotbar();

    void Init(unsigned int atlasTexture);
    void Draw(float screenW, float screenH);

    void ScrollSlot(int delta); // +1 или -1
    void SetSlot(int index);

    BlockType GetActiveBlock() const { return slots[activeSlot]; }

private:
    unsigned int VAO, VBO;
    unsigned int shaderProgram;
    unsigned int atlasTexID;
    unsigned int CompileShader(const char* vert, const char* frag);

    void BuildQuad(float* buf, int& off,
        float x, float y, float w, float h,
        float u0, float v0, float u1, float v1,
        float r, float g, float b, float a);
};