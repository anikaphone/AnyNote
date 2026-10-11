#include "RealtimeStore.h"
#include "NoteRepository.h"
#include "sync/SyncProtocol.h"
#include <map>
#include <source_location>
#include <sqlite3.h>
#include <stdexcept>

namespace anynote::storage
{
namespace
{
void Check(bool ok, const std::source_location where = std::source_location::current())
{
    if (!ok)
        throw std::runtime_error("REALTIME_STORAGE_FAILED:" + std::to_string(where.line()));
}
using Json = nlohmann::json;
using Statement = Database::Statement;
struct Transaction
{
    Database &db;
    bool committed = false;
    explicit Transaction(Database &value) : db(value)
    {
        Check(db.Execute("BEGIN IMMEDIATE"));
    }
    ~Transaction()
    {
        if (!committed)
            db.Rollback();
    }
    void Commit()
    {
        Check(db.Commit());
        committed = true;
    }
};
Statement Prepare(Database &db, const std::string &sql)
{
    Statement s;
    Check(s.Prepare(db, sql));
    return s;
}
} // namespace
RealtimeStore::RealtimeStore(Database &db) : m_db(db)
{
    Transaction tx(db);
    Check(db.Execute(R"SQL(
CREATE TABLE IF NOT EXISTS realtime_state(singleton INTEGER PRIMARY KEY CHECK(singleton=1), value TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS realtime_outbox(sync_id TEXT PRIMARY KEY);
CREATE TABLE IF NOT EXISTS realtime_history(id INTEGER PRIMARY KEY, sync_id TEXT NOT NULL, metadata TEXT NOT NULL, rtf BLOB NOT NULL);
CREATE TRIGGER IF NOT EXISTS realtime_node_insert AFTER INSERT ON nodes BEGIN INSERT INTO realtime_outbox SELECT new.sync_id WHERE NOT EXISTS(SELECT 1 FROM realtime_outbox WHERE sync_id=new.sync_id); END;
CREATE TRIGGER IF NOT EXISTS realtime_node_update AFTER UPDATE ON nodes BEGIN INSERT INTO realtime_outbox SELECT new.sync_id WHERE NOT EXISTS(SELECT 1 FROM realtime_outbox WHERE sync_id=new.sync_id); END;
CREATE TRIGGER IF NOT EXISTS realtime_node_delete BEFORE DELETE ON nodes BEGIN INSERT INTO realtime_outbox SELECT old.sync_id WHERE NOT EXISTS(SELECT 1 FROM realtime_outbox WHERE sync_id=old.sync_id); END;
CREATE TRIGGER IF NOT EXISTS realtime_content_update AFTER UPDATE ON node_contents BEGIN INSERT INTO realtime_outbox SELECT sync_id FROM nodes WHERE id=new.node_id AND sync_id NOT IN(SELECT sync_id FROM realtime_outbox); END;
CREATE TRIGGER IF NOT EXISTS realtime_content_insert AFTER INSERT ON node_contents BEGIN INSERT INTO realtime_outbox SELECT sync_id FROM nodes WHERE id=new.node_id AND sync_id NOT IN(SELECT sync_id FROM realtime_outbox); END;
CREATE TRIGGER IF NOT EXISTS realtime_content_delete BEFORE DELETE ON node_contents BEGIN INSERT INTO realtime_outbox SELECT sync_id FROM nodes WHERE id=old.node_id AND sync_id NOT IN(SELECT sync_id FROM realtime_outbox); END;
)SQL"));
    tx.Commit();
}
Json RealtimeStore::Meta(Json n)
{
    n.erase("source_id");
    n.erase("rtf_base64");
    return n;
}
Json RealtimeStore::Metadata()
{
    auto s = Prepare(
        m_db,
        R"SQL(SELECT n.sync_id,p.sync_id,n.sequence,n.title,n.node_type,n.created_time,n.modified_time,c.format_type,c.content_hash
FROM nodes n JOIN node_contents c ON c.node_id=n.id LEFT JOIN nodes p ON p.id=n.parent_id ORDER BY n.sync_id)SQL");
    Json nodes = Json::object();
    int rc;
    while ((rc = s.Step()) == SQLITE_ROW)
    {
        auto id = s.GetText(0), parent = s.GetText(1);
        nodes[id] = {{"sync_id", id},
                     {"parent_sync_id", parent.empty() ? Json(nullptr) : Json(parent)},
                     {"sequence", s.GetInt(2)},
                     {"title", s.GetText(3)},
                     {"node_type", s.GetInt(4)},
                     {"created_time", std::to_string(s.GetInt64(5))},
                     {"modified_time", std::to_string(s.GetInt64(6))},
                     {"format_type", s.GetInt(7)},
                     {"content_hash", s.GetText(8)}};
    }
    Check(rc == SQLITE_DONE);
    return nodes;
}
Json RealtimeStore::Body(const std::string &id)
{
    auto s = Prepare(
        m_db,
        R"SQL(SELECT n.id,c.content_rtf FROM nodes n JOIN node_contents c ON c.node_id=n.id WHERE n.sync_id=?)SQL");
    Check(s.BindText(1, id));
    auto rc = s.Step();
    if (rc == SQLITE_DONE)
        return nullptr;
    Check(rc == SQLITE_ROW);
    return {{"source_id", std::to_string(s.GetInt64(0))}, {"rtf_base64", sync::EncodeBase64(s.GetBlob(1))}};
}
int64_t RealtimeStore::Generation()
{
    auto s = Prepare(m_db, "SELECT generation FROM sync_state WHERE singleton=1");
    Check(s.Step() == SQLITE_ROW);
    return s.GetInt64(0);
}
Json RealtimeStore::Load()
{
    auto s = Prepare(m_db, "SELECT value FROM realtime_state WHERE singleton=1");
    auto rc = s.Step();
    if (rc == SQLITE_DONE)
        return Json::object();
    Check(rc == SQLITE_ROW);
    return Json::parse(s.GetText(0));
}
void RealtimeStore::Save(const Json &state)
{
    auto s = Prepare(
        m_db, "INSERT INTO realtime_state VALUES(1,?) ON CONFLICT(singleton) DO UPDATE SET value=excluded.value");
    Check(s.BindText(1, state.dump()) && s.Step() == SQLITE_DONE);
}
void RealtimeStore::Validate(const Json &nodes)
{
    std::vector<sync::Node> values;
    int64_t id = 0;
    for (const auto &n : nodes)
    {
        sync::Node node;
        node.syncId = n.at("sync_id");
        node.sourceId = ++id;
        if (!n.at("parent_sync_id").is_null())
            node.parentSyncId = n.at("parent_sync_id").get<std::string>();
        node.sequence = n.at("sequence");
        values.push_back(std::move(node));
    }
    sync::ValidateTree(values);
}
void RealtimeStore::Apply(const Json &changes, const Json &state)
{
    Transaction tx(m_db);
    auto current = Metadata(), final = current;
    for (auto it = changes.begin(); it != changes.end(); ++it)
    {
        if (it.value().is_null())
            final.erase(it.key());
        else
            final[it.key()] = Meta(it.value());
    }
    Validate(final);
    std::map<std::string, int64_t> ids;
    {
        auto s = Prepare(m_db, "SELECT sync_id,id FROM nodes");
        int rc;
        while ((rc = s.Step()) == SQLITE_ROW)
            ids[s.GetText(0)] = s.GetInt64(1);
        Check(rc == SQLITE_DONE);
    }
    // Preserve the old body before every replacement/deletion, in the encrypted
    // database itself. Retain the most recent ten revisions per note.
    for (auto it = changes.begin(); it != changes.end(); ++it)
    {
        if (!current.contains(it.key()))
            continue;
        auto history = Prepare(m_db, "INSERT INTO realtime_history(sync_id,metadata,rtf) SELECT ?,?,content_rtf FROM "
                                     "node_contents WHERE node_id=?");
        Check(history.BindText(1, it.key()) && history.BindText(2, current[it.key()].dump()) &&
              history.BindInt64(3, ids.at(it.key())) && history.Step() == SQLITE_DONE);
        auto prune = Prepare(m_db, "DELETE FROM realtime_history WHERE sync_id=? AND id NOT IN (SELECT id FROM "
                                   "realtime_history WHERE sync_id=? ORDER BY id DESC LIMIT 10)");
        Check(prune.BindText(1, it.key()) && prune.BindText(2, it.key()) && prune.Step() == SQLITE_DONE);
    }
    // Allocate all parents before installing edges; validate the final graph above.
    for (auto it = changes.begin(); it != changes.end(); ++it)
        if (!it.value().is_null() && !ids.contains(it.key()))
        {
            auto s = Prepare(m_db,
                             "INSERT INTO nodes(parent_id,sequence,title,node_type,created_time,modified_time,sync_id) "
                             "VALUES(0,0,'',0,0,0,?)");
            Check(s.BindText(1, it.key()) && s.Step() == SQLITE_DONE);
            ids[it.key()] = sqlite3_last_insert_rowid(m_db.GetRawHandle());
        }
    for (auto it = changes.begin(); it != changes.end(); ++it)
    {
        const auto &n = it.value();
        if (n.is_null())
            continue;
        auto rtf = sync::DecodeBase64(n.at("rtf_base64"), sync::Limits{}.maxNodeBytes);
        Check(sync::Sha256(rtf) == n.at("content_hash").get<std::string>());
        auto s = Prepare(
            m_db,
            "UPDATE nodes SET parent_id=?,sequence=?,title=?,node_type=?,created_time=?,modified_time=? WHERE id=?");
        Check(
            s.BindInt64(1, n.at("parent_sync_id").is_null() ? 0 : ids.at(n.at("parent_sync_id").get<std::string>())) &&
            s.BindInt(2, n.at("sequence")) && s.BindText(3, n.at("title").get<std::string>()) &&
            s.BindInt(4, n.at("node_type")) && s.BindInt64(5, std::stoll(n.at("created_time").get<std::string>())) &&
            s.BindInt64(6, std::stoll(n.at("modified_time").get<std::string>())) && s.BindInt64(7, ids.at(it.key())) &&
            s.Step() == SQLITE_DONE);
        auto c = Prepare(m_db, "INSERT INTO node_contents(node_id,format_type,content_rtf,plain_text,content_hash) "
                               "VALUES(?,?,?,?,?) ON CONFLICT(node_id) DO UPDATE SET "
                               "format_type=excluded.format_type,content_rtf=excluded.content_rtf,plain_text=excluded."
                               "plain_text,content_hash=excluded.content_hash");
        Check(c.BindInt64(1, ids.at(it.key())) && c.BindInt(2, n.at("format_type")) &&
              c.BindBlob(3, rtf.data(), rtf.size()) && c.BindText(4, NoteRepository::DerivePlainText(rtf)) &&
              c.BindText(5, n.at("content_hash").get<std::string>()));
        if (c.Step() != SQLITE_DONE)
            throw std::runtime_error(std::string("REALTIME_STORAGE_FAILED:") + sqlite3_errmsg(m_db.GetRawHandle()));
    }
    for (auto it = changes.begin(); it != changes.end(); ++it)
        if (it.value().is_null() && ids.contains(it.key()))
        {
            auto c = Prepare(m_db, "DELETE FROM node_contents WHERE node_id=?");
            Check(c.BindInt64(1, ids.at(it.key())) && c.Step() == SQLITE_DONE);
            auto s = Prepare(m_db, "DELETE FROM nodes WHERE id=?");
            Check(s.BindInt64(1, ids.at(it.key())) && s.Step() == SQLITE_DONE);
        }
    if (!changes.empty())
        Check(m_db.Execute(
            "DELETE FROM realtime_history WHERE id IN (SELECT id FROM (SELECT id,sum(length(rtf)+length(metadata)) "
            "OVER (ORDER BY id DESC) AS bytes FROM realtime_history) WHERE bytes>134217728)"));
    Save(state);
    // An acknowledgement only clears entries whose current value equals the
    // acknowledged base. A later save remains queued, even after a lost response.
    const auto &base = state.at("base");
    auto clear = Prepare(m_db, "DELETE FROM realtime_outbox WHERE sync_id=?");
    auto queued = Prepare(m_db, "SELECT sync_id FROM realtime_outbox");
    std::vector<std::string> done;
    int rc;
    while ((rc = queued.Step()) == SQLITE_ROW)
    {
        auto id = queued.GetText(0);
        if (final.value(id, Json(nullptr)) == base.value(id, Json(nullptr)))
            done.push_back(id);
    }
    Check(rc == SQLITE_DONE);
    queued = {};
    for (const auto &id : done)
        Check(clear.Reset() && clear.BindText(1, id) && clear.Step() == SQLITE_DONE);
    tx.Commit();
}
} // namespace anynote::storage
