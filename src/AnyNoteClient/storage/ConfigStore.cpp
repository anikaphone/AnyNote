#include "ConfigStore.h"
#include "VaultFileLease.h"
#include "common/Identity.h"
#include "common/StringUtils.h"
#include "sync/SyncProtocol.h"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <set>
#include <stdexcept>
#include <vector>

namespace anynote::storage {
namespace {
constexpr size_t MaximumBytes = 8 * 1024 * 1024;
std::mutex configMutex;
void Check(bool valid, const char* error) { if (!valid) throw std::runtime_error(error); }
std::string ReadBytes(const std::filesystem::path& path) {
    const auto length = std::filesystem::file_size(path);
    Check(length <= MaximumBytes, "CONFIG_TOO_LARGE");
    std::ifstream file(path, std::ios::binary);
    std::string bytes(static_cast<size_t>(length), '\0');
    file.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    Check(file.good() || (file.eof() && file.gcount() == static_cast<std::streamsize>(bytes.size())), "CONFIG_READ_FAILED");
    Check(file.peek() == std::char_traits<char>::eof(), "CONFIG_CHANGED");
    return bytes;
}
nlohmann::json Parse(const std::string& bytes) {
    std::vector<std::set<std::string>> keys;
    auto callback = [&](int depth, nlohmann::json::parse_event_t event, nlohmann::json& parsed) {
        Check(depth <= 32, "CONFIG_TOO_DEEP");
        using Event = nlohmann::json::parse_event_t;
        if (event == Event::object_start) keys.emplace_back();
        else if (event == Event::object_end) keys.pop_back();
        else if (event == Event::key) Check(!keys.empty() && keys.back().insert(parsed.get<std::string>()).second, "CONFIG_DUPLICATE_KEY");
        return true;
    };
    auto value = nlohmann::json::parse(bytes, callback);
    Check(value.is_object(), "CONFIG_INVALID_ROOT");
    return value;
}
ConfigDocument ReadUnlocked(const std::wstring& path) {
    if (!std::filesystem::exists(path)) return {nlohmann::json::object(), "", false};
    auto bytes = ReadBytes(path);
    return {Parse(bytes), sync::Sha256(bytes), true};
}
void WriteNewFile(const std::wstring& path, const std::string& bytes) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    Check(file != INVALID_HANDLE_VALUE, "CONFIG_TEMP_CREATE_FAILED");
    DWORD written = 0;
    const bool good = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) != FALSE &&
        written == bytes.size() && FlushFileBuffers(file) != FALSE;
    CloseHandle(file);
    Check(good, "CONFIG_FLUSH_FAILED");
}
template<class F> std::vector<std::wstring> IniStrings(F read) {
    std::vector<wchar_t> buffer(1024);
    for (;;) {
        DWORD length = read(buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length < buffer.size() - 2) {
            std::vector<std::wstring> values;
            for (const wchar_t* item = buffer.data(); *item; item += wcslen(item) + 1) values.emplace_back(item);
            return values;
        }
        Check(buffer.size() < MaximumBytes / sizeof(wchar_t), "CONFIG_TOO_LARGE");
        buffer.resize(buffer.size() * 2);
    }
}
}
ConfigDocument ConfigStore::Read(const std::wstring& path) {
    std::lock_guard guard(configMutex);
    if (!std::filesystem::exists(path)) return {nlohmann::json::object(), "", false};
    auto lease = VaultFileLease::Acquire(path);
    return ReadUnlocked(path);
}
std::string ConfigStore::Write(const std::wstring& path, const nlohmann::json& value,
    const std::string& expectedFingerprint) {
    const auto bytes = value.dump(2) + '\n';
    Check(bytes.size() <= MaximumBytes && Parse(bytes) == value, "CONFIG_VALIDATION_FAILED");
    std::lock_guard guard(configMutex);
    auto lease = VaultFileLease::Acquire(path);
    const auto old = ReadUnlocked(path); // A corrupt JSON is never overwritten.
    Check(old.fingerprint == expectedFingerprint, "CONFIG_CHANGED_RELOAD_REQUIRED");
    if (old.exists && old.value == value) return old.fingerprint;
    const auto token = anynote::utils::Utf8ToWide(common::NewUuid());
    const std::wstring temporary = path + L"." + token + L".tmp";
    const std::wstring backupTemporary = path + L".bak." + token + L".tmp";
    try {
        WriteNewFile(temporary, bytes);
        Check(ReadBytes(temporary) == bytes, "CONFIG_TEMP_VERIFY_FAILED");
        if (old.exists) {
            // Publish a verified previous JSON before atomically replacing the current one.
            WriteNewFile(backupTemporary, ReadBytes(path));
            Check(MoveFileExW(backupTemporary.c_str(), (path + L".bak").c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE,
                "CONFIG_BACKUP_FAILED");
        }
        Check(MoveFileExW(temporary.c_str(), path.c_str(), (old.exists ? MOVEFILE_REPLACE_EXISTING : 0) | MOVEFILE_WRITE_THROUGH) != FALSE,
            "CONFIG_REPLACE_FAILED");
    } catch (...) {
        DeleteFileW(temporary.c_str());
        DeleteFileW(backupTemporary.c_str());
        throw;
    }
    return sync::Sha256(bytes);
}
nlohmann::json ConfigStore::ReadIni(const std::wstring& path) {
    nlohmann::json result = nlohmann::json::object();
    if (!std::filesystem::exists(path)) return result;
    Check(std::filesystem::file_size(path) <= MaximumBytes, "CONFIG_TOO_LARGE");
    auto sections = IniStrings([&](wchar_t* out, DWORD size) { return GetPrivateProfileSectionNamesW(out, size, path.c_str()); });
    for (const auto& section : sections) {
        nlohmann::json values = nlohmann::json::object();
        auto entries = IniStrings([&](wchar_t* out, DWORD size) { return GetPrivateProfileSectionW(section.c_str(), out, size, path.c_str()); });
        for (const auto& entry : entries) {
            auto equal = entry.find(L'=');
            Check(equal != std::wstring::npos, "CONFIG_INVALID_INI");
            const auto key = entry.substr(0, equal);
            std::vector<wchar_t> buffer(1024);
            for (;;) {
                DWORD count = GetPrivateProfileStringW(section.c_str(), key.c_str(), L"", buffer.data(), static_cast<DWORD>(buffer.size()), path.c_str());
                if (count < buffer.size() - 1) {
                    values[anynote::utils::WideToUtf8(key)] = anynote::utils::WideToUtf8(std::wstring(buffer.data(), count));
                    break;
                }
                Check(buffer.size() < MaximumBytes / sizeof(wchar_t), "CONFIG_TOO_LARGE");
                buffer.resize(buffer.size() * 2);
            }
        }
        result[anynote::utils::WideToUtf8(section)] = std::move(values);
    }
    return result;
}
}
