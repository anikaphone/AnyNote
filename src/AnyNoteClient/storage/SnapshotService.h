#pragma once
#include "Database.h"
#include "sync/SyncProtocol.h"
#include <filesystem>
#include <functional>

namespace anynote::storage {
struct VaultSnapshot {
    sync::Snapshot snapshot;
    int64_t generation = 0;
};
struct ImportResult {
    bool alreadyApplied = false;
    std::filesystem::path backup;
    std::string cleanupWarning;
};
// Throws on any failed validation or storage operation. Callers must prevent UI
// writes during the operation and must not retain statements on the database.
class SnapshotService {
public:
    static VaultSnapshot Export(Database& db, const std::string& name, const sync::Limits& limits = {});
    static ImportResult Import(Database& db, const sync::Snapshot& snapshot, int64_t expectedGeneration,
        const std::string& operationId, const std::string& password = "", const sync::Limits& limits = {},
        const std::function<bool()>& cancelled = {}, bool replaceUid = false);
};
}
