#include "storage/Database.h"
#include "storage/NoteRepository.h"
#include "storage/SchemaMigrator.h"
#include "sync/SyncProtocol.h"
#include <sqlite3.h>
#include <windows.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>

using namespace anynote::storage;
void Check(bool valid, const char* error) { if (!valid) throw std::runtime_error(error); }
std::string Text(Database& db, const char* sql) {
    Database::Statement s;
    Check(s.Prepare(db, sql) && s.Step() == SQLITE_ROW, "query");
    return s.GetText(0);
}
int64_t Number(Database& db, const char* sql) {
    Database::Statement s;
    Check(s.Prepare(db, sql) && s.Step() == SQLITE_ROW, "number");
    return s.GetInt64(0);
}
const char* Legacy = R"sql(
CREATE TABLE nodes(id INTEGER PRIMARY KEY AUTOINCREMENT,parent_id INTEGER NOT NULL DEFAULT 0,
sequence INTEGER NOT NULL DEFAULT 0,title TEXT NOT NULL,node_type INTEGER NOT NULL DEFAULT 0,
created_time INTEGER NOT NULL,modified_time INTEGER NOT NULL);
CREATE TABLE node_contents(node_id INTEGER PRIMARY KEY,format_type INTEGER NOT NULL DEFAULT 1,content_rtf BLOB,
FOREIGN KEY(node_id) REFERENCES nodes(id) ON DELETE CASCADE);
INSERT INTO nodes VALUES(1,0,0,'root',0,100,200),(2,1,0,'child',0,101,201),(100,0,1,'deleted',0,102,202);
DELETE FROM nodes WHERE id=100;
INSERT INTO node_contents VALUES(1,1,NULL),(2,1,X'7b5c72746631006162637d');
)sql";
void Migrate(const std::filesystem::path& dir, bool encrypted) {
    const auto file=dir/(encrypted?L"encrypted.anynote":L"plain.anynote");
    const std::string password=encrypted?"test-only-encrypted-secret":"";
    Database db;
    Check(db.Open(file.wstring(),password) && db.Execute(Legacy), "legacy fixture");
    // Inject failure inside the migration transaction after copying the first node.
    Check(!SchemaMigrator::Migrate(db,password,[](const std::string&)->std::wstring {
        throw std::runtime_error("INJECTED_EXTRACT_FAILURE");
    }), "failure injection");
    Check(Number(db,"PRAGMA user_version")==0 && Number(db,"SELECT count(*) FROM nodes")==2, "rollback legacy schema");
    Check(Number(db,"SELECT count(*) FROM sqlite_master WHERE name='uid'")==0, "partial migration published");
    Check(Number(db,"SELECT count(*) FROM sqlite_master WHERE name='nodes_v1'")==0, "staging schema survived rollback");
    NoteRepository repository(db);
    Check(repository.InitializeSchema(password), db.GetLastError().c_str());
    const auto uid=Text(db,"SELECT value FROM uid WHERE singleton=1");
    const auto syncId=Text(db,"SELECT sync_id FROM nodes WHERE id=2");
    const auto hash=Text(db,"SELECT content_hash FROM node_contents WHERE node_id=2");
    Check(uid.size()==16 && anynote::sync::IsUuid(syncId), "identity format");
    Check(hash==anynote::sync::Sha256(repository.GetNoteContent(2)), "migrated hash");
    Check(Text(db,"SELECT content_hash FROM node_contents WHERE node_id=1")==anynote::sync::Sha256(""), "NULL hash");
    Check(Number(db,"SELECT modified_time FROM nodes WHERE id=2")==201, "time changed during migration");
    Check(Number(db,"SELECT parent_id FROM nodes WHERE id=2")==1, "parent changed");
    Check(repository.InitializeSchema(password), "idempotent migration");
    Check(Text(db,"SELECT value FROM uid")==uid && Text(db,"SELECT sync_id FROM nodes WHERE id=2")==syncId, "identity regenerated");
    const auto id=repository.CreateNote(0,L"after migration");
    Check(id>100, "AUTOINCREMENT high-water mark lost");
    auto before=Number(db,"SELECT generation FROM sync_state");
    std::string rtf("{\\rtf1\0updated}", 16);
    Check(repository.UpdateNoteContent(2,rtf), "update content");
    Check(Text(db,"SELECT content_hash FROM node_contents WHERE node_id=2")==anynote::sync::Sha256(rtf), "edit hash stale");
    Check(Number(db,"SELECT generation FROM sync_state")>before, "generation unchanged");
    Check(db.Execute("CREATE TRIGGER reject_time BEFORE UPDATE ON nodes BEGIN SELECT RAISE(ABORT,'fault'); END"), "install trigger");
    Check(!repository.UpdateNoteContent(2,"new bytes"), "failed time write reported success");
    Check(repository.GetNoteContent(2)==rtf && Text(db,"SELECT content_hash FROM node_contents WHERE node_id=2")==anynote::sync::Sha256(rtf), "content/hash transaction torn");
    Check(db.Execute("DROP TRIGGER reject_time"), "drop fault trigger");
    Check(!db.Execute("INSERT INTO nodes(parent_id,sequence,title,node_type,created_time,modified_time) VALUES(0,0,'bad',0,0,0)"), "missing sync_id accepted");
    Check(db.CheckpointAndClose(), "close migrated");
    Check(db.Open(file.wstring(),password), "reopen migrated");
    Check(Text(db,"SELECT value FROM uid")==uid, "UID lost after reopen");
    if (encrypted) { Database wrong; Check(!wrong.Open(file.wstring(),"wrong"), "wrong password accepted"); }
    size_t backups=0;
    for(const auto& entry:std::filesystem::directory_iterator(dir/L".sync-backups")) {
        if(entry.path().extension()!=L".anynote") continue;
        Database backup;
        if(!backup.Open(entry.path().wstring(),password)) continue;
        Check(Number(backup,"PRAGMA user_version")==0 && Number(backup,"SELECT count(*) FROM nodes")==2, "backup modified");
        ++backups;
    }
    Check(backups>=2, "missing pre-migration backups");
}
void Child(const std::filesystem::path& path, bool locked) {
    wchar_t executable[32768]{}; Check(GetModuleFileNameW(nullptr,executable,32768)!=0,"executable path");
    std::wstring command=L"\""+std::wstring(executable)+L"\" "+(locked?L"--expect-locked":L"--expect-open")+L" \""+path.wstring()+L"\"";
    STARTUPINFOW startup{sizeof(startup)}; PROCESS_INFORMATION process{};
    Check(CreateProcessW(executable,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process)!=FALSE,"child start");
    CloseHandle(process.hThread);
    const auto wait=WaitForSingleObject(process.hProcess,10000);
    if(wait!=WAIT_OBJECT_0) TerminateProcess(process.hProcess,1);
    DWORD exitCode=1; GetExitCodeProcess(process.hProcess,&exitCode); CloseHandle(process.hProcess);
    Check(wait==WAIT_OBJECT_0 && exitCode==0,"cross-process lease");
}
int wmain(int argc,wchar_t** argv) {
    try {
        if(argc==3) {
            Database db; bool opened=db.Open(argv[2]);
            if(std::wstring(argv[1])==L"--expect-locked") return !opened && db.GetLastError()=="VAULT_IN_USE_BY_ANOTHER_PROCESS"?0:1;
            return opened?0:1;
        }
        Check(argc==2,"scratch path required");
        std::filesystem::path dir(argv[1]); Check(std::filesystem::create_directory(dir),"scratch exists");
        Migrate(dir,false); Migrate(dir,true);
        const auto file=dir/L"lease.anynote";
        Database db; Check(db.Open(file.wstring()),"lease database");
        Child(file,true);
        {
            Database::Statement live; Check(live.Prepare(db,"SELECT 1"),"live statement");
            db.Close(); Child(file,true);
        }
        Child(file,false);
        Check(db.Open(file.wstring()) && db.Execute("PRAGMA user_version=999"),"future schema fixture");
        db.Close(); Check(!db.Open(file.wstring()) && db.GetLastError()=="UNSUPPORTED_SCHEMA_VERSION","future schema accepted");
        std::cout << "legacy/encrypted migration, rollback, stable identity/hash, generation and process lease passed\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
}
