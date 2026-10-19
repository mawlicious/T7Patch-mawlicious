#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include <cstring>
#include <stdexcept>
#include "../GameBuild.h"

namespace fs = std::filesystem;

struct Handle {
    HANDLE value;
    explicit Handle(HANDLE h) : value(h) {}
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    operator HANDLE() const { return value; }
    Handle(const Handle&) = delete;
};

DWORD FindGame() {
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    PROCESSENTRY32W entry{sizeof(entry)};
    if (Process32FirstW(snapshot, &entry)) do {
        if (_wcsicmp(entry.szExeFile, L"BlackOps3.exe") == 0) return entry.th32ProcessID;
    } while (Process32NextW(snapshot, &entry));
    return 0;
}

// Resolve the actual owner of LoadLibraryW (which can be forwarded to
// KernelBase), then use its relative address in the remote system module.
uintptr_t RemoteLoadLibrary(DWORD pid) {
    auto fn = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
    HMODULE owner = nullptr;
    if (!fn || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(fn), &owner)) return 0;
    wchar_t name[MAX_PATH]{};
    if (!GetModuleFileNameW(owner, name, MAX_PATH)) return 0;
    const auto filename = fs::path(name).filename().wstring();
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid));
    MODULEENTRY32W entry{sizeof(entry)};
    if (Module32FirstW(snapshot, &entry)) do {
        if (_wcsicmp(entry.szModule, filename.c_str()) == 0)
            return reinterpret_cast<uintptr_t>(entry.modBaseAddr) +
                reinterpret_cast<uintptr_t>(fn) - reinterpret_cast<uintptr_t>(owner);
    } while (Module32NextW(snapshot, &entry));
    return 0;
}

uintptr_t FindPatch(DWORD pid, const fs::path& dll) {
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid));
    MODULEENTRY32W entry{sizeof(entry)};
    if (Module32FirstW(snapshot, &entry)) do {
        if (_wcsicmp(entry.szExePath, dll.c_str()) == 0)
            return reinterpret_cast<uintptr_t>(entry.modBaseAddr);
        if (_wcsicmp(entry.szModule, L"t7patch.dll") == 0 ||
            _wcsicmp(entry.szModule, L"T7Patch-mawlicious.dll") == 0)
            throw std::runtime_error("Another T7Patch is already loaded. Close BO3 and disable the other launcher/manual install first.");
    } while (Module32NextW(snapshot, &entry));
    return 0;
}

DWORD RemoteCall(HANDLE process, uintptr_t fn, void* argument) {
    Handle thread(CreateRemoteThread(process, nullptr, 0,
        reinterpret_cast<LPTHREAD_START_ROUTINE>(fn), argument, 0, nullptr));
    if (!thread.value) throw std::runtime_error("Could not start patch initialization in BO3.");
    // Do not free remote arguments while a remote thread could still use them.
    if (WaitForSingleObject(thread, 60000) != WAIT_OBJECT_0)
        throw std::runtime_error("Patch initialization did not finish within 60 seconds. Restart BO3 before retrying.");
    DWORD code = 0;
    if (!GetExitCodeThread(thread, &code)) throw std::runtime_error("Cannot read initialization result.");
    return code;
}

int wmain() {
    SetConsoleTitleW(L"T7Patch-mawlicious - 30 second lobby info timeout");
    try {
        wchar_t self[32768]{};
        const auto count = GetModuleFileNameW(nullptr, self, 32768);
        if (!count || count >= 32768) throw std::runtime_error("Cannot locate launcher folder.");
        const auto folder = fs::path(self).parent_path();
        const auto dll = folder / L"T7Patch-mawlicious.dll";
        if (!fs::exists(dll)) throw std::runtime_error("T7Patch-mawlicious.dll is missing. Extract the entire ZIP first.");
        std::cout << "T7Patch-mawlicious 3.09\nLobby information wait: 30 seconds\n\n"
                     "Do not run another T7Patch alongside this one.\n"
                     "Settings: t7patch-mawlicious.conf in this folder.\n"
                     "Start BO3 through Steam, wait at the main menu, then press Enter here.\n";
        std::cin.get();
        const auto pid = FindGame();
        if (!pid) throw std::runtime_error("BlackOps3.exe was not found. Start the game first.");
        if (FindPatch(pid, dll)) throw std::runtime_error("T7Patch-mawlicious is already loaded in this game session.");
        Handle process(OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
            PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ, FALSE, pid));
        if (!process.value) throw std::runtime_error("Cannot access BO3. Run BO3 and this launcher with the same permissions.");
        // Read only PE headers; reject unsupported versions before injection.
        Handle modules(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid));
        MODULEENTRY32W game{sizeof(game)};
        if (!Module32FirstW(modules, &game)) throw std::runtime_error("Cannot inspect BO3's build.");
        IMAGE_DOS_HEADER dos{};
        IMAGE_NT_HEADERS64 nt{};
        SIZE_T read = 0;
        if (!ReadProcessMemory(process, game.modBaseAddr, &dos, sizeof(dos), &read) ||
            read != sizeof(dos) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0 || dos.e_lfanew > 1048576 ||
            !ReadProcessMemory(process, game.modBaseAddr + dos.e_lfanew, &nt, sizeof(nt), &read) || read != sizeof(nt) ||
            nt.Signature != IMAGE_NT_SIGNATURE || nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64)
            throw std::runtime_error("Cannot verify BO3's PE headers.");
        const auto stamp = nt.FileHeader.TimeDateStamp;
        const auto size = nt.OptionalHeader.SizeOfImage;
        const bool supported =
            (stamp == bo3::February2026TimeDateStamp && (size == bo3::February2026RetailImageSize || size == bo3::February2026DumpImageSize)) ||
            (stamp == bo3::September2026TimeDateStamp && (size == bo3::September2026RetailImageSize || size == bo3::September2026DumpImageSize));
        if (!supported) throw std::runtime_error("Unsupported BO3 build. Use the February/September 2026 Steam Windows update.");
        const auto load = RemoteLoadLibrary(pid);
        if (!load) throw std::runtime_error("Cannot locate the Windows library loader in BO3.");
        const auto bytes = (dll.wstring().size() + 1) * sizeof(wchar_t);
        void* remote = VirtualAllocEx(process, nullptr, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        SIZE_T written = 0;
        if (!remote || !WriteProcessMemory(process, remote, dll.c_str(), bytes, &written) || written != bytes)
            throw std::runtime_error("Cannot copy the patch path into BO3.");
        RemoteCall(process, load, remote);
        VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        const auto base = FindPatch(pid, dll);
        if (!base) throw std::runtime_error("Windows could not load the patch DLL.");
        // Load as data/code without running DllMain; exports give RVAs only.
        auto local = LoadLibraryExW(dll.c_str(), nullptr, DONT_RESOLVE_DLL_REFERENCES);
        if (!local) throw std::runtime_error("Cannot read patch exports.");
        auto init = GetProcAddress(local, "InitializeMawlicious");
        if (!init) { FreeLibrary(local); throw std::runtime_error("Wrong patch DLL: initializer missing."); }
        auto initRva = reinterpret_cast<uintptr_t>(init) - reinterpret_cast<uintptr_t>(local);
        FreeLibrary(local);
        if (RemoteCall(process, base + initRva, nullptr) != 0)
            throw std::runtime_error("Patch initialization failed. Restart BO3; this executable may be unsupported.");
        std::cout << "\nInitialization finished. Verify the in-game top-right label says:\n"
                     "T7Patch-mawlicious 3.09 - join info 30s\n"
                     "If it says NOT ACTIVE, the timeout change did not apply.\n"
                     "Keep the game running; you can close this launcher.\n";
    } catch (const std::exception& error) {
        std::cerr << "\nT7Patch-mawlicious: " << error.what() << "\n";
        std::cerr << "Press Enter to close.\n"; std::cin.get(); return 1;
    }
    std::cout << "Press Enter to close.\n"; std::cin.get(); return 0;
}
