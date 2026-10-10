#include "VaultInstaller.h"
#include "ConfigStore.h"
#include "NoteRepository.h"
#include "VaultFileLease.h"
#include "common/Identity.h"
#include "common/StringUtils.h"
#include <windows.h>
#include <sqlite3.h>
#include <map>
#include <stdexcept>

namespace anynote::storage {
namespace {
void Check(bool ok, const char* error) { if (!ok) throw std::runtime_error(error); }
std::string Utf8(const std::filesystem::path& path) { return utils::WideToUtf8(path.wstring()); }
void VerifyInstall(const std::filesystem::path& path, const std::string& password,
    const std::string& uid, const std::string& operation) {
    Database db; Check(db.Open(path.wstring(), password), "INSTALL_OPEN_FAILED");
    NoteRepository repository(db);
    Check(repository.GetVaultUid() == uid, "INSTALL_IDENTITY_MISMATCH");
    Database::Statement s;
    Check(s.Prepare(db, "SELECT count(*) FROM sync_operations WHERE operation_id=?") && s.BindText(1, operation) &&
        s.Step() == SQLITE_ROW && s.GetInt64(0) == 1, "INSTALL_NOT_COMMITTED");
    s = {};
    SnapshotService::Export(db, "");
    Check(db.CheckpointAndClose(), "INSTALL_DATABASE_BUSY");
}
}
sync::Snapshot VaultInstaller::AsNew(sync::Snapshot snapshot) {
    sync::ValidateTree(snapshot.nodes);
    std::map<std::string, std::string> identities;
    for (const auto& node : snapshot.nodes) identities.emplace(node.syncId, common::NewUuid());
    for (auto& node : snapshot.nodes) {
        node.syncId = identities.at(node.syncId);
        if (node.parentSyncId) node.parentSyncId = identities.at(*node.parentSyncId);
    }
    snapshot.uid = common::NewVaultUid(); return snapshot;
}
std::filesystem::path VaultInstaller::Install(VaultManager& manager, const sync::Snapshot& snapshot,
    const std::filesystem::path& destination, const std::wstring& displayName, const std::string& password) {
    const auto final = std::filesystem::absolute(destination).lexically_normal();
    Check(final.extension() == L".anynote" && std::filesystem::is_directory(final.parent_path()), "INSTALL_PATH_INVALID");
    auto destinationLease = VaultFileLease::Acquire(final.wstring());
    Check(!std::filesystem::exists(final), "INSTALL_DESTINATION_EXISTS");
    const auto operation = common::NewUuid();
    const auto temporary = final.parent_path() / ("install-" + operation + ".anynote");
    const auto journalDirectory = std::filesystem::path(manager.GetJsonPath()).parent_path() / L".sync-pending";
    std::filesystem::create_directories(journalDirectory);
    const auto journal = journalDirectory / (operation + ".json");
    ConfigStore::Write(journal.wstring(), nlohmann::json{{"version", 1}, {"operation_id", operation},
        {"uid", snapshot.uid}, {"destination", Utf8(final)}, {"temporary", Utf8(temporary)},
        {"display_name", utils::WideToUtf8(displayName)}}, "");
    // Exclusive creation prevents accidentally opening somebody else's staging file.
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    Check(file != INVALID_HANDLE_VALUE, "INSTALL_CREATE_FAILED"); CloseHandle(file);
    {
        Database db; Check(db.Open(temporary.wstring(), password), "INSTALL_OPEN_FAILED");
        NoteRepository repository(db); Check(repository.InitializeSchema(password), "INSTALL_SCHEMA_FAILED");
        Database::Statement identity;
        Check(identity.Prepare(db, "UPDATE uid SET value=? WHERE singleton=1") && identity.BindText(1, snapshot.uid) &&
            identity.Step() == SQLITE_DONE, "INSTALL_UID_FAILED"); identity = {};
        SnapshotService::Import(db, snapshot, 0, operation, password);
        Check(db.CheckpointAndClose(), "INSTALL_DATABASE_BUSY");
    }
    VerifyInstall(temporary, password, snapshot.uid, operation);
    Check(MoveFileExW(temporary.c_str(), final.c_str(), MOVEFILE_WRITE_THROUGH) != FALSE, "INSTALL_RENAME_FAILED");
    // Durable journal remains if config cannot be persisted. Never erase the new vault.
    if (!manager.AddVault(displayName, final.wstring(), snapshot.uid)) return journal;
    std::error_code ec; std::filesystem::remove(journal, ec);
    return ec ? journal : std::filesystem::path{};
}
void VaultInstaller::Recover(VaultManager& manager, const std::filesystem::path& journal, const std::string& password) {
    const auto value = ConfigStore::Read(journal.wstring()).value;
    Check(value.at("version") == 1, "INSTALL_JOURNAL_VERSION");
    const auto operation = value.at("operation_id").get<std::string>();
    const auto uid = value.at("uid").get<std::string>();
    Check(sync::IsUuid(operation) && journal.filename() == operation + ".json", "INSTALL_JOURNAL_INVALID");
    const std::filesystem::path final(utils::Utf8ToWide(value.at("destination").get<std::string>()));
    const std::filesystem::path temporary(utils::Utf8ToWide(value.at("temporary").get<std::string>()));
    Check(final.is_absolute() && final.extension() == L".anynote" && temporary == final.parent_path() / ("install-" + operation + ".anynote"),
        "INSTALL_JOURNAL_INVALID");
    auto destinationLease = VaultFileLease::Acquire(final.wstring());
    if (!std::filesystem::exists(final)) {
        Check(std::filesystem::exists(temporary), "INSTALL_FILE_MISSING");
        VerifyInstall(temporary, password, uid, operation);
        Check(MoveFileExW(temporary.c_str(), final.c_str(), MOVEFILE_WRITE_THROUGH) != FALSE, "INSTALL_RENAME_FAILED");
    } else VerifyInstall(final, password, uid, operation);
    Check(manager.AddVault(utils::Utf8ToWide(value.at("display_name").get<std::string>()), final.wstring(), uid), "INSTALL_REGISTRATION_FAILED");
    Check(std::filesystem::remove(journal), "INSTALL_JOURNAL_CLEANUP_FAILED");
}
}
