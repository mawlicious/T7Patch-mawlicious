// Exercise the real loader/initializer sequence, using this process as the
// target and a harmless delayed test DLL instead of BO3.
#include "../launcher/main.cpp"
#include <cassert>
#include <iostream>
int main() {
    const auto suffix = std::to_wstring(GetCurrentProcessId());
    Handle loading(CreateEventW(nullptr, TRUE, FALSE, (L"Local\\MawliciousTestLoading" + suffix).c_str()));
    Handle release(CreateEventW(nullptr, TRUE, FALSE, (L"Local\\MawliciousTestRelease" + suffix).c_str()));
    assert(loading.value && release.value);
    const auto dll = fs::absolute(L"attach-fixture-mawlicious.dll");
    DWORD result = 99;
    std::exception_ptr error;
    {
        App app;
        app.worker = std::thread([&] {
            try { result = Attach(GetCurrentProcess(), GetCurrentProcessId(), dll); }
            catch (...) { error = std::current_exception(); }
        });
        assert(WaitForSingleObject(loading, 10000) == WAIT_OBJECT_0);
        // Close the launcher while LoadLibrary is still running. App teardown
        // signals stop and must join the complete attachment transaction.
        SetEvent(app.stop);
        SetEvent(release);
    }
    if (error) std::rethrow_exception(error);
    assert(result == 0);
    const auto module = GetModuleHandleW(dll.c_str());
    assert(module);
    const auto count = reinterpret_cast<DWORD(WINAPI*)()>(GetProcAddress(module, "InitializationCount"));
    assert(count && count() == 1);
    FreeLibrary(module);
    std::cout << "Shutdown during DLL load completes initialization exactly once.\n";
}
