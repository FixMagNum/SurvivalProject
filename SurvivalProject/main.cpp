#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "camera.h"
#include "world.h"
#include "frustum.h"
#include <iostream>
#include <string>
#include <stb_image.h>

// Основной шейдер (блоки)
const char* vertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoord;
layout (location = 2) in vec2 aTileOffset;
layout (location = 3) in float aAO;

out vec2 TexCoord;
out vec2 TileOffset;
out float AO;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main()
{
    gl_Position = projection * view * model * vec4(aPos, 1.0);
    TexCoord    = aTexCoord;
    TileOffset  = aTileOffset;
    AO          = aAO;
}
)";

const char* fragmentShaderSource = R"(
#version 330 core
out vec4 FragColor;

in vec2 TexCoord;
in vec2 TileOffset;
in float AO;

uniform sampler2D texture1;

const float TILE_SIZE = 1.0 / 16.0;

void main()
{
    vec2 uv = TileOffset + fract(TexCoord) * TILE_SIZE;
    float light = mix(0.6, 1.0, AO);
    FragColor = texture(texture1, uv) * vec4(vec3(light), 1.0);
}
)";

// Шейдер крестика (2D, NDC координаты)
const char* crosshairVertSrc = R"(
#version 330 core
layout (location = 0) in vec2 aPos;
void main() { gl_Position = vec4(aPos, 0.0, 2.0); }
)";

const char* crosshairFragSrc = R"(
#version 330 core
out vec4 FragColor;
void main() { FragColor = vec4(1.0, 1.0, 1.0, 1.0); }
)";

// Глобальные переменные
Camera camera(glm::vec3(0.0f, 120.0f, 3.0f));

// Флаги кликов мыши (устанавливаются в callback, читаются в game loop)
static bool g_leftClick = false;
static bool g_rightClick = false;

static void mouse_callback(GLFWwindow* window, double xpos, double ypos)
{
    static float lastX = 400, lastY = 300;
    static bool  firstMouse = true;

    if (firstMouse) { lastX = (float)xpos; lastY = (float)ypos; firstMouse = false; }

    float xoffset = (float)xpos - lastX;
    float yoffset = lastY - (float)ypos;
    lastX = (float)xpos;
    lastY = (float)ypos;

    camera.ProcessMouse(xoffset, yoffset);
}

static void mouse_button_callback(GLFWwindow* window, int button, int action, int mods)
{
    if (action == GLFW_PRESS)
    {
        if (button == GLFW_MOUSE_BUTTON_LEFT)  g_leftClick = true;
        if (button == GLFW_MOUSE_BUTTON_RIGHT) g_rightClick = true;
    }
}

// Вспомогательная функция компиляции шейдера
static unsigned int CompileShader(const char* vert, const char* frag)
{
    unsigned int vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &vert, NULL);
    glCompileShader(vs);

    unsigned int fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &frag, NULL);
    glCompileShader(fs);

    unsigned int prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);

    glDeleteShader(vs);
    glDeleteShader(fs);
    return prog;
}

int main()
{
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    float resolutionX = 1920.0f;
    float resolutionY = 1080.0f;

    GLFWwindow* window = glfwCreateWindow((int)resolutionX, (int)resolutionY, "SurvivalProject", NULL, NULL);
    glfwMakeContextCurrent(window);

    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);

    // Шейдеры
    unsigned int shaderProgram = CompileShader(vertexShaderSource, fragmentShaderSource);
    unsigned int crosshairProgram = CompileShader(crosshairVertSrc, crosshairFragSrc);

    // Крестик (2 линии в NDC)
    // Горизонтальная и вертикальная линии, длина 0.02 в NDC
    float crosshairSize = 0.018f;
    float crosshairVerts[] = {
        -crosshairSize, 0.0f,
         crosshairSize, 0.0f,
         0.0f, -crosshairSize * (resolutionX / resolutionY),
         0.0f,  crosshairSize * (resolutionX / resolutionY),
    };

    unsigned int chVAO, chVBO;
    glGenVertexArrays(1, &chVAO);
    glGenBuffers(1, &chVBO);
    glBindVertexArray(chVAO);
    glBindBuffer(GL_ARRAY_BUFFER, chVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(crosshairVerts), crosshairVerts, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);

    // OpenGL состояние
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

    // Текстура
    unsigned int textureID;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 4);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);

    int width, height, nrChannels;
    stbi_set_flip_vertically_on_load(true);
    unsigned char* data = stbi_load("Assets/atlas.png", &width, &height, &nrChannels, 0);
    if (data)
    {
        GLenum fmt = (nrChannels == 4) ? GL_RGBA : (nrChannels == 3 ? GL_RGB : GL_RED);
        glTexImage2D(GL_TEXTURE_2D, 0, fmt, width, height, 0, fmt, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);
    }
    else std::cout << "Failed to load texture\n";
    stbi_image_free(data);

    // Мир
    World world;
    Frustum frustum;

    // Запускаем начальную генерацию через Update
    world.Update(0, 0);

    // Uniform locations
    unsigned int modelLoc = glGetUniformLocation(shaderProgram, "model");
    unsigned int viewLoc = glGetUniformLocation(shaderProgram, "view");
    unsigned int projLoc = glGetUniformLocation(shaderProgram, "projection");

    // Timing / FPS
    double previousTime = 0.0, currentTime = 0.0, timeDifference = 0.0;
    unsigned int counter = 0;
    int lastVisibleChunks = 0;

    float deltaTime = 0.0f, lastFrame = 0.0f;

    // Текущий чанк игрока (для обнаружения смены)
    int lastPlayerCX = INT_MIN, lastPlayerCZ = INT_MIN;

    // Game loop
    while (!glfwWindowShouldClose(window))
    {
        // Timing
        currentTime = glfwGetTime();
        timeDifference = currentTime - previousTime;
        counter++;
        if (timeDifference >= 1.0 / 30.0)
        {
            std::string FPS = std::to_string((1.0 / timeDifference) * counter);
            std::string ms = std::to_string((timeDifference / counter) * 1000.0);
            std::string newTitle = "SurvivalProject - " + FPS + " FPS / " + ms + " ms  chunks: " + std::to_string(lastVisibleChunks);
            glfwSetWindowTitle(window, newTitle.c_str());
            previousTime = currentTime;
            counter = 0;
        }

        float currentFrame = (float)glfwGetTime();
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        // Клавиатура
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) camera.ProcessKeyboard(GLFW_KEY_W, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) camera.ProcessKeyboard(GLFW_KEY_S, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) camera.ProcessKeyboard(GLFW_KEY_A, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) camera.ProcessKeyboard(GLFW_KEY_D, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) camera.ProcessKeyboard(GLFW_KEY_SPACE, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) camera.ProcessKeyboard(GLFW_KEY_LEFT_SHIFT, deltaTime);

        // Динамическая подгрузка
        int playerCX = (int)floor(camera.Position.x / Chunk::SIZE_X);
        int playerCZ = (int)floor(camera.Position.z / Chunk::SIZE_Z);

        // Update вызываем только когда игрок сменил чанк (чтобы не спамить)
        if (playerCX != lastPlayerCX || playerCZ != lastPlayerCZ) {
            world.Update(playerCX, playerCZ);
            lastPlayerCX = playerCX;
            lastPlayerCZ = playerCZ;
        }

        // Загружаем на GPU не более 4 чанков за кадр (без фризов)
        world.UploadPendingChunks(4);

        // Raycast + клики мыши
        // Дальность взаимодействия 6 блоков (как в Minecraft)
        RaycastResult hit = world.Raycast(camera.Position, camera.Front, 6.0f);

        if (g_leftClick)
        {
            g_leftClick = false;
            if (hit.hit)
            {
                // Разрушаем блок — ставим AIR
                world.SetBlock(hit.worldX, hit.worldY, hit.worldZ, AIR);
                world.RebuildChunkAt(hit.worldX, hit.worldY, hit.worldZ);
            }
        }

        if (g_rightClick)
        {
            g_rightClick = false;
            if (hit.hit)
            {
                // Ставим блок на грань (нормаль показывает куда)
                int placeX = hit.worldX + hit.normalX;
                int placeY = hit.worldY + hit.normalY;
                int placeZ = hit.worldZ + hit.normalZ;

                // Не ставим блок внутри игрока (упрощённая проверка)
                glm::vec3 playerMin = camera.Position - glm::vec3(0.3f, 1.7f, 0.3f);
                glm::vec3 playerMax = camera.Position + glm::vec3(0.3f, 0.3f, 0.3f);
                bool insidePlayer =
                    (placeX     < playerMax.x && placeX + 1 > playerMin.x) &&
                    (placeY     < playerMax.y && placeY + 1 > playerMin.y) &&
                    (placeZ     < playerMax.z && placeZ + 1 > playerMin.z);

                if (!insidePlayer && world.GetBlock(placeX, placeY, placeZ) == AIR)
                {
                    world.SetBlock(placeX, placeY, placeZ, DIRT);
                    world.RebuildChunkAt(placeX, placeY, placeZ);
                }
            }
        }

        // Рендер
        glm::mat4 model = glm::mat4(1.0f);
        glm::mat4 view = camera.GetViewMatrix();
        glm::mat4 projection = glm::perspective(glm::radians(75.0f), resolutionX / resolutionY, 0.1f, 1000.0f);
        glm::mat4 viewProj = projection * view;
        frustum.Update(viewProj);

        glClearColor(0.5f, 0.7f, 1.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glUseProgram(shaderProgram);
        glUniform1i(glGetUniformLocation(shaderProgram, "texture1"), 0);
        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));
        glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(projection));

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, textureID);

        int visibleChunks = 0;
        {
            std::lock_guard<std::mutex> lock(world.chunkMapMutex);
            for (auto& [key, chunk] : world.chunkMap)
            {
                if (chunk->state.load() != ChunkState::Uploaded) continue;
                if (frustum.IsBoxVisible(chunk->bounds.min, chunk->bounds.max)) {
                    chunk->Draw();
                    visibleChunks++;
                }
            }
        }
        lastVisibleChunks = visibleChunks;

        // Крестик (2D поверх всего)
        glDisable(GL_DEPTH_TEST);
        glUseProgram(crosshairProgram);
        glBindVertexArray(chVAO);
        glDrawArrays(GL_LINES, 0, 4);
        glBindVertexArray(0);
        glEnable(GL_DEPTH_TEST);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwTerminate();
    return 0;
}