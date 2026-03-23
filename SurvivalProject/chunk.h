#pragma once
#include <glad/glad.h>
#include <vector>
#include <atomic>
#include <glm/glm.hpp>
#include <cstdint>
#include <map>
#include <tuple>
#include "block.h"
#include "aabb.h"

class World;

// Состояние чанка — меняется из разных потоков, поэтому atomic
enum class ChunkState {
    Empty,        // только создан, ещё не генерировался
    Generating,   // идёт Generate() в рабочем потоке
    Generated,    // блоки готовы, меш ещё не построен
    MeshBuilding, // идёт GenerateMeshData() в рабочем потоке
    MeshReady,    // CPU-данные меша готовы, ждут загрузки на GPU
    Uploaded,     // полностью готов к рендеру
};

// Упакованная вершина — 8 байт вместо 44
struct PackedVertex {
    uint32_t data0; // X[31:24] Y[23:16] Z[15:8] faceId[7:5] ao[4:3] corner[2:1]
    uint32_t data1; // tileId[7:0] sizeU[15:8] sizeV[23:16]
};

class Chunk
{
public:
	World* world;  // указатель на мир, к которому принадлежит чанк
    AABB bounds;

	// Размеры чанка
    static const int SIZE_X = 16;
    static const int SIZE_Y = 16;
    static const int SIZE_Z = 16;

    int minY = 0;
    int maxY = SIZE_Y - 1;
        
    Chunk* neighborPX = nullptr;
    Chunk* neighborNX = nullptr;
    Chunk* neighborPZ = nullptr;
    Chunk* neighborNZ = nullptr;
    Chunk* neighborPY = nullptr;
    Chunk* neighborNY = nullptr;

    glm::ivec3 chunkPos; // x, y, z в чанковых координатах

    std::atomic<ChunkState> state{ ChunkState::Empty };
    std::atomic<bool> needsRebuild{ false }; // сосед достроился — перестроить наш меш
    
    std::map<std::tuple<int, int, int>, BlockType> modifiedBlocks;

    std::vector<PackedVertex> vertices;
    std::vector<PackedVertex> verticesT;
    std::vector<uint32_t>     indices;
    std::vector<uint32_t>     indicesT;

    Chunk(int chunkX, int chunkY, int chunkZ, World* worldPtr);
    
    BlockType blocks[SIZE_X][SIZE_Y][SIZE_Z];

    void Generate();

    // Строит CPU-данные меша (безопасно вызывать из рабочего потока)
    void GenerateMeshData();

    // Загружает данные на GPU (ТОЛЬКО из главного потока!)
    void UploadToGPU(bool isRebuild = false);

    // Для rebuild после break/place — тоже только из главного потока
    void BuildMesh();

    void Draw();

    void DrawTransparent();

    void CheckFence();

    // Освобождает GPU ресурсы (вызывать из главного потока)
    void FreeGPU();

    GLsync uploadFence = nullptr;  // fence после последнего UploadToGPU
    bool   gpuReady = false;       // true когда fence сигналил

    // В public секцию chunk.h
    uint16_t visibilityMask = 0xFFFF; // все пути открыты по умолчанию

    void ComputeVisibility();

    // Вспомогательный enum для индексов граней — уже совпадает с faceId в шейдере
    enum Face { PY = 0, NY = 1, PX = 2, NX = 3, PZ = 4, NZ = 5 };

    // Получить бит для пары граней (a, b)
    static int FacePairBit(int a, int b)
    {
        if (a > b) std::swap(a, b);
        // Индекс пары: 0=(0,1), 1=(0,2), 2=(0,3), 3=(0,4), 4=(0,5),
        //              5=(1,2), 6=(1,3), 7=(1,4), 8=(1,5),
        //              9=(2,3), 10=(2,4), 11=(2,5),
        //              12=(3,4), 13=(3,5), 14=(4,5)
        static const int table[6][6] = {
            {-1, 0, 1, 2, 3, 4},
            { 0,-1, 5, 6, 7, 8},
            { 1, 5,-1, 9,10,11},
            { 2, 6, 9,-1,12,13},
            { 3, 7,10,12,-1,14},
            { 4, 8,11,13,14,-1},
        };
        return table[a][b];
    }

    static bool CanPassThrough(uint16_t mask, int enterFace, int exitFace)
    {
        if (enterFace == exitFace) return false;
        int bit = FacePairBit(enterFace, exitFace);
        return (mask >> bit) & 1;
    }

private:
    // Добавляет прямоугольный quad (w x h блоков) с нужным тайлом
    void AddQuad(
        glm::vec3 origin,
        glm::vec3 axis1, int w,
        glm::vec3 axis2, int h,
        int tileID, bool flipWinding,
        float ao0, float ao1, float ao2, float ao3,
        int faceId,
        bool transparent = false);

    // Считает AO для одной вершины (0..3, где 3 = светло)
    int ComputeAO(int side1, int side2, int corner);

    unsigned int VAO, VBO, EBO;

    // Отдельный VAO/VBO/EBO для прозрачных блоков
    unsigned int VAO_T, VBO_T, EBO_T;
};