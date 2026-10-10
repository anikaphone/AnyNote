#pragma once
#include "SyncDialogs.h"
#include "sync/SyncCoordinator.h"
#include "storage/SnapshotService.h"
#include "storage/VaultManager.h"
#include <memory>

namespace anynote::ui {
class CloudSync {
public:
    CloudSync(HWND owner, storage::VaultManager& manager, std::function<storage::Database&()> database, std::function<void()> refresh);
    void Command(int command);
    void AutoLogin();
    bool IsLoggedIn() const { return m_client && m_client->IsLoggedIn(); }
private:
    static INT_PTR CALLBACK AccountProc(HWND, UINT, WPARAM, LPARAM);
    static INT_PTR CALLBACK ManageProc(HWND, UINT, WPARAM, LPARAM);
    void AccountTab(HWND dialog);
    void Authenticate(HWND dialog);
    void EnsureClient(const std::string& url, const std::wstring& ca);
    void UploadCurrent(HWND owner);
    void DownloadVault(HWND owner, const nlohmann::json& vault, const std::optional<std::string>& root = {});
    nlohmann::json Manifest(HWND owner, const std::string& uid);
    void RefreshVaults(HWND dialog);
    void ShowDetails(HWND dialog, const nlohmann::json& vault);
    void RenderDetails(HWND dialog);
    void ContextMenu(HWND dialog, HWND source, POINT point);
    void QueryPending(HWND owner);
    void RecoverInstalls(HWND owner);
    std::wstring ChooseLocal(HWND owner, const std::string& uid);
    sync::Snapshot LocalSnapshot(HWND owner, const std::string& uid);
    nlohmann::json Write(HWND owner, const std::string& uid, const nlohmann::json& precondition,
        const std::function<nlohmann::json(sync::TransferControl&)>& action);
    std::filesystem::path JournalDirectory() const;
    std::wstring CredentialPath() const;
    std::filesystem::path BindingPath() const;
    bool IsBound(const std::string& uid) const;
    void RememberVersion(const std::string& uid, const nlohmann::json& version);
    HWND m_owner;
    storage::VaultManager& m_manager;
    std::function<storage::Database&()> m_database;
    std::function<void()> m_refresh;
    std::unique_ptr<sync::SyncClient> m_client;
    std::wstring m_ca;
    std::string m_account;
    std::string m_accountId;
    nlohmann::json m_capabilities;
    sync::SyncCoordinator m_coordinator;
    std::vector<nlohmann::json> m_vaults;
    nlohmann::json m_selectedVault;
    sync::Snapshot m_remote, m_local;
    std::vector<std::string> m_diffIds;
    int m_vaultSortColumn = 0, m_diffSortColumn = 0;
    bool m_vaultSortAscending = true, m_diffSortAscending = true;
    std::string m_treeSelection;
};
sync::Snapshot ManifestSnapshot(const nlohmann::json& manifest);
nlohmann::json SyncPrecondition(const nlohmann::json& vault);
}
