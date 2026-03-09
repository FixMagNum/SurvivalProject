#include "world.h"
#include <cmath>
#include <algorithm>
#include <thread>
#include <chrono>

// ThreadPool
ThreadPool::ThreadPool(int numThreads)
{
    for (int i = 0; i < numThreads; i++)
    {
        workers.emplace_back([this] {
            while (true)
            {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lock(mutex);
                    cv.wait(lock, [this] { return stopping || !tasks.empty(); });
                    if (stopping && tasks.empty()) return;
                    task = std::move(tasks.front());
                    tasks.pop();
                }
                task();
            }
            });
    }
}

ThreadPool::~ThreadPool()
{
    {
        std::unique_lock<std::mutex> lock(mutex);
        stopping = true;
    }
    cv.notify_all();
    for (auto& w : workers) w.join();
}

void ThreadPool::Enqueue(std::function<void()> task)
{
    {
        std::unique_lock<std::mutex> lock(mutex);
        tasks.push(std::move(task));
    }
    cv.notify_one();
}

// World
World::World()
// Используем N-1 потоков чтобы не перегружать главный
    : threadPool(std::max(1, (int)std::thread::hardware_concurrency() - 1))
{
}

World::~World()
{
    // ThreadPool останавливается в деструкторе — дождёмся всех задач
    // Чанки удалятся автоматически через unique_ptr
}

void World::ScheduleChunk(int cx, int cz)
{
    // Создаём чанк под мьютексом
    Chunk* chunk = nullptr;
    {
        std::lock_guard<std::mutex> lock(chunkMapMutex);

        ChunkKey key{ cx, cz };
        if (chunkMap.count(key)) return; // уже есть

        auto uptr = std::make_unique<Chunk>(cx, cz, this);
        chunk = uptr.get();
        chunkMap[key] = std::move(uptr);
    }

    chunk->state.store(ChunkState::Generating);

    // Отправляем в рабочий поток
    threadPool.Enqueue([this, chunk, cx, cz] {
        // Только генерируем блоки — всё остальное через UploadPendingChunks
        chunk->Generate();
        chunk->state.store(ChunkState::Generated);

        // Линкуем соседей сразу — они тоже начнут видеть нас
        {
            std::lock_guard<std::mutex> lock(chunkMapMutex);
            LinkNeighbors(chunk);

            // Помечаем уже загруженных соседей для перестройки
            // чтобы они убрали лишние грани на границе с новым чанком
            auto markRebuild = [](Chunk* neighbor) {
                if (neighbor && neighbor->state.load() == ChunkState::Uploaded)
                    neighbor->needsRebuild.store(true);
                };

            markRebuild(chunk->neighborPX);
            markRebuild(chunk->neighborNX);
            markRebuild(chunk->neighborPZ);
            markRebuild(chunk->neighborNZ);
        }
        });
}

void World::LinkNeighbors(Chunk* chunk)
{
    int cx = chunk->chunkPos.x;
    int cz = chunk->chunkPos.y;

    auto find = [&](int x, int z) -> Chunk* {
        auto it = chunkMap.find({ x, z });
        return it != chunkMap.end() ? it->second.get() : nullptr;
        };

    chunk->neighborPX = find(cx + 1, cz);
    chunk->neighborNX = find(cx - 1, cz);
    chunk->neighborPZ = find(cx, cz + 1);
    chunk->neighborNZ = find(cx, cz - 1);

    // Сообщаем соседям о новом чанке
    if (chunk->neighborPX) chunk->neighborPX->neighborNX = chunk;
    if (chunk->neighborNX) chunk->neighborNX->neighborPX = chunk;
    if (chunk->neighborPZ) chunk->neighborPZ->neighborNZ = chunk;
    if (chunk->neighborNZ) chunk->neighborNZ->neighborPZ = chunk;
}

void World::Update(int playerChunkX, int playerChunkZ, glm::vec3 cameraFront)
{
    struct PendingChunk { int cx, cz; float priority; };
    std::vector<PendingChunk> pending;

    for (int dx = -LOAD_RADIUS; dx <= LOAD_RADIUS; dx++)
        for (int dz = -LOAD_RADIUS; dz <= LOAD_RADIUS; dz++)
        {
            int dist2 = dx * dx + dz * dz;
            if (dist2 > LOAD_RADIUS * LOAD_RADIUS) continue;

            int cx = playerChunkX + dx;
            int cz = playerChunkZ + dz;

            {
                std::lock_guard<std::mutex> lock(chunkMapMutex);
                if (chunkMap.count({ cx, cz })) continue;
            }

            // Нормализуем вектор к чанку
            float len = sqrt((float)(dx * dx + dz * dz));
            float ndx = (len > 0) ? dx / len : 0.0f;
            float ndz = (len > 0) ? dz / len : 0.0f;

            // dot: 1.0 = прямо перед игроком, -1.0 = за спиной
            float dot = ndx * cameraFront.x + ndz * cameraFront.z;

            // Меньше priority = раньше загрузится
            // Чанки перед игроком получают бонус до -BIAS
            constexpr float BIAS = 8.0f;
            float priority = (float)dist2 - dot * BIAS;

            pending.push_back({ cx, cz, priority });
        }

    std::sort(pending.begin(), pending.end(),
        [](const PendingChunk& a, const PendingChunk& b) {
            return a.priority < b.priority;
        });

    for (auto& p : pending)
    {
        if (threadPool.QueueSize() > 32) break;
        ScheduleChunk(p.cx, p.cz);
    }

    // Выгружаем дальние чанки
    std::vector<ChunkKey> toRemove;
    {
        std::lock_guard<std::mutex> lock(chunkMapMutex);
        for (auto& [key, chunk] : chunkMap)
        {
            int dx = key.x - playerChunkX;
            int dz = key.z - playerChunkZ;
            if (dx * dx + dz * dz > UNLOAD_RADIUS * UNLOAD_RADIUS)
            {
                // Не удаляем чанки которые ещё обрабатываются в потоке
                auto s = chunk->state.load();
                if (s == ChunkState::Uploaded || s == ChunkState::Empty)
                    toRemove.push_back(key);
            }
        }
    }

    // FreeGPU и удаление — только из главного потока (здесь мы в нём)
    for (auto& key : toRemove)
    {
        std::lock_guard<std::mutex> lock(chunkMapMutex);
        auto it = chunkMap.find(key);
        if (it == chunkMap.end()) continue;

        Chunk* chunk = it->second.get();

        // Отвязываем у соседей
        if (chunk->neighborPX) chunk->neighborPX->neighborNX = nullptr;
        if (chunk->neighborNX) chunk->neighborNX->neighborPX = nullptr;
        if (chunk->neighborPZ) chunk->neighborPZ->neighborNZ = nullptr;
        if (chunk->neighborNZ) chunk->neighborNZ->neighborPZ = nullptr;

        chunk->FreeGPU();
        chunkMap.erase(it);
    }
}

// True если все 4 соседа существуют и уже закончили Generate().
// Вызывать под chunkMapMutex.
static bool AllNeighborsGenerated(
    const std::unordered_map<ChunkKey, std::unique_ptr<Chunk>, ChunkKeyHash>& chunkMap,
    int cx, int cz)
{
    const int ddx[] = { 1, -1, 0,  0 };
    const int ddz[] = { 0,  0, 1, -1 };
    for (int i = 0; i < 4; i++)
    {
        auto it = chunkMap.find({ cx + ddx[i], cz + ddz[i] });
        if (it == chunkMap.end()) return false;
        auto s = it->second->state.load();
        if (s == ChunkState::Empty || s == ChunkState::Generating) return false;
    }
    return true;
}

int World::UploadPendingChunks(int maxPerFrame)
{
    int uploaded = 0;
    // Бюджет времени — не тратим больше 4ms на загрузку за кадр
    auto frameStart = std::chrono::steady_clock::now();
    constexpr int BUDGET_MS = 4;

    std::lock_guard<std::mutex> lock(chunkMapMutex);

    for (auto& [key, chunk] : chunkMap)
    {
        auto s = chunk->state.load();

        // Перестройка после break/place — только для уже загруженных чанков
        if (s == ChunkState::Uploaded && chunk->needsRebuild.exchange(false))
        {
            chunk->state.store(ChunkState::MeshBuilding);
            Chunk* ptr = chunk.get();
            threadPool.Enqueue([ptr] {
                ptr->GenerateMeshData();
                ptr->state.store(ChunkState::MeshReady);
                });
            continue;
        }

        // Запуск построения меша для новых чанков.
        // ЖДЁМ пока все 4 соседа сгенерируют блоки — меш строится
        // один раз с правильными данными, без мигания и дыр на границах.
        if (s == ChunkState::Generated &&
            AllNeighborsGenerated(chunkMap, key.x, key.z))
        {
            chunk->state.store(ChunkState::MeshBuilding);
            Chunk* ptr = chunk.get();
            threadPool.Enqueue([ptr] {
                ptr->GenerateMeshData();
                ptr->state.store(ChunkState::MeshReady);
                });
            continue;
        }

        // Загрузка готового меша на GPU
        if (uploaded >= maxPerFrame) continue;

        auto elapsed = std::chrono::steady_clock::now() - frameStart;
        if (std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() >= BUDGET_MS)
            break;

        if (s == ChunkState::MeshReady)
        {
            chunk->UploadToGPU();
            uploaded++;
        }
    }
    return uploaded;
}

// GetBlock / SetBlock / Raycast / RebuildChunkAt
BlockType World::GetBlock(int worldX, int worldY, int worldZ)
{
    if (worldY < 0 || worldY >= Chunk::SIZE_Y) return AIR;

    int chunkX = (int)floor((float)worldX / Chunk::SIZE_X);
    int chunkZ = (int)floor((float)worldZ / Chunk::SIZE_Z);

    std::lock_guard<std::mutex> lock(chunkMapMutex);
    auto it = chunkMap.find({ chunkX, chunkZ });
    if (it == chunkMap.end()) return AIR;

    int localX = worldX - chunkX * Chunk::SIZE_X;
    int localZ = worldZ - chunkZ * Chunk::SIZE_Z;
    return it->second->blocks[localX][worldY][localZ];
}

void World::SetBlock(int worldX, int worldY, int worldZ, BlockType type)
{
    if (worldY < 0 || worldY >= Chunk::SIZE_Y) return;

    int chunkX = (int)floor((float)worldX / Chunk::SIZE_X);
    int chunkZ = (int)floor((float)worldZ / Chunk::SIZE_Z);

    std::lock_guard<std::mutex> lock(chunkMapMutex);
    auto it = chunkMap.find({ chunkX, chunkZ });
    if (it == chunkMap.end()) return;

    int localX = worldX - chunkX * Chunk::SIZE_X;
    int localZ = worldZ - chunkZ * Chunk::SIZE_Z;
    it->second->blocks[localX][worldY][localZ] = type;
}

// DDA (Digital Differential Analyzer) raycast по блокам
// Точный и быстрый — шагает ровно по одному блоку за итерацию
RaycastResult World::Raycast(glm::vec3 origin, glm::vec3 dir, float maxDistance)
{
    RaycastResult result;

    // Нормализуем на всякий случай
    float len = glm::length(dir);
    if (len < 0.0001f) return result;
    dir /= len;

    // Текущий блок
    int x = (int)floor(origin.x);
    int y = (int)floor(origin.y);
    int z = (int)floor(origin.z);

    // Направление шага по каждой оси
    int stepX = (dir.x >= 0) ? 1 : -1;
    int stepY = (dir.y >= 0) ? 1 : -1;
    int stepZ = (dir.z >= 0) ? 1 : -1;

    // tDelta: сколько нужно пройти по лучу чтобы пересечь одну клетку по каждой оси
    float tDeltaX = (dir.x != 0) ? fabs(1.0f / dir.x) : 1e30f;
    float tDeltaY = (dir.y != 0) ? fabs(1.0f / dir.y) : 1e30f;
    float tDeltaZ = (dir.z != 0) ? fabs(1.0f / dir.z) : 1e30f;

    // tMax: расстояние до ближайшей границы по каждой оси
    float tMaxX = (dir.x >= 0) ? ((floor(origin.x) + 1 - origin.x) * tDeltaX) : ((origin.x - floor(origin.x)) * tDeltaX);
    float tMaxY = (dir.y >= 0) ? ((floor(origin.y) + 1 - origin.y) * tDeltaY) : ((origin.y - floor(origin.y)) * tDeltaY);
    float tMaxZ = (dir.z >= 0) ? ((floor(origin.z) + 1 - origin.z) * tDeltaZ) : ((origin.z - floor(origin.z)) * tDeltaZ);

    // Нормаль последней пересечённой грани
    int normX = 0, normY = 0, normZ = 0;

    float t = 0.0f;

    while (t < maxDistance)
    {
        // Проверяем текущий блок
        BlockType block = GetBlock(x, y, z);
        if (block != AIR)
        {
            result.hit = true;
            result.worldX = x;
            result.worldY = y;
            result.worldZ = z;
            result.normalX = normX;
            result.normalY = normY;
            result.normalZ = normZ;
            return result;
        }

        // Шагаем по оси с минимальным tMax
        if (tMaxX < tMaxY && tMaxX < tMaxZ)
        {
            t = tMaxX;
            tMaxX += tDeltaX;
            x += stepX;
            normX = -stepX; normY = 0; normZ = 0;
        }
        else if (tMaxY < tMaxZ)
        {
            t = tMaxY;
            tMaxY += tDeltaY;
            y += stepY;
            normX = 0; normY = -stepY; normZ = 0;
        }
        else
        {
            t = tMaxZ;
            tMaxZ += tDeltaZ;
            z += stepZ;
            normX = 0; normY = 0; normZ = -stepZ;
        }
    }

    return result; // hit = false
}

// Перестраиваем меш чанка где лежит блок.
// Если блок на границе чанка — перестраиваем соседний тоже
// (иначе у соседа останется «дыра» или лишняя грань).
void World::RebuildChunkAt(int worldX, int worldY, int worldZ)
{
    int chunkX = (int)floor((float)worldX / Chunk::SIZE_X);
    int chunkZ = (int)floor((float)worldZ / Chunk::SIZE_Z);

    // Перестраиваем основной чанк
    auto rebuild = [&](int cx, int cz) {
        // BuildMesh уже thread-safe читает данные, но GPU — только main thread
        // Здесь мы всегда в main thread (вызывается из обработки клика)
        std::lock_guard<std::mutex> lock(chunkMapMutex);
        auto it = chunkMap.find({ cx, cz });
        if (it != chunkMap.end() &&
            it->second->state.load() == ChunkState::Uploaded)
        {
            it->second->BuildMesh();
        }
        };

    rebuild(chunkX, chunkZ);

    // Локальная позиция внутри чанка
    int localX = worldX - chunkX * Chunk::SIZE_X;
    int localZ = worldZ - chunkZ * Chunk::SIZE_Z;

    // Если на границе — перестраиваем соседа
    if (localX == 0)                 rebuild(chunkX - 1, chunkZ);
    if (localX == Chunk::SIZE_X - 1) rebuild(chunkX + 1, chunkZ);
    if (localZ == 0)                 rebuild(chunkX, chunkZ - 1);
    if (localZ == Chunk::SIZE_Z - 1) rebuild(chunkX, chunkZ + 1);
}