#include "sync/SyncClient.h"
#include "sync/DiffEngine.h"
#include "storage/VaultInstaller.h"
#include "storage/NoteRepository.h"
#include "common/Identity.h"
#include <fstream>
#include <iostream>

using namespace anynote;
using Json = nlohmann::json;
void Check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
Json Version(const Json& result) { return {{"operation_id", common::NewUuid()}, {"expected_epoch", result.at("epoch")}, {"expected_revision", result.at("revision")}}; }
std::string Query(const Json& result) { return "?epoch=" + result.at("epoch").get<std::string>() + "&revision=" + result.at("revision").get<std::string>(); }
int wmain(int argc, wchar_t** argv) {
    try {
        Check(argc == 2, "fixture directory required"); const std::filesystem::path fixture(argv[1]);
        const auto directory = fixture / common::NewUuid(); std::filesystem::create_directory(directory);
        std::ifstream stream(fixture / "ready.json"); auto endpoints = Json::parse(stream);
        sync::SyncClient a(endpoints.at("valid"), (fixture / "valid.crt").wstring()), b(endpoints.at("valid"), (fixture / "valid.crt").wstring());
        sync::TransferControl control;
        auto credentials = Json{{"username", "e2e_" + common::NewVaultUid()}, {"password", "isolated-password"}};
        a.Request("POST", "/auth/register", credentials, control);
        a.SetToken(a.Request("POST", "/auth/login", credentials, control).at("token"));
        b.SetToken(b.Request("POST", "/auth/login", credentials, control).at("token"));
        storage::Database dbA; Check(dbA.Open((directory / "A.anynote").wstring()), "A open"); storage::NoteRepository repoA(dbA);
        Check(repoA.InitializeSchema(), "A migrate"); auto parent = repoA.CreateNote(0, L"root", -1, 0, "{\\rtf1 root}"); Check(parent > 0, "A root");
        auto original = storage::SnapshotService::Export(dbA, "shared"); auto path = "/vaults/" + original.snapshot.uid;
        auto version = a.Upload(path + "/snapshot", {{"operation_id", common::NewUuid()}, {"create_only", true}}, original.snapshot, false, control);
        auto download = b.Download(path + "/snapshot" + Query(version), control);
        storage::VaultManager manager((directory / "anynote.ini").wstring()); Check(manager.Load(), "B config");
        const auto bPath = directory / "B.anynote";
        Check(storage::VaultInstaller::Install(manager, download.snapshot, bPath, L"device B", "B-encryption-password").empty(), "B install");
        storage::Database dbB; Check(dbB.Open(bPath.wstring(), "B-encryption-password"), "B open"); storage::NoteRepository repoB(dbB);
        auto localA = repoA.CreateNote(parent, L"offline A", -1, 0, std::string("{\\rtf1 A\0B}", 12));
        auto bRoot = storage::SnapshotService::Export(dbB, "shared").snapshot.nodes.front().sourceId;
        auto localB = repoB.CreateNote(bRoot, L"offline B", -1, 0, "{\\rtf1 independent B}");
        Check(localA == localB, "fixture did not collide integer IDs");
        auto snapshotA = storage::SnapshotService::Export(dbA, "shared"), snapshotB = storage::SnapshotService::Export(dbB, "shared");
        std::string syncA, syncB;
        for (const auto& n : snapshotA.snapshot.nodes) if (n.sourceId == localA) syncA = n.syncId;
        for (const auto& n : snapshotB.snapshot.nodes) if (n.sourceId == localB) syncB = n.syncId;
        Check(syncA != syncB, "offline sync IDs collided");
        version = a.Upload(path + "/nodes/" + syncA + "/subtree", Version(version), snapshotA.snapshot, true, control);
        version = b.Upload(path + "/nodes/" + syncB + "/subtree", Version(version), snapshotB.snapshot, true, control);
        auto finalA = a.Download(path + "/snapshot" + Query(version), control), finalB = b.Download(path + "/snapshot" + Query(version), control);
        storage::SnapshotService::Import(dbA, finalA.snapshot, snapshotA.generation, common::NewUuid());
        storage::SnapshotService::Import(dbB, finalB.snapshot, snapshotB.generation, common::NewUuid(), "B-encryption-password");
        auto aFinal = storage::SnapshotService::Export(dbA, "shared"), bFinal = storage::SnapshotService::Export(dbB, "shared");
        Check(aFinal.snapshot.nodes.size() == 3 && sync::Compare(aFinal.snapshot, bFinal.snapshot).empty(), "clients did not converge");
        Check(repoA.GetNode(localA)->title == L"offline A" && repoB.GetNode(localB)->title == L"offline B", "integer identity overwrote an unrelated note");
        Check(repoA.GetNoteContent(localA) == std::string("{\\rtf1 A\0B}", 12), "binary RTF changed");
        Check(dbB.IsEncrypted(), "download removed B encryption");
        std::cout << "Two independent HTTPS clients and vaults converged after equal-ID offline subtree uploads; binary data and encryption preserved\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
