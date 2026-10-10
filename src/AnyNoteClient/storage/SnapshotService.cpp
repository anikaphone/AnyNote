#include "SnapshotService.h"
#include "NoteRepository.h"
#include "VaultFileLease.h"
#include "common/Identity.h"
#include "sync/RequestIdentity.h"
#include <sqlite3.h>
#include <windows.h>
#include <algorithm>
#include <chrono>
#include <limits>
#include <map>
#include <stdexcept>

namespace anynote::storage {
namespace {
void Check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
class Transaction {
public:
    Transaction(Database& db, bool write) : m_db(db) {
        Check(db.Execute(write ? "BEGIN IMMEDIATE" : "BEGIN"), "DATABASE_BUSY");
    }
    ~Transaction() { if (!m_done) m_db.Rollback(); }
    void Commit() { Check(m_db.Commit(), "COMMIT_FAILED"); m_done = true; }
private:
    Database& m_db;
    bool m_done = false;
};
int64_t Generation(Database& db) {
    Database::Statement s;
    Check(s.Prepare(db, "SELECT generation FROM sync_state WHERE singleton=1") && s.Step() == SQLITE_ROW, "INVALID_SYNC_SCHEMA");
    return s.GetInt64(0);
}
std::string Uid(Database& db) {
    Database::Statement s;
    Check(s.Prepare(db, "SELECT value FROM uid WHERE singleton=1") && s.Step() == SQLITE_ROW, "INVALID_SYNC_SCHEMA");
    return s.GetText(0);
}
class Discard : public std::streambuf {
    int_type overflow(int_type c) override { return traits_type::not_eof(c); }
    std::streamsize xsputn(const char*, std::streamsize n) override { return n; }
};
void Validate(const sync::Snapshot& snapshot, const sync::Limits& limits) {
    Discard discard; std::ostream output(&discard);
    sync::WriteSnapshot(output, snapshot, limits);
}
std::string ImportHash(const sync::Snapshot& snapshot, int64_t generation) {
    using Json = nlohmann::json;
    std::map<std::string, Json> sorted;
    for (const auto& n : snapshot.nodes) sorted.emplace(n.syncId, Json{
        {"sync_id", n.syncId}, {"parent_sync_id", n.parentSyncId ? Json(*n.parentSyncId) : Json(nullptr)},
        {"sequence", n.sequence}, {"title", n.title}, {"node_type", n.nodeType},
        {"created_time", std::to_string(n.createdTime)}, {"modified_time", std::to_string(n.modifiedTime)},
        {"format_type", n.formatType}, {"content_hash", n.contentHash}});
    Json nodes = Json::array();
    for (const auto& item : sorted) nodes.push_back(item.second);
    return sync::RequestHash(Json{{"action", "local_import"}, {"uid", snapshot.uid},
        {"generation", std::to_string(generation)}, {"nodes", nodes}});
}
bool Applied(Database& db, const std::string& operation, const std::string& hash) {
    Database::Statement s;
    Check(s.Prepare(db, "SELECT m.value FROM sync_operations o LEFT JOIN notebook_meta m ON m.key='sync_operation/'||o.operation_id WHERE o.operation_id=?") &&
        s.BindText(1, operation), "IMPORT_LOG_FAILED");
    int result = s.Step();
    Check(result == SQLITE_ROW || result == SQLITE_DONE, "IMPORT_LOG_FAILED");
    if (result == SQLITE_DONE) return false;
    Check(s.GetText(0) == hash, "OPERATION_ID_REUSED");
    return true;
}
void Cancel(const std::function<bool()>& cancelled) {
    Check(!cancelled || !cancelled(), "CANCELLED");
}
void Apply(Database& db, const sync::Snapshot& snapshot, int64_t generation,
    const std::string& operation, const std::string& hash, const std::function<bool()>& cancelled) {
    Transaction transaction(db, true);
    Check(Uid(db) == snapshot.uid, "UID_MISMATCH");
    Check(Generation(db) == generation, "LOCAL_CHANGED");
    std::map<std::string, int64_t> ids;
    int64_t next = 0;
    {
        Database::Statement s;
        Check(s.Prepare(db, "SELECT sync_id,id FROM nodes"), "IMPORT_ID_READ_FAILED");
        int result;
        while ((result = s.Step()) == SQLITE_ROW) { ids.emplace(s.GetText(0), s.GetInt64(1)); next = std::max(next, s.GetInt64(1)); }
        Check(result == SQLITE_DONE, "IMPORT_ID_READ_FAILED");
        s = {};
        Check(s.Prepare(db, "SELECT seq FROM sqlite_sequence WHERE name='nodes'"), "IMPORT_ID_READ_FAILED");
        result = s.Step();
        Check(result == SQLITE_ROW || result == SQLITE_DONE, "IMPORT_ID_READ_FAILED");
        if (result == SQLITE_ROW) next = std::max(next, s.GetInt64(0));
    }
    for (const auto& node : snapshot.nodes) if (!ids.count(node.syncId)) {
        Check(next < std::numeric_limits<int64_t>::max(), "LOCAL_ID_EXHAUSTED");
        ids.emplace(node.syncId, ++next);
    }
    Check(db.Execute("DELETE FROM node_contents; DELETE FROM nodes;"), "IMPORT_DELETE_FAILED");
    {
        Database::Statement node, content;
        Check(node.Prepare(db, "INSERT INTO nodes(id,parent_id,sequence,title,node_type,created_time,modified_time,sync_id) VALUES(?,?,?,?,?,?,?,?)") &&
            content.Prepare(db, "INSERT INTO node_contents(node_id,format_type,content_rtf,plain_text,content_hash) VALUES(?,?,?,?,?)"), "IMPORT_PREPARE_FAILED");
        for (const auto& n : snapshot.nodes) {
            Cancel(cancelled);
            auto id = ids.at(n.syncId);
            auto plain = NoteRepository::DerivePlainText(n.rtf);
            Check(node.Reset() && node.BindInt64(1, id) && node.BindInt64(2, n.parentSyncId ? ids.at(*n.parentSyncId) : 0) &&
                node.BindInt(3, n.sequence) && node.BindText(4, n.title) && node.BindInt(5, n.nodeType) &&
                node.BindInt64(6, n.createdTime) && node.BindInt64(7, n.modifiedTime) && node.BindText(8, n.syncId) &&
                node.Step() == SQLITE_DONE, "IMPORT_NODE_FAILED");
            Check(content.Reset() && content.BindInt64(1, id) && content.BindInt(2, n.formatType) &&
                content.BindBlob(3, n.rtf.data(), n.rtf.size()) && content.BindText(4, plain) &&
                content.BindText(5, n.contentHash) && content.Step() == SQLITE_DONE, "IMPORT_CONTENT_FAILED");
        }
    }
    {
        Database::Statement log, receipt;
        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        Check(log.Prepare(db, "INSERT INTO sync_operations(operation_id,applied_at) VALUES(?,?)") &&
            log.BindText(1, operation) && log.BindInt64(2, now) && log.Step() == SQLITE_DONE &&
            receipt.Prepare(db, "INSERT INTO notebook_meta(key,value) VALUES(?,?)") &&
            receipt.BindText(1, "sync_operation/" + operation) && receipt.BindText(2, hash) && receipt.Step() == SQLITE_DONE, "IMPORT_LOG_FAILED");
    }
    Cancel(cancelled);
    transaction.Commit();
}
void Verify(Database& db) {
    Database::Statement s;
    Check(s.Prepare(db, "PRAGMA integrity_check") && s.Step() == SQLITE_ROW && s.GetText(0) == "ok" && s.Step() == SQLITE_DONE,
        "BACKUP_CORRUPT");
}
void Copy(const std::filesystem::path& from, const std::filesystem::path& to) {
    Check(CopyFileW(from.c_str(), to.c_str(), TRUE) != FALSE, "BACKUP_COPY_FAILED");
    HANDLE file = CreateFileW(to.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    Check(file != INVALID_HANDLE_VALUE, "BACKUP_OPEN_FAILED");
    bool flushed = FlushFileBuffers(file) != FALSE; CloseHandle(file);
    Check(flushed, "BACKUP_FLUSH_FAILED");
}
struct StagingFile {
    std::filesystem::path path;
    ~StagingFile() {
        // Own randomly named disposable files only. Database must be closed first.
        for (const auto* suffix : {L"", L"-wal", L"-shm", L".lock"}) {
            std::error_code ec; std::filesystem::remove(path.wstring() + suffix, ec);
        }
    }
};
void Rotate(const std::filesystem::path& directory, const std::string& uid, const std::filesystem::path& current) {
    std::vector<std::filesystem::directory_entry> files;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        auto name = entry.path().filename().string();
        if (entry.is_regular_file() && name.starts_with(uid + "-") && entry.path().extension() == L".anynote") files.push_back(entry);
    }
    std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) { return a.path() > b.path(); });
    size_t retained = 1;
    for (const auto& file : files) {
        if (file.path() == current) continue;
        if (retained++ < 10) continue;
        // Never rotate a file still open by another cooperating process.
        auto lease = VaultFileLease::Acquire(file.path().wstring());
        std::filesystem::remove(file.path());
    }
}
}

VaultSnapshot SnapshotService::Export(Database& db, const std::string& name, const sync::Limits& limits) {
    Transaction transaction(db, false);
    VaultSnapshot result;
    result.generation = Generation(db); result.snapshot.uid = Uid(db); result.snapshot.name = name;
    Database::Statement s;
    Check(s.Prepare(db, "SELECT n.sync_id,n.id,p.sync_id,n.parent_id,n.sequence,n.title,n.node_type,n.created_time,n.modified_time,"
        "c.format_type,c.content_hash,c.content_rtf FROM nodes n LEFT JOIN nodes p ON p.id=n.parent_id "
        "LEFT JOIN node_contents c ON c.node_id=n.id ORDER BY n.sync_id"), "EXPORT_READ_FAILED");
    size_t bytes = 0; int code;
    while ((code = s.Step()) == SQLITE_ROW) {
        Check(result.snapshot.nodes.size() < limits.maxNodes, "LIMIT_EXCEEDED");
        sync::Node n;
        n.syncId = s.GetText(0); n.sourceId = s.GetInt64(1);
        if (s.GetInt64(3) != 0) { n.parentSyncId = s.GetText(2); Check(!n.parentSyncId->empty(), "MISSING_PARENT"); }
        n.sequence = s.GetInt(4); n.title = s.GetText(5); n.nodeType = s.GetInt(6);
        n.createdTime = s.GetInt64(7); n.modifiedTime = s.GetInt64(8); n.formatType = s.GetInt(9);
        n.contentHash = s.GetText(10);
        Check(static_cast<size_t>(sqlite3_column_bytes(s.GetRawStmt(), 11)) <= limits.maxNodeBytes, "LIMIT_EXCEEDED");
        n.rtf = s.GetBlob(11);
        Check(n.rtf.size() <= limits.maxVaultBytes - bytes, "LIMIT_EXCEEDED"); bytes += n.rtf.size();
        result.snapshot.nodes.push_back(std::move(n));
    }
    Check(code == SQLITE_DONE, "EXPORT_READ_FAILED"); s = {};
    Validate(result.snapshot, limits);
    transaction.Commit(); return result;
}

ImportResult SnapshotService::Import(Database& db, const sync::Snapshot& snapshot, int64_t expectedGeneration,
    const std::string& operationId, const std::string& password, const sync::Limits& limits, const std::function<bool()>& cancelled) {
    Check(sync::IsUuid(operationId) && expectedGeneration >= 0, "INVALID_IMPORT_REQUEST");
    Cancel(cancelled); Validate(snapshot, limits);
    const auto hash = ImportHash(snapshot, expectedGeneration);
    Check(Uid(db) == snapshot.uid, "UID_MISMATCH");
    if (Applied(db, operationId, hash)) return {true, {}, {}};
    Check(Generation(db) == expectedGeneration, "LOCAL_CHANGED");
    Check(!db.IsEncrypted() || !password.empty(), "PASSWORD_REQUIRED");
    const std::filesystem::path path(db.GetFilePath());
    Check(path != L":memory:", "FILE_BACKUP_REQUIRED");
    auto lease = VaultFileLease::Acquire(path.wstring());
    // A wrong supplied key must not close the user's still-unlocked connection.
    {
        Database keyCheck;
        Check(keyCheck.Open(path.wstring(), password), "BACKUP_KEY_FAILED");
        Verify(keyCheck);
        keyCheck.Close();
    }
    const auto directory = path.parent_path() / L".sync-backups";
    std::filesystem::create_directories(directory);
    auto time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    auto backup = directory / (snapshot.uid + "-" + std::to_string(time) + "-" + operationId + "-" + common::NewUuid() + ".anynote");
    Check(db.CheckpointAndClose(), "DATABASE_BUSY");
    try {
        Copy(path, backup);
        Database verify;
        Check(verify.Open(backup.wstring(), password), "BACKUP_KEY_FAILED"); Verify(verify);
        Check(verify.CheckpointAndClose(), "BACKUP_BUSY");
    } catch (...) { db.Open(path.wstring(), password); throw; }
    Check(db.Open(path.wstring(), password), "REOPEN_FAILED");
    Cancel(cancelled);
    StagingFile staging{directory / ("staging-" + common::NewUuid() + ".anynote")};
    Copy(backup, staging.path);
    {
        Database rehearsal;
        Check(rehearsal.Open(staging.path.wstring(), password), "STAGING_KEY_FAILED");
        Apply(rehearsal, snapshot, expectedGeneration, operationId, hash, cancelled);
        Verify(rehearsal);
        Check(rehearsal.CheckpointAndClose(), "STAGING_BUSY");
    }
    Apply(db, snapshot, expectedGeneration, operationId, hash, cancelled);
    ImportResult result{false, backup, {}};
    // Cleanup failure cannot turn a committed import into an apparent rollback.
    try { Rotate(directory, snapshot.uid, backup); }
    catch (const std::exception& error) { result.cleanupWarning = error.what(); }
    return result;
}
}
