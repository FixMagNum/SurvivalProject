#include <glad/glad.h>
#include <GLFW/glfw3.h>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/norm.hpp>
#include "camera.h"
#include "player.h"
#include "world.h"
#include "frustum.h"
#include <iostream>
#include <string>
#include <algorithm>
#include <stb_image.h>
#include <filesystem>
#include <fstream>
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "hotbar.h"
#include "inventory.h"
#include "audio.h"

// Основной шейдер (блоки)
const char* vertexShaderSource = R"(
#version 330 core
layout (location = 0) in uvec2 aData; // data0, data1

out vec2  TexCoord;
out vec2  TileOffset;
out float AO;
out vec3  Normal;
out vec3  FragPos;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

const vec3 normals[6] = vec3[6](
    vec3( 0, 1, 0),
    vec3( 0,-1, 0),
    vec3( 1, 0, 0),
    vec3(-1, 0, 0),
    vec3( 0, 0, 1),
    vec3( 0, 0,-1)
);

// UV углы quad: corner 0..3
// corners дают (0,0)..(1,1) — умножаем на реальный размер quad
const vec2 corners[4] = vec2[4](
    vec2(0,0), vec2(1,0), vec2(1,1), vec2(0,1)
);

const float TILE_SIZE = 1.0 / 16.0;

void main()
{
    uint d0 = aData.x;
    uint d1 = aData.y;

    float px = float((d0 >> 24) & 0xFFu);
    float py = float((d0 >> 16) & 0xFFu);
    float pz = float((d0 >>  8) & 0xFFu);

    uint faceId = (d0 >> 5) & 7u;
    uint ao     = (d0 >> 3) & 3u;
    uint corner = (d0 >> 1) & 3u;
    uint tileId = d1 & 0xFFu;
    uint sizeU  = (d1 >> 8)  & 0xFFu;
    uint sizeV  = (d1 >> 16) & 0xFFu;

    vec3 pos = vec3(px, py, pz);

    gl_Position = projection * view * model * vec4(pos, 1.0);
    FragPos     = (model * vec4(pos, 1.0)).xyz;

    Normal    = normals[faceId];
    AO        = float(ao) / 3.0;
    TexCoord  = corners[corner] * vec2(float(sizeU), float(sizeV));

    uint tileX = tileId % 16u;
    uint tileY = tileId / 16u;
    TileOffset = vec2(float(tileX), float(tileY)) * TILE_SIZE;
}
)";

const char* fragmentShaderSource = R"(
#version 330 core
out vec4 FragColor;

in vec2 TexCoord;
in vec2 TileOffset;
in float AO;
in vec3 Normal;
in vec3 FragPos;

uniform sampler2D texture1;
uniform vec3  uSunDir;          // направление к солнцу (нормализованное)
uniform vec3  uSunColor;        // цвет солнца (меняется день/ночь)
uniform vec3  uMoonDir;
uniform vec3  uMoonColor;
uniform float uAmbient;         // минимальная яркость (ночью меньше)
uniform bool  uAlphaClip;
uniform vec3  uCameraPos;       // позиция камеры
uniform vec3  uSkyColor;        // цвет неба (тот же что glClearColor)
uniform float uDaylight;        // 0.0 = полная ночь, 1.0 = полный день
uniform bool  uUnderwater;

const float TILE_SIZE = 1.0 / 16.0;

void main()
{
    vec2 uv = TileOffset + fract(TexCoord) * TILE_SIZE;
    vec4 texColor = texture(texture1, uv);

    if (uAlphaClip && texColor.a < 0.5)
        discard;

    float sunDiff  = max(dot(Normal, uSunDir),  0.0);
    float moonDiff = max(dot(Normal, uMoonDir), 0.0);

    float aoFactor = mix(0.2, 1.0, AO);

    float sunFade  = smoothstep(-0.1, 0.15, uSunDir.y);
    float moonFade = smoothstep(-0.1, 0.15, uMoonDir.y);

    // Свет без AO, масштабируем на daylight
    vec3 light = (uSunColor  * sunFade  * sunDiff
               +  uMoonColor * moonFade * moonDiff
               +  vec3(uAmbient)) * uDaylight;

    // AO применяем отдельно — он не зависит от времени суток
    FragColor = clamp(texColor * vec4(light, 1.0), 0.0, 1.0);
    FragColor.rgb *= aoFactor;

    // Туман и гамма
    float dist = length(FragPos - uCameraPos);
    float fogFactor = clamp((dist - 80.0) / (160.0 - 80.0), 0.0, 1.0);

    FragColor.rgb = pow(FragColor.rgb, vec3(1.0 / 2.2));

    if (uUnderwater)
    {
    vec3 waterColor = vec3(78.0/256.0, 106.0/256.0, 180.0/256.0);
    FragColor.rgb = mix(FragColor.rgb, waterColor, 0.5);
    }

    FragColor.rgb = mix(FragColor.rgb, uSkyColor, fogFactor);
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

const char* outlineVertSrc = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
void main()
{
    gl_Position = projection * view * model * vec4(aPos, 1.0);
}
)";

const char* outlineFragSrc = R"(
#version 330 core
out vec4 FragColor;
void main() { FragColor = vec4(0.0, 0.0, 0.0, 1.0); }
)";

// Глобальные переменные
Camera camera(glm::vec3(0.0f, 120.0f, 3.0f));
Player player(glm::vec3(0.0f, 120.0f, 0.0f));
Hotbar hotbar;
Inventory inventory;

static double g_scrollDelta = 0.0;
static float g_timeOfDay = 0.0f; // 0.0 = рассвет, 0.5 = закат, 1.0 = рассвет

static float g_width = 800.0f;
static float g_height = 600.0f;

static bool g_fullscreen = false;
static int  g_windowedX = 100, g_windowedY = 100;
static int  g_windowedW = 800, g_windowedH = 600;

// Флаги кликов мыши (устанавливаются в callback, читаются в game loop)
static bool g_leftClick = false;
static bool g_rightClick = false;
static bool g_prevSpace = false;

static double g_mouseX = 0.0, g_mouseY = 0.0;

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
    g_mouseX = xpos;
    g_mouseY = ypos;

    static float lastX = 400, lastY = 300;
    static bool  firstMouse = true;

    if (firstMouse) { lastX = (float)xpos; lastY = (float)ypos; firstMouse = false; }

    if (inventory.isOpen)
    {
        // Обновляем lastX/lastY чтобы не было рывка при закрытии
        lastX = (float)xpos;
        lastY = (float)ypos;
        return;
    }

    float xoffset = (float)xpos - lastX;
    float yoffset = lastY - (float)ypos;
    lastX = (float)xpos;
    lastY = (float)ypos;

    camera.ProcessMouse(xoffset, yoffset);
}

static void mouse_button_callback(GLFWwindow* window, int button, int action, int mods)
{
    if (inventory.isOpen)
    {
        if (button == GLFW_MOUSE_BUTTON_LEFT)
        {
            if (action == GLFW_PRESS)
                inventory.OnMousePress((float)g_mouseX, (float)g_mouseY, g_width, g_height, hotbar);
            else if (action == GLFW_RELEASE)
                inventory.OnMouseRelease((float)g_mouseX, (float)g_mouseY, g_width, g_height, hotbar);
        }
        return; // не передаём клики в мир когда инвентарь открыт
    }

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
    
    if (key == GLFW_KEY_E && action == GLFW_PRESS)
    {
        inventory.isOpen = !inventory.isOpen;
        glfwSetInputMode(window, GLFW_CURSOR,
            inventory.isOpen ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
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

static void SavePlayerPos(glm::vec3 pos)
{
    std::filesystem::create_directories("saves");
    std::ofstream f("saves/player.bin", std::ios::binary);
    if (!f) return;
    f.write((char*)&pos.x, sizeof(float));
    f.write((char*)&pos.y, sizeof(float));
    f.write((char*)&pos.z, sizeof(float));
}

static bool LoadPlayerPos(glm::vec3& pos)
{
    std::ifstream f("saves/player.bin", std::ios::binary);
    if (!f) return false;
    f.read((char*)&pos.x, sizeof(float));
    f.read((char*)&pos.y, sizeof(float));
    f.read((char*)&pos.z, sizeof(float));
    return true;
}

int main()
{
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    //glfwWindowHint(GLFW_SAMPLES, 4);

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
    unsigned int outlineProgram = CompileShader(outlineVertSrc, outlineFragSrc);

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

    float s = 0.502f; // чуть больше 0.5 чтобы не z-fighting
    float cubeVerts[] = {
        // 8 вершин куба
        -s,-s,-s,  s,-s,-s,  s, s,-s,  -s, s,-s,
        -s,-s, s,  s,-s, s,  s, s, s,  -s, s, s,
    };
    unsigned int cubeInds[] = {
        0,1, 1,2, 2,3, 3,0, // нижняя грань
        4,5, 5,6, 6,7, 7,4, // верхняя грань
        0,4, 1,5, 2,6, 3,7  // вертикальные рёбра
    };

    unsigned int outlineVAO, outlineVBO, outlineEBO;
    glGenVertexArrays(1, &outlineVAO);
    glGenBuffers(1, &outlineVBO);
    glGenBuffers(1, &outlineEBO);

    glBindVertexArray(outlineVAO);
    glBindBuffer(GL_ARRAY_BUFFER, outlineVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(cubeVerts), cubeVerts, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, outlineEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(cubeInds), cubeInds, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);

    // OpenGL состояние
	//glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    //glEnable(GL_MULTISAMPLE);
    //glfwSwapInterval(1);

    // Текстура
    unsigned int textureID;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 4);

    int width, height, nrChannels;
    stbi_set_flip_vertically_on_load(true);
    unsigned char* data = stbi_load("Assets/textures/atlas.png", &width, &height, &nrChannels, 0);
    if (data)
    {
        GLenum fmt = (nrChannels == 4) ? GL_RGBA : (nrChannels == 3 ? GL_RGB : GL_RED);
        GLenum fmtSRGB = (nrChannels == 4) ? GL_SRGB_ALPHA : (nrChannels == 3 ? GL_SRGB : GL_RED);
        glTexImage2D(GL_TEXTURE_2D, 0, fmtSRGB, width, height, 0, fmt, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);
    }
    else std::cout << "Failed to load texture\n";
    stbi_image_free(data);

    Audio::Init();
    Audio::LoadBlockSounds();
    ALuint placeSound = Audio::LoadWav("Assets/sounds/place.wav");

    hotbar.Init(textureID);
    inventory.Init(textureID);

    inventory.Load();
    hotbar.Load();

    hotbar.slots[0] = COBBLESTONE, hotbar.counts[0] = 64;
    hotbar.slots[1] = GLASS, hotbar.counts[1] = 64;
    hotbar.slots[2] = SNOW, hotbar.counts[2] = 64;
    hotbar.slots[3] = OAK_PLANKS, hotbar.counts[3] = 64;
    hotbar.slots[4] = OAK_LOG, hotbar.counts[4] = 64;
    hotbar.slots[5] = COBBLESTONE, hotbar.counts[5] = 64;
    hotbar.slots[6] = POOP, hotbar.counts[6] = 64;

    // Мир
    World world;
    Frustum frustum;

    // Запускаем начальную генерацию через Update
    world.Update(0, 7, 0, camera.Front); // Y=7 примерно соответствует высоте 120 (120/16=7.5)

    // Ждём пока чанк спавна сгенерируется
    while (true)
    {
        world.UploadPendingChunks(16);
        std::lock_guard<std::mutex> lock(world.chunkMapMutex);
        auto it = world.chunkMap.find({ 0, 7, 0 }); // Y=7 соответствует высоте ~112-128
        if (it != world.chunkMap.end() &&
            it->second->state.load() == ChunkState::Uploaded) break;
    }

    // Находим поверхность в точке спавна
    int spawnY = 150;
    for (int y = 500; y >= 0; y--)
    {
        BlockType b = world.GetBlock(0, y, 0);
        if (b != AIR && b != WATER)
        {
            spawnY = y + 1;
            break;
        }
    }

    // Загружаем позицию игрока если есть сохранение
    glm::vec3 savedPos;
    if (LoadPlayerPos(savedPos))
    {
        player.position = savedPos;
        camera.Position = savedPos + glm::vec3(0.0f, Player::EYE_HEIGHT, 0.0f);
    }
    else
    {
        player.position = glm::vec3(0.0f, (float)spawnY, 0.0f);
        camera.Position = player.position + glm::vec3(0.0f, Player::EYE_HEIGHT, 0.0f);
    }

    // Uniform locations
    unsigned int modelLoc = glGetUniformLocation(shaderProgram, "model");
    unsigned int viewLoc = glGetUniformLocation(shaderProgram, "view");
    unsigned int projLoc = glGetUniformLocation(shaderProgram, "projection");
    unsigned int sunDirLoc = glGetUniformLocation(shaderProgram, "uSunDir");
    unsigned int sunColorLoc = glGetUniformLocation(shaderProgram, "uSunColor");
    unsigned int moonDirLoc = glGetUniformLocation(shaderProgram, "uMoonDir");
    unsigned int moonColorLoc = glGetUniformLocation(shaderProgram, "uMoonColor");
    unsigned int ambientLoc = glGetUniformLocation(shaderProgram, "uAmbient");
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
    int lastPlayerCX = INT_MIN, lastPlayerCZ = INT_MIN, lastPlayerCY = INT_MIN;

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
        player.isSprinting = glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS;
        bool wantsCrouch = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS;

        // Если хочет встать — проверяем есть ли место
        if (!wantsCrouch && player.isCrouching)
        {
            // Проверяем есть ли место для полного роста
            float half = Player::WIDTH / 2.0f;
            int minX = (int)floor(player.position.x - half);
            int maxX = (int)floor(player.position.x + half - 0.001f);
            int minZ = (int)floor(player.position.z - half);
            int maxZ = (int)floor(player.position.z + half - 0.001f);
            int topY = (int)floor(player.position.y + Player::HEIGHT - 0.001f);

            bool canStand = true;
            for (int x = minX; x <= maxX && canStand; x++)
                for (int z = minZ; z <= maxZ && canStand; z++)
                    if (player.IsSolid(x, topY, z, world))
                        canStand = false;

            player.isCrouching = !canStand; // встаём только если есть место
        }
        else
        {
            player.isCrouching = wantsCrouch;
        }

        bool spaceDown = (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS);

        if (spaceDown && !g_prevSpace)
        {
            if (player.inWater)
                player.RequestSwimUp();
            else
                player.RequestJump();
        }

        g_prevSpace = spaceDown;

        // Обновляем физику и двигаем камеру
        player.Update(deltaTime, world, camera);

        Audio::SetListener(
            camera.Position.x,
            camera.Position.y,
            camera.Position.z,
            camera.Front.x,
            camera.Front.y,
            camera.Front.z,
            camera.Up.x,
            camera.Up.y,
            camera.Up.z
        );

        // Респаун после смерти
        static float respawnTime = -1.0f; // время респауна
        static bool respawnPending = false;

        if (player.isDead)
        {
            player.position = glm::vec3(0.0f, 150.0f, 0.0f);
            player.velocity = glm::vec3(0.0f);
            player.health = Player::MAX_HEALTH;
            player.isDead = false;
            player.maxFallSpeed = 0.0f;
            player.coyoteTimer = 0.0f;
            player.jumpBufferTimer = 0.0f;
            player.isGrounded = false;
            camera.Position = player.position + glm::vec3(0.0f, Player::EYE_HEIGHT, 0.0f);
            respawnTime = currentFrame;
            respawnPending = true;
            lastPlayerCX = INT_MIN;
            lastPlayerCY = INT_MIN;
            lastPlayerCZ = INT_MIN;
        }

        if (respawnPending)
        {
            // Замораживаем игрока — гравитация не действует
            player.velocity = glm::vec3(0.0f);

            bool chunkReady = false;
            {
                std::lock_guard<std::mutex> lock(world.chunkMapMutex);
                auto it = world.chunkMap.find({ 0, 7, 0 });
                chunkReady = (it != world.chunkMap.end() &&
                    it->second->state.load() == ChunkState::Uploaded);
            }

            if (chunkReady)
            {
                respawnPending = false;
                int spawnY = 150;
                for (int y = 500; y >= 0; y--)
                {
                    BlockType b = world.GetBlock(0, y, 0);
                    if (b != AIR && b != WATER) { spawnY = y + 1; break; }
                }
                player.position = glm::vec3(0.0f, (float)spawnY, 0.0f);
                player.velocity = glm::vec3(0.0f);
                player.maxFallSpeed = 0.0f; // сбрасываем урон от падения
                player.coyoteTimer = 0.0f;
                player.jumpBufferTimer = 0.0f;
                player.isGrounded = false;
                camera.Position = player.position + glm::vec3(0.0f, Player::EYE_HEIGHT, 0.0f);
            }
        }

        // Динамическая подгрузка
        int playerCX = (int)floor(camera.Position.x / Chunk::SIZE_X);
        int playerCY = (int)floor(camera.Position.y / Chunk::SIZE_Y);
        int playerCZ = (int)floor(camera.Position.z / Chunk::SIZE_Z);

        // Выгрузка каждый кадр
        world.UnloadDistantChunks(playerCX, playerCY, playerCZ);

        bool forceUpdate = currentFrame < 5.0f;
        bool respawnUpdate = (respawnTime >= 0.0f && currentFrame - respawnTime < 5.0f);

        if (forceUpdate || respawnUpdate || playerCX != lastPlayerCX || playerCY != lastPlayerCY || playerCZ != lastPlayerCZ)
        {
            world.Update(playerCX, playerCY, playerCZ, camera.Front);
            lastPlayerCX = playerCX;
            lastPlayerCY = playerCY;
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
                BlockType broken = world.GetBlock(hit.worldX, hit.worldY, hit.worldZ);

				// Разрушаем блок — ставим AIR
				world.SetBlock(hit.worldX, hit.worldY, hit.worldZ, AIR);
                Audio::PlayBlockBreak(broken,
                    hit.worldX + 0.5f,
                    hit.worldY + 0.5f,
                    hit.worldZ + 0.5f
                );
				world.RebuildChunkAt(hit.worldX, hit.worldY, hit.worldZ);

                // Кладём блок в инвентарь
                if (broken != AIR)
                {
                    bool placed = false;

                    // Сначала ищем существующий стак в хотбаре
                    for (int i = 0; i < Hotbar::SLOTS; i++)
                    {
                        if (hotbar.slots[i] == broken && hotbar.counts[i] < 64)
                        {
                            hotbar.counts[i]++;
                            placed = true;
                            break;
                        }
                    }

                    // Потом существующий стак в инвентаре
                    if (!placed)
                    {
                        for (int i = 0; i < Inventory::SIZE; i++)
                        {
                            if (inventory.slots[i].type == broken && inventory.slots[i].count < 64)
                            {
                                inventory.slots[i].count++;
                                placed = true;
                                break;
                            }
                        }
                    }

                    // Только если нигде нет — свободный слот в хотбаре
                    if (!placed)
                    {
                        for (int i = 0; i < Hotbar::SLOTS; i++)
                        {
                            if (hotbar.slots[i] == AIR)
                            {
                                hotbar.slots[i] = broken;
                                hotbar.counts[i] = 1;
                                placed = true;
                                break;
                            }
                        }
                    }

                    // Потом свободный слот в инвентаре
                    if (!placed)
                    {
                        for (int i = 0; i < Inventory::SIZE; i++)
                        {
                            if (inventory.slots[i].type == AIR)
                            {
                                inventory.slots[i] = { broken, 1 };
                                placed = true;
                                break;
                            }
                        }
                    }
                }
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
                glm::vec3 playerMax = player.position + glm::vec3(
                    Player::WIDTH / 2.0f,
                    player.isCrouching ? Player::CROUCH_HEIGHT : Player::HEIGHT,
                    Player::WIDTH / 2.0f
                );
                bool insidePlayer =
                    (placeX     < playerMax.x && placeX + 1 > playerMin.x) &&
                    (placeY     < playerMax.y && placeY + 1 > playerMin.y) &&
                    (placeZ     < playerMax.z && placeZ + 1 > playerMin.z);

                if (!insidePlayer && world.GetBlock(placeX, placeY, placeZ) == AIR)
                {
                    // Используем активный блок при ПКМ
                    world.SetBlock(placeX, placeY, placeZ, hotbar.GetActiveBlock());
                    Audio::Play3D(placeSound,
                        placeX + 0.5f,
                        placeY + 0.5f,
                        placeZ + 0.5f
                    );
                    world.RebuildChunkAt(placeX, placeY, placeZ);

                    // Трата блока 
                    hotbar.counts[hotbar.activeSlot]--;
                    if (hotbar.counts[hotbar.activeSlot] <= 0)
                        hotbar.slots[hotbar.activeSlot] = AIR;
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
        ImGui::Text("Visible chunks: %d", lastVisibleChunks);
        ImGui::Text("Total chunks: %d", (int)world.chunkMap.size());
        ImGui::Text("Time: %.2f", g_timeOfDay);
        ImGui::End();

        // Счётчики в хотбаре
        constexpr float SLOT_SIZE = 50.0f;
        constexpr float PADDING = 4.0f;
        float totalW = Hotbar::SLOTS * SLOT_SIZE + (Hotbar::SLOTS - 1) * PADDING;
        float startX = (g_width - totalW) / 2.0f;
        float startY = g_height - SLOT_SIZE - 16.0f;

        for (int i = 0; i < Hotbar::SLOTS; i++)
        {
            if (hotbar.slots[i] == AIR) continue;

            float x = startX + i * (SLOT_SIZE + PADDING);

            // Позиционируем текст в правом нижнем углу слота
            ImGui::SetNextWindowPos(ImVec2(x + SLOT_SIZE - 18.0f, startY + SLOT_SIZE - 18.0f), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(20.0f, 18.0f), ImGuiCond_Always);
            ImGui::SetNextWindowBgAlpha(0.0f);

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));

            ImGui::Begin(("##count" + std::to_string(i)).c_str(), nullptr,
                ImGuiWindowFlags_NoDecoration |
                ImGuiWindowFlags_NoInputs |
                ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoBringToFrontOnFocus);
            
            ImGui::SetCursorPos(ImVec2(1, 1));                              // смещение тени
            ImGui::TextColored(ImVec4(0, 0, 0, 1), "%d", hotbar.counts[i]); // тёмный
            ImGui::SetCursorPos(ImVec2(0, 0));                              // основной текст
            ImGui::Text("%d", hotbar.counts[i]);                            // белый
            
            ImGui::End();
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(2);
        }

        if (inventory.isOpen)
        {
            constexpr float INV_SLOT_SIZE = 50.0f;
            constexpr float INV_PADDING = 4.0f;
            float invTotalW = Inventory::COLS * INV_SLOT_SIZE + (Inventory::COLS - 1) * INV_PADDING;
            float invTotalH = Inventory::ROWS * INV_SLOT_SIZE + (Inventory::ROWS - 1) * INV_PADDING;
            float invStartX = (g_width - invTotalW) / 2.0f;
            float invStartY = (g_height - invTotalH) / 2.0f;

            for (int i = 0; i < Inventory::SIZE; i++)
            {
                if (inventory.slots[i].type == AIR) continue;
                if (inventory.dragFromInventory && i == inventory.dragSlot) continue;

                int row = i / Inventory::COLS;
                int col = i % Inventory::COLS;
                float x = invStartX + col * (INV_SLOT_SIZE + INV_PADDING);
                float y = invStartY + row * (INV_SLOT_SIZE + INV_PADDING);

                ImGui::SetNextWindowPos(ImVec2(x + INV_SLOT_SIZE - 18.0f, y + INV_SLOT_SIZE - 18.0f), ImGuiCond_Always);
                ImGui::SetNextWindowSize(ImVec2(20.0f, 18.0f), ImGuiCond_Always);
                ImGui::SetNextWindowBgAlpha(0.0f);

                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
                ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));

                ImGui::Begin(("##invcount" + std::to_string(i)).c_str(), nullptr,
                    ImGuiWindowFlags_NoDecoration |
                    ImGuiWindowFlags_NoInputs |
                    ImGuiWindowFlags_NoMove |
                    ImGuiWindowFlags_NoSavedSettings |
                    ImGuiWindowFlags_NoBringToFrontOnFocus);

                ImGui::SetCursorPos(ImVec2(1, 1));
                ImGui::TextColored(ImVec4(0, 0, 0, 1), "%d", inventory.slots[i].count);
                ImGui::SetCursorPos(ImVec2(0, 0));
                ImGui::Text("%d", inventory.slots[i].count);

                ImGui::End();
                ImGui::PopStyleColor(2);
                ImGui::PopStyleVar();
            }
        }

        // Полоска здоровья
        ImGui::SetNextWindowPos(ImVec2(g_width / 2.0f - 100.0f, g_height - 90.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(200.0f, 20.0f), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.8f, 0.1f, 0.1f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.25f));

        ImGui::Begin("##health", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoBringToFrontOnFocus);

        ImGui::ProgressBar(player.health / Player::MAX_HEALTH, ImVec2(200.0f, 15.0f));

        ImGui::End();
        ImGui::PopStyleColor(4);
        ImGui::PopStyleVar();

        if (player.health < Player::MAX_HEALTH * 0.3f)
        {
            float alpha = (1.0f - player.health / (Player::MAX_HEALTH * 0.3f)) * 0.3f;
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImVec2(g_width, g_height));
            ImGui::SetNextWindowBgAlpha(alpha);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.8f, 0.0f, 0.0f, 1.0f));
            ImGui::Begin("##damage", nullptr,
                ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoBringToFrontOnFocus);
            ImGui::End();
            ImGui::PopStyleColor();
        }

        // Цикл дня/ночи
        g_timeOfDay += deltaTime * 0.001f; // 1000 секунд = одни сутки
        if (g_timeOfDay > 1.0f) g_timeOfDay -= 1.0f;

        // Угол солнца: 0 = горизонт (рассвет), PI/2 = зенит (полдень), PI = горизонт (закат)
        float sunAngle = g_timeOfDay * glm::two_pi<float>();
        glm::vec3 sunDir = glm::normalize(glm::vec3(
            cos(sunAngle),
            sin(sunAngle),
            0.3f
        ));
        glm::vec3 moonDir = -sunDir;

        // Цвет неба и солнца зависит от высоты солнца
        float sunHeight = sunDir.y; // -1..1
        float moonHeight = moonDir.y;

        // День: голубое небо, Закат: оранжевое, Ночь: тёмно-синее
        glm::vec3 skyDay = glm::vec3(0.5f, 0.7f, 1.0f);
        glm::vec3 skySunset = glm::vec3(1.0f, 0.4f, 0.1f);
        glm::vec3 skyNight = glm::vec3(0.0002f, 0.0002f, 0.0008f);

        glm::vec3 skyColor;
        float ambient;

        if (sunHeight > 0.0f)
        {
            // День — закат
            float t = glm::smoothstep(0.0f, 0.3f, sunHeight);
            skyColor = glm::mix(skySunset, skyDay, t);
            ambient = glm::mix(0.3f, 0.15f, t); // на закате чуть темнее
        }
        else
        {
            // Ночь
            float t = glm::smoothstep(0.0f, -0.2f, sunHeight);
            skyColor = glm::mix(skySunset, skyNight, t);
            ambient = glm::mix(0.3f, 0.08f, t); // ночью очень темно
        }

        // Цвет солнца — белый днём, оранжевый на закате
        glm::vec3 sunColor = glm::mix(
            glm::vec3(1.0f, 0.6f, 0.3f),
            glm::vec3(1.0f, 1.0f, 1.0f),
            glm::clamp(sunHeight * 3.0f, 0.0f, 1.0f)
        );

        // Лунный свет — холодный синеватый
        float moonFade = glm::smoothstep(-0.1f, 0.15f, moonHeight);
        glm::vec3 moonColor = glm::vec3(0.2f, 0.25f, 0.4f) * moonFade;

        // Плавный переход день/ночь — 1.0 днём, 0.005 ночью
        float daylight = glm::clamp(sunHeight * 3.0f + 0.5f, 0.005f, 1.0f);

        BlockType cameraBlock = world.GetBlock(
            (int)floor(camera.Position.x),
            (int)floor(camera.Position.y),
            (int)floor(camera.Position.z)
        );
        bool underwater = (cameraBlock == WATER);

        // Рендер
        static const RenderGroup renderOrder[] = {
            RenderGroup::Opaque,
            RenderGroup::Leaves,
            RenderGroup::Water,
            RenderGroup::Glass
        };

        glm::mat4 model = glm::mat4(1.0f);
        glm::mat4 view = camera.GetViewMatrix();
        glm::mat4 projection = glm::perspective(glm::radians(75.0f), g_width / g_height, 0.1f, 1000.0f);
        glm::mat4 viewProj = projection * view;
        frustum.Update(viewProj);

        glm::vec3 skyColorGamma = glm::pow(skyColor, glm::vec3(1.0f / 2.2f));
        glClearColor(skyColorGamma.r, skyColorGamma.g, skyColorGamma.b, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glUseProgram(shaderProgram);

        int alphaClipLoc = glGetUniformLocation(shaderProgram, "uAlphaClip");
        glUniform1i(glGetUniformLocation(shaderProgram, "texture1"), 0);
        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));
        glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(projection));
        glUniform3fv(sunDirLoc, 1, glm::value_ptr(sunDir));
        glUniform3fv(sunColorLoc, 1, glm::value_ptr(sunColor));
        glUniform3fv(moonDirLoc, 1, glm::value_ptr(moonDir));
        glUniform3fv(moonColorLoc, 1, glm::value_ptr(moonColor));
        glUniform1f(ambientLoc, ambient);
        glUniform1f(glGetUniformLocation(shaderProgram, "uDaylight"), daylight);
        glUniform1i(glGetUniformLocation(shaderProgram, "uUnderwater"), underwater ? 1 : 0);
        glUniform3fv(glGetUniformLocation(shaderProgram, "uCameraPos"), 1, glm::value_ptr(camera.Position));
        glUniform3fv(glGetUniformLocation(shaderProgram, "uSkyColor"), 1, glm::value_ptr(skyColorGamma));

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, textureID);

        // Общие fixed-function state
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glEnable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);

        for (RenderGroup group : renderOrder)
        {
            // Настройка state под текущую группу
            if (group == RenderGroup::Opaque)
            {
                glDepthMask(GL_TRUE);
                glDisable(GL_BLEND);
                glUniform1i(alphaClipLoc, 0);
            }
            else if (group == RenderGroup::Leaves)
            {
                glDisable(GL_CULL_FACE);
                glEnable(GL_DEPTH_TEST);
                glDepthMask(GL_TRUE);
                glDisable(GL_BLEND);
                glUniform1i(alphaClipLoc, 1); // листья режем по альфе
            }
            else if (group == RenderGroup::Water)
            {
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                glUniform1i(alphaClipLoc, 0);
            }
            else
            {
                glEnable(GL_CULL_FACE);
                glCullFace(GL_BACK);
                glDepthMask(GL_FALSE);
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                glUniform1i(alphaClipLoc, 0);
            }

			std::lock_guard<std::mutex> lock(world.chunkMapMutex);

			for (auto& [key, chunk] : world.chunkMap)
			{
				if (chunk->state.load() != ChunkState::Uploaded) continue;
				chunk->CheckFence(); // проверяем без блокировки
				if (!chunk->gpuReady) continue; // пропускаем если GPU ещё не готов
				if (!frustum.IsBoxVisible(chunk->bounds.min, chunk->bounds.max)) continue;

				// Матрица трансляции для этого чанка
				glm::mat4 chunkModel = glm::translate(glm::mat4(1.0f), chunk->bounds.min);
				glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(chunkModel));

				chunk->DrawGroup(group, camera.Position);
			}
		}

        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        glCullFace(GL_BACK);
        glEnable(GL_CULL_FACE);
        glUniform1i(alphaClipLoc, 0);

        // Подсветка блока
        if (hit.hit)
        {
            glm::mat4 outlineModel = glm::translate(glm::mat4(1.0f),
                glm::vec3(hit.worldX + 0.5f, hit.worldY + 0.5f, hit.worldZ + 0.5f));

            glUseProgram(outlineProgram);
            glUniformMatrix4fv(glGetUniformLocation(outlineProgram, "model"), 1, GL_FALSE, glm::value_ptr(outlineModel));
            glUniformMatrix4fv(glGetUniformLocation(outlineProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));
            glUniformMatrix4fv(glGetUniformLocation(outlineProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));

            glDisable(GL_CULL_FACE);
            glLineWidth(2.0f);
            glBindVertexArray(outlineVAO);
            glDrawElements(GL_LINES, 24, GL_UNSIGNED_INT, 0);
            glBindVertexArray(0);
            glEnable(GL_CULL_FACE);
        }

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

        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        inventory.Draw(g_width, g_height, (float)g_mouseX, (float)g_mouseY);
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

    SavePlayerPos(player.position);

    inventory.Save();
    hotbar.Save();
    Audio::Shutdown();

    glfwTerminate();
    return 0;
}