#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

const char* stage = "starting";
LONG WINAPI ReportFault(EXCEPTION_POINTERS* fault) {
    std::cerr << "Native test fault during " << stage << ": code 0x" << std::hex
        << fault->ExceptionRecord->ExceptionCode << " at "
        << fault->ExceptionRecord->ExceptionAddress << ", accessed 0x"
        << fault->ExceptionRecord->ExceptionInformation[1] << '\n';
    return EXCEPTION_EXECUTE_HANDLER;
}

void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

// Exercise the production DLL without adding test-only exports to its interface.
// MSVC's map supplies addresses; the fake player-data page is the only game
// memory prepare() reads. No detours or game code are installed/executed.
std::uintptr_t Symbol(HMODULE dll, const char* mapPath, const char* prefix) {
    std::ifstream map(mapPath);
    Require(map.is_open(), "Cannot open production DLL map");
    const auto base = reinterpret_cast<std::uintptr_t>(dll);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(dll);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    std::uintptr_t preferredBase = 0;
    for (std::string line; std::getline(map, line);) {
        const std::string preferredLabel = "Preferred load address is ";
        if (const auto at = line.find(preferredLabel); at != std::string::npos) {
            preferredBase = std::stoull(line.substr(at + preferredLabel.size()), nullptr, 16);
            continue;
        }
        std::istringstream fields(line);
        std::string section, name, address;
        if (fields >> section >> name >> address && name.starts_with(prefix)) {
            // Windows updates the mapped PE's ImageBase during relocation.
            // Map addresses must be rebased using the map's original base.
            Require(preferredBase != 0, "Missing preferred load address in map");
            const auto mapped = std::stoull(address, nullptr, 16);
            Require(mapped >= preferredBase &&
                mapped - preferredBase < nt->OptionalHeader.SizeOfImage,
                "Map symbol outside production image");
            return base + mapped - preferredBase;
        }
    }
    throw std::runtime_error(std::string("Missing production symbol: ") + prefix);
}

unsigned continuationCalls = 0;
PCONTEXT continuedContext = nullptr;
unsigned long long __fastcall RecordContinuation(PCONTEXT context, BOOLEAN alert) {
    Require(!alert, "Unexpected alert flag");
    ++continuationCalls;
    continuedContext = context;
    return 0;
}

int main(int argc, char** argv) {
    SetUnhandledExceptionFilter(ReportFault);
    try {
        Require(argc == 3, "Expected DLL and map paths");
        const auto path = std::filesystem::absolute(argv[1]);
        stage = "loading production DLL";
        const auto dll = LoadLibraryW(path.c_str());
        Require(dll != nullptr, "Cannot load production patch DLL");
        stage = "resolving production symbols";
        auto& compare = *reinterpret_cast<std::uintptr_t*>(Symbol(dll, argv[2], "?I_stricmp@Protection@@"));
        auto& resume = *reinterpret_cast<std::uintptr_t*>(Symbol(dll, argv[2], "?ZwContinue@Protection@@"));
        std::cerr << "DLL " << dll << ", compare " << &compare << ", resume " << &resume << '\n';
        Require(compare == 0 && resume == 0, "Unexpected initial dependency state");

        const auto gameBase = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        constexpr std::uintptr_t playerDataRva = 0x3390190;
        SYSTEM_INFO system{};
        GetSystemInfo(&system);
        const auto page = (gameBase + playerDataRva) &
            ~(static_cast<std::uintptr_t>(system.dwAllocationGranularity) - 1);
        void* fakeGamePage = VirtualAlloc(reinterpret_cast<void*>(page),
            system.dwAllocationGranularity, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        Require(fakeGamePage == reinterpret_cast<void*>(page), "Cannot allocate fake player-data page");
        std::uint64_t xuid = 123456789;
        *reinterpret_cast<std::uint64_t**>(gameBase + playerDataRva) = &xuid;

        const auto prepare = reinterpret_cast<bool(*)()>(Symbol(dll, argv[2], "?prepare@Protection@@"));
        stage = "preparing dependencies";
        std::cerr << "Preparing at " << reinterpret_cast<void*>(prepare) << '\n';
        Require(prepare(), "Dependency preparation failed");
        Require(compare == gameBase + 0x227CB20, "Comparison dependency was not initialized");
        Require(resume == reinterpret_cast<std::uintptr_t>(GetProcAddress(
            GetModuleHandleW(L"ntdll.dll"), "ZwContinue")), "Exception continuation was not resolved");

        // Reproduce the protection marker path from the text crash reports.
        // A recording continuation avoids resuming the synthetic CPU context.
        const auto realResume = resume;
        resume = reinterpret_cast<std::uintptr_t>(&RecordContinuation);
        auto& original = *reinterpret_cast<std::uint64_t*>(Symbol(dll, argv[2], "?Old_lobbymsgprints@Protection@@"));
        original = 0x12345678;
        std::uint64_t stack[16]{};
        CONTEXT context{};
        context.Rsp = reinterpret_cast<DWORD64>(stack);
        context.Rcx = context.Rbx = 0xFFEEDDCC44332212;
        EXCEPTION_RECORD exception{};
        exception.ExceptionCode = EXCEPTION_ACCESS_VIOLATION;
        const auto handler = reinterpret_cast<void(*)(PEXCEPTION_RECORD, PCONTEXT)>(
            Symbol(dll, argv[2], "?ExceptHook@Protection@@"));
        stage = "exercising protection marker handler";
        handler(&exception, &context);
        Require(continuationCalls == 1 && continuedContext == &context,
            "Protection exception did not reach continuation");
        Require(context.Rcx == original && context.Rbx == original,
            "Protection marker registers were not restored");
        stage = "unloading fixture";
        resume = realResume;
        VirtualFree(fakeGamePage, 0, MEM_RELEASE);
        FreeLibrary(dll);
        std::cout << "Production dependency initialization and marker continuation passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
