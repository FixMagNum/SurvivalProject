#pragma once
#include <glm/glm.hpp>
#include "camera.h"
#include "world.h"
#include <cfloat>

class Player
{
public:
    glm::vec3 position; // нижняя точка AABB (ноги)
    glm::vec3 velocity;

    static constexpr float WIDTH = 0.6f;        // как в Minecraft
    static constexpr float HEIGHT = 1.8f;
    static constexpr float EYE_HEIGHT = 1.62f;  // высота глаз от ног

    static constexpr float GRAVITY = -28.0f;
    static constexpr float JUMP_SPEED = 9.0f;
    static constexpr float MOVE_SPEED = 5.0f;

    static constexpr float SPRINT_SPEED = 10.0f;     // скорость бега
    static constexpr float CROUCH_SPEED = 2.0f;      // скорость приседания
    static constexpr float CROUCH_HEIGHT = 1.4f;     // высота при приседании
    static constexpr float CROUCH_EYE_HEIGHT = 1.0f; // глаза при приседании

    static constexpr float COYOTE_TIME = 0.12f;
    static constexpr float JUMP_BUFFER_TIME = 0.12f;

    static constexpr float ACCEL_GROUND = 35.0f;
    static constexpr float ACCEL_AIR = 12.0f;
    static constexpr float FRICTION_GROUND = 22.0f;
    static constexpr float FRICTION_AIR = 2.0f;

    static constexpr float WATER_SPEED_MULT = 0.45f;
    static constexpr float WATER_GRAVITY_MULT = 0.20f;

    static constexpr float WATER_DRAG = 8.0f;

    static constexpr float WATER_SWIM_UP_ACCEL = 16.0f;
    static constexpr float WATER_SWIM_DOWN_ACCEL = 12.0f;
    static constexpr float WATER_MAX_UP_SPEED = 4.5f;
    static constexpr float WATER_MAX_DOWN_SPEED = -4.0f;

    bool isGrounded  = false;
    bool isSprinting = false;
    bool isCrouching = false;

    Player(glm::vec3 spawnPos);

    void Update(float deltaTime, World& world, Camera& camera);
    void RequestJump();
    void RequestSwimUp();
    // Возвращает true если блок твёрдый
    bool IsSolid(int x, int y, int z, World& world);

    // Флаги движения — устанавливать каждый кадр из main
    bool moveForward = false;
    bool moveBack = false;
    bool moveLeft = false;
    bool moveRight = false;
    bool inWater = false;
    bool swimUpRequested = false;

    struct WaterInfo
    {
        bool touching = false;
        float surfaceY = -FLT_MAX;   // верхняя граница самой высокой водяной клетки, пересекающей игрока
        float submersion = 0.0f;     // 0..1 насколько тело в воде
    };

    WaterInfo SampleWater(World& world) const;

    static constexpr float MAX_HEALTH = 20.0f;
    static constexpr float FALL_DAMAGE_THRESHOLD = 4.0f; // с какой высоты начинается урон

    float health = MAX_HEALTH;
    bool  isDead = false;

    // Для расчёта урона от падения
    float fallStartY = -1.0f;  // Y откуда начали падать (< 0 = не падаем)

    // Буферы управления
    float coyoteTimer = 0.0f;
    float jumpBufferTimer = 0.0f;

    float prevHealth = MAX_HEALTH; // здоровье на прошлом кадре

private:
    // Двигаем по одной оси и сразу резолвим коллизии
    void MoveAndCollide(glm::vec3 delta, World& world);
};