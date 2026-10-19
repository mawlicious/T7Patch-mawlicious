#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
namespace { LONG initialized = 0; }
extern "C" __declspec(dllexport) DWORD WINAPI InitializeMawlicious(LPVOID) {
    InterlockedIncrement(&initialized);
    return 0;
}
extern "C" __declspec(dllexport) DWORD WINAPI InitializationCount() {
    return InterlockedCompareExchange(&initialized, 0, 0);
}
BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        const auto suffix = std::to_wstring(GetCurrentProcessId());
        const auto loading = OpenEventW(EVENT_MODIFY_STATE, FALSE, (L"Local\\MawliciousTestLoading" + suffix).c_str());
        const auto release = OpenEventW(SYNCHRONIZE, FALSE, (L"Local\\MawliciousTestRelease" + suffix).c_str());
        if (!loading || !release) return FALSE;
        SetEvent(loading);
        const auto result = WaitForSingleObject(release, 10000);
        CloseHandle(loading); CloseHandle(release);
        if (result != WAIT_OBJECT_0) return FALSE;
    }
    return TRUE;
}
