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

const char* vertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoord;   // тайловые UV (0..w, 0..h)
layout (location = 2) in vec2 aTileOffset; // смещение тайла в атласе

out vec2 TexCoord;
out vec2 TileOffset;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main()
{
    gl_Position = projection * view * model * vec4(aPos, 1.0);
    TexCoord = aTexCoord;
    TileOffset = aTileOffset;
}
)";

const char* fragmentShaderSource = R"(
#version 330 core
out vec4 FragColor;

in vec2 TexCoord;
in vec2 TileOffset;

uniform sampler2D texture1;

const float TILE_SIZE = 1.0 / 16.0;

void main()
{
    // fract повторяет текстуру, затем масштабируем в пространство одного тайла в атласе
    vec2 uv = TileOffset + fract(TexCoord) * TILE_SIZE;
    FragColor = texture(texture1, uv);
}
)";

Camera camera(glm::vec3(0.0f, 0.0f, 3.0f));

static void mouse_callback(GLFWwindow* window, double xpos, double ypos)
{
    static float lastX = 400, lastY = 300;
    static bool firstMouse = true;

    if (firstMouse)
    {
        lastX = xpos;
        lastY = ypos;
        firstMouse = false;
    }

    float xoffset = xpos - lastX;
    float yoffset = lastY - ypos;

    lastX = xpos;
    lastY = ypos;

    camera.ProcessMouse(xoffset, yoffset);
}

int main()
{
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    float resolutionX = 1920;
	float resolutionY = 1080;

    GLFWwindow* window = glfwCreateWindow(resolutionX, resolutionY, "SurvivalProject", NULL, NULL);
    glfwMakeContextCurrent(window);

    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    float deltaTime = 0.0f;
    float lastFrame = 0.0f;

    gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);

    unsigned int vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexShaderSource, NULL);
    glCompileShader(vertexShader);

    unsigned int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentShaderSource, NULL);
    glCompileShader(fragmentShader);

    unsigned int shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    //glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

	// Бинд текстуры
    unsigned int textureID;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);

    // Настройки фильтрации
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 4);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);

    // Анизатропная фильтрация
    //GLfloat maxAnisotropy = 0.0f;
    //glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAnisotropy);

    //glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, maxAnisotropy);
    
	// Загрузка текстуры
    int width, height, nrChannels;
    stbi_set_flip_vertically_on_load(true);

    unsigned char* data = stbi_load("Assets/atlas.png", &width, &height, &nrChannels, 0);

    if (data)
    {
        GLenum format;
        if (nrChannels == 1)
            format = GL_RED;
        else if (nrChannels == 3)
            format = GL_RGB;
        else if (nrChannels == 4)
            format = GL_RGBA;

        glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);

        glGenerateMipmap(GL_TEXTURE_2D);
    }
    else
    {
        std::cout << "Failed to load texture\n";
    }

    stbi_image_free(data);
    
    World world;

    Frustum frustum;

    for (int x = -30; x <= 1; x++)
    {
        for (int z = -30; z <= 1; z++)
        {
            // Создаём и генерируем чанк, но собираем меши во втором проходе.
            // Это гарантирует, что при построении меша все соседние чанки
            // уже присутствуют в `world.chunks` и внутренние грани не будут
            // ошибочно сгенерированы.
            world.chunks.emplace_back(x, z, &world);
            world.chunks.back().Generate();
            //world.chunks.back().BuildMesh();
        }
    }

    // Построим меши во втором проходе, когда все чанки уже добавлены в мир.
    for (auto& chunk : world.chunks)
    {
        chunk.BuildMesh();
    }

    unsigned int modelLoc = glGetUniformLocation(shaderProgram, "model");
    unsigned int viewLoc = glGetUniformLocation(shaderProgram, "view");
    unsigned int projLoc = glGetUniformLocation(shaderProgram, "projection");
	
    double previousTime = 0.0;
    double currentTime = 0.0;
    double timeDifference = 0.0;
    unsigned int counter = 0;
    int visibleChunks = 0;
    int lastVisibleChunks = 0;


    while (!glfwWindowShouldClose(window))
    {
        // Reset per-frame visible chunk counter
        visibleChunks = 0;

		currentTime = glfwGetTime();
		timeDifference = currentTime - previousTime;
        counter++;
		if (timeDifference >= 1.0 / 30.0)
        {
			std::string FPS = std::to_string((1.0 / timeDifference) * counter);
			std::string ms = std::to_string((timeDifference / counter) * 1000);
            // Use last frame's visible chunk count for the title (updated at end of frame)
            std::string newTitle = "SurvivalProject - " + FPS + " FPS / " + ms + " ms " + "chunks: " + std::to_string(lastVisibleChunks);
			glfwSetWindowTitle(window, newTitle.c_str());
            previousTime = currentTime;
            counter = 0;
        }

        float currentFrame = glfwGetTime();
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
            camera.ProcessKeyboard(GLFW_KEY_W, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
            camera.ProcessKeyboard(GLFW_KEY_S, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
            camera.ProcessKeyboard(GLFW_KEY_A, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
            camera.ProcessKeyboard(GLFW_KEY_D, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS)
            camera.ProcessKeyboard(GLFW_KEY_SPACE, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)
            camera.ProcessKeyboard(GLFW_KEY_LEFT_SHIFT, deltaTime);

        glm::mat4 model = glm::mat4(1.0f);

        glm::mat4 view = camera.GetViewMatrix();

        glm::mat4 projection = glm::perspective(glm::radians(75.0f), resolutionX / resolutionY, 0.1f, 1000.0f);

        glm::mat4 viewProj = projection * view;
        frustum.Update(viewProj);
        
        glUseProgram(shaderProgram);

        glUniform1i(glGetUniformLocation(shaderProgram, "texture1"), 0);

        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));
        glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(projection));

		glClearColor(0.5f, 0.7f, 1.0f, 1.0f);

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, textureID);

        for (auto& chunk : world.chunks)
        {
            if (frustum.IsBoxVisible(chunk.bounds.min, chunk.bounds.max))
            {
                chunk.Draw();
                visibleChunks++;
            }
        }

        // Store this frame's visible chunk count so the title can display it on the next update
        lastVisibleChunks = visibleChunks;

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwTerminate();
    return 0;
}