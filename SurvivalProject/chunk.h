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
#include <array>

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

struct DrawCmd
{
    glm::vec3 center;
    uint32_t firstIndex;
    uint32_t indexCount;
};

struct MeshBucket
{
    std::vector<PackedVertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<DrawCmd> sortCmds;

    unsigned int VAO = 0;
    unsigned int VBO = 0;
    unsigned int EBO = 0;
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
    std::vector<uint32_t>     indices;

    std::array<MeshBucket, (size_t)RenderGroup::Count> meshGroups;

    Chunk(int chunkX, int chunkY, int chunkZ, World* worldPtr);
    
    BlockType blocks[SIZE_X][SIZE_Y][SIZE_Z];

    void Generate();

    // Строит CPU-данные меша (безопасно вызывать из рабочего потока)
    void GenerateMeshData();

    // Загружает данные на GPU (ТОЛЬКО из главного потока!)
    void UploadToGPU(bool isRebuild = false);

    // Для rebuild после break/place — тоже только из главного потока
    void BuildMesh();

    void DrawGroup(RenderGroup group, const glm::vec3& cameraPos);

    void CheckFence();

    // Освобождает GPU ресурсы (вызывать из главного потока)
    void FreeGPU();

    GLsync uploadFence = nullptr;  // fence после последнего UploadToGPU
    bool   gpuReady = false;       // true когда fence сигналил

private:
    // Добавляет прямоугольный quad (w x h блоков) с нужным тайлом
    void AddQuad(
        glm::vec3 origin,
        glm::vec3 axis1, int w,
        glm::vec3 axis2, int h,
        int tileID, bool flipWinding,
        float ao0, float ao1, float ao2, float ao3,
        int faceId,
        RenderGroup group);

    // Считает AO для одной вершины (0..3, где 3 = светло)
    int ComputeAO(int side1, int side2, int corner);

    unsigned int VAO, VBO, EBO;
};