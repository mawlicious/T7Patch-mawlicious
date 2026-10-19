#pragma once
#include <filesystem>

namespace lobby_timeout
{
    void Apply();
    void Restore();
    bool Active();
    const char* Label();
    const std::filesystem::path& ConfigPath();
}
