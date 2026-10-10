#include "sync/SyncClient.h"
#include "sync/CredentialStore.h"
#include "sync/SyncCoordinator.h"
#include "common/Identity.h"
#include "common/StringUtils.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <chrono>
#include <thread>
#include <psapi.h>

using namespace anynote;
void Check(bool ok, const char* reason) { if (!ok) throw std::runtime_error(reason); }
int wmain(int argc, wchar_t** argv) {
    try {
        Check(sync::SyncClient::NormalizeServerUrl("HTTPS://Example.COM:443/") == "https://example.com", "server binding normalization");
        Check(sync::SyncClient::NormalizeServerUrl("https://Example.COM:8443/prefix/") == "https://example.com:8443/prefix", "server prefix normalization");
        Check(argc == 2 || argc == 3, "TLS fixture directory required");
        std::filesystem::path root(argv[1]); std::ifstream file(root / "ready.json");
        auto addresses = nlohmann::json::parse(file);
        sync::SyncClient client(addresses.at("valid").get<std::string>(), (root / "valid.crt").wstring());
        sync::TransferControl control;
        auto account = "client_" + common::NewVaultUid();
        auto credentials = nlohmann::json{{"username", account}, {"password", "isolated-test-password"}};
        client.Request("POST", "/auth/register", credentials, control);
        auto login = client.Request("POST", "/auth/login", credentials, control);
        client.SetToken(login.at("token"));
        auto snapshot = sync::Snapshot{common::NewVaultUid(), "中文 test", {}};
        sync::Node node; node.syncId = common::NewUuid(); node.sourceId = 23; node.title = "binary";
        node.rtf = std::string("{\\rtf1\0binary}", 15); node.contentHash = sync::Sha256(node.rtf);
        snapshot.nodes.push_back(node);
        auto path = "/vaults/" + snapshot.uid;
        auto precondition = nlohmann::json{{"operation_id", common::NewUuid()}, {"create_only", true}};
        auto result = client.Upload(path + "/snapshot", precondition, snapshot, false, control);
        auto retry = client.Upload(path + "/snapshot", precondition, snapshot, false, control);
        Check(result == retry, "retry mismatch");
        auto query = "?epoch=" + result.at("epoch").get<std::string>() + "&revision=" + result.at("revision").get<std::string>();
        auto downloaded = client.Download(path + "/snapshot" + query, control);
        Check(downloaded.snapshot.uid == snapshot.uid && downloaded.snapshot.nodes.size() == 1 &&
            downloaded.snapshot.nodes[0].rtf == node.rtf, "RTF roundtrip");
        // The same transport decodes the subtree envelope without retaining a Base64 DOM.
        auto subtree = client.Download(path + "/nodes/" + node.syncId + "/subtree" + query, control);
        Check(subtree.snapshot.nodes[0].rtf == node.rtf, "subtree decode");
        client.Request("POST", "/fixture/drop-next-response", nlohmann::json::object(), control);
        auto update = nlohmann::json{{"operation_id", common::NewUuid()}, {"expected_epoch", result.at("epoch")}, {"expected_revision", result.at("revision")}};
        snapshot.nodes[0].title = "response lost after commit";
        sync::SyncCoordinator coordinator;
        auto recovered = coordinator.Write(client, account, root / account, snapshot.uid, update.at("operation_id"), control,
            [&] { return client.Upload(path + "/snapshot", update, snapshot, false, control); });
        Check(recovered.at("revision") == "2" && sync::SyncCoordinator::Pending(root / account, client.BaseUrl(), account).empty(), "lost response recovery");
        auto protectedFile = (root / (account + ".dat")).wstring();
        sync::CredentialStore::Save(protectedFile, client.BaseUrl(), account, "isolated-test-password");
        Check(sync::CredentialStore::Load(protectedFile, client.BaseUrl(), account) == "isolated-test-password", "DPAPI roundtrip");
        Check(sync::CredentialStore::Load(protectedFile, client.BaseUrl(), "another").empty(), "account credential leak");
        Check(sync::CredentialStore::Load(protectedFile, "https://another", account).empty(), "server credential leak");
        sync::CredentialStore::Erase(protectedFile); Check(!std::filesystem::exists(protectedFile), "credential removal");
        if (argc == 3 && std::wstring(argv[2]) == L"--scale") {
            auto start = std::chrono::steady_clock::now();
            sync::Snapshot large{common::NewVaultUid(), "network upper limits", {}};
            const auto emptyHash = sync::Sha256("");
            for (int i = 0; i < 20000; ++i) {
                sync::Node n; n.syncId = common::NewUuid(); n.sourceId = i + 1; n.title = "scale";
                if (i < 8) n.rtf.assign(16 * 1024 * 1024, static_cast<char>('a' + i));
                n.contentHash = n.rtf.empty() ? emptyHash : sync::Sha256(n.rtf); large.nodes.push_back(std::move(n));
            }
            auto largePath = "/vaults/" + large.uid;
            auto uploaded = client.Upload(largePath + "/snapshot", {{"operation_id", common::NewUuid()}, {"create_only", true}}, large, false, control);
            auto roundTrip = client.Download(largePath + "/snapshot?epoch=" + uploaded.at("epoch").get<std::string>() + "&revision=1", control);
            Check(roundTrip.snapshot.nodes.size() == 20000, "network node limit roundtrip");
            size_t bytes = 0; for (const auto& n : roundTrip.snapshot.nodes) { bytes += n.rtf.size(); Check(sync::Sha256(n.rtf) == n.contentHash, "network scale hash"); }
            Check(bytes == 128 * 1024 * 1024, "network byte limit roundtrip");
            roundTrip = {};
            PROCESS_MEMORY_COUNTERS memory{sizeof(memory)};
            GetProcessMemoryInfo(GetCurrentProcess(), &memory, sizeof(memory));
            std::cout << "network_scale nodes=20000 bytes=" << bytes << " elapsed_ms=" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count()
                << " peak_working_set=" << memory.PeakWorkingSetSize << '\n';
            large.uid = common::NewVaultUid(); sync::TransferControl interrupted; std::atomic<bool> finished{false};
            std::atomic<int64_t> cancelledAt{0};
            std::jthread cancel([&] { while (!finished) { if (interrupted.sent > 1024 * 1024) { cancelledAt = GetTickCount64(); interrupted.cancelled = true; return; } std::this_thread::sleep_for(std::chrono::milliseconds(10)); } });
            try { client.Upload("/vaults/" + large.uid + "/snapshot", {{"operation_id", common::NewUuid()}, {"create_only", true}}, large, false, interrupted); finished = true; throw std::runtime_error("network cancel not observed"); }
            catch (const std::runtime_error& e) { finished = true; Check(std::string(e.what()) == "CANCELLED", e.what()); }
            cancel.join(); auto latency = GetTickCount64() - cancelledAt.load(); Check(cancelledAt > 0 && latency < 5000, "cancel response exceeded 5 seconds");
            try { client.Request("GET", "/vaults/" + large.uid + "/manifest", {}, control); throw std::runtime_error("cancelled body committed"); }
            catch (const std::runtime_error& e) { Check(std::string(e.what()) == "NOT_FOUND", e.what()); }
            std::cout << "network_cancel latency_ms=" << latency << " source_unchanged=true\n";
        }
        sync::TransferControl cancelled; cancelled.cancelled = true;
        try { client.Request("GET", "/vaults", {}, cancelled); throw std::runtime_error("cancellation ignored"); }
        catch (const std::runtime_error& e) { Check(std::string(e.what()) == "CANCELLED", e.what()); }
        client.Request("POST", "/auth/logout", nlohmann::json::object(), control);
        try { client.Request("GET", "/vaults", {}, control); throw std::runtime_error("revoked token accepted"); }
        catch (const std::runtime_error& e) { Check(std::string(e.what()) == "UNAUTHORIZED", e.what()); }
        Check(!client.IsLoggedIn(), "expired session not cleared");
        std::cout << "HTTPS streaming upload/download, lost-response recovery, operation retry, binary RTF, DPAPI isolation and cancellation passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
