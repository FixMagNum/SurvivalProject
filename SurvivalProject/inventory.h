#pragma once
#include <glad/glad.h>
#include <glm/glm.hpp>
#include "block.h"
#include "hotbar.h"

struct ItemStack
{
    BlockType type = AIR;
    int count = 0;
};

class Inventory
{
public:
    static const int COLS = 9;
    static const int ROWS = 3;
    static const int SIZE = COLS * ROWS;

    ItemStack slots[SIZE];

    bool isOpen = false;

    Inventory();
    ~Inventory();

    void Init(unsigned int atlasTexture);
    void Draw(float screenW, float screenH, float mouseX, float mouseY);

    // Возвращает индекс слота под курсором или -1
    int GetSlotAt(float mouseX, float mouseY, float screenW, float screenH);

    // Перетаскивание
    int  dragSlot = -1;      // слот откуда тащим
    ItemStack dragItem;      // что тащим

    void OnMousePress(float mouseX, float mouseY, float screenW, float screenH);
    void OnMouseRelease(float mouseX, float mouseY, float screenW, float screenH);

    void OnMousePress(float mouseX, float mouseY, float screenW, float screenH, Hotbar& hotbar);
    void OnMouseRelease(float mouseX, float mouseY, float screenW, float screenH, Hotbar& hotbar);

    // Откуда тащим: true = инвентарь, false = хотбар
    bool dragFromInventory = true;
    int  dragFromHotbarSlot = -1;

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