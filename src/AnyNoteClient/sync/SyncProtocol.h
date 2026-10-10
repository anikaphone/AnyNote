#pragma once

#include <cstdint>
#include <functional>
#include <istream>
#include <optional>
#include <ostream>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace anynote::sync {

struct Limits {
    size_t maxNodes = 20000;
    size_t maxDepth = 128;
    size_t maxNodeBytes = 16 * 1024 * 1024;
    size_t maxVaultBytes = 128 * 1024 * 1024;
    size_t maxJsonBytes = 192 * 1024 * 1024;
};

struct Node {
    std::string syncId;
    int64_t sourceId = 0;
    std::optional<std::string> parentSyncId;
    int sequence = 0;
    std::string title;
    int nodeType = 0;
    int64_t createdTime = 0;
    int64_t modifiedTime = 0;
    int formatType = 1;
    std::string contentHash;
    std::string rtf;
};

struct Snapshot {
    std::string uid;
    std::string name;
    std::vector<Node> nodes;
};

// Throws on invalid input. A sink may write only to disposable staging storage:
// a later node or the final tree validation can still reject the entire stream.
Snapshot ReadSnapshot(std::istream& input, const Limits& limits = {},
    const std::function<void(const Node&)>& stagingSink = {});
void WriteSnapshot(std::ostream& output, const Snapshot& snapshot, const Limits& limits = {});
void ValidateTree(const std::vector<Node>& nodes, const Limits& limits = {});
std::string Sha256(const std::string& bytes);
std::string EncodeBase64(const std::string& bytes);
std::string DecodeBase64(const std::string& text, size_t maximum);
bool IsUuid(const std::string& value);
struct SnapshotResponse { Snapshot snapshot; nlohmann::json envelope; };
SnapshotResponse ReadSnapshotResponse(std::istream& input, const Limits& limits = {});

} // namespace anynote::sync
