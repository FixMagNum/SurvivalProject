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

        // 1. Генерируем блоки
        chunk->Generate();
        chunk->state.store(ChunkState::Generated);

        if (threadPool.IsStopping()) return;

        // 2. Линкуем соседей (только тех кто уже существует в chunkMap)
        {
            std::lock_guard<std::mutex> lock(chunkMapMutex);
            LinkNeighbors(chunk);
        }

        chunk->state.store(ChunkState::MeshBuilding);

        // 3. Строим меш с теми соседями что есть.
        // Граница с ещё-не-готовыми соседями будет иметь артефакты,
        // но они исправятся когда сосед сам достроится и вызовет ScheduleRebuild.
        chunk->GenerateMeshData();
        chunk->state.store(ChunkState::MeshReady);

        // 4. Просим соседей перестроить свои пограничные меши —
        // теперь они смогут читать наши блоки корректно.
        {
            std::lock_guard<std::mutex> lock(chunkMapMutex);
            const int ddx[] = { 1, -1, 0,  0 };
            const int ddz[] = { 0,  0, 1, -1 };
            for (int i = 0; i < 4; i++)
            {
                auto it = chunkMap.find({ cx + ddx[i], cz + ddz[i] });
                if (it == chunkMap.end()) continue;
                // Только если сосед уже полностью готов (Uploaded) — ставим флаг перестройки
                if (it->second->state.load() == ChunkState::Uploaded)
                    it->second->needsRebuild.store(true);
            }
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

void World::Update(int playerChunkX, int playerChunkZ)
{
    // Подгружаем новые чанки в радиусе LOAD_RADIUS
    for (int dx = -LOAD_RADIUS; dx <= LOAD_RADIUS; dx++)
        for (int dz = -LOAD_RADIUS; dz <= LOAD_RADIUS; dz++)
        {
            if (dx * dx + dz * dz > LOAD_RADIUS * LOAD_RADIUS) continue; // круг

            int cx = playerChunkX + dx;
            int cz = playerChunkZ + dz;

            bool exists = false;
            {
                std::lock_guard<std::mutex> lock(chunkMapMutex);
                exists = chunkMap.count({ cx, cz }) > 0;
            }

            if (!exists)
                ScheduleChunk(cx, cz);
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

int World::UploadPendingChunks(int maxPerFrame)
{
    int uploaded = 0;
    std::lock_guard<std::mutex> lock(chunkMapMutex);

    for (auto& [key, chunk] : chunkMap)
    {
        // Перестройка границ после того как сосед достроился
        if (chunk->needsRebuild.exchange(false) &&
            chunk->state.load() == ChunkState::Uploaded)
        {
            chunk->BuildMesh(); // уже в главном потоке — ОК
        }

        if (uploaded >= maxPerFrame) continue;

        if (chunk->state.load() == ChunkState::MeshReady)
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