#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "camera.h"
#include "player.h"
#include "world.h"
#include "frustum.h"
#include <iostream>
#include <string>
#include <stb_image.h>
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "hotbar.h"

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
    TexCoord = aTexCoord;
    TileOffset = aTileOffset;
    AO = aAO;
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

const char* crosshairVertSrc = R"(
#version 330 core
layout (location = 0) in vec2 aPos;
uniform vec2 uScreenSize; // ширина и высота в пикселях
void main()
{
    // aPos задан в пикселях от центра, переводим в NDC
    vec2 pos = aPos / uScreenSize * 2.0;
    gl_Position = vec4(pos, 0.0, 1.0);
}
)";

const char* crosshairFragSrc = R"(
#version 330 core
out vec4 FragColor;
void main() { FragColor = vec4(1.0, 1.0, 1.0, 1.0); }
)";

// Глобальные переменные
Camera camera(glm::vec3(0.0f, 120.0f, 3.0f));
Player player(glm::vec3(0.0f, 120.0f, 0.0f));

static double g_scrollDelta = 0.0;

static float g_width = 800.0f;
static float g_height = 600.0f;

static bool g_fullscreen = false;
static int  g_windowedX = 100, g_windowedY = 100;
static int  g_windowedW = 800, g_windowedH = 600;

// Флаги кликов мыши (устанавливаются в callback, читаются в game loop)
static bool g_leftClick = false;
static bool g_rightClick = false;

static void scroll_callback(GLFWwindow* window, double xoffset, double yoffset)
{
    g_scrollDelta += yoffset;
}

static void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
    if (width == 0 || height == 0) return; // минимизация окна
    g_width = (float)width;
    g_height = (float)height;
    glViewport(0, 0, width, height);
}

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

static void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    if (key == GLFW_KEY_F11 && action == GLFW_PRESS)
    {
        g_fullscreen = !g_fullscreen;

        if (g_fullscreen)
        {
            // Запоминаем текущее положение и размер окна
            glfwGetWindowPos(window, &g_windowedX, &g_windowedY);
            glfwGetWindowSize(window, &g_windowedW, &g_windowedH);

            // Переходим в fullscreen на текущем мониторе
            GLFWmonitor* monitor = glfwGetPrimaryMonitor();
            const GLFWvidmode* mode = glfwGetVideoMode(monitor);
            glfwSetWindowMonitor(window, monitor, 0, 0,
                mode->width, mode->height, mode->refreshRate);
        }
        else
        {
            // Возвращаемся в оконный режим
            glfwSetWindowMonitor(window, nullptr,
                g_windowedX, g_windowedY,
                g_windowedW, g_windowedH, 0);
        }
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

    GLFWwindow* window = glfwCreateWindow((int)g_width, (int)g_height, "SurvivalProject", NULL, NULL);
    glfwMakeContextCurrent(window);

    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetKeyCallback(window, key_callback);
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetScrollCallback(window, scroll_callback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    // Получаем реальный размер фреймбуфера сразу после создания окна
    // (на Retina/HiDPI он может отличаться от размера окна)
    int fbW, fbH;
    glfwGetFramebufferSize(window, &fbW, &fbH);
    g_width = (float)fbW;
    g_height = (float)fbH;

    gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);
    glViewport(0, 0, fbW, fbH);

    // Dear ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; // не создаём imgui.ini

    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    // Шейдеры
    unsigned int shaderProgram = CompileShader(vertexShaderSource, fragmentShaderSource);
    unsigned int crosshairProgram = CompileShader(crosshairVertSrc, crosshairFragSrc);

    float ch = 5.0f;    // длина
    float th = 1.0f;    // толщина

    float crosshairVerts[] = {
        // Горизонтальная линия (2 треугольника)
        -ch, -th,
         ch, -th,
         ch,  th,
        -ch, -th,
         ch,  th,
        -ch,  th,

        // Вертикальная линия (2 треугольника)
        -th, -ch,
         th, -ch,
         th,  ch,
        -th, -ch,
         th,  ch,
        -th,  ch,
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
	//glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
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

    Hotbar hotbar;
    hotbar.Init(textureID);

    hotbar.slots[0] = GRASS;
    hotbar.slots[1] = DIRT;
    hotbar.slots[2] = STONE;
    hotbar.slots[3] = OAK_PLANKS;
    hotbar.slots[4] = GLASS;

    // Мир
    World world;
    Frustum frustum;

    // Запускаем начальную генерацию через Update
    world.Update(0, 0, camera.Front);

    // Uniform locations
    unsigned int modelLoc = glGetUniformLocation(shaderProgram, "model");
    unsigned int viewLoc = glGetUniformLocation(shaderProgram, "view");
    unsigned int projLoc = glGetUniformLocation(shaderProgram, "projection");
    unsigned int screenSizeLoc = glGetUniformLocation(crosshairProgram, "uScreenSize");

    // Timing / FPS
    double previousTime = 0.0, currentTime = 0.0, timeDifference = 0.0;
    unsigned int counter = 0;
    int lastVisibleChunks = 0;

    // Сглаженные значения для ImGui (чтобы не дёргались)
    float displayFPS = 0.f;
    float displayMS = 0.f;

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
            displayFPS = (float)((1.0 / timeDifference) * counter);
            displayMS = (float)((timeDifference / counter) * 1000.0);
            previousTime = currentTime;
            counter = 0;
        }

        float currentFrame = (float)glfwGetTime();
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        // Ограничиваем deltaTime — при ресайзе/фризах игрок не проваливается
        if (deltaTime > 0.05f) deltaTime = 0.05f;

        // Клавиатура
        player.moveForward = glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS;
        player.moveBack = glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS;
        player.moveLeft = glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS;
        player.moveRight = glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS;

        // Обновляем физику и двигаем камеру
        player.Update(deltaTime, world, camera);

        std::string newTitle = "isGrounded: " + std::to_string(player.isGrounded) +
            " velY: " + std::to_string(player.velocity.y);
        glfwSetWindowTitle(window, newTitle.c_str());

        if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS)
            player.Jump();

        // Динамическая подгрузка
        int playerCX = (int)floor(camera.Position.x / Chunk::SIZE_X);
        int playerCZ = (int)floor(camera.Position.z / Chunk::SIZE_Z);

        // Update вызываем только когда игрок сменил чанк (чтобы не спамить)
        if (playerCX != lastPlayerCX || playerCZ != lastPlayerCZ) {
            world.Update(playerCX, playerCZ, camera.Front);
            lastPlayerCX = playerCX;
            lastPlayerCZ = playerCZ;
        }

        // Загружаем на GPU не более 2 чанков за кадр (без фризов)
        world.UploadPendingChunks(2);

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
                glm::vec3 playerMin = player.position - glm::vec3(Player::WIDTH / 2.0f, 0.0f, Player::WIDTH / 2.0f);
                glm::vec3 playerMax = player.position + glm::vec3(Player::WIDTH / 2.0f, Player::HEIGHT, Player::WIDTH / 2.0f);
                bool insidePlayer =
                    (placeX     < playerMax.x && placeX + 1 > playerMin.x) &&
                    (placeY     < playerMax.y && placeY + 1 > playerMin.y) &&
                    (placeZ     < playerMax.z && placeZ + 1 > playerMin.z);

                if (!insidePlayer && world.GetBlock(placeX, placeY, placeZ) == AIR)
                {
                    // Используем активный блок при ПКМ
                    world.SetBlock(placeX, placeY, placeZ, hotbar.GetActiveBlock());
                    world.RebuildChunkAt(placeX, placeY, placeZ);
                }
            }
        }

        // Колесо мыши
        if (g_scrollDelta != 0.0) {
            hotbar.ScrollSlot(g_scrollDelta > 0 ? -1 : 1);
            g_scrollDelta = 0.0;
        }

        // Цифровые клавиши 1-9
        for (int i = 0; i < 9; i++)
            if (glfwGetKey(window, GLFW_KEY_1 + i) == GLFW_PRESS)
                hotbar.SetSlot(i);

        // ImGui новый кадр
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // ImGui оверлей со статистикой
        ImGui::SetNextWindowPos(ImVec2(10.f, 10.f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(210.f, 0.f), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.45f);

        ImGuiWindowFlags overlayFlags =
            ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_NoInputs |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoBringToFrontOnFocus;

        ImGui::Begin("##stats", nullptr, overlayFlags);
        ImGui::Text("FPS: %6.1f", displayFPS);
        ImGui::Text("Frametime: %6.2f ms", displayMS);
        ImGui::Separator();
        ImGui::Text("Chunks: %d", lastVisibleChunks);
        ImGui::End();

        // Рендер
        glm::mat4 model = glm::mat4(1.0f);
        glm::mat4 view = camera.GetViewMatrix();
        glm::mat4 projection = glm::perspective(glm::radians(75.0f), g_width / g_height, 0.1f, 1000.0f);
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
        glUniform2f(screenSizeLoc, g_width, g_height);
        glBindVertexArray(chVAO);
        glDrawArrays(GL_TRIANGLES, 0, 12); // 12 вершин = 2 прямоугольника
        glBindVertexArray(0);
        glEnable(GL_DEPTH_TEST);

        // Рендер хотбара
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        hotbar.Draw(g_width, g_height);
        glEnable(GL_CULL_FACE);
        glEnable(GL_DEPTH_TEST);

        // ImGui рендер (поверх всего)
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // Очистка ImGui
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwTerminate();
    return 0;
}