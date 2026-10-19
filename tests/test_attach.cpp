// Exercise the real loader/initializer sequence, using this process as the
// target and a harmless delayed test DLL instead of BO3.
#include "../launcher/main.cpp"
#include <stdexcept>
#include <iostream>
#include <memory>
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
int main() {
    try {
        const auto suffix = std::to_wstring(GetCurrentProcessId());
        Handle loading(CreateEventW(nullptr, TRUE, FALSE, (L"Local\\MawliciousTestLoading" + suffix).c_str()));
        Handle release(CreateEventW(nullptr, TRUE, FALSE, (L"Local\\MawliciousTestRelease" + suffix).c_str()));
        Handle closeStart(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        Handle closeDone(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        Handle attachReturned(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        Require(loading.value && release.value && closeStart.value && closeDone.value && attachReturned.value,
            "Cannot create fixture events");
        // Match production's real process handle, not a pseudo-handle.
        Handle process(OpenProcess(PROCESS_ALL_ACCESS, FALSE, GetCurrentProcessId()));
        Require(process.value != nullptr, "Cannot open fixture process");
        const auto dll = fs::absolute(L"attach-fixture-mawlicious.dll");
        DWORD result = 99;
        std::exception_ptr error;
        auto app = std::make_unique<App>();
        HANDLE duplicate = nullptr;
        Require(DuplicateHandle(GetCurrentProcess(), app->stop, GetCurrentProcess(), &duplicate,
            SYNCHRONIZE, FALSE, 0), "Cannot observe teardown stop event");
        Handle stopped(duplicate);

        // Start this thread BEFORE DllMain can hold the loader lock: starting
        // any new thread while the fixture is blocked there would itself wait.
        std::thread closer([&] {
            WaitForSingleObject(closeStart, INFINITE);
            app.reset(); // Real destructor signals stop and joins the worker.
            SetEvent(closeDone);
        });
        struct Cleanup {
            HANDLE release, closeStart;
            std::thread& closer;
            ~Cleanup() {
                SetEvent(release); SetEvent(closeStart);
                if (closer.joinable()) closer.join();
            }
        } cleanup{release, closeStart, closer};
        app->worker = std::thread([&] {
            try { result = Attach(process, GetCurrentProcessId(), dll); }
            catch (...) { error = std::current_exception(); }
            // Observe the operation returning separately from thread exit:
            // DLL_THREAD_DETACH also needs this process's loader lock.
            SetEvent(attachReturned);
        });
        Require(WaitForSingleObject(loading, 10000) == WAIT_OBJECT_0,
            "Fixture never entered DLL_PROCESS_ATTACH");
        SetEvent(closeStart);
        Require(WaitForSingleObject(stopped, 10000) == WAIT_OBJECT_0,
            "App teardown did not signal stop");
        // Hold DllMain blocked while shutdown is definitely in progress.
        // Cancelling attachment here must fail this check, even if the worker
        // cannot exit yet because DLL_THREAD_DETACH needs the loader lock.
        const auto operationWhileBlocked = WaitForSingleObject(attachReturned, 500);
        const auto teardownWhileBlocked = WaitForSingleObject(closeDone, 0);
        SetEvent(release);
        Require(WaitForSingleObject(closeDone, 10000) == WAIT_OBJECT_0,
            "Teardown did not complete after releasing the loader");
        closer.join();
        Require(operationWhileBlocked == WAIT_TIMEOUT,
            "Attachment returned on shutdown before loading/initialization completed");
        Require(teardownWhileBlocked == WAIT_TIMEOUT,
            "Teardown returned before loading/initialization completed");
        if (error) std::rethrow_exception(error);
        Require(result == 0, "Initializer returned failure");
        const auto module = GetModuleHandleW(dll.c_str());
        Require(module != nullptr, "Fixture module missing after attachment");
        const auto count = reinterpret_cast<DWORD(WINAPI*)()>(GetProcAddress(module, "InitializationCount"));
        Require(count && count() == 1, "Fixture initialization count must equal one");
        FreeLibrary(module);
        std::cout << "Shutdown blocks while DLL loading is held; initialization then completes exactly once.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Attachment regression failed: " << error.what() << "\n";
        return 1;
    }
}
