// Exercise the real loader/initializer sequence, using this process as the
// target and a harmless delayed test DLL instead of BO3.
#include "../launcher/main.cpp"
#include <stdexcept>
#include <iostream>
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
int main() {
    try {
    std::cerr << "Starting attachment shutdown regression test.\n";
    const auto suffix = std::to_wstring(GetCurrentProcessId());
    Handle loading(CreateEventW(nullptr, TRUE, FALSE, (L"Local\\MawliciousTestLoading" + suffix).c_str()));
    Handle release(CreateEventW(nullptr, TRUE, FALSE, (L"Local\\MawliciousTestRelease" + suffix).c_str()));
    Require(loading.value && release.value, "Cannot create fixture events");
    const auto dll = fs::absolute(L"attach-fixture-mawlicious.dll");
    DWORD result = 99;
    std::exception_ptr error;
    {
        App app;
        app.worker = std::thread([&] {
            try { std::cerr << "Starting Attach.\n"; result = Attach(GetCurrentProcess(), GetCurrentProcessId(), dll); std::cerr << "Attach returned " << result << ".\n"; }
            catch (...) { error = std::current_exception(); }
        });
        const auto waited = WaitForSingleObject(loading, 10000);
        std::cerr << "Fixture loading wait returned " << waited << ".\n";
        if (waited != WAIT_OBJECT_0) { SetEvent(release); app.worker.join(); if (error) std::rethrow_exception(error); }
        Require(waited == WAIT_OBJECT_0, "Fixture never entered DLL_PROCESS_ATTACH");
        // Close the launcher while LoadLibrary is still running. App teardown
        // signals stop and must join the complete attachment transaction.
        SetEvent(app.stop);
        SetEvent(release);
    }
    if (error) std::rethrow_exception(error);
    Require(result == 0, "Initializer returned failure");
    const auto module = GetModuleHandleW(dll.c_str());
    Require(module != nullptr, "Fixture module missing after attachment");
    const auto count = reinterpret_cast<DWORD(WINAPI*)()>(GetProcAddress(module, "InitializationCount"));
    Require(count && count() == 1, "Fixture initialization count must equal one");
    FreeLibrary(module);
    std::cout << "Shutdown during DLL load completes initialization exactly once.\n";
    return 0;
    } catch (const std::exception& error) {
        std::cerr << "Attachment regression failed: " << error.what() << "\n";
        return 1;
    }
}
