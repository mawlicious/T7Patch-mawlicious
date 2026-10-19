#ifdef MAWLICIOUS_TIMEOUT_TEST
#include "tests/fake_engine.h"
#include "LobbyTimeout.h"
#else
#include "framework.h"
#endif
#include <atomic>
#include <cstring>

namespace lobby_timeout
{
    namespace
    {
        constexpr std::uint32_t NameHash = 0xD23BBF56;
        constexpr char Name[] = "join_infoProbeWindow";
        static_assert(fnv1a_const("join_infoprobewindow") == NameHash);

        // BOIII's September symbol map gives Dvar_FindVar RVA 0x22601B0,
        // and Dvar_SetFromStringByName 0x226A9E0. Both precede the same
        // 0x6C0 delta applied by Scroptss's February-based GameBuild mapping.
        using FindVar = void* (__fastcall*)(const char*);
        std::atomic<bool> applied{false};
        void* patchedDvar = nullptr;
        int previous = 0;

        bool ReadInt(const void* dvar, int& value)
        {
            if (!dvar) return false;
            __try
            {
                const auto* bytes = static_cast<const unsigned char*>(dvar);
                std::uint32_t hash, type;
                std::memcpy(&hash, bytes, sizeof(hash));
                std::memcpy(&type, bytes + 0x1C, sizeof(type));
                if (hash != NameHash || type != 6) return false; // DVAR_INT
                std::memcpy(&value, bytes + 0x28, sizeof(value));
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }
    }

    const char* ConfigPath()
    {
        // Settings belong to this distribution even when BO3's cwd differs.
        static const std::string path = [] {
            HMODULE module = nullptr;
            char filename[MAX_PATH]{};
            if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                    GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    reinterpret_cast<LPCSTR>(&ConfigPath), &module))
                return std::string("t7patch-mawlicious.conf");
            const auto length = GetModuleFileNameA(module, filename, MAX_PATH);
            if (!length || length >= MAX_PATH)
                return std::string("t7patch-mawlicious.conf");
            return (std::filesystem::path(filename).parent_path() /
                "t7patch-mawlicious.conf").string();
        }();
        return path.c_str();
    }

    void Apply()
    {
        if (applied.load() || !bo3::supported_build()) return;
        const auto find = reinterpret_cast<FindVar>(bo3::address(0x2260870));
        auto* dvar = find(Name);
        int value = 0;
        if (!ReadInt(dvar, value) || (value != 10000 && value != 30000))
        {
            OutputDebugStringA("T7Patch-mawlicious: join-info setting absent or unexpected; unchanged.\n");
            return;
        }
        previous = value;
        Dvar_SetFromStringByName(Name, "30000", false);
        if (ReadInt(dvar, value) && value == 30000)
        {
            patchedDvar = dvar;
            applied.store(true);
            OutputDebugStringA("T7Patch-mawlicious: join_infoProbeWindow verified at 30000 ms.\n");
        }
    }

    void Restore()
    {
        int current = 0;
        if (applied.exchange(false) && ReadInt(patchedDvar, current) && current == 30000)
            Dvar_SetFromStringByName(Name, previous == 10000 ? "10000" : "30000", false);
        patchedDvar = nullptr;
    }

    const char* Label()
    {
        int value = 0;
        return applied.load() && ReadInt(patchedDvar, value) && value == 30000
            ? "T7Patch-mawlicious 3.09 - join info 30s - by serious <3"
            : "T7Patch-mawlicious 3.09 - join info 30s NOT ACTIVE - by serious <3";
    }
}
