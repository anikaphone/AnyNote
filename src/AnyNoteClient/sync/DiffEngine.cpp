#include "DiffEngine.h"
#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>

namespace anynote::sync {
namespace {
using Index = std::map<std::string, const Node*>;
Index MakeIndex(const Snapshot& snapshot) {
    ValidateTree(snapshot.nodes);
    Index index;
    for (const auto& node : snapshot.nodes) index.emplace(node.syncId, &node);
    return index;
}
std::set<std::string> Descendants(const Snapshot& snapshot, const std::string& root) {
    std::map<std::string, std::vector<std::string>> children;
    for (const auto& node : snapshot.nodes)
        if (node.parentSyncId) children[*node.parentSyncId].push_back(node.syncId);
    std::set<std::string> result;
    std::vector<std::string> pending{root};
    while (!pending.empty()) {
        auto id = pending.back(); pending.pop_back();
        if (!result.insert(id).second) continue;
        auto found = children.find(id);
        if (found != children.end()) pending.insert(pending.end(), found->second.begin(), found->second.end());
    }
    return result;
}
}

std::vector<DiffEntry> Compare(const Snapshot& local, const Snapshot& remote) {
    if (local.uid != remote.uid) throw std::runtime_error("UID_MISMATCH");
    auto left = MakeIndex(local), right = MakeIndex(remote);
    std::vector<DiffEntry> result;
    for (const auto& [id, node] : left) {
        auto found = right.find(id);
        if (found == right.end()) { result.push_back({id, LocalOnly}); continue; }
        const auto& other = *found->second;
        unsigned flags = 0;
        if (node->title != other.title) flags |= Title;
        if (node->contentHash != other.contentHash) flags |= Content;
        if (node->parentSyncId != other.parentSyncId || node->sequence != other.sequence) flags |= Position;
        if (node->createdTime != other.createdTime || node->modifiedTime != other.modifiedTime) flags |= Time;
        if (node->nodeType != other.nodeType || node->formatType != other.formatType) flags |= Format;
        if (flags) result.push_back({id, flags});
    }
    for (const auto& [id, node] : right) if (!left.contains(id)) result.push_back({id, RemoteOnly});
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.syncId < b.syncId; });
    return result;
}

Snapshot PlanSubtree(const Snapshot& target, const Snapshot& source, const std::string& root) {
    if (source.uid != target.uid) throw std::runtime_error("UID_MISMATCH");
    const auto incoming = MakeIndex(source), existing = MakeIndex(target);
    if (!incoming.contains(root)) throw std::runtime_error("NOT_FOUND");
    auto selected = Descendants(source, root);
    auto replaced = existing.contains(root) ? Descendants(target, root) : std::set<std::string>{};
    for (const auto& id : selected) {
        if (!existing.contains(id) || replaced.contains(id)) continue;
        for (const auto& child : Descendants(target, id)) {
            if (!selected.contains(child) && !replaced.contains(child)) throw std::runtime_error("OUTSIDE_SUBTREE");
        }
    }
    auto parent = incoming.at(root)->parentSyncId;
    while (parent) {
        if (!existing.contains(*parent)) selected.insert(*parent);
        parent = incoming.at(*parent)->parentSyncId;
    }
    Snapshot result{target.uid, target.name, {}};
    int64_t nextId = 0;
    std::set<std::optional<std::string>> affectedParents;
    for (const auto& node : target.nodes) {
        nextId = std::max(nextId, node.sourceId);
        if (!selected.contains(node.syncId) && !replaced.contains(node.syncId)) result.nodes.push_back(node);
        else affectedParents.insert(node.parentSyncId);
    }
    for (const auto& id : selected) {
        auto node = *incoming.at(id);
        auto old = existing.find(id);
        if (old != existing.end()) node.sourceId = old->second->sourceId;
        else {
            if (nextId == INT64_MAX) throw std::runtime_error("ID_EXHAUSTED");
            node.sourceId = ++nextId;
        }
        affectedParents.insert(node.parentSyncId);
        result.nodes.push_back(std::move(node));
    }
    // Source ordering interleaves with untouched siblings; untouched siblings
    // retain their relative order. All changed sequences appear in Compare.
    for (const auto& parentId : affectedParents) {
        std::vector<Node*> siblings;
        for (auto& node : result.nodes) if (node.parentSyncId == parentId) siblings.push_back(&node);
        std::stable_sort(siblings.begin(), siblings.end(), [&](const Node* a, const Node* b) {
            if (a->sequence != b->sequence) return a->sequence < b->sequence;
            if (selected.contains(a->syncId) != selected.contains(b->syncId)) return selected.contains(a->syncId);
            return a->sourceId < b->sourceId;
        });
        int sequence = 0;
        for (auto* node : siblings) node->sequence = sequence++;
    }
    ValidateTree(result.nodes);
    return result;
}
} // namespace anynote::sync
