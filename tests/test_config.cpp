#define WIN32_LEAN_AND_MEAN
#include "../launcher/config.h"
#include <thread>
#include <iostream>
namespace fs = std::filesystem;
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
int main() {
    const auto folder = fs::temp_directory_path() /
        ("t7patch-mawlicious-lock-test-" + std::to_string(GetCurrentProcessId()));
    struct Cleanup {
        fs::path path;
        ~Cleanup() { std::error_code error; fs::remove_all(path, error); }
    } cleanup{folder};
    try {
        fs::create_directories(folder);
        const auto path = folder / L"t7patch-mawlicious.conf";
        settings::Config original;
        settings::Save(path, original);
        settings::Config updated;
        updated.name = "SavedDuringRead";
        {
            // Same MSVC ifstream sharing mode as the running patch DLL.
            std::ifstream reader(path);
            Require(reader.is_open(), "Cannot hold settings open");
            std::jthread release([&] { Sleep(100); reader.close(); });
            settings::Save(path, updated);
        }
        Require(settings::Load(path).name == updated.name,
            "Save did not recover when the reader released the file");
        {
            // A persistent lock must fail within the bounded retry period,
            // preserving the previous configuration and cleaning the temp file.
            std::ifstream reader(path);
            Require(reader.is_open(), "Cannot hold persistent reader");
            const auto start = GetTickCount64();
            bool rejected = false;
            try { settings::Save(path, original); }
            catch (const std::runtime_error&) { rejected = true; }
            Require(rejected, "Persistent sharing conflict was not reported");
            Require(GetTickCount64() - start < 5000, "Save retries were not bounded");
        }
        Require(settings::Load(path).name == updated.name,
            "Failed save replaced the previous configuration");
        for (const auto& entry : fs::directory_iterator(folder))
            Require(entry.path() == path, "Failed save left a temporary file");
        std::cout << "Transient reader conflict retries successfully; persistent conflict preserves settings and cleans temporary files.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Settings contention test failed: " << error.what() << "\n";
        return 1;
    }
}
