#pragma once

#include <string>
#include <vector>
#include <cstddef>

namespace anynote::storage {

struct VaultItem {
    std::wstring name;
    std::wstring path;
};

class VaultManager {
public:
    VaultManager();
    explicit VaultManager(const std::wstring& iniPath);
    ~VaultManager() = default;

    // 初始化并加载配置 (若初次启动则平滑迁移旧 notebook.anynote 或创建默认库)
    bool Load();

    // 保存当前配置到 anynote.ini
    bool Save();

    // 库列表访问
    const std::vector<VaultItem>& GetVaults() const noexcept { return m_vaults; }
    size_t GetVaultCount() const noexcept { return m_vaults.size(); }
    const VaultItem* GetVault(size_t index) const;

    // 当前激活库
    std::wstring GetActiveVaultPath() const;
    void SetActiveVaultPath(const std::wstring& path);
    int GetActiveVaultIndex() const;
    std::wstring GetActiveVaultDisplayName() const;

    // 添加库 (如果路径已存在则更新名称，否则追加)
    bool AddVault(const std::wstring& name, const std::wstring& path);

    // 从列表移除库 (不物理删除磁盘文件)
    bool RemoveVault(size_t index);

    // 重命名库显示名
    bool RenameVault(size_t index, const std::wstring& newName);

    // 查找库索引 (通过文件路径，忽略大小写)
    int FindVaultByPath(const std::wstring& path) const;

    // 根据库名称生成默认存放路径 (<exe_dir>\notebooks\<vaultName>.anynote)
    std::wstring GenerateNewVaultPath(const std::wstring& vaultName) const;

    // 获取路径信息
    const std::wstring& GetIniPath() const noexcept { return m_iniPath; }
    const std::wstring& GetNotebooksDir() const noexcept { return m_notebooksDir; }
    const std::wstring& GetExeDir() const noexcept { return m_exeDir; }
    bool IsFirstTimeCreation() const noexcept { return m_isFirstTimeCreation; }

    // INI 配置通用读取与写入
    std::wstring GetConfigString(const std::wstring& section, const std::wstring& key, const std::wstring& defaultValue = L"") const;
    bool SetConfigString(const std::wstring& section, const std::wstring& key, const std::wstring& value);

private:
    void EnsureIniFileExists();
    static std::wstring SanitizeFileName(const std::wstring& name);

    std::wstring m_exeDir;
    std::wstring m_iniPath;
    std::wstring m_notebooksDir;
    std::wstring m_activeVaultPath;
    std::vector<VaultItem> m_vaults;
    bool m_isFirstTimeCreation = false;
};

} // namespace anynote::storage
