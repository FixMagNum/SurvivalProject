#pragma once
#include <glm/glm.hpp>
#include "camera.h"
#include "world.h"

class Player
{
public:
    glm::vec3 position; // нижняя точка AABB (ноги)
    glm::vec3 velocity;

    static constexpr float WIDTH = 0.6f;    // как в Minecraft
    static constexpr float HEIGHT = 1.8f;
    static constexpr float EYE_HEIGHT = 1.62f;  // высота глаз от ног

    static constexpr float GRAVITY = -28.0f;
    static constexpr float JUMP_SPEED = 9.0f;
    static constexpr float MOVE_SPEED = 5.0f;

    bool isGrounded = false;

    Player(glm::vec3 spawnPos);

    void Update(float deltaTime, World& world, Camera& camera);
    void Jump();

    // Флаги движения — устанавливать каждый кадр из main
    bool moveForward = false;
    bool moveBack = false;
    bool moveLeft = false;
    bool moveRight = false;

private:
    // Двигаем по одной оси и сразу резолвим коллизии
    void MoveAndCollide(glm::vec3 delta, World& world);

    // Возвращает true если блок твёрдый
    bool IsSolid(int x, int y, int z, World& world);
};