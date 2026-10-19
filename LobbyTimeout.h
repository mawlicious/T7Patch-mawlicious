#pragma once

namespace lobby_timeout
{
    void Apply();
    void Restore();
    bool Active();
    const char* Label();
    const char* ConfigPath();
}
