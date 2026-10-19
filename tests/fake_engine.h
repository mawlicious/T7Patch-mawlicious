#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <filesystem>
#include <cstdint>
#include <cstring>
#include <cstdlib>
inline bool testSupported = true;
inline bool testWritable = true;
inline bool testFound = true;
inline unsigned char testDvar[128]{};
constexpr uint32_t fnv1a_const(const char* s) {
    uint32_t h = 0x4B9ACE2F;
    while (*s) { auto c = *s++; if (c >= 'A' && c <= 'Z') c += 'a'-'A'; h = (h ^ c)*0x1000193; }
    return h*0x1000193;
}
inline void* FakeFind(const char*) { return testFound ? testDvar : nullptr; }
namespace bo3 {
    inline bool supported_build() { return testSupported; }
    inline uintptr_t address(uintptr_t rva) {
        if (rva != 0x2260870) std::abort();
        return reinterpret_cast<uintptr_t>(&FakeFind);
    }
}
inline uintptr_t Dvar_SetFromStringByName(const char* name, const char* value, bool create) {
    if (std::strcmp(name, "join_infoProbeWindow") || create) std::abort();
    if (testWritable) { int n = std::atoi(value); std::memcpy(testDvar + 0x28, &n, 4); }
    return 0;
}
