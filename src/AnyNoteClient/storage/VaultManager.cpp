#include "VaultManager.h"
#include "ConfigStore.h"
#include "common/StringUtils.h"
#include <windows.h>
#include <filesystem>
#include <algorithm>
#include <map>
#include <stdexcept>

namespace anynote::storage {
namespace {
using Json = nlohmann::json;
bool PathsEqual(const std::wstring& a, const std::wstring& b) {
    return _wcsicmp(std::filesystem::path(a).lexically_normal().c_str(),
        std::filesystem::path(b).lexically_normal().c_str()) == 0;
}
void Check(bool valid, const char* error) { if (!valid) throw std::runtime_error(error); }
bool ValidUid(const std::string& uid) { return uid.size() == 16 && uid.find_first_not_of("0123456789abcdef") == std::string::npos; }
std::string ExistingKey(const Json& object, const std::wstring& key) {
    if (object.is_object()) for (auto it = object.begin(); it != object.end(); ++it)
        if (_wcsicmp(anynote::utils::Utf8ToWide(it.key()).c_str(), key.c_str()) == 0) return it.key();
    return anynote::utils::WideToUtf8(key);
}
std::wstring Setting(const Json& settings, const std::wstring& section, const std::wstring& key, const std::wstring& fallback) {
    auto found = settings.find(ExistingKey(settings, section));
    if (found == settings.end() || !found->is_object()) return fallback;
    auto value = found->find(ExistingKey(*found, key));
    return value != found->end() && value->is_string() ? anynote::utils::Utf8ToWide(value->get<std::string>()) : fallback;
}
}

VaultManager::VaultManager() {
    std::wstring executable(32768, L'\0');
    DWORD length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    executable.resize(length);
    m_exeDir = std::filesystem::path(executable).parent_path().wstring();
    m_iniPath = (std::filesystem::path(m_exeDir) / L"anynote.ini").wstring();
    m_jsonPath = (std::filesystem::path(m_exeDir) / L"anynote.json").wstring();
    m_notebooksDir = (std::filesystem::path(m_exeDir) / L"notebooks").wstring();
}
VaultManager::VaultManager(const std::wstring& iniPath) : VaultManager() {
    if (iniPath.empty()) return;
    auto path = std::filesystem::absolute(iniPath).lexically_normal();
    m_exeDir = path.parent_path().wstring();
    m_iniPath = path.replace_extension(L".ini").wstring();
    m_jsonPath = path.replace_extension(L".json").wstring();
    m_notebooksDir = (path.parent_path() / L"notebooks").wstring();
}
std::wstring VaultManager::ResolvePath(const std::string& text) const {
    Check(text.find('\0') == std::string::npos, "CONFIG_INVALID_PATH");
    if (text.empty()) return {};
    auto path = std::filesystem::path(anynote::utils::Utf8ToWide(text));
    if (path.is_relative()) path = std::filesystem::path(m_exeDir) / path;
    return path.lexically_normal().wstring();
}
std::string VaultManager::StoredPath(const std::wstring& text) const {
    if (text.empty()) return {};
    const auto path = std::filesystem::path(text).lexically_normal();
    const auto relative = path.lexically_relative(m_exeDir);
    if (!relative.empty() && *relative.begin() != L"..") return anynote::utils::WideToUtf8(relative.generic_wstring());
    return anynote::utils::WideToUtf8(path.wstring());
}
bool VaultManager::EnsureDocument() {
    if (m_loadFailed) return false;
    if (m_loaded) return true;
    try {
        const auto config = ConfigStore::Read(m_jsonPath);
        auto document = config.value;
        std::vector<VaultItem> vaults;
        std::wstring active;
        if (config.exists) {
            Check(document.contains("schema_version") && document["schema_version"].is_number_integer() && document["schema_version"] == 1,
                "CONFIG_UNSUPPORTED_VERSION");
            Check(document.at("vaults").is_array() && document.at("settings").is_object() &&
                document.at("active_vault_path").is_string(), "CONFIG_INVALID_STRUCTURE");
            active = ResolvePath(document.at("active_vault_path").get<std::string>());
            for (const auto& item : document.at("vaults")) {
                VaultItem vault;
                vault.name = anynote::utils::Utf8ToWide(item.at("name").get<std::string>());
                vault.path = ResolvePath(item.at("path").get<std::string>());
                Check(!vault.path.empty(), "CONFIG_INVALID_PATH");
                if (!item.at("uid").is_null()) { vault.uid = item.at("uid").get<std::string>(); Check(ValidUid(vault.uid), "CONFIG_INVALID_UID"); }
                Check(std::none_of(vaults.begin(), vaults.end(), [&](const auto& old) { return PathsEqual(old.path, vault.path); }), "CONFIG_DUPLICATE_PATH");
                vaults.push_back(std::move(vault));
            }
        } else {
            auto settings = ConfigStore::ReadIni(m_iniPath);
            document = {{"schema_version", 1}, {"settings", settings}, {"vaults", Json::array()}, {"vault_files", Json::object()},
                {"cloud", {{"server_url", ""}, {"certificate_path", "certs/server.crt"}, {"account", ""}, {"remember_password", false}, {"auto_login", false}}}};
            active = ResolvePath(anynote::utils::WideToUtf8(Setting(settings, L"General", L"ActiveVaultPath", L"")));
            const auto countText = Setting(settings, L"Vaults", L"Count", L"0");
            size_t parsed = 0;
            int count = std::stoi(countText, &parsed);
            Check(parsed == countText.size() && count >= 0 && count <= 10000, "CONFIG_INVALID_VAULT_COUNT");
            for (int i = 0; i < count; ++i) {
                auto prefix = L"Vault_" + std::to_wstring(i);
                auto path = ResolvePath(anynote::utils::WideToUtf8(Setting(settings, L"Vaults", prefix + L"_Path", L"")));
                if (path.empty()) continue;
                auto name = Setting(settings, L"Vaults", prefix + L"_Name", std::filesystem::path(path).stem().wstring());
                Check(std::none_of(vaults.begin(), vaults.end(), [&](const auto& old) { return PathsEqual(old.path, path); }), "CONFIG_DUPLICATE_PATH");
                vaults.push_back({name, path, {}});
            }
        }
        m_document = std::move(document); m_vaults = std::move(vaults); m_activeVaultPath = std::move(active);
        m_fingerprint = config.fingerprint; m_loaded = true; m_lastError.clear();
        return true;
    } catch (const std::exception& error) {
        m_loadFailed = true; m_lastError = error.what(); return false;
    }
}
bool VaultManager::Load() {
    m_loaded = false; m_loadFailed = false; m_isFirstTimeCreation = false;
    if (!EnsureDocument()) return false;
    if (m_vaults.empty()) {
        const auto legacy = (std::filesystem::path(m_exeDir) / L"notebook.anynote").wstring();
        if (std::filesystem::exists(legacy)) m_vaults.push_back({L"默认笔记本", legacy, {}});
        else {
            std::error_code error;
            std::filesystem::create_directories(m_notebooksDir, error);
            if (error) { m_lastError = "CONFIG_NOTEBOOK_DIRECTORY_FAILED"; return false; }
            m_vaults.push_back({L"我的笔记", GenerateNewVaultPath(L"我的笔记"), {}});
            m_isFirstTimeCreation = m_fingerprint.empty() && !std::filesystem::exists(m_iniPath);
        }
    }
    if (m_activeVaultPath.empty() || FindVaultByPath(m_activeVaultPath) < 0) m_activeVaultPath = m_vaults.front().path;
    const bool saved = Save();
    // A failed INI migration leaves the INI intact and usable. No stale INI
    // fallback is permitted when a JSON already exists or is damaged.
    return saved || (m_fingerprint.empty() && std::filesystem::exists(m_iniPath));
}
bool VaultManager::Save() {
    if (!EnsureDocument()) return false;
    try {
        auto document = m_document;
        Json vaults = Json::array(), files = Json::object();
        std::map<std::string, size_t> counts;
        for (const auto& item : m_vaults) if (!item.uid.empty()) ++counts[item.uid];
        for (const auto& item : m_vaults) {
            Json record = Json::object();
            if (m_document.contains("vaults")) for (const auto& old : m_document["vaults"])
                if (PathsEqual(ResolvePath(old.at("path").get<std::string>()), item.path)) { record = old; break; }
            record["name"] = anynote::utils::WideToUtf8(item.name); record["path"] = StoredPath(item.path);
            record["uid"] = item.uid.empty() ? Json(nullptr) : Json(item.uid); vaults.push_back(std::move(record));
            if (!item.uid.empty() && counts[item.uid] == 1) files[item.uid] = anynote::utils::WideToUtf8(std::filesystem::path(item.path).filename().wstring());
        }
        document["vaults"] = std::move(vaults); document["vault_files"] = std::move(files);
        document["active_vault_path"] = StoredPath(m_activeVaultPath);
        m_fingerprint = ConfigStore::Write(m_jsonPath, document, m_fingerprint);
        m_document = std::move(document); m_lastError.clear(); return true;
    } catch (const std::exception& error) { m_lastError = error.what(); return false; }
}
std::wstring VaultManager::SanitizeFileName(const std::wstring& name) {
    auto result = name;
    for (auto& c : result) if (c < 32 || wcschr(L"\\/:*?\"<>|", c)) c = L'_';
    while (!result.empty() && (result.back() == L' ' || result.back() == L'.')) result.pop_back();
    return result.empty() ? L"Notebook" : result;
}
std::wstring VaultManager::GenerateNewVaultPath(const std::wstring& name) const {
    return (std::filesystem::path(m_notebooksDir) / (SanitizeFileName(name) + L".anynote")).wstring();
}
const VaultItem* VaultManager::GetVault(size_t index) const { return index < m_vaults.size() ? &m_vaults[index] : nullptr; }
std::wstring VaultManager::GetActiveVaultPath() const { return m_activeVaultPath; }
int VaultManager::GetActiveVaultIndex() const { return FindVaultByPath(m_activeVaultPath); }
std::wstring VaultManager::GetActiveVaultDisplayName() const {
    const int index = GetActiveVaultIndex();
    if (index >= 0) return m_vaults[index].name;
    return m_activeVaultPath.empty() ? L"未选择库" : std::filesystem::path(m_activeVaultPath).stem().wstring();
}
int VaultManager::FindVaultByPath(const std::wstring& path) const {
    for (size_t i = 0; i < m_vaults.size(); ++i) if (PathsEqual(m_vaults[i].path, path)) return static_cast<int>(i);
    return -1;
}
bool VaultManager::SetActiveVaultPath(const std::wstring& path) {
    if (!EnsureDocument()) return false;
    const auto previous = m_activeVaultPath; m_activeVaultPath = path;
    if (Save()) return true;
    m_activeVaultPath = previous; return false;
}
bool VaultManager::AddVault(const std::wstring& name, const std::wstring& path, const std::string& verifiedUid) {
    if (path.empty() || (!verifiedUid.empty() && !ValidUid(verifiedUid)) || !EnsureDocument()) return false;
    const auto previous = m_vaults;
    const auto absolutePath = ResolvePath(anynote::utils::WideToUtf8(path));
    const int index = FindVaultByPath(absolutePath);
    if (index >= 0) {
        if (!name.empty()) m_vaults[index].name = name;
        if (!verifiedUid.empty()) m_vaults[index].uid = verifiedUid;
    }
    else m_vaults.push_back({name.empty() ? std::filesystem::path(path).stem().wstring() : name, absolutePath, verifiedUid});
    if (Save()) return true;
    m_vaults = previous; return false;
}
bool VaultManager::RemoveVault(size_t index) {
    if (!EnsureDocument() || index >= m_vaults.size()) return false;
    const auto previous = m_vaults; const auto active = m_activeVaultPath;
    bool selected = PathsEqual(m_vaults[index].path, active);
    m_vaults.erase(m_vaults.begin() + index);
    if (selected) m_activeVaultPath = m_vaults.empty() ? L"" : m_vaults.front().path;
    if (Save()) return true;
    m_vaults = previous; m_activeVaultPath = active; return false;
}
bool VaultManager::RenameVault(size_t index, const std::wstring& name) {
    if (!EnsureDocument() || index >= m_vaults.size() || name.empty()) return false;
    auto previous = m_vaults[index].name; m_vaults[index].name = name;
    if (Save()) return true;
    m_vaults[index].name = previous; return false;
}
std::wstring VaultManager::GetConfigString(const std::wstring& section, const std::wstring& key, const std::wstring& fallback) const {
    if (!m_loaded) { VaultManager copy(m_iniPath); return copy.EnsureDocument() ? copy.GetConfigString(section, key, fallback) : fallback; }
    return Setting(m_document.at("settings"), section, key, fallback);
}
bool VaultManager::SetConfigString(const std::wstring& section, const std::wstring& key, const std::wstring& value) {
    if (!EnsureDocument()) return false;
    auto previous = m_document;
    auto& settings = m_document["settings"];
    auto& group = settings[ExistingKey(settings, section)];
    if (group.is_null()) group = Json::object();
    if (!group.is_object()) { m_lastError = "CONFIG_INVALID_SECTION"; return false; }
    group[ExistingKey(group, key)] = anynote::utils::WideToUtf8(value);
    if (Save()) return true;
    m_document = previous; return false;
}
bool VaultManager::RegisterVaultIdentity(const std::wstring& path, const std::string& uid) {
    if (!EnsureDocument() || !ValidUid(uid)) return false;
    int index = FindVaultByPath(path); if (index < 0) return false;
    const auto old = m_vaults[index].uid; m_vaults[index].uid = uid;
    if (Save()) return true;
    m_vaults[index].uid = old; return false;
}
std::vector<size_t> VaultManager::FindVaultsByUid(const std::string& uid) const {
    std::vector<size_t> result;
    for (size_t i = 0; i < m_vaults.size(); ++i) if (m_vaults[i].uid == uid && !uid.empty()) result.push_back(i);
    return result;
}
}
