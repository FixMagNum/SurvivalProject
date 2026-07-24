#!/usr/bin/env bash
# Run this once from the project root.
set -e

git submodule add https://github.com/glfw/glfw.git external/glfw
git -C external/glfw checkout 3.4

git submodule add https://github.com/kcat/openal-soft.git external/openal-soft
git -C external/openal-soft checkout 1.25.1

git submodule add https://github.com/g-truc/glm.git external/glm
git -C external/glm checkout 1.0.1

git submodule add https://github.com/ocornut/imgui.git external/imgui
git -C external/imgui checkout v1.92.6

git submodule add https://github.com/Auburn/FastNoiseLite.git external/FastNoiseLite

git submodule add https://github.com/nothings/stb.git external/stb

echo
echo "Done. Submodules added under external/."