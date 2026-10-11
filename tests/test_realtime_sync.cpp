#include "common/Identity.h"
#include "common/StringUtils.h"
#include "storage/NoteRepository.h"
#include "storage/VaultInstaller.h"
#include "sync/CredentialStore.h"
#include "sync/RealtimeSync.h"
#include <fstream>
#include <iostream>
#include <sqlite3.h>
#include <thread>

using namespace anynote;
using Json = nlohmann::json;
void Check(bool ok, const char *reason)
{
    if (!ok)
        throw std::runtime_error(reason);
}
template <class F> void Until(sync::RealtimeSync &a, sync::RealtimeSync &b, F ready, const std::string &protectedB = "")
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(35);
    while (std::chrono::steady_clock::now() < deadline)
    {
        a.Tick();
        b.Tick(protectedB);
        auto ea = a.TakeEnableError(), eb = b.TakeEnableError();
        if (!ea.empty() || !eb.empty())
            throw std::runtime_error(ea + " / " + eb);
        if (ready())
            return;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    std::cerr << utils::WideToUtf8(a.Status()) << " / " << utils::WideToUtf8(b.Status()) << '\n';
    throw std::runtime_error("convergence timeout");
}
int wmain(int argc, wchar_t **argv)
{
    try
    {
        Check(argc == 2 || argc == 3, "fixture directory required");
        std::filesystem::path fixture(argv[1]);
        const auto directory = fixture / common::NewUuid();
        std::filesystem::create_directory(directory);
        std::ifstream input(fixture / "ready.json");
        auto endpoints = Json::parse(input);
        sync::SyncClient a(endpoints.at("valid"), (fixture / "valid.crt").wstring()),
            b(endpoints.at("valid"), (fixture / "valid.crt").wstring());
        sync::TransferControl control;
        auto credentials = Json{{"username", "rt_" + common::NewVaultUid()}, {"password", "isolated-password"}};
        a.Request("POST", "/auth/register", credentials, control);
        a.SetToken(a.Request("POST", "/auth/login", credentials, control).at("token"));
        b.SetToken(b.Request("POST", "/auth/login", credentials, control).at("token"));
        storage::Database dbA;
        Check(dbA.Open((directory / "A.anynote").wstring()), "open A");
        storage::NoteRepository repoA(dbA);
        Check(repoA.InitializeSchema(), "schema A");
        auto root = repoA.CreateNote(0, L"root", -1, 0, "{\\rtf1 root}"),
             other = repoA.CreateNote(0, L"other", -1, 0, "{\\rtf1 other}");
        auto initial = storage::SnapshotService::Export(dbA, "realtime");
        auto uid = initial.snapshot.uid;
        a.Upload("/vaults/" + uid + "/snapshot", {{"operation_id", common::NewUuid()}, {"create_only", true}},
                 initial.snapshot, false, control);
        storage::VaultManager manager((directory / "anynote.ini").wstring());
        Check(manager.Load(), "manager");
        Check(storage::VaultInstaller::Install(manager, initial.snapshot, directory / "B.anynote", L"B", "encrypted-B")
                  .empty(),
              "install B");
        storage::Database dbB;
        Check(dbB.Open((directory / "B.anynote").wstring(), "encrypted-B"), "open B");
        storage::NoteRepository repoB(dbB);
        storage::RealtimeStore sa(dbA), sb(dbB);
        auto meta = sa.Metadata();
        std::string rootId, otherId;
        for (const auto &n : initial.snapshot.nodes)
        {
            if (n.sourceId == root)
                rootId = n.syncId;
            else
                otherId = n.syncId;
        }
        auto idB = [&](const std::string &id) {
            storage::Database::Statement s;
            Check(s.Prepare(dbB, "SELECT id FROM nodes WHERE sync_id=?") && s.BindText(1, id) && s.Step() == SQLITE_ROW,
                  "B id");
            return s.GetInt64(0);
        };
        auto rootB = idB(rootId), otherB = idB(otherId);
        sync::RealtimeSync ra(dbA, a.Clone(), "fixture-account", uid), rb(dbB, b.Clone(), "fixture-account", uid);
        ra.Enable();
        rb.Enable();
        Until(ra, rb, [&] { return ra.Active() && rb.Active(); });
        std::cout << "gate passed" << std::endl;
        a.Request("POST", "/fixture/drop-next-response", {}, control);
        Check(repoA.UpdateNoteContent(root, "{\\rtf1 saved A}"), "save A");
        Until(ra, rb, [&] { return repoB.GetNoteContent(rootB) == "{\\rtf1 saved A}"; });
        std::cout << "save passed" << std::endl;
        Check(repoB.UpdateNoteTitle(otherB, L"renamed B"), "rename B");
        Until(ra, rb, [&] { return repoA.GetNode(other)->title == L"renamed B"; });
        std::cout << "rename passed" << std::endl;
        auto child = repoA.CreateNote(root, L"new child", -1, 0, "{\\rtf1 child}");
        Check(child > 0, "new child");
        Until(ra, rb, [&] { return repoB.GetNodeCount() == 3; });
        Check(repoA.DeleteNote(child), "delete child");
        Until(ra, rb, [&] { return repoB.GetNodeCount() == 2; });
        std::cout << "create/delete passed" << std::endl;
        // Concurrent saves to the same UUID must survive as a conflict on both devices.
        Check(repoA.UpdateNoteContent(root, "{\\rtf1 conflict A}"), "conflict A");
        Check(repoB.UpdateNoteContent(rootB, "{\\rtf1 conflict B}"), "conflict B");
        Until(ra, rb, [&] { return !ra.Conflicts().empty() || !rb.Conflicts().empty(); });
        Check(repoA.GetNoteContent(root) == "{\\rtf1 conflict A}" &&
                  repoB.GetNoteContent(rootB) == "{\\rtf1 conflict B}",
              "conflict overwrote local");
        // Unrelated notes still flow while the conflicting note remains untouched.
        Check(repoA.UpdateNoteTitle(other, L"independent update"), "independent change");
        Until(ra, rb, [&] { return repoB.GetNode(otherB)->title == L"independent update"; });
        Until(ra, rb, [&] { return !ra.Busy() && !rb.Busy(); });
        if (!ra.Conflicts().empty())
            ra.Resolve(rootId, false);
        if (!rb.Conflicts().empty())
            rb.Resolve(rootId, false);
        Until(ra, rb,
              [&] { return sa.Metadata() == sb.Metadata() && ra.Conflicts().empty() && rb.Conflicts().empty(); });
        // A dirty editor protects its row; other changes may still be applied.
        const auto before = repoB.GetNoteContent(rootB);
        Check(repoA.UpdateNoteContent(root, "{\\rtf1 remote protected}"), "protected save");
        Until(
            ra, rb,
            [&] {
                return !ra.Busy() &&
                       sa.Load().at("base").at(rootId).at("content_hash") == sync::Sha256("{\\rtf1 remote protected}");
            },
            rootId);
        Check(repoB.GetNoteContent(rootB) == before, "protected row overwritten");
        Until(ra, rb, [&] { return repoB.GetNoteContent(rootB) == "{\\rtf1 remote protected}"; });
        Check(dbB.IsEncrypted(), "encryption lost");
        // Failure inside apply rolls back both data and stored baseline.
        auto state = sb.Load(), old = sb.Metadata();
        auto changed = old.at(rootId);
        changed.update(sb.Body(rootId));
        changed["title"] = "blocked";
        Check(dbB.Execute("CREATE TRIGGER reject_rt BEFORE UPDATE ON nodes BEGIN SELECT RAISE(ABORT,'test'); END"),
              "fault trigger");
        bool rejected = false;
        try
        {
            sb.Apply({{rootId, changed}}, state);
        }
        catch (...)
        {
            rejected = true;
        }
        Check(rejected && old == sb.Metadata() && state == sb.Load(), "partial apply survived");
        Check(dbB.Execute("DROP TRIGGER reject_rt"), "drop fault");
        // Delete versus a concurrently created descendant is a structural
        // conflict, not permission to silently cascade-delete the new note.
        Check(repoA.DeleteNote(root), "remove parent");
        Check(repoB.CreateNote(rootB, L"concurrent child", -1, 0, "{\\rtf1 keep child}") > 0, "concurrent child");
        Until(ra, rb, [&] { return !ra.Conflicts().empty() || !rb.Conflicts().empty(); });
        Until(ra, rb, [&] { return !ra.Busy() && !rb.Busy(); });
        if (!ra.Conflicts().empty())
            ra.Resolve(ra.Conflicts().front().get<std::string>(), false);
        if (!rb.Conflicts().empty())
            rb.Resolve(rb.Conflicts().front().get<std::string>(), false);
        Until(ra, rb,
              [&] { return sa.Metadata() == sb.Metadata() && ra.Conflicts().empty() && rb.Conflicts().empty(); });
        ra.Stop();
        rb.Stop();
        // Recover a receipt lost after commit, including a newer local save.
        auto saved = sa.Load();
        auto nodes = sa.Metadata();
        auto chosen = nodes.begin().key();
        auto sent = nodes.at(chosen);
        sent["title"] = "lost response";
        sent.update(sa.Body(chosen));
        auto head = a.Request("GET", "/vaults/" + uid + "/sync-state", {}, control).at("vault");
        Json pending = {{"precondition",
                         {{"operation_id", common::NewUuid()},
                          {"expected_epoch", head.at("epoch")},
                          {"expected_revision", head.at("revision")}}},
                        {"upserts", Json::array({sent})},
                        {"deletes", Json::array()}};
        saved["pending"] = pending;
        sa.Save(saved);
        auto committed = a.Request("POST", "/vaults/" + uid + "/sync-batch", pending, control);
        sync::RealtimeSync recovered(dbA, a.Clone(), "fixture-account", uid);
        recovered.Enable();
        const auto recoveryDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (sa.Load().contains("pending") && std::chrono::steady_clock::now() < recoveryDeadline)
        {
            recovered.Tick();
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        Check(!sa.Load().contains("pending"), "lost response was not recovered");
        Check(a.Request("GET", "/vaults/" + uid + "/sync-state", {}, control).at("vault").at("revision") ==
                  committed.at("revision"),
              "recovery duplicated write");
        Check(sa.Metadata() == nodes, "receipt recovery overwrote newer local data");
        recovered.Stop();
        // Gate must reject a divergent library without applying either side.
        sync::RealtimeSync rejectedGate(dbA, a.Clone(), "fixture-account", uid);
        rejectedGate.Enable();
        std::string gateError;
        const auto gateDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (gateError.empty() && std::chrono::steady_clock::now() < gateDeadline)
        {
            rejectedGate.Tick();
            gateError = rejectedGate.TakeEnableError();
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        Check(gateError == "REALTIME_INITIAL_DIFFERENCE" && !rejectedGate.Active() && sa.Metadata() == nodes,
              "gate allowed divergent data");
        rejectedGate.Stop();
        if (argc == 3)
        {
            // Disposable native GUI fixture, with credentials protected by DPAPI.
            auto snapshot = storage::SnapshotService::Export(dbA, "Realtime GUI").snapshot;
            head = a.Request("GET", "/vaults/" + uid + "/sync-state", {}, control).at("vault");
            a.Upload("/vaults/" + uid + "/snapshot",
                     {{"operation_id", common::NewUuid()},
                      {"expected_epoch", head.at("epoch")},
                      {"expected_revision", head.at("revision")}},
                     snapshot, false, control);
            const auto gui = directory / "gui";
            std::filesystem::create_directory(gui);
            storage::VaultManager guiManager((gui / "anynote.ini").wstring());
            Check(guiManager.Load(), "GUI config");
            Check(dbA.CheckpointAndClose(), "GUI checkpoint");
            std::filesystem::copy_file(directory / "A.anynote", gui / "GUI.anynote");
            Check(guiManager.AddVault(L"实时同步测试", (gui / "GUI.anynote").wstring(), uid), "GUI register");
            Check(guiManager.SetActiveVaultPath((gui / "GUI.anynote").wstring()), "GUI active");
            Check(guiManager.SetConfigString(L"Sync", L"ServerUrl",
                                             utils::Utf8ToWide(endpoints.at("valid").get<std::string>())) &&
                      guiManager.SetConfigString(L"Sync", L"CaFile", (fixture / "valid.crt").wstring()) &&
                      guiManager.SetConfigString(L"Sync", L"Username",
                                                 utils::Utf8ToWide(credentials.at("username").get<std::string>())) &&
                      guiManager.SetConfigString(L"Sync", L"RememberPassword", L"1") &&
                      guiManager.SetConfigString(L"Sync", L"AutoLogin", L"1"),
                  "GUI settings");
            sync::CredentialStore::Save((gui / "credentials.dat").wstring(), endpoints.at("valid"),
                                        credentials.at("username"), credentials.at("password"));
            std::ofstream(fixture / "gui-ready.txt") << utils::WideToUtf8(gui.wstring());
            std::ofstream(fixture / "gui-account.json") << Json{{"username", credentials.at("username")}, {"uid", uid}};
        }
        std::cout << "Realtime HTTPS: enable gate, save, rename, create, delete, concurrent conflict, independent "
                     "progress, resolution, dirty-editor protection, encrypted apply and rollback passed\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
