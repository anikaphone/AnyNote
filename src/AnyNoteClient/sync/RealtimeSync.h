#pragma once
#include "SyncClient.h"
#include "storage/RealtimeStore.h"
#include <chrono>
#include <future>
#include <utility>

namespace anynote::sync
{
class RealtimeSync
{
  public:
    using Json = nlohmann::json;
    RealtimeSync(storage::Database &db, std::unique_ptr<SyncClient> client, std::string binding, std::string uid);
    ~RealtimeSync();
    void Enable();
    void Stop();
    // Called on the UI thread. Returns true only after local nodes changed.
    bool Tick(const std::string &protectedId = "");
    bool Active() const
    {
        return m_active;
    }
    bool Checking() const
    {
        return m_checking;
    }
    bool Ready() const
    {
        return m_future.valid() && m_future.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
    }
    bool Busy() const
    {
        return m_future.valid();
    }
    const std::wstring &Status() const
    {
        return m_status;
    }
    std::string TakeEnableError()
    {
        return std::exchange(m_enableError, "");
    }
    Json Conflicts() const;
    void Resolve(const std::string &id, bool keepLocal);
    static Json Compare(const Json &base, const Json &local, const Json &remote);

  private:
    void Launch(bool gate);
    void LaunchUpload();
    bool Consume(const Json &result, const std::string &protectedId);
    storage::RealtimeStore m_store;
    std::unique_ptr<SyncClient> m_client;
    std::string m_binding, m_uid, m_enableError;
    Json m_state, m_local;
    std::future<Json> m_future;
    TransferControl m_control;
    int64_t m_generation = -1;
    int64_t m_observedGeneration = -1;
    bool m_active = false, m_checking = false, m_upload = false;
    std::wstring m_status = L"实时同步已关闭";
    std::chrono::steady_clock::time_point m_next{};
};
} // namespace anynote::sync
