#pragma once
#include "SyncProtocol.h"

namespace anynote::sync {
enum Difference : unsigned {
    LocalOnly = 1, RemoteOnly = 2, Title = 4, Content = 8,
    Position = 16, Time = 32, Format = 64
};
struct DiffEntry { std::string syncId; unsigned differences; };
std::vector<DiffEntry> Compare(const Snapshot& local, const Snapshot& remote);
// Produces a disposable preview. Throws if moving a source node would silently
// affect descendants outside the selected target subtree.
Snapshot PlanSubtree(const Snapshot& target, const Snapshot& source, const std::string& root);
} // namespace anynote::sync
