#include "hotbar.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cstring>

// Позиция тайла в атласе для каждого блока (top face)
static void GetTileUV(int tileID, float& u0, float& v0, float& u1, float& v1)
{
    constexpr int   ATLAS = 16;
    constexpr float T = 1.0f / ATLAS;
    int tx = tileID % ATLAS;
    int ty = tileID / ATLAS;
    u0 = tx * T;
    v0 = ty * T;
    u1 = u0 + T;
    v1 = v0 + T;
}

static int GetTopTile(BlockType type)
{
    // Дублируем логику blockDatabase из chunk.cpp
    constexpr int ATLAS = 16;
    auto tile = [](int x, int y) { return y * ATLAS + x; };

    switch (type)
    {
    case GRASS:      return tile(2, 0);
    case DIRT:       return tile(0, 0);
    case STONE:      return tile(3, 0);
    case OAK_PLANKS: return tile(5, 0);
    case GLASS:      return tile(6, 0);
    default:         return -1;
    }
}

static const char* hotbarVert = R"(
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aUV;
layout (location = 2) in vec4 aColor;

out vec2 vUV;
out vec4 vColor;

uniform vec2 uScreenSize;

void main()
{
    // Переводим пиксельные координаты в NDC
    vec2 ndc = (aPos / uScreenSize) * 2.0 - 1.0;
    ndc.y = -ndc.y; // Y вниз в пикселях, вверх в NDC
    gl_Position = vec4(ndc, 0.0, 1.0);
    vUV    = aUV;
    vColor = aColor;
}
)";

static const char* hotbarFrag = R"(
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
}
)";

Hotbar::Hotbar() : VAO(0), VBO(0), shaderProgram(0), atlasTexID(0) {}

Hotbar::~Hotbar()
{
    if (VAO) glDeleteVertexArrays(1, &VAO);
    if (VBO) glDeleteBuffers(1, &VBO);
    if (shaderProgram) glDeleteProgram(shaderProgram);
}

unsigned int Hotbar::CompileShader(const char* vert, const char* frag)
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

// Один quad = 6 вершин, каждая: pos(2) + uv(2) + color(4) = 8 floats
void Hotbar::BuildQuad(float* buf, int& off,
    float x, float y, float w, float h,
    float u0, float v0, float u1, float v1,
    float r = 1, float g = 1, float b = 1, float a = 1)
{
    // triangle 1
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

void Hotbar::Init(unsigned int atlasTexture)
{
    atlasTexID = atlasTexture;
    shaderProgram = CompileShader(hotbarVert, hotbarFrag);

    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);

    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);

    // Максимум квадов: 9 слотов + 9 иконок + 1 рамка = 19, с запасом 32
    glBufferData(GL_ARRAY_BUFFER, 32 * 6 * 8 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);

    constexpr int STRIDE = 8 * sizeof(float);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, STRIDE, (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, STRIDE, (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, STRIDE, (void*)(4 * sizeof(float)));
    glEnableVertexAttribArray(2);

    glBindVertexArray(0);
}

void Hotbar::Draw(float screenW, float screenH)
{
    constexpr float SLOT_SIZE = 50.0f;  // размер слота в пикселях
    constexpr float PADDING = 4.0f;     // отступ между слотами
    constexpr float MARGIN = 6.0f;      // отступ иконки внутри слота

    float totalW = SLOTS * SLOT_SIZE + (SLOTS - 1) * PADDING;
    float startX = (screenW - totalW) / 2.0f;
    float startY = screenH - SLOT_SIZE - 16.0f; // 16px от низа экрана

    // Буфер: сначала непрозрачные слоты, потом иконки, потом рамка
    float buf[32 * 6 * 8];
    int   off = 0;

    // Фон слотов
    for (int i = 0; i < SLOTS; i++)
    {
        float x = startX + i * (SLOT_SIZE + PADDING);
        float r = 0.2f, g = 0.2f, b = 0.2f, a = 0.75f;
        BuildQuad(buf, off, x, startY, SLOT_SIZE, SLOT_SIZE,
            0, 0, 0, 0, r, g, b, a);
    }

    int bgQuads = off / (6 * 8);

    // Иконки блоков
    for (int i = 0; i < SLOTS; i++)
    {
        if (slots[i] == AIR) continue;

        int tileID = GetTopTile(slots[i]);
        if (tileID < 0) continue;

        float u0, v0, u1, v1;
        GetTileUV(tileID, u0, v0, u1, v1);

        float x = startX + i * (SLOT_SIZE + PADDING) + MARGIN;
        float y = startY + MARGIN;
        float s = SLOT_SIZE - MARGIN * 2;

        BuildQuad(buf, off, x, y, s, s, u0, v0, u1, v1);
    }

    int iconQuads = off / (6 * 8) - bgQuads;

    // Рамка активного слота
    float ax = startX + activeSlot * (SLOT_SIZE + PADDING);
    float border = 2.0f;
    // Рисуем 4 тонких прямоугольника вокруг слота
    // Верх
    BuildQuad(buf, off, ax - border, startY - border,
        SLOT_SIZE + border * 2, border, 0, 0, 0, 0, 1, 1, 1, 1);
    // Низ
    BuildQuad(buf, off, ax - border, startY + SLOT_SIZE,
        SLOT_SIZE + border * 2, border, 0, 0, 0, 0, 1, 1, 1, 1);
    // Лево
    BuildQuad(buf, off, ax - border, startY,
        border, SLOT_SIZE, 0, 0, 0, 0, 1, 1, 1, 1);
    // Право
    BuildQuad(buf, off, ax + SLOT_SIZE, startY,
        border, SLOT_SIZE, 0, 0, 0, 0, 1, 1, 1, 1);

    // Загружаем всё в GPU
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

    // Рисуем фон (без текстуры)
    glUniform1i(glGetUniformLocation(shaderProgram, "uUseTexture"), 0);
    glDrawArrays(GL_TRIANGLES, 0, bgQuads * 6);

    // Рисуем иконки (с текстурой)
    glUniform1i(glGetUniformLocation(shaderProgram, "uUseTexture"), 1);
    glDrawArrays(GL_TRIANGLES, bgQuads * 6, iconQuads * 6);

    // Рисуем рамку (без текстуры)
    glUniform1i(glGetUniformLocation(shaderProgram, "uUseTexture"), 0);
    glDrawArrays(GL_TRIANGLES, (bgQuads + iconQuads) * 6, 4 * 6);

    glDisable(GL_BLEND);
    glBindVertexArray(0);
}

void Hotbar::ScrollSlot(int delta)
{
    activeSlot = (activeSlot + delta + SLOTS) % SLOTS;
}

void Hotbar::SetSlot(int index)
{
    if (index >= 0 && index < SLOTS)
        activeSlot = index;
}