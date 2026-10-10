#pragma once
#include "SyncProtocol.h"
#include <atomic>
#include <functional>
#include <string>

namespace anynote::sync {
struct TransferControl {
    std::atomic<bool> cancelled{false};
    std::atomic<int64_t> sent{0}, received{0};
};
class SyncClient {
public:
    SyncClient(std::string baseUrl, std::wstring caFile);
    static std::string NormalizeServerUrl(const std::string& baseUrl);
    void SetToken(std::string token);
    void ClearToken();
    bool IsLoggedIn() const { return !m_token.empty(); }
    const std::string& BaseUrl() const { return m_baseUrl; }
    nlohmann::json Request(const std::string& method, const std::string& path,
        const nlohmann::json& body, TransferControl& control);
    SnapshotResponse Download(const std::string& path, TransferControl& control);
    nlohmann::json Upload(const std::string& path, const nlohmann::json& precondition,
        const Snapshot& snapshot, bool subtree, TransferControl& control);
    ~SyncClient() { ClearToken(); }
private:
    void Transfer(const std::string& method, const std::string& path, TransferControl& control,
        const std::function<void(std::ostream&)>& upload, const std::function<void(std::istream&)>& download);
    std::string m_baseUrl, m_token;
    std::wstring m_caFile;
};
std::wstring DescribeSyncError(const std::string& code);
}
