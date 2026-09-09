#pragma once

#include <filesystem>
#include <string>

namespace FileUtils
{
    [[nodiscard]] std::string ReadTextFile(const std::filesystem::path& path);
}