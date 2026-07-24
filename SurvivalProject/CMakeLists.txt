cmake_minimum_required(VERSION 3.20)
project(SurvivalProject LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/utf-8)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

find_package(OpenGL REQUIRED)

set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
add_subdirectory(external/glfw)

set(LIBTYPE STATIC)
set(ALSOFT_UTILS        OFF CACHE BOOL "" FORCE)
set(ALSOFT_EXAMPLES     OFF CACHE BOOL "" FORCE)
set(ALSOFT_TESTS        OFF CACHE BOOL "" FORCE)
set(ALSOFT_INSTALL      OFF CACHE BOOL "" FORCE)
set(ALSOFT_NO_CONFIG_UTIL ON CACHE BOOL "" FORCE)
add_subdirectory(external/openal-soft)

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
    external/imgui/imgui_demo.cpp
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
    src/QuadMesh.cpp
    src/SkyRenderer.cpp
    src/world.cpp
)

target_include_directories(SurvivalProject PRIVATE src)

target_link_libraries(SurvivalProject PRIVATE
    glad
    glfw
    glm
    FastNoiseLite
    stb
    imgui
    OpenAL
    OpenGL::GL
)

add_custom_command(TARGET SurvivalProject POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/assets
            $<TARGET_FILE_DIR:SurvivalProject>/assets
    COMMENT "Copying assets next to the executable"
)
