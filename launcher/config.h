#pragma once
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <stdexcept>

namespace settings {
struct Config {
    std::string name = "Unknown Soldier";
    bool friendsOnly = true;
    std::string password;
    std::string extra;
};
inline std::string Utf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (!n) throw std::runtime_error("Text contains an invalid character.");
    std::string result(n, '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), n, nullptr, nullptr);
    return result;
}
inline std::wstring Wide(const std::string& text) {
    if (text.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (!n) throw std::runtime_error("Settings contain invalid UTF-8. Correct the configuration file first.");
    std::wstring result(n, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), n);
    return result;
}
inline void Validate(const Config& c) {
    if (c.name.empty() || c.name.size() > 15)
        throw std::runtime_error("Choose a player name of 1-15 UTF-8 bytes (up to 15 English characters).");
    if (c.password.size() > 1023)
        throw std::runtime_error("Network password is too long (maximum 1023 UTF-8 bytes).");
    for (const auto* s : {&c.name, &c.password})
        if (s->find_first_of("\r\n") != std::string::npos || s->find('\0') != std::string::npos)
            throw std::runtime_error("Names and passwords cannot contain line breaks or null characters.");
    (void)Wide(c.name); (void)Wide(c.password);
}
inline Config Load(const std::filesystem::path& path) {
    Config c;
    if (!std::filesystem::exists(path)) return c;
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("Cannot read the settings file.");
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto equals = line.find('=');
        const auto key = line.substr(0, equals);
        const auto value = equals == std::string::npos ? std::string{} : line.substr(equals + 1);
        if (key == "playername") c.name = value;
        else if (key == "isfriendsonly") c.friendsOnly = value == "1";
        else if (key == "networkpassword") c.password = value;
        else c.extra += line + "\n"; // Preserve upstream options such as autoinstallmods.
    }
    return c;
}
inline void Save(const std::filesystem::path& path, const Config& c) {
    Validate(c);
    auto temp = path;
    temp += L"." + std::to_wstring(GetCurrentProcessId()) + L".tmp";
    try {
        std::ofstream file(temp, std::ios::binary | std::ios::trunc);
        if (!file) throw std::runtime_error("Cannot save settings. Extract the folder to a writable location.");
        file << "playername=" << c.name << "\nisfriendsonly=" << (c.friendsOnly ? 1 : 0)
             << "\nnetworkpassword=" << c.password << "\n" << c.extra;
        file.close();
        bool moved = false;
        for (int attempt = 0; file && attempt < 10; ++attempt) {
            if (MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                moved = true;
                break;
            }
            const auto error = GetLastError();
            if (error != ERROR_SHARING_VIOLATION && error != ERROR_ACCESS_DENIED) break;
            if (attempt < 9) Sleep(25);
        }
        if (!moved)
            throw std::runtime_error("Cannot replace the settings file. Check folder permissions.");
    } catch (...) {
        std::error_code error;
        std::filesystem::remove(temp, error);
        throw;
    }
}
}
