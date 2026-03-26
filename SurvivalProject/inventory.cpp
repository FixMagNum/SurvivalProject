#include "inventory.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cstring>
#include <filesystem>
#include <fstream>

constexpr int   ATLAS = 16;
constexpr float T = 1.0f / ATLAS;

static void GetTileUV(int tileID, float& u0, float& v0, float& u1, float& v1)
{
    int tx = tileID % ATLAS;
    int ty = tileID / ATLAS;
    u0 = tx * T;
    v0 = ty * T;
    u1 = u0 + T;
    v1 = v0 + T;
}

static int GetTopTile(BlockType type)
{
    auto tile = [](int x, int y) { return y * ATLAS + x; };
    switch (type)
    {
    case GRASS:      return tile(2,  0);
    case DIRT:       return tile(0,  0);
    case STONE:      return tile(3,  0);
    case OAK_PLANKS: return tile(5,  0);
    case GLASS:      return tile(6,  0);
	case WATER:      return tile(7,  0);
	case OAK_LOG:    return tile(8,  0);
	case OAK_LEAVES: return tile(10, 0);
	case SAND:       return tile(11, 0);
	case SNOW:       return tile(12, 0);
    case BEDROCK:    return tile(13, 0);
    default:         return -1;
    }
}

static const char* invVert = R"(
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aUV;
layout (location = 2) in vec4 aColor;

out vec2 vUV;
out vec4 vColor;

uniform vec2 uScreenSize;

void main()
{
    vec2 ndc = (aPos / uScreenSize) * 2.0 - 1.0;
    ndc.y = -ndc.y;
    gl_Position = vec4(ndc, 0.0, 1.0);
    vUV    = aUV;
    vColor = aColor;
}
)";

static const char* invFrag = R"(
#version 330 core
in vec2 vUV;
in vec4 vColor;
out vec4 FragColor;

uniform sampler2D uAtlas;
uniform bool      uUseTexture;

void main()
{
    if (uUseTexture)
        FragColor = texture(uAtlas, vUV) * vColor;
    else
        FragColor = vColor;

    FragColor.rgb = pow(FragColor.rgb, vec3(1.0 / 2.2));
}
)";

Inventory::Inventory() : VAO(0), VBO(0), shaderProgram(0), atlasTexID(0) {}

Inventory::~Inventory()
{
    if (VAO) glDeleteVertexArrays(1, &VAO);
    if (VBO) glDeleteBuffers(1, &VBO);
    if (shaderProgram) glDeleteProgram(shaderProgram);
}

unsigned int Inventory::CompileShader(const char* vert, const char* frag)
{
    unsigned int vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &vert, NULL);
    glCompileShader(vs);

    unsigned int fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &frag, NULL);
    glCompileShader(fs);

    unsigned int prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);

    glDeleteShader(vs);
    glDeleteShader(fs);
    return prog;
}

void Inventory::BuildQuad(float* buf, int& off,
    float x, float y, float w, float h,
    float u0, float v0, float u1, float v1,
    float r, float g, float b, float a)
{
    float verts[6][8] = {
        { x,     y,     u0, v0, r, g, b, a },
        { x + w, y,     u1, v0, r, g, b, a },
        { x + w, y + h, u1, v1, r, g, b, a },
        { x,     y,     u0, v0, r, g, b, a },
        { x + w, y + h, u1, v1, r, g, b, a },
        { x,     y + h, u0, v1, r, g, b, a },
    };
    memcpy(buf + off, verts, sizeof(verts));
    off += 6 * 8;
}

void Inventory::Init(unsigned int atlasTexture)
{
    atlasTexID = atlasTexture;
    shaderProgram = CompileShader(invVert, invFrag);

    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);

    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);

    // Максимум квадов с запасом: фон + слоты + иконки + перетаскивание
    glBufferData(GL_ARRAY_BUFFER, 256 * 6 * 8 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);

    constexpr int STRIDE = 8 * sizeof(float);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, STRIDE, (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, STRIDE, (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, STRIDE, (void*)(4 * sizeof(float)));
    glEnableVertexAttribArray(2);

    glBindVertexArray(0);
}

void Inventory::Draw(float screenW, float screenH, float mouseX, float mouseY)
{
    if (!isOpen) return;

    constexpr float SLOT_SIZE = 50.0f;
    constexpr float PADDING = 4.0f;
    constexpr float MARGIN = 6.0f;

    float totalW = COLS * SLOT_SIZE + (COLS - 1) * PADDING;
    float totalH = ROWS * SLOT_SIZE + (ROWS - 1) * PADDING;

    float startX = (screenW - totalW) / 2.0f;
    float startY = (screenH - totalH) / 2.0f;

    float buf[256 * 6 * 8];
    int   off = 0;

    // Тёмный фон за инвентарём
    BuildQuad(buf, off,
        startX - 12, startY - 12,
        totalW + 24, totalH + 24,
        0, 0, 0, 0,
        0.0f, 0.0f, 0.0f, 0.25f);

    int bgQuads = off / (6 * 8);

    // Фон слотов
    for (int row = 0; row < ROWS; row++)
        for (int col = 0; col < COLS; col++)
        {
            float x = startX + col * (SLOT_SIZE + PADDING);
            float y = startY + row * (SLOT_SIZE + PADDING);

            int idx = row * COLS + col;
            bool hovered = (GetSlotAt(mouseX, mouseY, screenW, screenH) == idx);

            float brightness = hovered ? 0.4f : 0.2f;
            BuildQuad(buf, off, x, y, SLOT_SIZE, SLOT_SIZE,
                0, 0, 0, 0,
                brightness, brightness, brightness, 0.5f);
        }

    int slotQuads = off / (6 * 8) - bgQuads;

    // Иконки предметов
    for (int i = 0; i < SIZE; i++)
    {
        if (slots[i].type == AIR) continue;
        if (dragFromInventory && i == dragSlot) continue; // тащимый предмет рисуем отдельно

        int tileID = GetTopTile(slots[i].type);
        if (tileID < 0) continue;

        float u0, v0, u1, v1;
        GetTileUV(tileID, u0, v0, u1, v1);

        int row = i / COLS;
        int col = i % COLS;
        float x = startX + col * (SLOT_SIZE + PADDING) + MARGIN;
        float y = startY + row * (SLOT_SIZE + PADDING) + MARGIN;
        float s = SLOT_SIZE - MARGIN * 2;

        BuildQuad(buf, off, x, y, s, s, u0, v0, u1, v1, 1, 1, 1, 1);
    }

    int iconQuads = off / (6 * 8) - bgQuads - slotQuads;

    // Перетаскиваемый предмет — рисуем у курсора
    int dragIconQuads = 0;
    if (dragSlot >= 0 && dragItem.type != AIR)
    {
        int tileID = GetTopTile(dragItem.type);
        if (tileID >= 0)
        {
            float u0, v0, u1, v1;
            GetTileUV(tileID, u0, v0, u1, v1);
            float s = SLOT_SIZE - MARGIN * 2;
            BuildQuad(buf, off,
                mouseX - s / 2, mouseY - s / 2, s, s,
                u0, v0, u1, v1,
                1, 1, 1, 1);
            dragIconQuads = 1;
        }
    }

    // Загружаем на GPU
    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferSubData(GL_ARRAY_BUFFER, 0, off * sizeof(float), buf);

    glUseProgram(shaderProgram);
    glUniform2f(glGetUniformLocation(shaderProgram, "uScreenSize"), screenW, screenH);
    glUniform1i(glGetUniformLocation(shaderProgram, "uAtlas"), 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, atlasTexID);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Фон
    glUniform1i(glGetUniformLocation(shaderProgram, "uUseTexture"), 0);
    glDrawArrays(GL_TRIANGLES, 0, (bgQuads + slotQuads) * 6);

    // Иконки
    glUniform1i(glGetUniformLocation(shaderProgram, "uUseTexture"), 1);
    glDrawArrays(GL_TRIANGLES, (bgQuads + slotQuads) * 6, (iconQuads + dragIconQuads) * 6);

    glDisable(GL_BLEND);
    glBindVertexArray(0);
}

int Inventory::GetSlotAt(float mouseX, float mouseY, float screenW, float screenH)
{
    constexpr float SLOT_SIZE = 50.0f;
    constexpr float PADDING = 4.0f;

    float totalW = COLS * SLOT_SIZE + (COLS - 1) * PADDING;
    float totalH = ROWS * SLOT_SIZE + (ROWS - 1) * PADDING;
    float startX = (screenW - totalW) / 2.0f;
    float startY = (screenH - totalH) / 2.0f;

    for (int row = 0; row < ROWS; row++)
        for (int col = 0; col < COLS; col++)
        {
            float x = startX + col * (SLOT_SIZE + PADDING);
            float y = startY + row * (SLOT_SIZE + PADDING);
            if (mouseX >= x && mouseX <= x + SLOT_SIZE &&
                mouseY >= y && mouseY <= y + SLOT_SIZE)
                return row * COLS + col;
        }
    return -1;
}

static int GetHotbarSlotAt(float mouseX, float mouseY, float screenW, float screenH)
{
    constexpr float SLOT_SIZE = 50.0f;
    constexpr float PADDING = 4.0f;

    float totalW = 9 * SLOT_SIZE + 8 * PADDING;
    float startX = (screenW - totalW) / 2.0f;
    float startY = screenH - SLOT_SIZE - 16.0f;

    for (int i = 0; i < 9; i++)
    {
        float x = startX + i * (SLOT_SIZE + PADDING);
        if (mouseX >= x && mouseX <= x + SLOT_SIZE &&
            mouseY >= startY && mouseY <= startY + SLOT_SIZE)
            return i;
    }
    return -1;
}

void Inventory::OnMousePress(float mouseX, float mouseY, float screenW, float screenH, Hotbar& hotbar)
{
    int slot = GetSlotAt(mouseX, mouseY, screenW, screenH);
    if (slot >= 0 && slots[slot].type != AIR)
    {
        dragSlot = slot;
        dragItem = slots[slot];
        dragFromInventory = true;
        dragFromHotbarSlot = -1;
        slots[slot] = { AIR, 0 };
        return;
    }

    int hSlot = GetHotbarSlotAt(mouseX, mouseY, screenW, screenH);
    if (hSlot >= 0 && hotbar.slots[hSlot] != AIR)
    {
        dragSlot = hSlot;
        dragItem = { hotbar.slots[hSlot], hotbar.counts[hSlot] }; // реальный count!
        dragFromInventory = false;
        dragFromHotbarSlot = hSlot;
        hotbar.slots[hSlot] = AIR;
        hotbar.counts[hSlot] = 0; // сбрасываем count
    }
}

void Inventory::OnMouseRelease(float mouseX, float mouseY, float screenW, float screenH, Hotbar& hotbar)
{
    if (dragSlot < 0) return;

    int invSlot = GetSlotAt(mouseX, mouseY, screenW, screenH);
    int hbarSlot = GetHotbarSlotAt(mouseX, mouseY, screenW, screenH);

    if (invSlot >= 0 && !(dragFromInventory && invSlot == dragSlot))
    {
        ItemStack& target = slots[invSlot];

        // Если тот же тип — складываем стаки
        if (target.type == dragItem.type && target.count < 64)
        {
            int canAdd = 64 - target.count;
            int add = std::min(canAdd, dragItem.count);
            target.count += add;
            dragItem.count -= add;

            // Если перетащили всё — возвращать нечего
            if (dragItem.count <= 0)
            {
                dragSlot = -1;
                dragItem = { AIR, 0 };
                return;
            }
            // Иначе остаток возвращаем откуда взяли
            if (dragFromInventory) slots[dragSlot] = dragItem;
            else { hotbar.slots[dragFromHotbarSlot] = dragItem.type; hotbar.counts[dragFromHotbarSlot] = dragItem.count; }
        }
        else
        {
            // Разные типы — меняем местами
            ItemStack temp = target;
            target = dragItem;
            if (temp.type != AIR)
            {
                if (dragFromInventory) slots[dragSlot] = temp;
                else { hotbar.slots[dragFromHotbarSlot] = temp.type; hotbar.counts[dragFromHotbarSlot] = temp.count; } // добавляем temp.count!
            }
        }
    }
    else if (hbarSlot >= 0 && !(!dragFromInventory && hbarSlot == dragSlot))
    {
        BlockType targetType = hotbar.slots[hbarSlot];
        int       targetCount = hotbar.counts[hbarSlot];

        // Если тот же тип — складываем
        if (targetType == dragItem.type && targetCount < 64)
        {
            int canAdd = 64 - targetCount;
            int add = std::min(canAdd, dragItem.count);
            hotbar.counts[hbarSlot] += add;
            dragItem.count -= add;

            if (dragItem.count <= 0)
            {
                dragSlot = -1;
                dragItem = { AIR, 0 };
                return;
            }
            if (dragFromInventory) slots[dragSlot] = dragItem;
            else { hotbar.slots[dragFromHotbarSlot] = dragItem.type; hotbar.counts[dragFromHotbarSlot] = dragItem.count; }
        }
        else
        {
            // Разные типы — меняем местами
            hotbar.slots[hbarSlot] = dragItem.type;
            hotbar.counts[hbarSlot] = dragItem.count;
            if (targetType != AIR)
            {
                if (dragFromInventory) slots[dragSlot] = { targetType, targetCount };
                else { hotbar.slots[dragFromHotbarSlot] = targetType; hotbar.counts[dragFromHotbarSlot] = targetCount; }
            }
        }
    }
    else
    {
        // Возвращаем на место
        if (dragFromInventory) slots[dragSlot] = dragItem;
        else { hotbar.slots[dragFromHotbarSlot] = dragItem.type; hotbar.counts[dragFromHotbarSlot] = dragItem.count; } // добавляем counts!
    }

    dragSlot = -1;
    dragItem = { AIR, 0 };
}

void Inventory::Save()
{
    std::filesystem::create_directories("saves");
    std::ofstream f("saves/inventory.bin", std::ios::binary);
    if (!f) return;
    for (int i = 0; i < SIZE; i++)
    {
        f.write((char*)&slots[i].type, sizeof(BlockType));
        f.write((char*)&slots[i].count, sizeof(int));
    }
}

void Inventory::Load()
{
    std::ifstream f("saves/inventory.bin", std::ios::binary);
    if (!f) return;
    for (int i = 0; i < SIZE; i++)
    {
        f.read((char*)&slots[i].type, sizeof(BlockType));
        f.read((char*)&slots[i].count, sizeof(int));
    }
}