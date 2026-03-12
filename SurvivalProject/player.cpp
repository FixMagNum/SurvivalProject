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
    position += delta;

    float half = WIDTH / 2.0f;

    int minX = (int)floor(position.x - half);
    int maxX = (int)floor(position.x + half);
    int minY = (int)floor(position.y);
    int maxY = (int)floor(position.y + HEIGHT - 0.001f);
    int minZ = (int)floor(position.z - half);
    int maxZ = (int)floor(position.z + half);

    isGrounded = false;

    for (int x = minX; x <= maxX; x++)
    for (int y = minY; y <= maxY; y++)
    for (int z = minZ; z <= maxZ; z++)
    {
        if (!IsSolid(x, y, z, world)) continue;

        // Перекрытие по каждой оси
        float overlapPX = (x + 1.0f) - (position.x - half);  // блок справа от нас
        float overlapNX = (position.x + half) - x;           // блок слева
        float overlapPY = (y + 1.0f) - position.y;           // блок снизу
        float overlapNY = (position.y + HEIGHT) - y;         // блок сверху
        float overlapPZ = (z + 1.0f) - (position.z - half);  // блок спереди
        float overlapNZ = (position.z + half) - z;           // блок сзади

        // Минимальное перекрытие — по этой оси и выталкиваем
        float minOverlap = overlapPX;
        int   axis = 0; // 0=X, 1=Y, 2=Z
        bool  positive = true;

        if (overlapNX < minOverlap) { minOverlap = overlapNX; axis = 0; positive = false; }
        if (overlapPY < minOverlap) { minOverlap = overlapPY; axis = 1; positive = true;  }
        if (overlapNY < minOverlap) { minOverlap = overlapNY; axis = 1; positive = false; }
        if (overlapPZ < minOverlap) { minOverlap = overlapPZ; axis = 2; positive = true;  }
        if (overlapNZ < minOverlap) { minOverlap = overlapNZ; axis = 2; positive = false; }

        if (axis == 0)
        {
            position.x += positive ? minOverlap : -minOverlap;
            velocity.x = 0.0f;
        }
        else if (axis == 1)
        {
            position.y += positive ? minOverlap : -minOverlap;
            if (!positive && delta.y <= 0.0f) isGrounded = true; // выталкиваем вверх = стоим на полу
            velocity.y = 0.0f;
        }
        else
        {
            position.z += positive ? minOverlap : -minOverlap;
            velocity.z = 0.0f;
        }
    }
    
    // Отдельно проверяем блок под ногами для isGrounded
    isGrounded = false;
    {
        float half = WIDTH / 2.0f;
        int minX = (int)floor(position.x - half);
        int maxX = (int)floor(position.x + half);
        int minZ = (int)floor(position.z - half);
        int maxZ = (int)floor(position.z + half);
        int floorY = (int)floor(position.y) - 1;

        for (int x = minX; x <= maxX; x++)
            for (int z = minZ; z <= maxZ; z++)
            {
                if (IsSolid(x, floorY, z, world))
                {
                    // Стоим вплотную к блоку снизу
                    if (fabs(position.y - (floorY + 1.0f)) < 0.05f)
                    {
                        isGrounded = true;
                        break;
                    }
                }
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