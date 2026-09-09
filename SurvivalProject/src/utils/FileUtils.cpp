#include "FileUtils.h"

#include <fstream>
#include <sstream>
#include <iostream>

namespace FileUtils
{
    // Reads the entire text file into a std::string.
    // Returns an empty string on failure.
    std::string ReadTextFile(const std::filesystem::path& path)
    {
        std::ifstream file(path);

        if (!file)
        {
            std::cerr << "Failed to open file: " << path << '\n';
            return {};
        }

        std::stringstream buffer;
        buffer << file.rdbuf();

        return buffer.str();
    }
}