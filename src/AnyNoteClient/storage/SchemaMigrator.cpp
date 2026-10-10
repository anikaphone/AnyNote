#include "SchemaMigrator.h"
#include "common/Identity.h"
#include "sync/SyncProtocol.h"
#include <sqlite3.h>
#include <windows.h>
#include <filesystem>
#include <stdexcept>

namespace anynote::storage {
namespace {
void Check(bool ok, const char* error) { if (!ok) throw std::runtime_error(error); }
int Scalar(Database& db, const char* sql) {
    Database::Statement statement;
    Check(statement.Prepare(db, sql) && statement.Step() == SQLITE_ROW, "MIGRATION_READ_FAILED");
    return statement.GetInt(0);
}
bool HasColumn(Database& db, const char* sql, const std::string& name) {
    Database::Statement statement;
    Check(statement.Prepare(db, sql), "MIGRATION_SCHEMA_READ_FAILED");
    int result;
    while ((result = statement.Step()) == SQLITE_ROW) if (statement.GetText(1) == name) return true;
    Check(result == SQLITE_DONE, "MIGRATION_SCHEMA_READ_FAILED");
    return false;
}
class Transaction {
public:
    explicit Transaction(Database& db) : m_db(db) { Check(db.Execute("BEGIN IMMEDIATE"), "MIGRATION_BEGIN_FAILED"); }
    ~Transaction() { if (!m_committed) m_db.Rollback(); }
    void Commit() { Check(m_db.Commit(), "MIGRATION_COMMIT_FAILED"); m_committed = true; }
private:
    Database& m_db;
    bool m_committed = false;
};

void Backup(Database& db, const std::string& password) {
    Check(!db.IsEncrypted() || !password.empty(), "MIGRATION_PASSWORD_REQUIRED");
    const std::filesystem::path path(db.GetFilePath());
    // Memory databases have no on-disk data requiring a migration backup.
    if (path == L":memory:") return;
    const auto directory = path.parent_path() / L".sync-backups";
    std::filesystem::create_directories(directory);
    auto name = "pre-v1-" + common::NewUuid() + ".anynote";
    const auto backup = directory / name;
    Check(db.CheckpointAndClose(), "MIGRATION_DATABASE_BUSY");
    try {
        Check(CopyFileW(path.c_str(), backup.c_str(), TRUE) != FALSE, "MIGRATION_COPY_FAILED");
        HANDLE file = CreateFileW(backup.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        Check(file != INVALID_HANDLE_VALUE, "MIGRATION_BACKUP_OPEN_FAILED");
        const bool flushed = FlushFileBuffers(file) != FALSE;
        CloseHandle(file);
        Check(flushed, "MIGRATION_BACKUP_FLUSH_FAILED");
        Database verify;
        Check(verify.Open(backup.wstring(), password), "MIGRATION_BACKUP_KEY_FAILED");
        {
            Database::Statement integrity;
            Check(integrity.Prepare(verify, "PRAGMA integrity_check") && integrity.Step() == SQLITE_ROW &&
                integrity.GetText(0) == "ok", "MIGRATION_BACKUP_CORRUPT");
        }
        Check(verify.CheckpointAndClose(), "MIGRATION_BACKUP_CLOSE_FAILED");
    } catch (...) {
        // Reopen the untouched source; do not replace it with an unverified backup.
        db.Open(path.wstring(), password);
        throw;
    }
    Check(db.Open(path.wstring(), password), "MIGRATION_REOPEN_FAILED");
}

const char* NewSchema = R"sql(
CREATE TABLE nodes_v1 (
 id INTEGER PRIMARY KEY AUTOINCREMENT,
 parent_id INTEGER NOT NULL DEFAULT 0,
 sequence INTEGER NOT NULL DEFAULT 0,
 title TEXT NOT NULL,
 node_type INTEGER NOT NULL DEFAULT 0,
 created_time INTEGER NOT NULL,
 modified_time INTEGER NOT NULL,
 sync_id TEXT NOT NULL UNIQUE CHECK(length(sync_id)=36)
);
CREATE TABLE node_contents_v1 (
 node_id INTEGER PRIMARY KEY,
 format_type INTEGER NOT NULL DEFAULT 1,
 content_rtf BLOB,
 plain_text TEXT,
 content_hash TEXT NOT NULL CHECK(length(content_hash)=64 AND content_hash NOT GLOB '*[^0-9a-f]*'),
 FOREIGN KEY(node_id) REFERENCES nodes_v1(id) ON DELETE CASCADE
);
CREATE TABLE uid (singleton INTEGER PRIMARY KEY CHECK(singleton=1), value TEXT NOT NULL UNIQUE
 CHECK(length(value)=16 AND value NOT GLOB '*[^0-9a-f]*'));
CREATE TABLE IF NOT EXISTS notebook_meta (key TEXT PRIMARY KEY, value TEXT);
CREATE TABLE sync_state (singleton INTEGER PRIMARY KEY CHECK(singleton=1), generation INTEGER NOT NULL
 CHECK(typeof(generation)='integer' AND generation>=0));
INSERT INTO sync_state VALUES(1,0);
CREATE TABLE sync_operations (operation_id TEXT PRIMARY KEY, applied_at INTEGER NOT NULL);
)sql";

void CopyNodes(Database& db, bool plainText, const std::function<std::wstring(const std::string&)>& extract) {
    Database::Statement source, node, content;
    Check(source.Prepare(db, std::string("SELECT n.id,n.parent_id,n.sequence,n.title,n.node_type,n.created_time,n.modified_time,") +
        "COALESCE(c.format_type,1),c.content_rtf," + (plainText ? "c.plain_text" : "NULL") +
        " FROM nodes n LEFT JOIN node_contents c ON c.node_id=n.id ORDER BY n.id"), "MIGRATION_READ_FAILED");
    Check(node.Prepare(db, "INSERT INTO nodes_v1 VALUES(?,?,?,?,?,?,?,?)") &&
        content.Prepare(db, "INSERT INTO node_contents_v1 VALUES(?,?,?,?,?)"), "MIGRATION_PREPARE_FAILED");
    int result;
    while ((result = source.Step()) == SQLITE_ROW) {
        const auto id = source.GetInt64(0);
        Check(node.Reset() && node.BindInt64(1, id) && node.BindInt64(2, source.GetInt64(1)) &&
            node.BindInt(3, source.GetInt(2)) && node.BindText(4, source.GetText(3)) && node.BindInt(5, source.GetInt(4)) &&
            node.BindInt64(6, source.GetInt64(5)) && node.BindInt64(7, source.GetInt64(6)) &&
            node.BindText(8, common::NewUuid()) && node.Step() == SQLITE_DONE, "MIGRATION_NODE_FAILED");
        auto rtf = source.GetBlob(8);
        auto plain = plainText ? source.GetWideText(9) : std::wstring{};
        if (plain.empty() && !rtf.empty()) plain = extract(rtf);
        Check(content.Reset() && content.BindInt64(1, id) && content.BindInt(2, source.GetInt(7)) &&
            content.BindBlob(3, rtf.data(), rtf.size()) && content.BindText(4, plain) &&
            content.BindText(5, sync::Sha256(rtf)) && content.Step() == SQLITE_DONE, "MIGRATION_CONTENT_FAILED");
    }
    Check(result == SQLITE_DONE, "MIGRATION_READ_FAILED");
}
}

bool SchemaMigrator::Migrate(Database& db, const std::string& password,
    const std::function<std::wstring(const std::string&)>& extractPlainText) {
    try {
        Check(db.IsOpen(), "MIGRATION_CLOSED");
        // Preserve the process lease across checkpoint/close/copy/reopen.
        auto lease = db.m_fileLease;
        const int version = Scalar(db, "PRAGMA user_version");
        Check(version >= 0 && version <= CurrentVersion, "UNSUPPORTED_SCHEMA_VERSION");
        if (version == CurrentVersion) {
            Check(Scalar(db, "SELECT count(*) FROM uid WHERE singleton=1 AND length(value)=16") == 1 &&
                HasColumn(db, "PRAGMA table_info(nodes)", "sync_id") &&
                HasColumn(db, "PRAGMA table_info(node_contents)", "content_hash"), "INVALID_SYNC_SCHEMA");
            return true;
        }
        const bool oldNodes = Scalar(db, "SELECT count(*) FROM sqlite_master WHERE type='table' AND name='nodes'") != 0;
        const bool oldContents = Scalar(db, "SELECT count(*) FROM sqlite_master WHERE type='table' AND name='node_contents'") != 0;
        Check(oldNodes == oldContents, "INCOMPLETE_LEGACY_SCHEMA");
        if (Scalar(db, "SELECT count(*) FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%'") > 0) Backup(db, password);
        Transaction transaction(db);
        // Another cooperating migrator must never overwrite a completed identity.
        Check(Scalar(db, "PRAGMA user_version") == 0, "MIGRATION_CONCURRENT_CHANGE");
        Check(db.Execute(NewSchema), "MIGRATION_CREATE_FAILED");
        int64_t previousSequence = 0;
        if (oldNodes) {
            Database::Statement sequence;
            Check(sequence.Prepare(db, "SELECT seq FROM sqlite_sequence WHERE name='nodes'"), "MIGRATION_SEQUENCE_FAILED");
            int result = sequence.Step();
            Check(result == SQLITE_ROW || result == SQLITE_DONE, "MIGRATION_SEQUENCE_FAILED");
            if (result == SQLITE_ROW) previousSequence = sequence.GetInt64(0);
            sequence = {};
            Check(Scalar(db, "SELECT count(*) FROM node_contents c LEFT JOIN nodes n ON n.id=c.node_id WHERE n.id IS NULL") == 0,
                "MIGRATION_ORPHAN_CONTENT");
            CopyNodes(db, HasColumn(db, "PRAGMA table_info(node_contents)", "plain_text"), extractPlainText);
            Check(db.Execute("DROP TABLE node_contents; DROP TABLE nodes;"), "MIGRATION_REPLACE_FAILED");
        }
        Check(db.Execute("ALTER TABLE nodes_v1 RENAME TO nodes; ALTER TABLE node_contents_v1 RENAME TO node_contents; "
            "CREATE INDEX idx_nodes_parent_seq ON nodes(parent_id,sequence);"), "MIGRATION_RENAME_FAILED");
        if (previousSequence > 0) {
            Database::Statement sequence;
            Check(sequence.Prepare(db, "UPDATE sqlite_sequence SET seq=max(seq,?) WHERE name='nodes'") &&
                sequence.BindInt64(1, previousSequence) && sequence.Step() == SQLITE_DONE, "MIGRATION_SEQUENCE_FAILED");
            sequence = {};
            Check(sequence.Prepare(db, "INSERT INTO sqlite_sequence(name,seq) SELECT 'nodes',? WHERE NOT EXISTS(SELECT 1 FROM sqlite_sequence WHERE name='nodes')") &&
                sequence.BindInt64(1, previousSequence) && sequence.Step() == SQLITE_DONE, "MIGRATION_SEQUENCE_FAILED");
        }
        {
            Database::Statement uid;
            Check(uid.Prepare(db, "INSERT INTO uid VALUES(1,?)") && uid.BindText(1, common::NewVaultUid()) &&
                uid.Step() == SQLITE_DONE, "MIGRATION_UID_FAILED");
        }
        for (const auto* table : {"nodes", "node_contents"}) {
            for (const auto* action : {"INSERT", "UPDATE", "DELETE"}) {
                Check(db.Execute(std::string("CREATE TRIGGER sync_generation_") + table + "_" + action +
                    " AFTER " + action + " ON " + table + " BEGIN UPDATE sync_state SET generation=generation+1 WHERE singleton=1; END;"),
                    "MIGRATION_TRIGGER_FAILED");
            }
        }
        Check(db.Execute("PRAGMA user_version=1"), "MIGRATION_VERSION_FAILED");
        transaction.Commit();
        return true;
    } catch (const std::exception& error) {
        db.m_lastError = error.what();
        return false;
    }
}
}
