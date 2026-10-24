cmake_minimum_required(VERSION 3.25)
project(SurvivalProject LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenGL REQUIRED)

option(GLFW_BUILD_EXAMPLES "" OFF)
option(GLFW_BUILD_TESTS    "" OFF)
option(GLFW_BUILD_DOCS     "" OFF)
option(GLFW_INSTALL        "" OFF)
add_subdirectory(external/glfw)

set(LIBTYPE STATIC)
option(ALSOFT_UTILS          "" OFF)
option(ALSOFT_EXAMPLES       "" OFF)
option(ALSOFT_TESTS          "" OFF)
option(ALSOFT_INSTALL        "" OFF)
option(ALSOFT_NO_CONFIG_UTIL "" ON)
option(ALSOFT_EMBED_FMT      "" OFF)
add_subdirectory(external/openal-soft openal-build)

add_library(glad STATIC external/glad/src/glad.c)
target_include_directories(glad PUBLIC external/glad/include)

add_library(glm INTERFACE)
target_include_directories(glm INTERFACE external/glm)

add_library(FastNoiseLite INTERFACE)
target_include_directories(FastNoiseLite INTERFACE external/FastNoiseLite/Cpp)

add_library(stb STATIC external/stb_impl.cpp)
target_include_directories(stb PUBLIC external/stb)

add_library(imgui STATIC
    external/imgui/imgui.cpp
    external/imgui/imgui_draw.cpp
    external/imgui/imgui_tables.cpp
    external/imgui/imgui_widgets.cpp
    external/imgui/backends/imgui_impl_glfw.cpp
    external/imgui/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC external/imgui external/imgui/backends)
target_link_libraries(imgui PUBLIC glfw glad)

add_executable(SurvivalProject
    src/audio.cpp
    src/camera.cpp
    src/chunk.cpp
    src/frustum.cpp
    src/hotbar.cpp
    src/inventory.cpp
    src/main.cpp
    src/player.cpp
    src/world.cpp
    src/core/QuadMesh.cpp
    src/core/Shader.cpp
    src/core/Texture.cpp
    src/renderer/SkyRenderer.cpp
    src/renderer/HUDRenderer.cpp
    src/utils/FileUtils.cpp
)

target_include_directories(SurvivalProject PRIVATE src)

if(MSVC)
    target_compile_options(SurvivalProject PRIVATE /W4 /utf-8 /MP)
    target_compile_definitions(SurvivalProject PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(SurvivalProject PRIVATE -Wall -Wextra)
endif()

target_compile_definitions(SurvivalProject PRIVATE GLFW_INCLUDE_NONE)

target_link_libraries(SurvivalProject PRIVATE
    glad
    glfw
    glm
    FastNoiseLite
    stb
    imgui
    OpenAL::OpenAL
    OpenGL::GL
)

add_custom_command(TARGET SurvivalProject POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
            "${CMAKE_SOURCE_DIR}/assets"
            "$<TARGET_FILE_DIR:SurvivalProject>/assets"
    COMMENT "Syncing assets next to the executable"
)