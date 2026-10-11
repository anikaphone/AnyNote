#pragma once
#include "Database.h"
#include <nlohmann/json.hpp>

namespace anynote::storage
{
// All methods run on the owning UI thread. Network workers only receive values.
class RealtimeStore
{
  public:
    using Json = nlohmann::json;
    explicit RealtimeStore(Database &db);
    Json Metadata();
    Json Body(const std::string &id);
    int64_t Generation();
    Json Load();
    void Save(const Json &state);
    // Apply a complete, validated set of remote changes and save the cursor/base
    // atomically. UUIDs keep their local integer IDs; unrelated nodes are untouched.
    void Apply(const Json &changes, const Json &state);
    static Json Meta(Json node);
    static void Validate(const Json &nodes);

  private:
    Database &m_db;
};
} // namespace anynote::storage
