#pragma once

namespace lobby_timeout
{
    void Apply();
    void Restore();
    const char* Label();
    const char* ConfigPath();
}
