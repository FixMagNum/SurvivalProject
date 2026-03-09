#pragma once
#include <vector>
#include <unordered_map>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <functional>
#include <atomic>
#include "chunk.h"

struct ChunkKey {
    int x, z;
    bool operator==(const ChunkKey& o) const { return x == o.x && z == o.z; }
};

struct ChunkKeyHash {
    size_t operator()(const ChunkKey& k) const {
        return std::hash<int>()(k.x) ^ (std::hash<int>()(k.z) << 16);
    }
};

// Результат рейкаста
struct RaycastResult {
    bool  hit = false;
    int   worldX = 0, worldY = 0, worldZ = 0;   // координаты блока который hit
    int   normalX = 0, normalY = 0, normalZ = 0; // нормаль грани (куда ставить блок)
};

// Простой thread pool — N рабочих потоков берут задачи из очереди
class ThreadPool {
public:
    ThreadPool(int numThreads);
    ~ThreadPool();

    size_t QueueSize() {
        std::lock_guard<std::mutex> lock(mutex);
        return tasks.size();
    }

    void Enqueue(std::function<void()> task);
    bool IsStopping() const { return stopping; }

private:
    std::vector<std::thread>          workers;
    std::queue<std::function<void()>> tasks;
    std::mutex                        mutex;
    std::condition_variable           cv;
    bool                              stopping = false;
};

class World {
public:
    World();
    ~World();
    
    // Радиус подгрузки в чанках
    static const int LOAD_RADIUS = 12;
    // Чанки дальше UNLOAD_RADIUS удаляются (с запасом чтобы не мигали)
    static const int UNLOAD_RADIUS = 14;

    // Главный метод — вызывать каждый кадр из main
    // playerChunkX/Z — позиция игрока в чанковых координатах
    void Update(int playerChunkX, int playerChunkZ, glm::vec3 cameraFront);

    // Загружает на GPU чанки со статусом MeshReady (вызывать из main thread)
    // Возвращает количество загруженных чанков за этот кадр
    int  UploadPendingChunks(int maxPerFrame = 4);

    BlockType     GetBlock(int worldX, int worldY, int worldZ);
    void          SetBlock(int worldX, int worldY, int worldZ, BlockType type);
    RaycastResult Raycast(glm::vec3 origin, glm::vec3 direction, float maxDistance);
    void          RebuildChunkAt(int worldX, int worldY, int worldZ);

    void SaveChunk(Chunk* chunk);
    void LoadChunkDelta(Chunk* chunk);

    // Для рендера — итерируем по всем загруженным чанкам
    // Мьютекс нужен т.к. рабочие потоки меняют chunkMap
    std::mutex chunkMapMutex;
    std::unordered_map<ChunkKey, std::unique_ptr<Chunk>, ChunkKeyHash> chunkMap;

private:
    ThreadPool threadPool;

    // Запускает генерацию + построение меша для чанка в рабочем потоке
    void ScheduleChunk(int cx, int cz);

    // Заполняет ссылки на соседей для чанка (вызывать под chunkMapMutex)
    void LinkNeighbors(Chunk* chunk);
};