#include "CloudSync.h"
#include "storage/NoteRepository.h"
#include "storage/VaultInstaller.h"
#include "storage/ConfigStore.h"
#include "sync/DiffEngine.h"
#include "common/Identity.h"
#include "common/StringUtils.h"
#include <commdlg.h>
#include <map>

namespace anynote::ui {
namespace {
struct Secret { std::string value; ~Secret() { if (!value.empty()) SecureZeroMemory(value.data(), value.size()); } };
std::wstring Impact(const sync::Snapshot& local, const sync::Snapshot& remote) {
    size_t added = 0, removed = 0, changed = 0;
    for (const auto& diff : sync::Compare(local, remote)) {
        if (diff.differences & sync::LocalOnly) ++removed;
        else if (diff.differences & sync::RemoteOnly) ++added;
        else ++changed;
    }
    return L"新增 " + std::to_wstring(added) + L"，替换/移动 " + std::to_wstring(changed) + L"，删除 " + std::to_wstring(removed) + L"。";
}
void RequireConfirm(HWND owner, const std::wstring& text, const std::wstring& title) {
    if (MessageBoxW(owner, text.c_str(), title.c_str(), MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) throw std::runtime_error("CANCELLED");
}
void ReadPassword(HWND owner, Secret& password, const std::wstring& title) {
    auto input = SyncPrompt(owner, title, L"", true); if (!input) throw std::runtime_error("CANCELLED");
    password.value = utils::WideToUtf8(*input); SecureZeroMemory(input->data(), input->size() * sizeof(wchar_t));
}
void OpenVault(HWND owner, storage::Database& database, const std::wstring& path, Secret& password) {
    if (database.Open(path)) return;
    if (!database.IsPasswordRequiredOrWrong()) throw std::runtime_error(database.GetLastError());
    ReadPassword(owner, password, L"输入本地笔记库密码");
    if (!database.Open(path, password.value)) throw std::runtime_error(database.GetLastError());
}
}
std::filesystem::path CloudSync::BindingPath() const { return std::filesystem::path(m_manager.GetJsonPath()).parent_path() / L"sync-state.json"; }
bool CloudSync::IsBound(const std::string& uid) const {
    const auto state = storage::ConfigStore::Read(BindingPath().wstring());
    if (!state.exists) return false;
    const auto key = sync::Sha256(m_client->BaseUrl() + "\n" + m_accountId + "\n" + uid);
    return state.value.at("bindings").contains(key);
}
void CloudSync::RememberVersion(const std::string& uid, const nlohmann::json& version) {
    const auto previous = storage::ConfigStore::Read(BindingPath().wstring());
    auto state = previous.exists ? previous.value : nlohmann::json{{"schema_version", 1}, {"bindings", nlohmann::json::object()}};
    const auto key = sync::Sha256(m_client->BaseUrl() + "\n" + m_accountId + "\n" + uid);
    state["bindings"][key] = {{"server", m_client->BaseUrl()}, {"account_id", m_accountId}, {"uid", uid}, {"epoch", version.at("epoch")}, {"revision", version.at("revision")}};
    storage::ConfigStore::Write(BindingPath().wstring(), state, previous.fingerprint);
}
nlohmann::json SyncPrecondition(const nlohmann::json& vault) {
    return {{"operation_id", common::NewUuid()}, {"expected_epoch", vault.at("epoch")}, {"expected_revision", vault.at("revision")}};
}
sync::Snapshot ManifestSnapshot(const nlohmann::json& manifest) {
    sync::Snapshot result{manifest.at("vault").at("uid"), manifest.at("vault").at("name"), {}};
    for (const auto& value : manifest.at("nodes")) {
        sync::Node node; node.syncId = value.at("sync_id"); node.sourceId = std::stoll(value.at("source_id").get<std::string>());
        if (!value.at("parent_sync_id").is_null()) node.parentSyncId = value.at("parent_sync_id").get<std::string>();
        node.sequence = value.at("sequence"); node.title = value.at("title"); node.nodeType = value.at("node_type"); node.formatType = value.at("format_type");
        node.createdTime = std::stoll(value.at("created_time").get<std::string>()); node.modifiedTime = std::stoll(value.at("modified_time").get<std::string>());
        node.contentHash = value.at("content_hash"); result.nodes.push_back(std::move(node));
    }
    sync::ValidateTree(result.nodes); return result;
}
nlohmann::json CloudSync::Manifest(HWND owner, const std::string& uid) {
    nlohmann::json result;
    RunSyncTask(owner, L"读取远端版本", [&](sync::TransferControl& control) { result = m_client->Request("GET", "/vaults/" + uid + "/manifest", {}, control); }); return result;
}
nlohmann::json CloudSync::Write(HWND owner, const std::string& uid, const nlohmann::json& precondition, const std::function<nlohmann::json(sync::TransferControl&)>& action) {
    nlohmann::json result;
    RunSyncTask(owner, L"提交云端变更", [&](sync::TransferControl& control) {
        result = m_coordinator.Write(*m_client, m_account, JournalDirectory(), uid, precondition.at("operation_id"), control, [&] { return action(control); });
    });
    try { RememberVersion(uid, result); }
    catch (const std::exception&) { MessageBoxW(owner, L"云端已提交，但本地绑定状态保存失败。请重新读取云端版本；无需重复提交。", L"同步已提交", MB_OK | MB_ICONWARNING); }
    return result;
}
void CloudSync::UploadCurrent(HWND owner) {
    storage::VaultSnapshot local;
    m_coordinator.SetState(sync::SyncState::Planning);
    RunSyncTask(owner, L"生成本地快照", [&](sync::TransferControl&) {
        local = storage::SnapshotService::Export(m_database(), utils::WideToUtf8(m_manager.GetActiveVaultDisplayName()));
    });
    nlohmann::json condition; sync::Snapshot remote{local.snapshot.uid, "", {}};
    try { auto manifest = Manifest(owner, local.snapshot.uid); remote = ManifestSnapshot(manifest); condition = SyncPrecondition(manifest.at("vault")); }
    catch (const std::runtime_error& error) { if (std::string(error.what()) != "NOT_FOUND") throw; condition = {{"operation_id", common::NewUuid()}, {"create_only", true}}; }
    if (!condition.contains("create_only") && !IsBound(local.snapshot.uid)) {
        int choice = MessageBoxW(owner, L"此账号的云端已存在相同 UID，但此设备尚未绑定。\r\n\r\n“是”：确认它们属于同一逻辑库，继续比较覆盖。\r\n“否”：当前库是独立副本，另存为新身份后上传。\r\n“取消”：停止。", L"首次绑定确认", MB_YESNOCANCEL | MB_ICONWARNING | MB_DEFBUTTON3);
        if (choice == IDCANCEL) throw std::runtime_error("CANCELLED");
        if (choice == IDNO) {
            auto independent = storage::VaultInstaller::AsNew(local.snapshot);
            auto name = utils::Utf8ToWide(independent.name) + L"（独立副本）";
            auto path = m_manager.GenerateNewVaultPath(name + L"-" + utils::Utf8ToWide(independent.uid));
            Secret password;
            if (m_database().IsEncrypted()) {
                ReadPassword(owner, password, L"为独立副本设置加密密码");
                Secret confirm; ReadPassword(owner, confirm, L"再次输入独立副本密码");
                if (password.value.empty() || password.value != confirm.value) throw std::runtime_error("PASSWORD_CONFIRMATION_MISMATCH");
            }
            RequireConfirm(owner, L"将生成新的库 UID 和所有节点 sync_id，原库保留。\r\n新文件：" + path, L"另存独立新库");
            std::filesystem::path journal;
            RunSyncTask(owner, L"建立独立副本", [&](sync::TransferControl& control) {
                if (control.cancelled) throw std::runtime_error("CANCELLED");
                journal = storage::VaultInstaller::Install(m_manager, independent, path, name, password.value);
            });
            if (!journal.empty()) throw std::runtime_error("INSTALL_REGISTRATION_FAILED");
            local.snapshot = std::move(independent); remote = {local.snapshot.uid, "", {}};
            condition = {{"operation_id", common::NewUuid()}, {"create_only", true}};
        }
    }
    m_coordinator.SetState(sync::SyncState::AwaitingConfirmation);
    RequireConfirm(owner, L"是否将本笔记库全量笔记上传？\r\n账号：" + utils::Utf8ToWide(m_account) + L"\r\n服务器：" + utils::Utf8ToWide(m_client->BaseUrl()) +
        L"\r\n库：" + utils::Utf8ToWide(local.snapshot.name) + L"\r\nUID：" + utils::Utf8ToWide(local.snapshot.uid) + L"\r\n\r\n云端影响：" + Impact(remote, local.snapshot) +
        L"\r\n远端删除过的笔记可能被重新创建。", L"全库上传预览");
    if (storage::SnapshotService::Export(m_database(), local.snapshot.name).generation != local.generation) throw std::runtime_error("LOCAL_CHANGED");
    Write(owner, local.snapshot.uid, condition, [&](sync::TransferControl& control) { return m_client->Upload("/vaults/" + local.snapshot.uid + "/snapshot", condition, local.snapshot, false, control); });
    MessageBoxW(owner, L"全库上传已提交。", L"云同步", MB_OK | MB_ICONINFORMATION);
}
std::wstring CloudSync::ChooseLocal(HWND owner, const std::string& uid) {
    auto candidates = m_manager.FindVaultsByUid(uid);
    // Always verify the open vault itself, even if configuration registration previously failed.
    if (storage::NoteRepository(m_database()).GetVaultUid() == uid) {
        auto index = m_manager.FindVaultByPath(m_database().GetFilePath());
        if (index >= 0 && std::find(candidates.begin(), candidates.end(), static_cast<size_t>(index)) == candidates.end()) candidates.push_back(static_cast<size_t>(index));
        if (candidates.empty()) return m_database().GetFilePath();
    }
    if (candidates.empty()) return {};
    if (candidates.size() == 1) return m_manager.GetVault(candidates.front())->path;
    std::vector<std::wstring> choices; for (auto index : candidates) { const auto* vault = m_manager.GetVault(index); choices.push_back(vault->name + L" — " + vault->path); }
    auto selected = SyncChoose(owner, L"同一 UID 有多个副本，请明确选择", choices); if (!selected) throw std::runtime_error("CANCELLED");
    return m_manager.GetVault(candidates[*selected])->path;
}
sync::Snapshot CloudSync::LocalSnapshot(HWND owner, const std::string& uid) {
    auto path = ChooseLocal(owner, uid); if (path.empty()) return {uid, "", {}};
    sync::Snapshot result; storage::Database other; Secret password;
    auto* db = &m_database();
    if (_wcsicmp(path.c_str(), db->GetFilePath().c_str()) != 0) { OpenVault(owner, other, path, password); db = &other; }
    RunSyncTask(owner, L"读取本地副本", [&](sync::TransferControl&) { result = storage::SnapshotService::Export(*db, "").snapshot; });
    if (result.uid != uid) throw std::runtime_error("UID_MISMATCH"); return result;
}
void CloudSync::DownloadVault(HWND owner, const nlohmann::json& vault, const std::optional<std::string>& root) {
    const std::string uid = vault.at("uid");
    auto path = ChooseLocal(owner, uid);
    storage::Database other; Secret password;
    storage::Database* target = nullptr; storage::VaultSnapshot local{{uid, "", {}}, 0};
    if (!path.empty()) {
        target = &m_database();
        if (_wcsicmp(path.c_str(), target->GetFilePath().c_str()) != 0) { OpenVault(owner, other, path, password); target = &other; }
        if (storage::NoteRepository(*target).GetVaultUid() != uid) throw std::runtime_error("UID_MISMATCH");
        if (target->IsEncrypted() && password.value.empty()) {
            ReadPassword(owner, password, L"输入现有笔记库密码以验证加密备份");
            storage::Database verify; if (!verify.Open(path, password.value)) throw std::runtime_error(verify.GetLastError());
        }
        RunSyncTask(owner, L"生成本地快照", [&](sync::TransferControl&) { local = storage::SnapshotService::Export(*target, vault.at("name")); });
    }
    sync::SnapshotResponse remote;
    auto route = "/vaults/" + uid + (root ? "/nodes/" + *root + "/subtree" : "/snapshot") +
        "?epoch=" + vault.at("epoch").get<std::string>() + "&revision=" + vault.at("revision").get<std::string>();
    RunSyncTask(owner, L"下载并校验快照", [&](sync::TransferControl& control) { remote = m_client->Download(route, control); });
    if (remote.snapshot.uid != uid || remote.envelope.at("data").at("vault").at("epoch") != vault.at("epoch") ||
        remote.envelope.at("data").at("vault").at("revision") != vault.at("revision")) throw std::runtime_error("VERSION_CONFLICT");
    auto incoming = root ? sync::PlanSubtree(local.snapshot, remote.snapshot, *root) : std::move(remote.snapshot);
    if (path.empty()) {
        wchar_t file[32768]{}; auto suggested = m_manager.GenerateNewVaultPath(utils::Utf8ToWide(vault.at("name")));
        wcsncpy_s(file, suggested.c_str(), _TRUNCATE); OPENFILENAMEW ofn{sizeof(ofn)}; ofn.hwndOwner = owner; ofn.lpstrFile = file; ofn.nMaxFile = 32768;
        ofn.lpstrFilter = L"AnyNote 笔记库\0*.anynote\0"; ofn.lpstrDefExt = L"anynote"; ofn.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (!GetSaveFileNameW(&ofn)) throw std::runtime_error("CANCELLED"); path = file;
        if (std::filesystem::exists(path)) { MessageBoxW(owner, L"同名文件已存在。请另选文件名，避免覆盖不同 UID 的笔记库。", L"另存为", MB_OK | MB_ICONWARNING); return; }
        if (MessageBoxW(owner, L"是否为新建本地笔记库设置加密密码？", L"本地加密", MB_YESNO | MB_ICONQUESTION) == IDYES) {
            ReadPassword(owner, password, L"新建笔记库密码"); Secret confirm; ReadPassword(owner, confirm, L"再次输入新建笔记库密码");
            if (password.value.empty() || password.value != confirm.value) throw std::runtime_error("PASSWORD_CONFIRMATION_MISMATCH");
        }
    }
    RequireConfirm(owner, L"本地目标：" + path + L"\r\n\r\n" + Impact(local.snapshot, incoming) + L"\r\n即将按远端内容替换此范围。", L"下载影响预览");
    RequireConfirm(owner, L"再次确认：下载将覆盖所示范围的本地笔记。已有库会先建立备份，并保留当前加密密码。", L"确认下载覆盖");
    bool committed = false; std::filesystem::path journal;
    RunSyncTask(owner, L"备份并事务导入", [&](sync::TransferControl& control) {
        m_coordinator.SetState(sync::SyncState::Committing);
        if (target) { storage::SnapshotService::Import(*target, incoming, local.generation, common::NewUuid(), password.value, {}, [&] { return control.cancelled.load(); }); }
        else { if (control.cancelled) throw std::runtime_error("CANCELLED"); journal = storage::VaultInstaller::Install(m_manager, incoming, path, utils::Utf8ToWide(vault.at("name")), password.value); }
        committed = true;
    });
    if (committed) {
        try { RememberVersion(uid, vault); }
        catch (const std::exception&) { MessageBoxW(owner, L"本地数据已提交，但绑定状态保存失败。下次上传需要重新确认绑定。", L"下载已提交", MB_OK | MB_ICONWARNING); }
        m_coordinator.SetState(sync::SyncState::Refreshing);
        if (target == &m_database()) m_refresh();
        m_coordinator.SetState(sync::SyncState::Idle);
        MessageBoxW(owner, journal.empty() ? L"下载已提交，本地数据已保存。" : L"数据已保存，但配置登记失败。文件已保留，下次打开在线管理可重试登记。", L"云同步", MB_OK | MB_ICONINFORMATION);
    }
}
void CloudSync::QueryPending(HWND owner) {
    auto pending = sync::SyncCoordinator::Pending(JournalDirectory(), m_client->BaseUrl(), m_account);
    if (pending.empty()) { MessageBoxW(owner, L"没有待查询的操作。", L"云同步", MB_OK); return; }
    for (const auto& journal : pending) {
        try { RunSyncTask(owner, L"查询提交结果", [&](sync::TransferControl& c) { sync::SyncCoordinator::Query(*m_client, journal, m_account, c); }); }
        catch (const std::exception& e) { MessageBoxW(owner, (L"操作 " + journal.stem().wstring() + L"\r\n" + sync::DescribeSyncError(e.what()) + L"\r\n未自动重发写入；请刷新版本后决定下一步。").c_str(), L"提交结果", MB_OK | MB_ICONWARNING); }
    }
}
void CloudSync::RecoverInstalls(HWND owner) {
    auto directory = std::filesystem::path(m_manager.GetJsonPath()).parent_path() / L".sync-pending";
    if (!std::filesystem::exists(directory)) return;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (entry.path().extension() != L".json") continue;
        if (MessageBoxW(owner, L"发现尚未完成登记的新库记录，是否尝试恢复登记？", L"新库登记恢复", MB_YESNO | MB_ICONQUESTION) != IDYES) continue;
        try { storage::VaultInstaller::Recover(m_manager, entry.path()); }
        catch (const std::exception& e) {
            if (std::string(e.what()) != "INSTALL_OPEN_FAILED") { SyncError(owner, e); continue; }
            Secret password; ReadPassword(owner, password, L"输入待登记笔记库密码");
            try { storage::VaultInstaller::Recover(m_manager, entry.path(), password.value); } catch (const std::exception& error) { SyncError(owner, error); }
        }
    }
}
}
