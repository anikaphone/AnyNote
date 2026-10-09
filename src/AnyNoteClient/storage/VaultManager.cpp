#include "VaultManager.h"
#include <windows.h>
#include <shlwapi.h>
#include <filesystem>
#include <algorithm>

namespace anynote::storage {

namespace {

bool PathsEqual(const std::wstring& p1, const std::wstring& p2) {
    std::error_code ec;
    std::filesystem::path path1 = std::filesystem::path(p1).lexically_normal();
    std::filesystem::path path2 = std::filesystem::path(p2).lexically_normal();
    return _wcsicmp(path1.wstring().c_str(), path2.wstring().c_str()) == 0;
}

} // namespace

VaultManager::VaultManager() {
    wchar_t exePathBuf[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, exePathBuf, MAX_PATH);
    PathRemoveFileSpecW(exePathBuf);

    m_exeDir = exePathBuf;
    m_iniPath = m_exeDir + L"\\anynote.ini";
    m_notebooksDir = m_exeDir + L"\\notebooks";
}

VaultManager::VaultManager(const std::wstring& iniPath) : VaultManager() {
    if (!iniPath.empty()) {
        std::filesystem::path configPath(iniPath);
        if (configPath.has_parent_path()) {
            m_exeDir = configPath.parent_path().wstring();
        }
        m_iniPath = configPath.wstring();
        m_notebooksDir = (configPath.parent_path() / L"notebooks").wstring();
    }
}

void VaultManager::EnsureIniFileExists() {
    if (!std::filesystem::exists(m_iniPath)) {
        HANDLE hFile = CreateFileW(
            m_iniPath.c_str(),
            GENERIC_WRITE,
            0,
            nullptr,
            CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL,
            nullptr
        );
        if (hFile != INVALID_HANDLE_VALUE) {
            // 写入 UTF-16LE BOM 标识 (0xFF, 0xFE)，以便 Windows INI API 支持全 Unicode 编码
            unsigned char bom[2] = { 0xFF, 0xFE };
            DWORD written = 0;
            WriteFile(hFile, bom, sizeof(bom), &written, nullptr);
            CloseHandle(hFile);
        }
    }
}

std::wstring VaultManager::SanitizeFileName(const std::wstring& name) {
    std::wstring result = name;
    const wchar_t invalidChars[] = L"\\/:*?\"<>|";
    for (wchar_t& ch : result) {
        if (wcschr(invalidChars, ch) != nullptr || ch < 32) {
            ch = L'_';
        }
    }
    while (!result.empty() && (result.back() == L' ' || result.back() == L'.')) {
        result.pop_back();
    }
    if (result.empty()) {
        result = L"Notebook";
    }
    return result;
}

std::wstring VaultManager::GenerateNewVaultPath(const std::wstring& vaultName) const {
    std::wstring safeName = SanitizeFileName(vaultName);
    return m_notebooksDir + L"\\" + safeName + L".anynote";
}

bool VaultManager::Load() {
    m_vaults.clear();
    m_activeVaultPath.clear();
    m_isFirstTimeCreation = false;

    if (!std::filesystem::exists(m_iniPath)) {
        // 初次运行无 anynote.ini
        std::wstring legacyPath = m_exeDir + L"\\notebook.anynote";
        if (std::filesystem::exists(legacyPath)) {
            // 平滑兼容老版本 notebook.anynote
            m_vaults.push_back({ L"默认笔记本", legacyPath });
            m_activeVaultPath = legacyPath;
            m_isFirstTimeCreation = false;
        } else {
            // 全新环境：默认在 notebooks/ 创建“我的笔记”
            std::error_code ec;
            std::filesystem::create_directories(m_notebooksDir, ec);
            std::wstring defaultPath = m_notebooksDir + L"\\我的笔记.anynote";
            m_vaults.push_back({ L"我的笔记", defaultPath });
            m_activeVaultPath = defaultPath;
            m_isFirstTimeCreation = true;
        }
        Save();
        return true;
    }

    // 从 anynote.ini 读取配置
    wchar_t buf[MAX_PATH * 2] = {0};
    GetPrivateProfileStringW(
        L"General",
        L"ActiveVaultPath",
        L"",
        buf,
        static_cast<DWORD>(std::size(buf)),
        m_iniPath.c_str()
    );
    m_activeVaultPath = buf;

    int count = GetPrivateProfileIntW(L"Vaults", L"Count", 0, m_iniPath.c_str());
    for (int i = 0; i < count; ++i) {
        std::wstring keyName = L"Vault_" + std::to_wstring(i) + L"_Name";
        std::wstring keyPath = L"Vault_" + std::to_wstring(i) + L"_Path";

        wchar_t nameBuf[256] = {0};
        wchar_t pathBuf[MAX_PATH * 2] = {0};

        GetPrivateProfileStringW(L"Vaults", keyName.c_str(), L"", nameBuf, static_cast<DWORD>(std::size(nameBuf)), m_iniPath.c_str());
        GetPrivateProfileStringW(L"Vaults", keyPath.c_str(), L"", pathBuf, static_cast<DWORD>(std::size(pathBuf)), m_iniPath.c_str());

        if (pathBuf[0] != L'\0') {
            std::wstring name = nameBuf;
            if (name.empty()) {
                name = std::filesystem::path(pathBuf).stem().wstring();
            }
            m_vaults.push_back({ std::move(name), pathBuf });
        }
    }

    if (m_vaults.empty()) {
        std::wstring legacyPath = m_exeDir + L"\\notebook.anynote";
        if (std::filesystem::exists(legacyPath)) {
            m_vaults.push_back({ L"默认笔记本", legacyPath });
            m_activeVaultPath = legacyPath;
        } else {
            std::wstring defaultPath = m_notebooksDir + L"\\我的笔记.anynote";
            m_vaults.push_back({ L"我的笔记", defaultPath });
            m_activeVaultPath = defaultPath;
        }
        Save();
    } else if (m_activeVaultPath.empty() || FindVaultByPath(m_activeVaultPath) < 0) {
        m_activeVaultPath = m_vaults.front().path;
        Save();
    }

    return true;
}

bool VaultManager::Save() {
    EnsureIniFileExists();

    bool ok = true;
    if (!WritePrivateProfileStringW(
        L"General",
        L"ActiveVaultPath",
        m_activeVaultPath.c_str(),
        m_iniPath.c_str())) {
        ok = false;
    }

    // 配置文件可由用户编辑，负数不能转换为 size_t 后作为循环上界。
    int oldCount = GetPrivateProfileIntW(L"Vaults", L"Count", 0, m_iniPath.c_str());
    if (oldCount < 0) {
        oldCount = 0;
    }

    std::wstring countStr = std::to_wstring(m_vaults.size());
    if (!WritePrivateProfileStringW(L"Vaults", L"Count", countStr.c_str(), m_iniPath.c_str())) {
        ok = false;
    }

    for (size_t i = 0; i < m_vaults.size(); ++i) {
        std::wstring keyName = L"Vault_" + std::to_wstring(i) + L"_Name";
        std::wstring keyPath = L"Vault_" + std::to_wstring(i) + L"_Path";

        if (!WritePrivateProfileStringW(L"Vaults", keyName.c_str(), m_vaults[i].name.c_str(), m_iniPath.c_str())) {
            ok = false;
        }
        if (!WritePrivateProfileStringW(L"Vaults", keyPath.c_str(), m_vaults[i].path.c_str(), m_iniPath.c_str())) {
            ok = false;
        }
    }

    // 清理多余的历史旧键，防止库缩减时残留废弃项
    for (size_t i = m_vaults.size(); i < static_cast<size_t>(oldCount); ++i) {
        std::wstring keyName = L"Vault_" + std::to_wstring(i) + L"_Name";
        std::wstring keyPath = L"Vault_" + std::to_wstring(i) + L"_Path";
        WritePrivateProfileStringW(L"Vaults", keyName.c_str(), nullptr, m_iniPath.c_str());
        WritePrivateProfileStringW(L"Vaults", keyPath.c_str(), nullptr, m_iniPath.c_str());
    }

    // 刷新 Windows INI 缓存写入磁盘
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, m_iniPath.c_str());
    return ok;
}

const VaultItem* VaultManager::GetVault(size_t index) const {
    if (index < m_vaults.size()) {
        return &m_vaults[index];
    }
    return nullptr;
}

std::wstring VaultManager::GetActiveVaultPath() const {
    return m_activeVaultPath;
}

void VaultManager::SetActiveVaultPath(const std::wstring& path) {
    m_activeVaultPath = path;
    Save();
}

int VaultManager::GetActiveVaultIndex() const {
    return FindVaultByPath(m_activeVaultPath);
}

std::wstring VaultManager::GetActiveVaultDisplayName() const {
    int idx = GetActiveVaultIndex();
    if (idx >= 0 && idx < static_cast<int>(m_vaults.size())) {
        return m_vaults[idx].name;
    }
    if (!m_activeVaultPath.empty()) {
        return std::filesystem::path(m_activeVaultPath).stem().wstring();
    }
    return L"未选择库";
}

int VaultManager::FindVaultByPath(const std::wstring& path) const {
    for (size_t i = 0; i < m_vaults.size(); ++i) {
        if (PathsEqual(m_vaults[i].path, path)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool VaultManager::AddVault(const std::wstring& name, const std::wstring& path) {
    if (path.empty()) return false;

    int idx = FindVaultByPath(path);
    if (idx >= 0) {
        if (!name.empty()) {
            m_vaults[idx].name = name;
        }
    } else {
        std::wstring displayName = name;
        if (displayName.empty()) {
            displayName = std::filesystem::path(path).stem().wstring();
        }
        m_vaults.push_back({ std::move(displayName), path });
    }

    Save();
    return true;
}

bool VaultManager::RemoveVault(size_t index) {
    if (index >= m_vaults.size()) return false;

    bool wasActive = PathsEqual(m_vaults[index].path, m_activeVaultPath);
    m_vaults.erase(m_vaults.begin() + index);

    if (wasActive) {
        if (!m_vaults.empty()) {
            m_activeVaultPath = m_vaults.front().path;
        } else {
            m_activeVaultPath.clear();
        }
    }

    Save();
    return true;
}

bool VaultManager::RenameVault(size_t index, const std::wstring& newName) {
    if (index >= m_vaults.size() || newName.empty()) return false;

    m_vaults[index].name = newName;
    Save();
    return true;
}

std::wstring VaultManager::GetConfigString(const std::wstring& section, const std::wstring& key, const std::wstring& defaultValue) const {
    if (!std::filesystem::exists(m_iniPath)) {
        return defaultValue;
    }
    wchar_t buf[256] = {0};
    GetPrivateProfileStringW(
        section.c_str(),
        key.c_str(),
        defaultValue.c_str(),
        buf,
        static_cast<DWORD>(std::size(buf)),
        m_iniPath.c_str()
    );
    return buf;
}

bool VaultManager::SetConfigString(const std::wstring& section, const std::wstring& key, const std::wstring& value) {
    EnsureIniFileExists();
    BOOL ok = WritePrivateProfileStringW(
        section.c_str(),
        key.c_str(),
        value.c_str(),
        m_iniPath.c_str()
    );
    return (ok != FALSE);
}

} // namespace anynote::storage
