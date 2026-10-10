#include "CloudSync.h"
#include "sync/CredentialStore.h"
#include "storage/NoteRepository.h"
#include "common/StringUtils.h"
#include "res/resource.h"
#include <commctrl.h>
#include <commdlg.h>
#include <algorithm>

namespace anynote::ui {
CloudSync::CloudSync(HWND owner, storage::VaultManager& manager, std::function<storage::Database&()> database, std::function<void()> refresh)
    : m_owner(owner), m_manager(manager), m_database(std::move(database)), m_refresh(std::move(refresh)) {}
std::wstring CloudSync::CredentialPath() const { return (std::filesystem::path(m_manager.GetJsonPath()).parent_path() / L"credentials.dat").wstring(); }
std::filesystem::path CloudSync::JournalDirectory() const { return std::filesystem::path(m_manager.GetJsonPath()).parent_path() / L".sync-operations"; }
void CloudSync::EnsureClient(const std::string& url, const std::wstring& ca) {
    if (m_client && m_client->BaseUrl() == sync::SyncClient::NormalizeServerUrl(url) && m_ca == ca) return;
    auto replacement = std::make_unique<sync::SyncClient>(url, ca);
    m_client = std::move(replacement); m_ca = ca; m_account.clear(); m_accountId.clear(); m_vaults.clear(); m_selectedVault = {}; m_remote = {}; m_local = {};
}
void CloudSync::Command(int command) {
    if (command == ID_CLOUD_ACCOUNT) { DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_SYNC_ACCOUNT), m_owner, AccountProc, reinterpret_cast<LPARAM>(this)); return; }
    if (!IsLoggedIn()) { MessageBoxW(m_owner, L"请先通过“云同步 → 登录/注册”登录。", L"云同步", MB_OK | MB_ICONINFORMATION); return; }
    if (command == ID_CLOUD_UPLOAD) UploadCurrent(m_owner);
    if (command == ID_CLOUD_DOWNLOAD) { auto data = Manifest(m_owner, storage::NoteRepository(m_database()).GetVaultUid()); DownloadVault(m_owner, data.at("vault")); }
    if (command == ID_CLOUD_MANAGE) {
        RecoverInstalls(m_owner);
        DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_SYNC_MANAGE), m_owner, ManageProc, reinterpret_cast<LPARAM>(this));
    }
}
void CloudSync::AutoLogin() {
    if (m_manager.GetConfigString(L"Sync", L"AutoLogin", L"0") != L"1" || m_manager.GetConfigString(L"Sync", L"RememberPassword", L"0") != L"1") return;
    auto url = utils::WideToUtf8(m_manager.GetConfigString(L"Sync", L"ServerUrl"));
    auto account = utils::WideToUtf8(m_manager.GetConfigString(L"Sync", L"Username"));
    auto password = sync::CredentialStore::Load(CredentialPath(), url, account);
    if (password.empty()) return;
    try {
        EnsureClient(url, m_manager.GetConfigString(L"Sync", L"CaFile"));
        RunSyncTask(m_owner, L"自动登录", [&](sync::TransferControl& control) {
            auto result = m_client->Request("POST", "/auth/login", {{"username", account}, {"password", password}}, control);
            m_client->SetToken(result.at("token")); m_account = result.at("username"); m_accountId = result.at("account_id");
            m_capabilities = m_client->Request("GET", "/capabilities", {}, control);
        });
    } catch (...) { SecureZeroMemory(password.data(), password.size()); throw; }
    SecureZeroMemory(password.data(), password.size());
}
void CloudSync::AccountTab(HWND dialog) {
    int tab = TabCtrl_GetCurSel(GetDlgItem(dialog, IDC_SYNC_TAB));
    for (int id : {IDC_SYNC_NEW_PASSWORD, IDC_SYNC_NEW_LABEL}) ShowWindow(GetDlgItem(dialog, id), tab == 2 ? SW_SHOW : SW_HIDE);
    for (int id : {IDC_SYNC_REMEMBER, IDC_SYNC_AUTO}) ShowWindow(GetDlgItem(dialog, id), tab == 0 ? SW_SHOW : SW_HIDE);
    EnableWindow(GetDlgItem(dialog, IDC_SYNC_USER), tab != 2);
    EnableWindow(GetDlgItem(dialog, IDC_SYNC_EXECUTE), tab != 2 || IsLoggedIn());
    SetDlgItemTextW(dialog, IDC_SYNC_EXECUTE, tab == 0 ? L"登录" : tab == 1 ? L"注册" : L"修改密码");
    SetDlgItemTextW(dialog, IDC_SYNC_PASSWORD_LABEL, tab == 2 ? L"旧密码" : L"密码");
    auto status = IsLoggedIn() ? L"已登录：" + utils::Utf8ToWide(m_account) : L"尚未登录；改密仅登录后可用。";
    if (IsLoggedIn() && m_capabilities.contains("limits")) status += L"\r\n单库最多 " + std::to_wstring(m_capabilities.at("limits").at("nodes").get<int>()) + L" 节点，" + std::to_wstring(m_capabilities.at("limits").at("vault_bytes").get<int>() / (1024 * 1024)) + L" MiB。";
    SetDlgItemTextW(dialog, IDC_SYNC_STATUS, status.c_str());
    EnableWindow(GetDlgItem(dialog, IDC_SYNC_LOGOUT), IsLoggedIn());
}
void CloudSync::Authenticate(HWND dialog) {
    int tab = TabCtrl_GetCurSel(GetDlgItem(dialog, IDC_SYNC_TAB));
    auto url = sync::SyncClient::NormalizeServerUrl(utils::WideToUtf8(SyncControlText(dialog, IDC_SYNC_URL)));
    auto ca = SyncControlText(dialog, IDC_SYNC_CA);
    auto account = utils::WideToUtf8(SyncControlText(dialog, IDC_SYNC_USER));
    if (account.size() < 3 || account.size() > 32 || account.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") != std::string::npos) throw std::runtime_error("INVALID_CREDENTIALS");
    std::transform(account.begin(), account.end(), account.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (tab == 0 && IsLoggedIn() && (m_account != account || m_client->BaseUrl() != url || m_ca != ca)) {
        RunSyncTask(dialog, L"退出旧账号", [&](sync::TransferControl& control) { m_client->Request("POST", "/auth/logout", nlohmann::json::object(), control); });
        m_client->ClearToken(); m_account.clear(); m_accountId.clear(); m_vaults.clear(); m_local = {}; m_remote = {};
        sync::CredentialStore::Erase(CredentialPath());
    }
    EnsureClient(url, ca);
    if (tab == 2 && !IsLoggedIn()) throw std::runtime_error("UNAUTHORIZED");
    auto password = utils::WideToUtf8(SyncControlText(dialog, IDC_SYNC_PASSWORD));
    auto newPassword = utils::WideToUtf8(SyncControlText(dialog, IDC_SYNC_NEW_PASSWORD));
    struct Wipe { std::string& a; std::string& b; ~Wipe() { SecureZeroMemory(a.data(), a.size()); SecureZeroMemory(b.data(), b.size()); } } wipe{password, newPassword};
    if (tab == 0 && m_account != account) { m_client->ClearToken(); m_account.clear(); m_vaults.clear(); m_local = {}; m_remote = {}; sync::CredentialStore::Erase(CredentialPath()); }
    RunSyncTask(dialog, tab == 0 ? L"登录" : tab == 1 ? L"注册" : L"修改密码", [&](sync::TransferControl& control) {
        if (tab == 2) {
            m_client->Request("POST", "/auth/password", {{"old_password", password}, {"new_password", newPassword}}, control);
            m_client->ClearToken(); m_account.clear(); m_accountId.clear(); sync::CredentialStore::Erase(CredentialPath());
        } else {
            auto result = m_client->Request("POST", tab == 0 ? "/auth/login" : "/auth/register", {{"username", account}, {"password", password}}, control);
            if (tab == 0) { m_client->SetToken(result.at("token")); m_account = result.at("username"); m_accountId = result.at("account_id"); m_capabilities = m_client->Request("GET", "/capabilities", {}, control); }
        }
    });
    if (tab == 0) {
        bool remember = IsDlgButtonChecked(dialog, IDC_SYNC_REMEMBER) == BST_CHECKED;
        bool automatic = remember && IsDlgButtonChecked(dialog, IDC_SYNC_AUTO) == BST_CHECKED;
        if (remember) sync::CredentialStore::Save(CredentialPath(), url, account, password); else sync::CredentialStore::Erase(CredentialPath());
        for (const auto& [key, value] : std::vector<std::pair<std::wstring, std::wstring>>{{L"ServerUrl", utils::Utf8ToWide(url)}, {L"CaFile", ca},
            {L"Username", utils::Utf8ToWide(account)}, {L"RememberPassword", remember ? L"1" : L"0"}, {L"AutoLogin", automatic ? L"1" : L"0"}})
            if (!m_manager.SetConfigString(L"Sync", key, value)) throw std::runtime_error("CONFIG_SAVE_FAILED");
    }
    SetDlgItemTextW(dialog, IDC_SYNC_PASSWORD, L""); SetDlgItemTextW(dialog, IDC_SYNC_NEW_PASSWORD, L"");
    if (tab != 0) TabCtrl_SetCurSel(GetDlgItem(dialog, IDC_SYNC_TAB), 0);
    AccountTab(dialog);
    if (tab != 0) MessageBoxW(dialog, tab == 1 ? L"注册成功，请登录。" : L"密码已修改，全部会话已退出，请重新登录。", L"云同步", MB_OK | MB_ICONINFORMATION);
}
INT_PTR CALLBACK CloudSync::AccountProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<CloudSync*>(GetWindowLongPtrW(dialog, DWLP_USER));
    try {
        if (message == WM_INITDIALOG) {
            self = reinterpret_cast<CloudSync*>(lParam); SetWindowLongPtrW(dialog, DWLP_USER, lParam);
            int index = 0; for (const auto* name : {L"登录", L"注册", L"改密"}) { TCITEMW item{}; item.mask = TCIF_TEXT; item.pszText = const_cast<wchar_t*>(name); TabCtrl_InsertItem(GetDlgItem(dialog, IDC_SYNC_TAB), index++, &item); }
            SetDlgItemTextW(dialog, IDC_SYNC_URL, self->m_manager.GetConfigString(L"Sync", L"ServerUrl", L"https://localhost:8443").c_str());
            SetDlgItemTextW(dialog, IDC_SYNC_CA, self->m_manager.GetConfigString(L"Sync", L"CaFile", (std::filesystem::path(self->m_manager.GetExeDir()) / L"certs/server.crt").wstring()).c_str());
            SetDlgItemTextW(dialog, IDC_SYNC_USER, self->m_manager.GetConfigString(L"Sync", L"Username").c_str());
            for (int id : {IDC_SYNC_PASSWORD, IDC_SYNC_NEW_PASSWORD}) SendDlgItemMessageW(dialog, id, EM_SETLIMITTEXT, 256, 0);
            CheckDlgButton(dialog, IDC_SYNC_REMEMBER, self->m_manager.GetConfigString(L"Sync", L"RememberPassword", L"0") == L"1" ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(dialog, IDC_SYNC_AUTO, self->m_manager.GetConfigString(L"Sync", L"AutoLogin", L"0") == L"1" ? BST_CHECKED : BST_UNCHECKED);
            if (IsDlgButtonChecked(dialog, IDC_SYNC_REMEMBER) == BST_CHECKED) {
                auto password = sync::CredentialStore::Load(self->CredentialPath(), utils::WideToUtf8(SyncControlText(dialog, IDC_SYNC_URL)), utils::WideToUtf8(SyncControlText(dialog, IDC_SYNC_USER)));
                auto wide = utils::Utf8ToWide(password); SetDlgItemTextW(dialog, IDC_SYNC_PASSWORD, wide.c_str());
                SecureZeroMemory(password.data(), password.size()); SecureZeroMemory(wide.data(), wide.size() * sizeof(wchar_t));
            }
            self->AccountTab(dialog); return TRUE;
        }
        if (!self) return FALSE;
        if (message == WM_NOTIFY && reinterpret_cast<NMHDR*>(lParam)->idFrom == IDC_SYNC_TAB && reinterpret_cast<NMHDR*>(lParam)->code == TCN_SELCHANGE) { self->AccountTab(dialog); return TRUE; }
        if (message == WM_COMMAND) {
            switch (LOWORD(wParam)) {
            case IDCANCEL: EndDialog(dialog, IDCANCEL); return TRUE;
            case IDC_SYNC_EXECUTE: self->Authenticate(dialog); return TRUE;
            case IDC_SYNC_AUTO: if (IsDlgButtonChecked(dialog, IDC_SYNC_AUTO) == BST_CHECKED) CheckDlgButton(dialog, IDC_SYNC_REMEMBER, BST_CHECKED); return TRUE;
            case IDC_SYNC_REMEMBER: if (IsDlgButtonChecked(dialog, IDC_SYNC_REMEMBER) != BST_CHECKED) CheckDlgButton(dialog, IDC_SYNC_AUTO, BST_UNCHECKED); return TRUE;
            case IDC_SYNC_LOGOUT:
                try { RunSyncTask(dialog, L"退出登录", [&](sync::TransferControl& c) { self->m_client->Request("POST", "/auth/logout", nlohmann::json::object(), c); }); }
                catch (const std::exception& e) { SyncError(dialog, e); }
                self->m_client->ClearToken(); self->m_account.clear(); self->m_accountId.clear(); self->m_capabilities = {}; self->m_vaults.clear(); self->m_local = {}; self->m_remote = {};
                sync::CredentialStore::Erase(self->CredentialPath());
                if (!self->m_manager.SetConfigString(L"Sync", L"AutoLogin", L"0")) throw std::runtime_error("CONFIG_SAVE_FAILED");
                self->AccountTab(dialog); return TRUE;
            case IDC_SYNC_BROWSE: {
                wchar_t file[32768]{}; OPENFILENAMEW ofn{sizeof(ofn)}; ofn.hwndOwner = dialog; ofn.lpstrFile = file; ofn.nMaxFile = 32768;
                ofn.lpstrFilter = L"证书 (*.crt;*.pem)\0*.crt;*.pem\0所有文件\0*.*\0"; ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
                if (GetOpenFileNameW(&ofn)) SetDlgItemTextW(dialog, IDC_SYNC_CA, file); return TRUE;
            }
            }
        }
    } catch (const std::exception& error) { SyncError(dialog, error); if (self) self->AccountTab(dialog); return TRUE; }
    return FALSE;
}
}
