#include "storage/SnapshotService.h"
#include "storage/NoteRepository.h"
#include "storage/VaultInstaller.h"
#include "common/Identity.h"
#include <sqlite3.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>

using namespace anynote;
void Check(bool ok, const char* reason) { if (!ok) throw std::runtime_error(reason); }
template<class F> void Reject(F action, const std::string& expected) {
    try { action(); } catch (const std::exception& e) { Check(e.what() == expected, e.what()); return; }
    throw std::runtime_error("unexpected success");
}
int64_t Number(storage::Database& db, const char* sql) {
    storage::Database::Statement s; Check(s.Prepare(db,sql)&&s.Step()==SQLITE_ROW,"number"); return s.GetInt64(0);
}
void Run(const std::filesystem::path& dir, bool encrypted) {
    const std::string password = encrypted ? "isolated-test-password" : "";
    auto path = dir / (encrypted ? L"encrypted.anynote" : L"plain.anynote");
    storage::Database db; Check(db.Open(path.wstring(), password), "open");
    storage::NoteRepository repo(db); Check(repo.InitializeSchema(password), "schema");
    auto root = repo.CreateNote(0, L"root");
    auto child = repo.CreateNote(root, L"child", -1, 0, "{\\rtf1 before}");
    auto remove = repo.CreateNote(0, L"removed"); Check(root>0&&child>0&&remove>0,"fixture");
    auto before = storage::SnapshotService::Export(db,"test");
    auto remote = before.snapshot;
    std::string rootSync, childSync;
    for(const auto& n : remote.nodes) { if(n.sourceId==root)rootSync=n.syncId; if(n.sourceId==child)childSync=n.syncId; }
    std::erase_if(remote.nodes,[&](const auto& n){return n.sourceId==remove;});
    for(auto& n:remote.nodes) {
        n.sourceId += 1000; // Other device integer IDs are unrelated.
        if(n.syncId==childSync) { n.rtf=std::string("{\\rtf1 a\0b}",12); n.contentHash=sync::Sha256(n.rtf); n.modifiedTime=42; }
    }
    sync::Node added; added.syncId=common::NewUuid(); added.sourceId=child; // Collides with an existing local ID.
    added.parentSyncId=rootSync; added.sequence=1; added.title="added"; added.contentHash=sync::Sha256("");
    remote.nodes.push_back(added);
    auto invalid=remote; invalid.nodes.back().parentSyncId=common::NewUuid();
    Reject([&]{storage::SnapshotService::Import(db,invalid,before.generation,common::NewUuid(),password);},"MISSING_PARENT");
    auto wrong=remote; wrong.uid="0123456789abcdef";
    Reject([&]{storage::SnapshotService::Import(db,wrong,before.generation,common::NewUuid(),password);},"UID_MISMATCH");
    const auto cancelledOperation=common::NewUuid(); int calls=0;
    Reject([&]{storage::SnapshotService::Import(db,remote,before.generation,cancelledOperation,password,{},[&]{return ++calls==3;});},"CANCELLED");
    Check(repo.GetNode(remove).has_value() && repo.GetNoteContent(child)=="{\\rtf1 before}" &&
        Number(db,"SELECT count(*) FROM sync_operations")==0,"cancel modified source");
    // A trigger copied into staging rejects the import before touching the source.
    Check(db.Execute("CREATE TRIGGER reject_import BEFORE INSERT ON nodes BEGIN SELECT RAISE(ABORT,'fault'); END"),"trigger");
    Reject([&]{storage::SnapshotService::Import(db,remote,before.generation,common::NewUuid(),password);},"IMPORT_NODE_FAILED");
    Check(repo.GetNode(remove).has_value() && Number(db,"SELECT generation FROM sync_state")==before.generation,"rehearsal modified source");
    Check(db.Execute("DROP TRIGGER reject_import"),"drop trigger");
    const auto result=storage::SnapshotService::Import(db,remote,before.generation,cancelledOperation,password);
    Check(!result.alreadyApplied && std::filesystem::exists(result.backup),"backup missing");
    auto after=storage::SnapshotService::Export(db,"test");
    Check(after.snapshot.uid==before.snapshot.uid && after.snapshot.nodes.size()==3,"identity/count");
    for(const auto& n:after.snapshot.nodes) {
        if(n.syncId==childSync) Check(n.sourceId==child&&n.modifiedTime==42&&n.rtf==std::string("{\\rtf1 a\0b}",12),"existing ID/time/binary lost");
        if(n.syncId==added.syncId) Check(n.sourceId>remove&&n.parentSyncId==rootSync,"new ID/parent mapping");
    }
    Check(!repo.GetNode(remove).has_value(),"removed retained");
    Check(storage::SnapshotService::Import(db,remote,before.generation,cancelledOperation,password).alreadyApplied,"retry applied twice");
    remote.nodes.back().title="different";
    Reject([&]{storage::SnapshotService::Import(db,remote,before.generation,cancelledOperation,password);},"OPERATION_ID_REUSED");
    Reject([&]{storage::SnapshotService::Import(db,remote,before.generation,common::NewUuid(),password);},"LOCAL_CHANGED");
    storage::Database backup; Check(backup.Open(result.backup.wstring(),password),"backup open");
    Check(Number(backup,"SELECT count(*) FROM nodes")==3 && Number(backup,"SELECT count(*) FROM sync_operations")==0,"backup not original");
    Check(backup.CheckpointAndClose(),"backup close");
    if(encrypted) { storage::Database wrongKey; Check(!wrongKey.Open(result.backup.wstring()),"backup lost encryption"); }
    // Race after rehearsal, before the real transaction: a committed local edit survives.
    auto race=storage::SnapshotService::Export(db,"test"); calls=0;
    // Calls: entry, after backup, each staging node, staging precommit, each real node.
    Reject([&]{storage::SnapshotService::Import(db,race.snapshot,race.generation,common::NewUuid(),password,{},[&]{
        if(++calls==static_cast<int>(race.snapshot.nodes.size())+3) Check(repo.UpdateNoteTitle(root,L"concurrent edit"),"race write");
        return false;
    });},"LOCAL_CHANGED");
    Check(repo.GetNode(root)->title==L"concurrent edit","concurrent write lost");
    Check(db.CheckpointAndClose()&&db.Open(path.wstring(),password),"reopen");
    Check(Number(db,"SELECT count(*) FROM sync_operations")==1,"receipt persistence");
    // Rotation keeps the newest ten verified recovery copies after successful commits.
    for(int i=0;i<11;++i) {
        auto current=storage::SnapshotService::Export(db,"test");
        auto applied=storage::SnapshotService::Import(db,current.snapshot,current.generation,common::NewUuid(),password);
        Check(applied.cleanupWarning.empty(),"rotation failed");
    }
    size_t count=0;
    for(const auto& file:std::filesystem::directory_iterator(dir/L".sync-backups"))
        if(file.path().extension()==L".anynote"&&file.path().filename().string().starts_with(before.snapshot.uid+"-"))++count;
    Check(count==10,"rotation count");
    auto installDirectory=dir/(encrypted?L"install-encrypted":L"install-plain");
    std::filesystem::create_directory(installDirectory);
    storage::VaultManager manager((installDirectory/L"anynote.ini").wstring());
    Check(manager.Load(),"install config");
    storage::VaultManager stale((installDirectory/L"anynote.ini").wstring()); Check(stale.Load(),"stale config");
    Check(manager.SetConfigString(L"test",L"change",L"new"),"config change");
    auto installedPath=installDirectory/L"download.anynote";
    auto copy=storage::SnapshotService::Export(db,"download").snapshot;
    auto journal=storage::VaultInstaller::Install(stale,copy,installedPath,L"download",password);
    Check(!journal.empty()&&std::filesystem::exists(installedPath)&&std::filesystem::exists(journal),"registration recovery journal missing");
    Reject([&]{storage::VaultInstaller::Install(manager,copy,installedPath,L"overwrite",password);},"INSTALL_DESTINATION_EXISTS");
    storage::VaultInstaller::Recover(manager,journal,password);
    Check(!std::filesystem::exists(journal)&&manager.FindVaultsByUid(copy.uid).size()==1,"registration recovery failed");
    auto unrelated=storage::VaultInstaller::AsNew(copy);
    Check(unrelated.uid!=copy.uid&&unrelated.nodes.front().syncId!=copy.nodes.front().syncId,"as-new identity reused");
    Check(storage::VaultInstaller::Install(manager,unrelated,installDirectory/L"new.anynote",L"new",password).empty(),"new install failed");
    storage::Database installed;
    Check(installed.Open(installedPath.wstring(),password),"installed open");
    auto installedSnapshot=storage::SnapshotService::Export(installed,"download");
    Check(installedSnapshot.snapshot.uid==copy.uid&&installedSnapshot.snapshot.nodes.size()==copy.nodes.size(),"installed identity/content");
}
int wmain(int argc,wchar_t** argv) {
    try {
        Check(argc==2,"scratch directory required"); std::filesystem::path dir(argv[1]);
        Check(std::filesystem::create_directory(dir),"scratch exists");
        Run(dir,false); Run(dir,true);
        std::cout<<"snapshot ID remapping, binary RTF, encrypted backup, rollback, generation, idempotency and rotation passed\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
