#include "world.h"
#include <cmath>
#include <algorithm>
#include <thread>
#include <chrono>
#include <filesystem>
#include <fstream>

static std::string ChunkSavePath(int cx, int cy, int cz)
{
    return "saves/chunk_" + std::to_string(cx) + "_" + std::to_string(cy) + "_" + std::to_string(cz) + ".bin";
}

void World::SaveChunk(Chunk* chunk)
{
    if (chunk->modifiedBlocks.empty()) return;
    std::filesystem::create_directories("saves");
    std::ofstream f(ChunkSavePath(chunk->chunkPos.x, chunk->chunkPos.y, chunk->chunkPos.z), std::ios::binary);
    if (!f) return;

    for (auto& [key, type] : chunk->modifiedBlocks)
    {
        int x = std::get<0>(key);
        int y = std::get<1>(key);
        int z = std::get<2>(key);
        f.write((char*)&x, sizeof(int));
        f.write((char*)&y, sizeof(int));
        f.write((char*)&z, sizeof(int));
        f.write((char*)&type, sizeof(uint8_t));
    }
}

void World::LoadChunkDelta(Chunk* chunk)
{
    std::ifstream f(ChunkSavePath(chunk->chunkPos.x, chunk->chunkPos.y, chunk->chunkPos.z), std::ios::binary);
    if (!f) return; // файла нет — чанк нетронутый, всё ок

    int x, y, z;
    uint8_t type;
    while (f.read((char*)&x, sizeof(int))
        && f.read((char*)&y, sizeof(int))
        && f.read((char*)&z, sizeof(int))
        && f.read((char*)&type, sizeof(uint8_t)))
    {
        chunk->blocks[x][y][z] = (BlockType)type;
        chunk->modifiedBlocks[{x, y, z}] = (BlockType)type;
    }
}

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
    // Сначала останавливаем пул — ждём завершения всех задач
    // (ThreadPool::~ThreadPool сам это делает)

    // Затем сохраняем все загруженные чанки
    for (auto& [key, chunk] : chunkMap)
        SaveChunk(chunk.get());
}

void World::ScheduleChunk(int cx, int cy, int cz)
{
    Chunk* chunk = nullptr;
    {
        std::lock_guard<std::mutex> lock(chunkMapMutex);

        ChunkKey key{ cx, cy, cz };
        if (chunkMap.count(key)) return;

        auto uptr = std::make_unique<Chunk>(cx, cy, cz, this);
        chunk = uptr.get();
        chunkMap[key] = std::move(uptr);
    }

    // Читаем дельту в главном потоке ДО отправки в threadPool
    // Файловый I/O дешевле чем блокировать рабочий поток
    LoadChunkDelta(chunk);

    chunk->state.store(ChunkState::Generating);

    threadPool.Enqueue([this, chunk, cx, cy, cz] {
        chunk->Generate();

        // Применяем призрачные блоки от уже сгенерированных соседей
        {
            std::lock_guard<std::mutex> lock(ghostBlocksMutex);
            auto it = ghostBlocks.find({ cx, cy, cz });
            if (it != ghostBlocks.end()) {
                for (auto& pb : it->second)
                    if (chunk->blocks[pb.lx][pb.ly][pb.lz] == AIR)
                        chunk->blocks[pb.lx][pb.ly][pb.lz] = pb.type;
                ghostBlocks.erase(it);
            }
        }

        // Накладываем уже загруженную дельту поверх сгенерированных блоков
        for (auto& [key, type] : chunk->modifiedBlocks)
        {
            int x = std::get<0>(key);
            int y = std::get<1>(key);
            int z = std::get<2>(key);
            chunk->blocks[x][y][z] = type;
        }

        chunk->state.store(ChunkState::Generated);

        // распределяем ghost-блоки этого чанка соседям
        {
            std::lock_guard<std::mutex> mapLock(chunkMapMutex);
            LinkNeighbors(chunk);

            auto markRebuild = [](Chunk* neighbor) {
                if (!neighbor) return;
                auto s = neighbor->state.load();
                // Помечаем rebuild для всех состояний, когда меш уже строится или готов:
                // - Uploaded:     обычный путь, перестроим на следующем кадре
                // - MeshBuilding: меш строится сейчас без нас как соседа → после загрузки перестроим
                // - MeshReady:    меш построен но ещё не на GPU → флаг сохранится до Uploaded
                if (s == ChunkState::Uploaded ||
                    s == ChunkState::MeshBuilding ||
                    s == ChunkState::MeshReady)
                    neighbor->needsRebuild.store(true);
                };

            markRebuild(chunk->neighborPX);
            markRebuild(chunk->neighborNX);
            markRebuild(chunk->neighborPY);
            markRebuild(chunk->neighborNY);
            markRebuild(chunk->neighborPZ);
            markRebuild(chunk->neighborNZ);

            if (!chunk->generatedGhostBlocks.empty()) {
                std::lock_guard<std::mutex> ghostLock(ghostBlocksMutex);

                for (auto& ghost : chunk->generatedGhostBlocks) {
                    int gCX = (int)std::floor((float)ghost.wx / Chunk::SIZE_X);
                    int gCY = (int)std::floor((float)ghost.wy / Chunk::SIZE_Y);
                    int gCZ = (int)std::floor((float)ghost.wz / Chunk::SIZE_Z);
                    int lx = ghost.wx - gCX * Chunk::SIZE_X;
                    int ly = ghost.wy - gCY * Chunk::SIZE_Y;
                    int lz = ghost.wz - gCZ * Chunk::SIZE_Z;

                    auto nit = chunkMap.find({ gCX, gCY, gCZ });
                    if (nit != chunkMap.end()) {
                        auto s = nit->second->state.load();
                        if (s != ChunkState::Empty && s != ChunkState::Generating) {
                            // Чанк уже сгенерирован - пишем напрямую
                            if (nit->second->blocks[lx][ly][lz] == AIR)
                                nit->second->blocks[lx][ly][lz] = ghost.type;
                            if (s == ChunkState::Uploaded ||
                                s == ChunkState::MeshBuilding ||
                                s == ChunkState::MeshReady)
                                nit->second->needsRebuild.store(true);
                            continue;
                        }
                    }

                    // Сосед ещё не готов - сохраняем, он подберёт при своём Generate()
                    ghostBlocks[{ gCX, gCY, gCZ }].push_back({ lx, ly, lz, ghost.type });
                }
            }
        }

        chunk->generatedGhostBlocks.clear();
        });
}

void World::LinkNeighbors(Chunk* chunk)
{
    int cx = chunk->chunkPos.x;
    int cy = chunk->chunkPos.y;
    int cz = chunk->chunkPos.z;

    auto find = [&](int x, int y, int z) -> Chunk* {
        auto it = chunkMap.find({ x, y, z });
        return it != chunkMap.end() ? it->second.get() : nullptr;
        };

    chunk->neighborPX = find(cx + 1, cy, cz);
    chunk->neighborNX = find(cx - 1, cy, cz);
    chunk->neighborPY = find(cx, cy + 1, cz);
    chunk->neighborNY = find(cx, cy - 1, cz);
    chunk->neighborPZ = find(cx, cy, cz + 1);
    chunk->neighborNZ = find(cx, cy, cz - 1);

    if (chunk->neighborPX) chunk->neighborPX->neighborNX = chunk;
    if (chunk->neighborNX) chunk->neighborNX->neighborPX = chunk;
    if (chunk->neighborPY) chunk->neighborPY->neighborNY = chunk;
    if (chunk->neighborNY) chunk->neighborNY->neighborPY = chunk;
    if (chunk->neighborPZ) chunk->neighborPZ->neighborNZ = chunk;
    if (chunk->neighborNZ) chunk->neighborNZ->neighborPZ = chunk;
}

void World::Update(int playerChunkX, int playerChunkY, int playerChunkZ, glm::vec3 cameraFront)
{
    struct PendingChunk { int cx, cy, cz; float priority; };
    std::vector<PendingChunk> pending;

    for (int dx = -LOAD_RADIUS; dx <= LOAD_RADIUS; dx++)
        for (int dz = -LOAD_RADIUS; dz <= LOAD_RADIUS; dz++)
        {
            int dist2 = dx * dx + dz * dz;
            if (dist2 > LOAD_RADIUS * LOAD_RADIUS) continue;

            for (int dy = -LOAD_RADIUS_Y; dy <= LOAD_RADIUS_Y; dy++)
            {
                int cx = playerChunkX + dx;
                int cy = playerChunkY + dy;
                int cz = playerChunkZ + dz;

                // Не грузим чанки ниже 0
                if (cy < 0) continue;

                {
                    std::lock_guard<std::mutex> lock(chunkMapMutex);
                    if (chunkMap.count({ cx, cy, cz })) continue;
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
                float priority = (float)(dist2 + dy * dy) - dot * BIAS;

                pending.push_back({ cx, cy, cz, priority });
            }
        }

    std::sort(pending.begin(), pending.end(),
        [](const PendingChunk& a, const PendingChunk& b) {
            return a.priority < b.priority;
        });

    for (auto& p : pending)
    {
        if (threadPool.QueueSize() > 64) break;
        ScheduleChunk(p.cx, p.cy, p.cz);
    }
}

void World::UnloadDistantChunks(int playerChunkX, int playerChunkY, int playerChunkZ)
{
    std::vector<ChunkKey> toRemove;
    {
        std::lock_guard<std::mutex> lock(chunkMapMutex);
        for (auto& [key, chunk] : chunkMap)
        {
            int dx = key.x - playerChunkX;
            int dy = key.y - playerChunkY;
            int dz = key.z - playerChunkZ;
            if (dx * dx + dz * dz > UNLOAD_RADIUS * UNLOAD_RADIUS ||
                abs(dy) > LOAD_RADIUS_Y + 2)
            {
                auto s = chunk->state.load();
                // Не трогаем только то что прямо сейчас обрабатывается в потоке
                if (s != ChunkState::Generating && s != ChunkState::MeshBuilding)
                    toRemove.push_back(key);
            }
        }
    }

    for (auto& key : toRemove)
    {
        std::lock_guard<std::mutex> lock(chunkMapMutex);
        auto it = chunkMap.find(key);
        if (it == chunkMap.end()) continue;

        Chunk* chunk = it->second.get();
        // Двойная проверка — вдруг поток успел сменить статус
        auto s = chunk->state.load();
        if (s == ChunkState::Generating || s == ChunkState::MeshBuilding) continue;

        SaveChunk(chunk);

        if (chunk->neighborPX) chunk->neighborPX->neighborNX = nullptr;
        if (chunk->neighborNX) chunk->neighborNX->neighborPX = nullptr;
        if (chunk->neighborPY) chunk->neighborPY->neighborNY = nullptr;
        if (chunk->neighborNY) chunk->neighborNY->neighborPY = nullptr;
        if (chunk->neighborPZ) chunk->neighborPZ->neighborNZ = nullptr;
        if (chunk->neighborNZ) chunk->neighborNZ->neighborPZ = nullptr;

        chunk->FreeGPU();
        chunkMap.erase(it);
    }
}

// True если все существующие соседи (все 6 направлений) уже закончили Generate()
// Вызывать под chunkMapMutex
static bool AllNeighborsGenerated(
    const std::unordered_map<ChunkKey, std::unique_ptr<Chunk>, ChunkKeyHash>& chunkMap,
    int cx, int cy, int cz)
{
    // Все 6 направлений: +X, -X, +Y, -Y, +Z, -Z
    const int ddx[] = { 1, -1, 0,  0, 0,  0 };
    const int ddy[] = { 0,  0, 1, -1, 0,  0 };
    const int ddz[] = { 0,  0, 0,  0, 1, -1 };
    for (int i = 0; i < 6; i++)
    {
        auto it = chunkMap.find({ cx + ddx[i], cy + ddy[i], cz + ddz[i] });
        if (it == chunkMap.end()) continue; // соседа нет совсем — OK (край мира)
        auto s = it->second->state.load();
        if (s == ChunkState::Empty || s == ChunkState::Generating) return false;
    }
    return true;
}

int World::UploadPendingChunks(int maxPerFrame)
{
    int uploaded = 0;
    auto frameStart = std::chrono::steady_clock::now();
    constexpr int BUDGET_MS = 4;

    // Собираем списки под мьютексом — быстро, без GL вызовов
    std::vector<Chunk*> toUpload;
    std::vector<Chunk*> toRebuild;

    {
        std::lock_guard<std::mutex> lock(chunkMapMutex);

        for (auto& [key, chunk] : chunkMap)
        {
            auto s = chunk->state.load();

            if (s == ChunkState::Uploaded && chunk->needsRebuild.exchange(false))
            {
                toRebuild.push_back(chunk.get());
                continue;
            }

            if (s == ChunkState::Generated &&
                AllNeighborsGenerated(chunkMap, key.x, key.y, key.z))
            {
                toRebuild.push_back(chunk.get());
                continue;
            }

            if (s == ChunkState::MeshReady)
                toUpload.push_back(chunk.get());
        }
    }
    // Мьютекс отпущен — рабочие потоки больше не блокируются

    // Запускаем перестройку мешей
    for (Chunk* ptr : toRebuild)
    {
        ptr->state.store(ChunkState::MeshBuilding);
        threadPool.Enqueue([ptr] {
            ptr->GenerateMeshData();
            ptr->state.store(ChunkState::MeshReady);
            });
    }

    // Загружаем на GPU с бюджетом времени
    for (Chunk* chunk : toUpload)
    {
        if (uploaded >= maxPerFrame) break;

        auto elapsed = std::chrono::steady_clock::now() - frameStart;
        if (std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() >= BUDGET_MS)
            break;

        chunk->UploadToGPU();
        uploaded++;
    }

    return uploaded;
}

BlockType World::GetBlock(int worldX, int worldY, int worldZ)
{
    if (worldY < 0) return AIR;
    if (worldY >= 512) return AIR;

    int chunkX = (int)floor((float)worldX / Chunk::SIZE_X);
    int chunkY = (int)floor((float)worldY / Chunk::SIZE_Y);
    int chunkZ = (int)floor((float)worldZ / Chunk::SIZE_Z);

    std::lock_guard<std::mutex> lock(chunkMapMutex);
    auto it = chunkMap.find({ chunkX, chunkY, chunkZ });
    if (it == chunkMap.end()) return AIR;

    // Пока чанк генерируется, blocks[] заполнен нулями (AIR из memset)
    auto s = it->second->state.load();
    if (s == ChunkState::Empty || s == ChunkState::Generating) return AIR;

    int localX = worldX - chunkX * Chunk::SIZE_X;
    int localY = worldY - chunkY * Chunk::SIZE_Y;
    int localZ = worldZ - chunkZ * Chunk::SIZE_Z;
    return it->second->blocks[localX][localY][localZ];
}

void World::SetBlock(int worldX, int worldY, int worldZ, BlockType type)
{
    if (worldY < 0 || worldY >= 512) return;

    int chunkX = (int)floor((float)worldX / Chunk::SIZE_X);
    int chunkY = (int)floor((float)worldY / Chunk::SIZE_Y);
    int chunkZ = (int)floor((float)worldZ / Chunk::SIZE_Z);

    std::lock_guard<std::mutex> lock(chunkMapMutex);
    auto it = chunkMap.find({ chunkX, chunkY, chunkZ });
    if (it == chunkMap.end()) return;

    int localX = worldX - chunkX * Chunk::SIZE_X;
    int localY = worldY - chunkY * Chunk::SIZE_Y;
    int localZ = worldZ - chunkZ * Chunk::SIZE_Z;
    it->second->blocks[localX][localY][localZ] = type;
    it->second->modifiedBlocks[{localX, localY, localZ}] = type;
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
    int chunkY = (int)floor((float)worldY / Chunk::SIZE_Y);
    int chunkZ = (int)floor((float)worldZ / Chunk::SIZE_Z);

    // Перестраиваем основной чанк
    auto rebuild = [&](int cx, int cy, int cz) {
        // BuildMesh уже thread-safe читает данные, но GPU — только main thread
        // Здесь мы всегда в main thread (вызывается из обработки клика)
        std::lock_guard<std::mutex> lock(chunkMapMutex);
        auto it = chunkMap.find({ cx, cy, cz });
        if (it != chunkMap.end() &&
            it->second->state.load() == ChunkState::Uploaded)
            it->second->BuildMesh();
        };

    // Локальная позиция внутри чанка
    int localX = worldX - chunkX * Chunk::SIZE_X;
    int localY = worldY - chunkY * Chunk::SIZE_Y;
    int localZ = worldZ - chunkZ * Chunk::SIZE_Z;

    // Перестраиваем все затронутые чанки - включая диагональные
    for (int dx = -1; dx <= 1; dx++)
        for (int dy = -1; dy <= 1; dy++)
            for (int dz = -1; dz <= 1; dz++)
            {
                bool okX = (dx == 0)
                    || (dx == -1 && localX == 0)
                    || (dx == 1 && localX == Chunk::SIZE_X - 1);
                bool okY = (dy == 0)
                    || (dy == -1 && localY == 0)
                    || (dy == 1 && localY == Chunk::SIZE_Y - 1);
                bool okZ = (dz == 0)
                    || (dz == -1 && localZ == 0)
                    || (dz == 1 && localZ == Chunk::SIZE_Z - 1);

                if (okX && okY && okZ)
                    rebuild(chunkX + dx, chunkY + dy, chunkZ + dz);
            }
}