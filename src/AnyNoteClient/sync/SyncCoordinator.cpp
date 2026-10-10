#include "SyncCoordinator.h"
#include "storage/ConfigStore.h"

namespace anynote::sync {
nlohmann::json SyncCoordinator::Write(SyncClient& client, const std::string& account, const std::filesystem::path& directory,
    const std::string& uid, const std::string& operationId, TransferControl& control, const std::function<nlohmann::json()>& action) {
    std::filesystem::create_directories(directory);
    auto journal = directory / (operationId + ".json");
    storage::ConfigStore::Write(journal.wstring(), nlohmann::json{{"server", client.BaseUrl()}, {"account", account},
        {"uid", uid}, {"operation_id", operationId}}, "");
    m_state = SyncState::Transferring;
    try {
        auto result = action();
        std::error_code ec; std::filesystem::remove(journal, ec); m_state = SyncState::Idle; return result;
    } catch (const std::exception& error) {
        m_state = SyncState::Failed;
        const std::string code = error.what();
        if (code == "NETWORK_RESULT_UNKNOWN" || code == "TIMEOUT_RESULT_UNKNOWN" || code == "CANCELLED") {
            // Never repeat the mutation blindly. A separate bounded query can establish a commit.
            if (!control.cancelled) {
                try { auto result = Query(client, journal, account, control); m_state = SyncState::Idle; return result; } catch (...) {}
            }
        } else if (code == "VERSION_CONFLICT" || code == "UNAUTHORIZED" || code == "NOT_FOUND" ||
            code == "QUOTA_EXCEEDED" || code == "LIMIT_EXCEEDED" || code == "HASH_MISMATCH" || code == "OUTSIDE_SUBTREE") {
            std::error_code ec; std::filesystem::remove(journal, ec);
        }
        throw;
    }
}
std::vector<std::filesystem::path> SyncCoordinator::Pending(const std::filesystem::path& directory, const std::string& server, const std::string& account) {
    std::vector<std::filesystem::path> result;
    if (!std::filesystem::exists(directory)) return result;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (!entry.is_regular_file() || entry.path().extension() != L".json") continue;
        auto document = storage::ConfigStore::Read(entry.path().wstring()).value;
        if (document.at("server") == server && document.at("account") == account) result.push_back(entry.path());
    }
    return result;
}
nlohmann::json SyncCoordinator::Query(SyncClient& client, const std::filesystem::path& journal, const std::string& account, TransferControl& control) {
    const auto value = storage::ConfigStore::Read(journal.wstring()).value;
    if (value.at("server") != client.BaseUrl() || value.at("account") != account) throw std::runtime_error("ACCOUNT_MISMATCH");
    auto id = value.at("operation_id").get<std::string>(); if (!IsUuid(id)) throw std::runtime_error("INVALID_OPERATION_ID");
    auto result = client.Request("GET", "/operations/" + id, {}, control);
    if (result.at("status") != "committed" || result.at("operation_id") != id || result.at("uid") != value.at("uid")) throw std::runtime_error("INVALID_RESPONSE");
    std::error_code ec; std::filesystem::remove(journal, ec); return result;
}
}
