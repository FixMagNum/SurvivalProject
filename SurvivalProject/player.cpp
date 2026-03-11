#include "player.h"
#include <cmath>

Player::Player(glm::vec3 spawnPos)
{
    position = spawnPos;
    velocity = glm::vec3(0.0f);
    isGrounded = false;
}

bool Player::IsSolid(int x, int y, int z, World& world)
{
    return world.GetBlock(x, y, z) != AIR;
}

void Player::MoveAndCollide(glm::vec3 delta, World& world)
{
    // AABB игрока: от position до position + (WIDTH, HEIGHT, WIDTH)
    // Двигаем по каждой оси отдельно — так коллизии надёжнее

    float half = WIDTH / 2.0f;

    // X
    position.x += delta.x;
    {
        int minX = (int)floor(position.x - half);
        int maxX = (int)floor(position.x + half);
        int minY = (int)floor(position.y);
        int maxY = (int)floor(position.y + HEIGHT - 0.001f);
        int minZ = (int)floor(position.z - half);
        int maxZ = (int)floor(position.z + half);

        for (int x = minX; x <= maxX; x++)
            for (int y = minY; y <= maxY; y++)
                for (int z = minZ; z <= maxZ; z++)
                {
                    if (!IsSolid(x, y, z, world)) continue;

                    if (delta.x > 0)
                        position.x = x - half;
                    else
                        position.x = x + 1.0f + half;

                    velocity.x = 0.0f;
                    break;
                }
    }

    // Z
    position.z += delta.z;
    {
        int minX = (int)floor(position.x - half);
        int maxX = (int)floor(position.x + half);
        int minY = (int)floor(position.y);
        int maxY = (int)floor(position.y + HEIGHT - 0.001f);
        int minZ = (int)floor(position.z - half);
        int maxZ = (int)floor(position.z + half);

        for (int x = minX; x <= maxX; x++)
            for (int y = minY; y <= maxY; y++)
                for (int z = minZ; z <= maxZ; z++)
                {
                    if (!IsSolid(x, y, z, world)) continue;

                    if (delta.z > 0)
                        position.z = z - half;
                    else
                        position.z = z + 1.0f + half;

                    velocity.z = 0.0f;
                    break;
                }
    }

    // Y
    position.y += delta.y;
    isGrounded = false;
    {
        int minX = (int)floor(position.x - half);
        int maxX = (int)floor(position.x + half);
        int minY = (int)floor(position.y);
        int maxY = (int)floor(position.y + HEIGHT - 0.001f);
        int minZ = (int)floor(position.z - half);
        int maxZ = (int)floor(position.z + half);

        for (int x = minX; x <= maxX; x++)
            for (int y = minY; y <= maxY; y++)
                for (int z = minZ; z <= maxZ; z++)
                {
                    if (!IsSolid(x, y, z, world)) continue;

                    if (delta.y < 0)
                    {
                        position.y = y + 1.0f;
                        isGrounded = true;
                    }
                    else
                    {
                        position.y = y - HEIGHT;
                    }

                    velocity.y = 0.0f;
                    break;
                }
    }
}

void Player::Jump()
{
    if (isGrounded)
        velocity.y = JUMP_SPEED;
}

void Player::Update(float deltaTime, World& world, Camera& camera)
{
    // Гравитация
    velocity.y += GRAVITY * deltaTime;

    // Ограничиваем скорость падения
    if (velocity.y < -50.0f) velocity.y = -50.0f;

    // Горизонтальное движение — от направления камеры
    glm::vec3 forward = glm::normalize(glm::vec3(camera.Front.x, 0.0f, camera.Front.z));
    glm::vec3 right = glm::normalize(glm::vec3(camera.Right.x, 0.0f, camera.Right.z));

    glm::vec3 moveDir(0.0f);
    // Движение передаётся через флаги — читаем в Update
    // (флаги устанавливаются снаружи перед вызовом Update)
    if (moveForward) moveDir += forward;
    if (moveBack)    moveDir -= forward;
    if (moveLeft)    moveDir -= right;
    if (moveRight)   moveDir += right;

    if (glm::length(moveDir) > 0.0f)
        moveDir = glm::normalize(moveDir);

    glm::vec3 delta;
    delta.x = moveDir.x * MOVE_SPEED * deltaTime;
    delta.z = moveDir.z * MOVE_SPEED * deltaTime;
    delta.y = velocity.y * deltaTime;

    MoveAndCollide(delta, world);

    // Камера следует за игроком — глаза на высоте EYE_HEIGHT
    camera.Position = position + glm::vec3(0.0f, EYE_HEIGHT, 0.0f);
}