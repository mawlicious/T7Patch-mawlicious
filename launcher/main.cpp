#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <filesystem>
#include <fstream>
#include <atomic>
#include <mutex>
#include <thread>
#include "config.h"
#include <vector>
#include <cstring>
#include <stdexcept>
#include "../GameBuild.h"

namespace fs = std::filesystem;

struct Handle {
    HANDLE value;
    explicit Handle(HANDLE h) : value(h) {}
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(Handle&& other) noexcept : value(other.value) { other.value = nullptr; }
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

DWORD RemoteCall(HANDLE process, uintptr_t fn, void* argument, HANDLE stop) {
    Handle thread(CreateRemoteThread(process, nullptr, 0,
        reinterpret_cast<LPTHREAD_START_ROUTINE>(fn), argument, 0, nullptr));
    if (!thread.value) throw std::runtime_error("Could not start patch initialization in BO3.");
    // Do not free remote arguments while a remote thread could still use them.
    HANDLE waits[]{thread.value, stop};
    if (WaitForMultipleObjects(2, waits, FALSE, 60000) != WAIT_OBJECT_0)
        throw std::runtime_error("Patch initialization did not finish within 60 seconds. Restart BO3 before retrying.");
    DWORD code = 0;
    if (!GetExitCodeThread(thread, &code)) throw std::runtime_error("Cannot read initialization result.");
    return code;
}

struct Game {
    uintptr_t base;
    bo3::Build build;
};
Game Inspect(HANDLE process, DWORD pid) {
    Handle modules(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid));
    MODULEENTRY32W game{sizeof(game)};
    if (!Module32FirstW(modules, &game)) return {}; // Loader may still be starting.
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    SIZE_T read = 0;
    if (!ReadProcessMemory(process, game.modBaseAddr, &dos, sizeof(dos), &read) ||
        read != sizeof(dos) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0 || dos.e_lfanew > 1048576 ||
        !ReadProcessMemory(process, game.modBaseAddr + dos.e_lfanew, &nt, sizeof(nt), &read) || read != sizeof(nt) ||
        nt.Signature != IMAGE_NT_SIGNATURE || nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64)
        return {};
    const auto stamp = nt.FileHeader.TimeDateStamp;
    const auto size = nt.OptionalHeader.SizeOfImage;
    const auto base = reinterpret_cast<uintptr_t>(game.modBaseAddr);
    if (stamp == bo3::February2026TimeDateStamp && (size == bo3::February2026RetailImageSize || size == bo3::February2026DumpImageSize))
        return {base, bo3::Build::February2026};
    if (stamp == bo3::September2026TimeDateStamp && (size == bo3::September2026RetailImageSize || size == bo3::September2026DumpImageSize))
        return {base, bo3::Build::September2026};
    throw std::runtime_error("Unsupported BO3 build. Use the February/September 2026 Windows Steam update.");
}

bool ReadPointer(HANDLE process, uintptr_t address, uintptr_t& value) {
    SIZE_T size = 0;
    return ReadProcessMemory(process, reinterpret_cast<void*>(address), &value, sizeof(value), &size) && size == sizeof(value) && value;
}
bool Ready(HANDLE process, DWORD pid, const Game& game) {
    // These are the globals the upstream initializer immediately dereferences.
    // Wait for Steam interfaces, player data and the actual game window, rather
    // than injecting the moment BlackOps3.exe appears during Steam startup.
    uintptr_t pointer = 0, value = 0;
    for (uintptr_t rva : {0x10B3DC20ull, 0x10B3DC30ull, 0x10B3DC40ull, 0x10B3DC50ull})
        if (!ReadPointer(process, game.base + rva, pointer) || !ReadPointer(process, pointer, value)) return false;
    if (!ReadPointer(process, game.base + 0x3390190, pointer) || !ReadPointer(process, pointer, value)) return false;
    if (!ReadPointer(process, game.base + 0x9B35878, pointer) ||
        !ReadPointer(process, pointer + 1384, pointer) || !ReadPointer(process, pointer, value)) return false;
    for (uintptr_t rva : {0x1686ED20ull, 0xA0378B8ull})
        if (!ReadPointer(process, game.base + rva, pointer) || !ReadPointer(process, pointer, value)) return false;
    struct WindowCheck { DWORD pid; bool found; } check{pid, false};
    EnumWindows([](HWND window, LPARAM parameter) -> BOOL {
        auto& check = *reinterpret_cast<WindowCheck*>(parameter);
        DWORD owner = 0; GetWindowThreadProcessId(window, &owner);
        if (owner == check.pid && IsWindowVisible(window) && !GetWindow(window, GW_OWNER)) {
            check.found = true; return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&check));
    return check.found;
}

DWORD Attach(HANDLE process, DWORD pid, const fs::path& dll, HANDLE stop) {
    // Resolve the initializer before mutating the target process.
    auto local = LoadLibraryExW(dll.c_str(), nullptr, DONT_RESOLVE_DLL_REFERENCES);
    if (!local) throw std::runtime_error("Cannot read T7Patch-mawlicious.dll. Extract the entire release ZIP.");
    auto init = GetProcAddress(local, "InitializeMawlicious");
    const auto initRva = reinterpret_cast<uintptr_t>(init) - reinterpret_cast<uintptr_t>(local);
    FreeLibrary(local);
    if (!init) throw std::runtime_error("Wrong patch DLL: initializer missing. Extract the new ZIP into a fresh folder.");
    const auto load = RemoteLoadLibrary(pid);
    if (!load) throw std::runtime_error("Cannot locate the Windows library loader in BO3.");
    const auto bytes = (dll.wstring().size() + 1) * sizeof(wchar_t);
    void* remote = VirtualAllocEx(process, nullptr, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    SIZE_T written = 0;
    if (!remote) throw std::runtime_error("Cannot allocate memory for the patch path.");
    if (!WriteProcessMemory(process, remote, dll.c_str(), bytes, &written) || written != bytes) {
        VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        throw std::runtime_error("Cannot copy the patch path into BO3.");
    }
    RemoteCall(process, load, remote, stop);
    VirtualFreeEx(process, remote, 0, MEM_RELEASE);
    const auto base = FindPatch(pid, dll);
    if (!base) throw std::runtime_error("Windows could not load the patch DLL. Restart BO3 before retrying.");
    return RemoteCall(process, base + initRva, nullptr, stop);
}

struct App {
    fs::path folder;
    settings::Config config;
    HWND window = nullptr, name = nullptr, password = nullptr, friends = nullptr;
    HWND connection = nullptr, saved = nullptr;
    HFONT font = nullptr, titleFont = nullptr;
    Handle stop{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    std::thread worker;
    std::atomic<bool> configReady{false};
    std::mutex mutex;
    std::wstring status = L"Waiting for BO3. Open the game through Steam.";
    bool initializing = true;

    void Status(const std::wstring& text) { std::lock_guard lock(mutex); status = text; }
    void Watch() {
        try {
            const auto dll = folder / L"T7Patch-mawlicious.dll";
            if (!fs::exists(dll)) throw std::runtime_error("T7Patch-mawlicious.dll is missing. Extract the entire release ZIP.");
            while (WaitForSingleObject(stop, 1000) == WAIT_TIMEOUT) {
                const auto pid = FindGame();
                if (!pid) { Status(L"Waiting for BO3. Open the game through Steam."); continue; }
                Handle process(OpenProcess(SYNCHRONIZE | PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
                    PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ, FALSE, pid));
                if (!process.value) {
                    Status(L"Cannot access BO3. Run the launcher and BO3 with the same permissions.");
                    continue;
                }
                bool finished = false;
                while (WaitForSingleObject(stop, 1000) == WAIT_TIMEOUT && WaitForSingleObject(process, 0) == WAIT_TIMEOUT) {
                    if (finished) continue; // Never initialize twice in the same process.
                    if (!configReady.load()) { Status(L"Save valid settings to enable automatic attachment."); continue; }
                    try {
                        if (FindPatch(pid, dll)) {
                            Status(L"T7Patch-mawlicious is already loaded. Confirm 'join info 30s' in game.");
                            finished = true; continue;
                        }
                        const auto game = Inspect(process, pid);
                        if (!game.base || !Ready(process, pid, game)) {
                            Status(L"BO3 detected. Waiting for the game to initialize..."); continue;
                        }
                        Status(L"Attaching T7Patch-mawlicious...");
                        const auto result = Attach(process, pid, dll, stop);
                        finished = true;
                        if (result == 0) Status(L"Attached. Lobby-info timeout verified at 30 seconds.");
                        else if (result == 3) Status(L"Patch attached, but 30s is NOT ACTIVE. Check the in-game label.");
                        else Status(L"Patch initialization failed. Restart BO3 before trying again.");
                    } catch (const std::exception& error) {
                        finished = true;
                        Status(settings::Wide(error.what()));
                    }
                }
                Status(L"Waiting for BO3. Open the game through Steam.");
            }
        } catch (const std::exception& error) { Status(settings::Wide(error.what())); }
    }
    void Save() {
        KillTimer(window, 2);
        try {
            wchar_t n[64]{}, p[2048]{};
            GetWindowTextW(name, n, 64); GetWindowTextW(password, p, 2048);
            auto next = config;
            next.name = settings::Utf8(n); next.password = settings::Utf8(p);
            next.friendsOnly = SendMessageW(friends, BM_GETCHECK, 0, 0) == BST_CHECKED;
            settings::Save(folder / L"t7patch-mawlicious.conf", next);
            config = next;
            configReady.store(true);
            SetWindowTextW(saved, L"Settings saved. Changes also apply while BO3 is running.");
        } catch (const std::exception& error) {
            configReady.store(false);
            SetWindowTextW(saved, settings::Wide(error.what()).c_str());
        }
    }
    ~App() {
        SetEvent(stop);
        if (worker.joinable()) worker.join();
        if (font) DeleteObject(font);
        if (titleFont) DeleteObject(titleFont);
    }
};

HWND Control(App& app, const wchar_t* kind, const wchar_t* text, DWORD style,
             int x, int y, int width, int height, int id = 0, bool border = false) {
    const auto control = CreateWindowExW(border ? WS_EX_CLIENTEDGE : 0, kind, text,
        WS_CHILD | WS_VISIBLE | style, x, y, width, height, app.window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(app.font), TRUE);
    return control;
}
LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM w, LPARAM l) {
    auto* app = reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        app = static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
        app->window = window; SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }
    if (!app) return DefWindowProcW(window, message, w, l);
    switch (message) {
    case WM_CREATE: {
        app->font = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        app->titleFont = CreateFontW(-25, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        const auto title = Control(*app, L"STATIC", L"T7Patch-mawlicious", 0, 24, 20, 475, 35);
        SendMessageW(title, WM_SETFONT, reinterpret_cast<WPARAM>(app->titleFont), TRUE);
        Control(*app, L"STATIC", L"3.09  |  30-second lobby-info wait  |  based on serious / Scroptss", 0, 24, 62, 480, 24);
        Control(*app, L"STATIC", L"Player name", 0, 24, 104, 160, 24);
        app->name = Control(*app, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, 24, 130, 480, 29, 101, true);
        SendMessageW(app->name, EM_SETLIMITTEXT, 15, 0);
        SetWindowTextW(app->name, settings::Wide(app->config.name).c_str());
        app->friends = Control(*app, L"BUTTON", L"Friends only", WS_TABSTOP | BS_AUTOCHECKBOX, 24, 174, 230, 26, 102);
        SendMessageW(app->friends, BM_SETCHECK, app->config.friendsOnly ? BST_CHECKED : BST_UNCHECKED, 0);
        Control(*app, L"STATIC", L"Network password (leave blank for none)", 0, 24, 214, 480, 24);
        app->password = Control(*app, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL | ES_PASSWORD, 24, 240, 480, 29, 103, true);
        SendMessageW(app->password, EM_SETLIMITTEXT, 1023, 0);
        SetWindowTextW(app->password, settings::Wide(app->config.password).c_str());
        Control(*app, L"BUTTON", L"Save settings", WS_TABSTOP | BS_DEFPUSHBUTTON, 24, 286, 135, 32, 104);
        Control(*app, L"BUTTON", L"Show password", WS_TABSTOP | BS_AUTOCHECKBOX, 330, 288, 174, 28, 105);
        app->saved = Control(*app, L"STATIC", L"", 0, 24, 333, 480, 46, 106);
        Control(*app, L"STATIC", L"Open this first, then launch BO3 through Steam.\nKeep this window open for automatic attachment after each restart.", 0, 24, 390, 480, 50);
        app->connection = Control(*app, L"STATIC", app->status.c_str(), 0, 24, 454, 480, 74, 107);
        Control(*app, L"STATIC", L"In game: T7Patch-mawlicious 3.09 - join info 30s", 0, 24, 540, 480, 25);
        app->initializing = false;
        app->Save();
        SetTimer(window, 1, 250, nullptr);
        app->worker = std::thread([app] { app->Watch(); });
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(w) == 104 && HIWORD(w) == BN_CLICKED) { app->Save(); return 0; }
        if (LOWORD(w) == 105 && HIWORD(w) == BN_CLICKED) {
            const bool show = SendMessageW(reinterpret_cast<HWND>(l), BM_GETCHECK, 0, 0) == BST_CHECKED;
            SendMessageW(app->password, EM_SETPASSWORDCHAR, show ? 0 : L'\x25CF', 0);
            InvalidateRect(app->password, nullptr, TRUE); return 0;
        }
        if (!app->initializing && ((HIWORD(w) == EN_CHANGE && (LOWORD(w) == 101 || LOWORD(w) == 103)) ||
            (LOWORD(w) == 102 && HIWORD(w) == BN_CLICKED))) {
            app->configReady.store(false);
            SetWindowTextW(app->saved, L"Saving settings...");
            SetTimer(window, 2, 400, nullptr); return 0;
        }
        break;
    case WM_TIMER:
        if (w == 2) app->Save();
        else if (w == 1) {
            std::lock_guard lock(app->mutex);
            SetWindowTextW(app->connection, app->status.c_str());
        }
        return 0;
    case WM_CLOSE:
        app->Save();
        SetEvent(app->stop);
        DestroyWindow(window); return 0;
    case WM_DESTROY:
        KillTimer(window, 1); KillTimer(window, 2);
        PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(window, message, w, l);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    try {
        Handle single(CreateMutexW(nullptr, FALSE, L"Local\\T7Patch-mawlicious-launcher"));
        if (!single.value) throw std::runtime_error("Cannot initialize the launcher.");
        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            const auto existing = FindWindowW(L"T7Patch-mawlicious", nullptr);
            if (existing) { ShowWindow(existing, SW_RESTORE); SetForegroundWindow(existing); }
            return 0;
        }
        App app;
        if (!app.stop.value) throw std::runtime_error("Cannot initialize the game watcher.");
        wchar_t self[32768]{};
        const auto count = GetModuleFileNameW(nullptr, self, 32768);
        if (!count || count >= 32768) throw std::runtime_error("Cannot locate launcher folder.");
        app.folder = fs::path(self).parent_path();
        app.config = settings::Load(app.folder / L"t7patch-mawlicious.conf");
        // Validate encoding before WM_CREATE so errors cannot escape a WndProc.
        (void)settings::Wide(app.config.name); (void)settings::Wide(app.config.password);
        SetProcessDPIAware();
        WNDCLASSW klass{};
        klass.lpfnWndProc = WindowProc; klass.hInstance = instance;
        klass.lpszClassName = L"T7Patch-mawlicious"; klass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        klass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        klass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        if (!RegisterClassW(&klass)) throw std::runtime_error("Cannot register the launcher window.");
        RECT bounds{0, 0, 528, 588};
        const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
        AdjustWindowRect(&bounds, style, FALSE);
        const auto window = CreateWindowExW(WS_EX_CONTROLPARENT, klass.lpszClassName,
            L"T7Patch-mawlicious 3.09 - 30 seconds", style,
            CW_USEDEFAULT, CW_USEDEFAULT, bounds.right - bounds.left, bounds.bottom - bounds.top,
            nullptr, nullptr, instance, &app);
        if (!window) throw std::runtime_error("Cannot create the launcher window.");
        ShowWindow(window, show); UpdateWindow(window);
        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            if (!IsDialogMessageW(window, &message)) {
                TranslateMessage(&message); DispatchMessageW(&message);
            }
        }
        return 0;
    } catch (const std::exception& error) {
        MessageBoxW(nullptr, settings::Wide(error.what()).c_str(), L"T7Patch-mawlicious", MB_OK | MB_ICONERROR);
        return 1;
    }
}
