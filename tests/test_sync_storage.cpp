#include "storage/Database.h"
#include <sqlite3.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

using anynote::storage::Database;
void Check(bool valid, const char* error) { if (!valid) throw std::runtime_error(error); }
std::string ReadBlob(Database& db) {
    Database::Statement query;
    Check(query.Prepare(db, "SELECT data FROM probe WHERE id=1"), "prepare read");
    Check(query.Step() == SQLITE_ROW, "read row");
    return query.GetBlob(0);
}
void Probe(const std::filesystem::path& directory, bool encrypted) {
    auto file = directory / (encrypted ? L"encrypted.anynote" : L"plain.anynote");
    auto copy = directory / (encrypted ? L"encrypted-backup.anynote" : L"plain-backup.anynote");
    std::string password = encrypted ? "isolated-test-password" : "";
    Database db;
    Check(db.Open(file.wstring(), password), "open");
    Check(db.Execute("CREATE TABLE probe(id INTEGER PRIMARY KEY, data BLOB NOT NULL);"), "create");
    const std::string bytes("{\\rtf1\0image\xff}", 15);
    {
        Database::Statement insert;
        Check(insert.Prepare(db, "INSERT INTO probe VALUES(1, ?)"), "prepare insert");
        Check(insert.BindBlob(1, bytes.data(), bytes.size()), "bind binary");
        Check(insert.Step() == SQLITE_DONE, "insert");
        Check(!db.CheckpointAndClose(), "backup accepted live statement");
    }
    Check(db.BeginTransaction(), "begin");
    Check(db.Execute("DELETE FROM probe"), "logical import delete");
    Check(!db.Execute("INSERT INTO probe VALUES(2, NULL)"), "fault injection not rejected");
    Check(!db.CheckpointAndClose(), "backup accepted transaction");
    Check(db.Rollback(), "rollback");
    Check(ReadBlob(db) == bytes, "import rollback lost RTF");
    // A WAL reader pinned before a new write prevents a complete checkpoint.
    {
        Database reader;
        Check(reader.Open(file.wstring(), password), "reader open");
        Check(reader.BeginTransaction(), "reader begin");
        Check(ReadBlob(reader) == bytes, "reader snapshot");
        Check(db.Execute("INSERT INTO probe VALUES(2, X'00')"), "write after reader");
        Check(!db.CheckpointAndClose(), "backup accepted incomplete WAL checkpoint");
        Check(reader.Rollback(), "reader rollback");
    }
    Check(db.CheckpointAndClose() && !db.IsOpen(), "checkpoint and close");
    Check(std::filesystem::copy_file(file, copy, std::filesystem::copy_options::none), "closed file copy");
    if (encrypted) {
        std::ifstream input(copy, std::ios::binary); char header[16]{}; input.read(header, sizeof(header));
        Check(std::string(header, 16) != std::string("SQLite format 3\0", 16), "backup is plaintext");
        Database wrong;
        Check(!wrong.Open(copy.wstring()) && !wrong.Open(copy.wstring(), "wrong"), "backup encryption lost");
    }
    Database restored;
    Check(restored.Open(copy.wstring(), password), "reopen backup with original password");
    Check(ReadBlob(restored) == bytes, "backup RTF mismatch");
    Check(restored.Execute("PRAGMA integrity_check"), "backup integrity query");
    Database::Statement integrity;
    Check(integrity.Prepare(restored, "PRAGMA integrity_check") && integrity.Step() == SQLITE_ROW &&
        integrity.GetText(0) == "ok", "backup integrity failed");
}
int wmain(int argc, wchar_t** argv) {
    try {
        Check(argc == 2, "new scratch directory required");
        std::filesystem::path dir(argv[1]);
        Check(std::filesystem::create_directory(dir), "scratch directory already exists");
        Probe(dir, false); Probe(dir, true);
        std::cout << "plaintext/encrypted closed-file backup, WAL reader refusal and rollback passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
