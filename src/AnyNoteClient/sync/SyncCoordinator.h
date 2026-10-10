#pragma once
#include "SyncClient.h"
#include <filesystem>

namespace anynote::sync {
enum class SyncState { Idle, Saving, Planning, AwaitingConfirmation, Transferring, Validating, Committing, Refreshing, Failed };
class SyncCoordinator {
public:
    SyncState State() const { return m_state.load(); }
    void SetState(SyncState state) { m_state = state; }
    nlohmann::json Write(SyncClient& client, const std::string& account, const std::filesystem::path& directory,
        const std::string& uid, const std::string& operationId, TransferControl& control,
        const std::function<nlohmann::json()>& action);
    static std::vector<std::filesystem::path> Pending(const std::filesystem::path& directory, const std::string& server, const std::string& account);
    static nlohmann::json Query(SyncClient& client, const std::filesystem::path& journal, const std::string& account, TransferControl& control);
private:
    std::atomic<SyncState> m_state{SyncState::Idle};
};
}
