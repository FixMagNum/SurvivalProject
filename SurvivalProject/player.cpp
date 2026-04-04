#include "player.h"
#include <cmath>
#include <algorithm>
#include "audio.h"

Player::Player(glm::vec3 spawnPos)
{
    position = spawnPos;
    velocity = glm::vec3(0.0f);
    isGrounded = false;
    isSprinting = false;
    isCrouching = false;
    swimUpRequested = false;
}

bool Player::IsSolid(int x, int y, int z, World& world)
{
    BlockType b = world.GetBlock(x, y, z);
    return b != AIR && b != WATER;
}

Player::WaterInfo Player::SampleWater(World& world) const
{
    WaterInfo info;

    float half = WIDTH * 0.5f;
    float height = isCrouching ? CROUCH_HEIGHT : HEIGHT;

    int minX = (int)floor(position.x - half + 0.01f);
    int maxX = (int)floor(position.x + half - 0.01f);
    int minY = (int)floor(position.y + 0.01f);
    int maxY = (int)floor(position.y + height - 0.01f);
    int minZ = (int)floor(position.z - half + 0.01f);
    int maxZ = (int)floor(position.z + half - 0.01f);

    for (int x = minX; x <= maxX; x++)
        for (int y = minY; y <= maxY; y++)
            for (int z = minZ; z <= maxZ; z++)
            {
                if (world.GetBlock(x, y, z) == WATER)
                {
                    info.touching = true;
                    info.surfaceY = std::max(info.surfaceY, (float)y + 1.0f);
                }
            }

    if (info.touching)
    {
        info.submersion = std::clamp((info.surfaceY - position.y) / height, 0.0f, 1.0f);
    }

    return info;
}

void Player::RequestJump()
{
    jumpBufferTimer = JUMP_BUFFER_TIME;
}

void Player::RequestSwimUp()
{
    swimUpRequested = true;
}

void Player::MoveAndCollide(glm::vec3 delta, World& world)
{
    float half = WIDTH / 2.0f;

    float currentHeight = isCrouching ? CROUCH_HEIGHT : HEIGHT;

    auto resolveAxis = [&](int axis, float d)
        {
            if (d == 0.0f) return;

            // Двигаем только по одной оси
            if (axis == 0) position.x += d;
            if (axis == 1) position.y += d;
            if (axis == 2) position.z += d;

            int minX = (int)floor(position.x - half);
            int maxX = (int)floor(position.x + half - 0.001f);
            int minY = (int)floor(position.y);
            int maxY = (int)floor(position.y + currentHeight - 0.001f);
            int minZ = (int)floor(position.z - half);
            int maxZ = (int)floor(position.z + half - 0.001f);

            for (int x = minX; x <= maxX; x++)
                for (int y = minY; y <= maxY; y++)
                    for (int z = minZ; z <= maxZ; z++)
                    {
                        if (!IsSolid(x, y, z, world)) continue;

                        // Выталкиваем строго по текущей оси
                        if (axis == 0)
                        {
                            if (d > 0) position.x = x - half;
                            else       position.x = x + 1.0f + half;
                            velocity.x = 0.0f;
                        }
                        else if (axis == 1)
                        {
                            if (d > 0) position.y = y - currentHeight;
                            else
                            {
                                position.y = y + 1.0f;
                                isGrounded = true;
                            }
                            velocity.y = 0.0f;
                        }
                        else
                        {
                            if (d > 0) position.z = z - half;
                            else       position.z = z + 1.0f + half;
                            velocity.z = 0.0f;
                        }

                        // Пересчитываем AABB после выталкивания
                        minX = (int)floor(position.x - half);
                        maxX = (int)floor(position.x + half - 0.001f);
                        minY = (int)floor(position.y);
                        maxY = (int)floor(position.y + currentHeight - 0.001f);
                        minZ = (int)floor(position.z - half);
                        maxZ = (int)floor(position.z + half - 0.001f);
                    }
        };

    isGrounded = false;

    // Порядок важен: сначала Y (гравитация), потом X и Z
    resolveAxis(1, delta.y);
    resolveAxis(0, delta.x);
    resolveAxis(2, delta.z);
}

void Player::Update(float deltaTime, World& world, Camera& camera)
{
    float half = WIDTH / 2.0f;

    WaterInfo water = SampleWater(world);
    inWater = water.touching;

    float eyeHeight = isCrouching ? CROUCH_EYE_HEIGHT : EYE_HEIGHT;

    glm::vec3 forward(camera.Front.x, 0.0f, camera.Front.z);
    glm::vec3 right(camera.Right.x, 0.0f, camera.Right.z);

    if (glm::length(forward) > 0.0001f)
        forward = glm::normalize(forward);
    else
        forward = glm::vec3(0.0f);

    if (glm::length(right) > 0.0001f)
        right = glm::normalize(right);
    else
        right = glm::vec3(0.0f);

    glm::vec3 wishDir(0.0f);
    if (moveForward) wishDir += forward;
    if (moveBack)    wishDir -= forward;
    if (moveLeft)    wishDir -= right;
    if (moveRight)   wishDir += right;

    if (glm::length(wishDir) > 0.0f)
        wishDir = glm::normalize(wishDir);

    // Скорость зависит от состояния
    float speed = MOVE_SPEED;
    if (isSprinting && !isCrouching) speed = SPRINT_SPEED;
    if (isCrouching)                 speed = CROUCH_SPEED;

    if (isGrounded)
        coyoteTimer = COYOTE_TIME;
    else
        coyoteTimer = std::max(0.0f, coyoteTimer - deltaTime);

    jumpBufferTimer = std::max(0.0f, jumpBufferTimer - deltaTime);

    if (!inWater && jumpBufferTimer > 0.0f && (isGrounded || coyoteTimer > 0.0f))
    {
        velocity.y = JUMP_SPEED;
        isGrounded = false;
        coyoteTimer = 0.0f;
        jumpBufferTimer = 0.0f;
        maxFallSpeed = 0.0f;
    }

    if (inWater)
    {
        // Горизонтальное движение в воде — более вязкое
        glm::vec2 currentXZ(velocity.x, velocity.z);
        glm::vec2 targetXZ(wishDir.x * speed * WATER_SPEED_MULT, wishDir.z * speed * WATER_SPEED_MULT);

        float accel = ACCEL_GROUND * 0.45f;
        float friction = WATER_DRAG;

        if (glm::length(wishDir) > 0.0f)
        {
            glm::vec2 deltaV = targetXZ - currentXZ;
            float maxChange = accel * deltaTime;

            float len = glm::length(deltaV);
            if (len > maxChange && len > 0.0f)
                deltaV = deltaV / len * maxChange;

            currentXZ += deltaV;
        }
        else
        {
            float speed2D = glm::length(currentXZ);
            float drop = friction * deltaTime;
            if (speed2D <= drop)
                currentXZ = glm::vec2(0.0f);
            else
                currentXZ -= currentXZ / speed2D * drop;
        }

        velocity.x = currentXZ.x;
        velocity.z = currentXZ.y;

        if (swimUpRequested && water.submersion > 0.2f)
            velocity.y = WATER_MAX_UP_SPEED;

        swimUpRequested = false;

        if (isCrouching)
            velocity.y -= WATER_SWIM_DOWN_ACCEL * deltaTime;

        // Слабая гравитация в воде
        velocity.y += GRAVITY * WATER_GRAVITY_MULT * deltaTime;

        // Немного гасим дрожание
        velocity.y *= 0.985f;

        if (velocity.y > WATER_MAX_UP_SPEED)   velocity.y = WATER_MAX_UP_SPEED;
        if (velocity.y < WATER_MAX_DOWN_SPEED) velocity.y = WATER_MAX_DOWN_SPEED;
    }
    else
    {
        velocity.y += GRAVITY * deltaTime;
        if (velocity.y < -50.0f)
            velocity.y = -50.0f;

        glm::vec2 currentXZ(velocity.x, velocity.z);
        glm::vec2 targetXZ(wishDir.x * speed, wishDir.z * speed);

        float accel = isGrounded ? ACCEL_GROUND : ACCEL_AIR;
        float friction = isGrounded ? FRICTION_GROUND : FRICTION_AIR;

        if (glm::length(wishDir) > 0.0f)
        {
            glm::vec2 deltaV = targetXZ - currentXZ;
            float maxChange = accel * deltaTime;

            float len = glm::length(deltaV);
            if (len > maxChange && len > 0.0f)
                deltaV = deltaV / len * maxChange;

            currentXZ += deltaV;
        }
        else
        {
            float speed2D = glm::length(currentXZ);
            float drop = friction * deltaTime;
            if (speed2D <= drop)
                currentXZ = glm::vec2(0.0f);
            else
                currentXZ -= currentXZ / speed2D * drop;
        }

        velocity.x = currentXZ.x;
        velocity.z = currentXZ.y;
    }

    if (velocity.y < 0.0f)
        maxFallSpeed = std::max(maxFallSpeed, -velocity.y);

    glm::vec3 delta;
    delta.x = velocity.x * deltaTime;
    delta.z = velocity.z * deltaTime;
    delta.y = velocity.y * deltaTime;

    // Sneak — не падать с края при приседании
    if (isCrouching && isGrounded)
    {
        // Проверяем X
        glm::vec3 testPos = position;
        testPos.x += delta.x;
        int bx = (int)floor(testPos.x - half);
        int bx2 = (int)floor(testPos.x + half - 0.001f);
        int by = (int)floor(position.y) - 1;
        int bz1 = (int)floor(position.z - half);
        int bz2 = (int)floor(position.z + half - 0.001f);

        bool solidUnderX = false;
        for (int x = bx; x <= bx2 && !solidUnderX; x++)
            for (int z = bz1; z <= bz2 && !solidUnderX; z++)
                if (IsSolid(x, by, z, world)) solidUnderX = true;

        if (!solidUnderX)
        {
            delta.x = 0.0f;
            velocity.x = 0.0f;
        }

        // Проверяем Z
        testPos = position;
        testPos.z += delta.z;
        int bz = (int)floor(testPos.z - half);
        int bz_2 = (int)floor(testPos.z + half - 0.001f);
        int by2 = (int)floor(position.y) - 1;
        int bx1 = (int)floor(position.x - half);
        int bx_2 = (int)floor(position.x + half - 0.001f);

        bool solidUnderZ = false;
        for (int x = bx1; x <= bx_2 && !solidUnderZ; x++)
            for (int z = bz; z <= bz_2 && !solidUnderZ; z++)
                if (IsSolid(x, by2, z, world)) solidUnderZ = true;

        if (!solidUnderZ)
        {
            delta.z = 0.0f;
            velocity.z = 0.0f;
        }
    }

    bool wasGrounded = isGrounded;
    MoveAndCollide(delta, world);

    if (!wasGrounded && isGrounded && !inWater && maxFallSpeed > 0.0f)
    {
        // Урон начинается с падения больше ~4 блоков
        float fallDamage = maxFallSpeed - FALL_DAMAGE_THRESHOLD * 4.0f;
        if (fallDamage > 0.0f)
            health -= fallDamage * 0.5f;
        maxFallSpeed = 0.0f;
    }

    // Смерть
    if (isGrounded)
        maxFallSpeed = 0.0f;

    if (health <= 0.0f)
    {
        health = 0.0f;
        isDead = true;
    }

    // Звук получения урона (сравниваем с прошлым кадром)
    if (health < prevHealth)
    {
        Audio::PlayPlayerHurt();
    }
    prevHealth = health;

    // Камера следует за игроком — глаза на высоте EYE_HEIGHT
    camera.Position = position + glm::vec3(0.0f, eyeHeight, 0.0f);
}