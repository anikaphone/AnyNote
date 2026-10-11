#include "RealtimeSync.h"
#include "common/Identity.h"
#include <algorithm>
#include <ctime>
#include <set>
#include <utility>

namespace anynote::sync
{
using Json = nlohmann::json;
namespace
{
Json At(const Json &object, const std::string &id)
{
    return object.value(id, Json(nullptr));
}
void Put(Json &object, const std::string &id, const Json &value)
{
    if (value.is_null())
        object.erase(id);
    else
        object[id] = value;
}
Json ManifestNodes(const Json &manifest)
{
    Json nodes = Json::object();
    for (auto n : manifest.at("nodes"))
    {
        auto id = n.at("sync_id").get<std::string>();
        if (nodes.contains(id))
            throw std::runtime_error("DUPLICATE_SYNC_ID");
        nodes[id] = storage::RealtimeStore::Meta(n);
    }
    storage::RealtimeStore::Validate(nodes);
    return nodes;
}
std::wstring Clock()
{
    time_t now = time(nullptr);
    tm value{};
    localtime_s(&value, &now);
    wchar_t text[32]{};
    wcsftime(text, 32, L"%H:%M:%S", &value);
    return text;
}
} // namespace
RealtimeSync::RealtimeSync(storage::Database &db, std::unique_ptr<SyncClient> client, std::string binding,
                           std::string uid)
    : m_store(db), m_client(std::move(client)), m_binding(std::move(binding)), m_uid(std::move(uid)),
      m_state(m_store.Load())
{
}
RealtimeSync::~RealtimeSync()
{
    Stop();
}
void RealtimeSync::Stop()
{
    m_control.cancelled = true;
    if (m_future.valid())
    {
        try
        {
            m_future.get();
        }
        catch (...)
        {
        }
    }
    m_active = false;
    m_checking = false;
    m_status = L"实时同步已关闭";
    // The immutable pending request remains in the database. Never discard an
    // unknown result merely because the window/account/vault is closing.
}
void RealtimeSync::Enable()
{
    if (m_active || m_checking)
        return;
    if (!m_client || !m_client->IsLoggedIn())
        throw std::runtime_error("UNAUTHORIZED");
    if (m_state.contains("pending") && m_state.value("binding", "") != m_binding)
        throw std::runtime_error("REALTIME_PENDING_OTHER_ACCOUNT");
    m_checking = true;
    m_control.cancelled = false;
    m_status = L"正在检查云端一致性…";
    if (m_state.contains("pending"))
        LaunchUpload();
    else
    {
        // A persisted base/remote/vault triple is a trusted synchronization
        // baseline. Resume through the normal three-way comparison so changes
        // made while this device was offline can be pulled or pushed.
        const bool hasBaseline = m_state.contains("base") && m_state.contains("remote") &&
                                 m_state.contains("vault") && m_state.value("binding", "") == m_binding;
        Launch(!hasBaseline);
    }
}
Json RealtimeSync::Compare(const Json &base, const Json &local, const Json &remote)
{
    std::set<std::string> ids;
    for (const auto *map : {&base, &local, &remote})
        for (auto it = map->begin(); it != map->end(); ++it)
            ids.insert(it.key());
    Json result = {
        {"equal", Json::array()}, {"pull", Json::array()}, {"push", Json::array()}, {"conflicts", Json::array()}};
    for (const auto &id : ids)
    {
        auto b = At(base, id), l = At(local, id), r = At(remote, id);
        auto kind = l == r ? "equal" : l == b ? "pull" : r == b ? "push" : "conflicts";
        result[kind].push_back(id);
    }
    return result;
}
void RealtimeSync::Launch(bool gate)
{
    const auto generation = m_store.Generation();
    if (m_generation != generation || m_local.is_null())
        m_local = m_store.Metadata();
    m_generation = generation;
    m_observedGeneration = generation;
    m_upload = false;
    auto state = m_state, local = m_local;
    auto path = "/vaults/" + m_uid;
    m_control.cancelled = false;
    m_future = std::async(std::launch::async, [this, state, local, path, gate] {
        if (gate && m_client->Request("GET", "/capabilities", {}, m_control).value("realtime_version", 0) != 1)
            throw std::runtime_error("REALTIME_UPGRADE_REQUIRED");
        auto head = m_client->Request("GET", path + "/sync-state", {}, m_control);
        if (head.at("vault").at("uid") != m_uid)
            throw std::runtime_error("UID_MISMATCH");
        Json remote;
        if (!gate && state.at("vault").at("epoch") != head.at("vault").at("epoch"))
            throw std::runtime_error("REALTIME_EPOCH_CHANGED");
        if (!gate && state.at("vault").at("revision") == head.at("vault").at("revision"))
            remote = state.at("remote");
        else if (gate)
        {
            head = m_client->Request("GET", path + "/sync-manifest", {}, m_control);
            remote = ManifestNodes(head);
        }
        else
        {
            auto query = "?epoch=" + state.at("vault").at("epoch").get<std::string>() +
                         "&cursor=" + state.at("vault").at("revision").get<std::string>();
            try
            {
                head = m_client->Request("GET", path + "/changes" + query, {}, m_control);
                remote = state.at("remote");
                for (const auto &item : head.at("changes"))
                {
                    auto id = item.at("sync_id").get<std::string>();
                    if (item.at("deleted").get<bool>())
                        remote.erase(id);
                    else
                    {
                        if (item.at("node").at("sync_id") != id)
                            throw std::runtime_error("INVALID_RESPONSE");
                        remote[id] = storage::RealtimeStore::Meta(item.at("node"));
                    }
                }
                storage::RealtimeStore::Validate(remote);
            }
            catch (const std::runtime_error &e)
            {
                if (std::string(e.what()) != "CURSOR_EXPIRED")
                    throw;
                head = m_client->Request("GET", path + "/sync-manifest", {}, m_control);
                remote = ManifestNodes(head);
            }
        }
        if (!gate && state.at("vault").at("epoch") != head.at("vault").at("epoch"))
            throw std::runtime_error("REALTIME_EPOCH_CHANGED");
        if (head.at("vault").at("uid") != m_uid)
            throw std::runtime_error("UID_MISMATCH");
        Json result = {{"gate", gate}, {"vault", head.at("vault")}, {"remote", remote}, {"bodies", Json::object()}};
        if (gate)
            return result;
        auto plan = Compare(state.at("base"), local, remote);
        result["plan"] = plan;
        auto suffix = "?epoch=" + head.at("vault").at("epoch").get<std::string>() +
                      "&revision=" + head.at("vault").at("revision").get<std::string>();
        size_t bytes = 0;
        for (const auto &item : plan.at("pull"))
        {
            auto id = item.get<std::string>();
            if (!remote.contains(id))
            {
                result["bodies"][id] = nullptr;
                continue;
            }
            auto node = m_client->Request("GET", path + "/nodes/" + id + "/content" + suffix, {}, m_control);
            if (storage::RealtimeStore::Meta(node) != remote.at(id))
                throw std::runtime_error("INVALID_RESPONSE");
            auto body = DecodeBase64(node.at("rtf_base64"), Limits{}.maxNodeBytes);
            bytes += body.size();
            if (bytes > Limits{}.maxVaultBytes || Sha256(body) != node.at("content_hash").get<std::string>())
                throw std::runtime_error("INVALID_RESPONSE");
            result["bodies"][id] = std::move(node);
        }
        return result;
    });
}
void RealtimeSync::LaunchUpload()
{
    m_upload = true;
    m_control.cancelled = false;
    auto pending = m_state.at("pending");
    auto path = "/vaults/" + m_uid + "/sync-batch";
    m_future = std::async(std::launch::async,
                          [this, pending, path] { return m_client->Request("POST", path, pending, m_control); });
}
bool RealtimeSync::Consume(const Json &result, const std::string &protectedId)
{
    if (m_upload)
    {
        const auto pending = m_state.at("pending");
        if (result.at("operation_id") != pending.at("precondition").at("operation_id") || result.at("uid") != m_uid ||
            result.at("epoch") != pending.at("precondition").at("expected_epoch") || result.at("status") != "committed")
            throw std::runtime_error("INVALID_RESPONSE");
        auto acknowledged = m_state;
        for (const auto &n : pending.at("upserts"))
            acknowledged["base"][n.at("sync_id").get<std::string>()] = storage::RealtimeStore::Meta(n);
        for (const auto &id : pending.at("deletes"))
            acknowledged["base"].erase(id.get<std::string>());
        acknowledged.erase("pending");
        m_store.Apply(Json::object(), acknowledged);
        m_state = std::move(acknowledged);
        // Poll from the last read revision, not the write receipt: another device
        // may have changed independent nodes before/after our write.
        if (m_checking)
            Launch(true);
        else
            m_status = L"同步成功 · " + Clock();
        return false;
    }
    if (m_generation != m_store.Generation())
    {
        m_next = {};
        return false;
    }
    auto remote = result.at("remote");
    if (result.at("gate").get<bool>())
    {
        if (m_local != remote)
            throw std::runtime_error("REALTIME_INITIAL_DIFFERENCE");
        m_state = {{"binding", m_binding},
                   {"base", m_local},
                   {"remote", remote},
                   {"vault", result.at("vault")},
                   {"conflicts", Json::array()}};
        m_store.Apply(Json::object(), m_state);
        m_checking = false;
        m_active = true;
        m_status = L"实时同步已开启 · " + Clock();
        return false;
    }
    if (result.at("vault") == m_state.at("vault") && result.at("plan").at("pull").empty() &&
        result.at("plan").at("push").empty() && result.at("plan").at("conflicts") == m_state.at("conflicts"))
        return false;
    auto next = m_state;
    next["remote"] = remote;
    next["vault"] = result.at("vault");
    next["conflicts"] = result.at("plan").at("conflicts");
    next["structural_conflicts"] = Json::array();
    auto changes = result.at("bodies");
    bool deferred = false;
    if (!protectedId.empty() && changes.contains(protectedId))
    {
        changes.erase(protectedId);
        deferred = true;
    }
    auto final = m_local;
    for (auto it = changes.begin(); it != changes.end(); ++it)
        Put(final, it.key(), it.value().is_null() ? Json(nullptr) : storage::RealtimeStore::Meta(it.value()));
    try
    {
        storage::RealtimeStore::Validate(final);
    }
    catch (...)
    {
        // A changed child may depend on a concurrently deleted/moved parent.
        // Keep independent content edits flowing, and resolve the structural
        // group together so neither side can create an orphan.
        for (auto it = changes.begin(); it != changes.end();)
        {
            auto local = At(m_local, it.key());
            if (!local.is_null() && !it.value().is_null() &&
                local.at("parent_sync_id") == it.value().at("parent_sync_id"))
                ++it;
            else
            {
                next["structural_conflicts"].push_back(it.key());
                it = changes.erase(it);
            }
        }
        deferred = true;
    }
    for (const auto &item : result.at("plan").at("equal"))
    {
        auto id = item.get<std::string>();
        Put(next["base"], id, At(remote, id));
    }
    for (auto it = changes.begin(); it != changes.end(); ++it)
        Put(next["base"], it.key(), At(remote, it.key()));
    Json batch = {{"precondition",
                   {{"operation_id", common::NewUuid()},
                    {"expected_epoch", result.at("vault").at("epoch")},
                    {"expected_revision", result.at("vault").at("revision")}}},
                  {"upserts", Json::array()},
                  {"deletes", Json::array()}};
    auto serverFinal = remote;
    for (const auto &item : result.at("plan").at("push"))
    {
        auto id = item.get<std::string>();
        auto n = At(m_local, id);
        Put(serverFinal, id, n);
        if (n.is_null())
            batch["deletes"].push_back(id);
        else
        {
            n.update(m_store.Body(id));
            batch["upserts"].push_back(std::move(n));
        }
    }
    try
    {
        storage::RealtimeStore::Validate(serverFinal);
    }
    catch (...)
    {
        for (auto it = batch["upserts"].begin(); it != batch["upserts"].end();)
        {
            auto id = it->at("sync_id").get<std::string>();
            if (remote.contains(id) && remote.at(id).at("parent_sync_id") == it->at("parent_sync_id"))
                ++it;
            else
            {
                next["structural_conflicts"].push_back(id);
                it = batch["upserts"].erase(it);
            }
        }
        for (const auto &id : batch["deletes"])
            next["structural_conflicts"].push_back(id);
        batch["deletes"] = Json::array();
        deferred = true;
    }
    for (const auto &id : next["structural_conflicts"])
        if (std::find(next["conflicts"].begin(), next["conflicts"].end(), id) == next["conflicts"].end())
            next["conflicts"].push_back(id);
    const bool push = !batch["upserts"].empty() || !batch["deletes"].empty();
    if (push)
        next["pending"] = batch;
    m_store.Apply(changes, next);
    m_state = std::move(next);
    if (push)
    {
        m_status = L"正在同步…";
        LaunchUpload();
    }
    else if (!m_state.at("conflicts").empty())
        m_status = L"存在 " + std::to_wstring(m_state.at("conflicts").size()) + L" 个冲突 · 点击处理";
    else if (deferred)
        m_status = L"等待当前编辑保存 / 目录冲突处理";
    else if (!changes.empty())
        m_status = L"同步成功 · " + Clock();
    return !changes.empty();
}
bool RealtimeSync::Tick(const std::string &protectedId)
{
    if (!m_active && !m_checking)
        return false;
    try
    {
        const auto observed = m_store.Generation();
        if (observed != m_observedGeneration)
        {
            m_observedGeneration = observed;
            m_next = std::chrono::steady_clock::now() + std::chrono::milliseconds(400);
        }
        if (m_future.valid())
        {
            if (m_future.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
                return false;
            auto result = m_future.get();
            m_next = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            return Consume(result, protectedId);
        }
        if (std::chrono::steady_clock::now() < m_next)
            return false;
        if (m_state.contains("pending"))
            LaunchUpload();
        else
            Launch(m_checking);
    }
    catch (const std::exception &e)
    {
        std::string error = e.what();
        m_next = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        if (error == "VERSION_CONFLICT")
        {
            m_state.erase("pending");
            m_store.Save(m_state);
            m_status = L"云端发生变化，正在重新比较…";
        }
        else if (m_checking)
        {
            m_checking = false;
            m_enableError = error;
            m_status = L"实时同步未开启";
        }
        else if (error == "UNAUTHORIZED" || error == "REALTIME_EPOCH_CHANGED" || error == "NOT_FOUND")
        {
            m_active = false;
            m_status = L"同步已暂停 · 请登录或手动同步后重新开启";
        }
        else
            m_status = L"同步失败，待重试 · " + DescribeSyncError(error);
    }
    return false;
}
Json RealtimeSync::Conflicts() const
{
    return m_state.value("conflicts", Json::array());
}
void RealtimeSync::Resolve(const std::string &id, bool keepLocal)
{
    if (m_future.valid())
        throw std::runtime_error("REALTIME_BUSY");
    auto current = m_store.Metadata();
    auto group = m_state.value("structural_conflicts", Json::array());
    if (std::find(group.begin(), group.end(), Json(id)) == group.end())
        group = Json::array({id});
    for (const auto &item : group)
    {
        auto key = item.get<std::string>();
        Put(m_state["base"], key, keepLocal ? At(m_state.at("remote"), key) : At(current, key));
    }
    m_store.Save(m_state);
    m_next = {};
}
} // namespace anynote::sync
