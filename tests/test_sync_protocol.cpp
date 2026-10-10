#include "sync/SyncProtocol.h"
#include "sync/DiffEngine.h"
#include "sync/RequestIdentity.h"
#include <nlohmann/json.hpp>
#include <windows.h>
#include <psapi.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

using namespace anynote::sync;
using Json = nlohmann::json;
void Check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
template<class F> void Reject(F action) {
    bool rejected = false;
    try { action(); } catch (const std::exception&) { rejected = true; }
    Check(rejected, "invalid input accepted");
}
Node Sample(int n) {
    Node node;
    char id[37];
    snprintf(id, sizeof(id), "00000000-0000-4000-8000-%012x", n);
    node.syncId = id; node.sourceId = n;
    node.contentHash = Sha256("");
    return node;
}
void Fixtures(const std::filesystem::path& directory) {
    std::ifstream vectors(directory.parent_path() / "request-identity/v1.json", std::ios::binary);
    Json identities; vectors >> identities;
    for (const auto& entry : identities) {
        Check(RequestHash(entry.at("descriptor")) == entry.at("sha256").get<std::string>(), "request identity mismatch");
        auto changed = entry.at("descriptor"); changed["path"] = "/different";
        Check(RequestHash(changed) != entry.at("sha256").get<std::string>(), "request identity collision");
    }
    Reject([] { RequestHash(Json{{"operation_id", "forbidden"}}); });
    Reject([] { RequestHash(Json{{"value", 1.5}}); });
    int count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (entry.path().extension() != ".json") continue;
        bool valid = entry.path().filename().string().starts_with("valid-");
        bool accepted = false;
        try { std::ifstream stream(entry.path(), std::ios::binary); ReadSnapshot(stream); accepted = true; }
        catch (const std::exception&) {}
        if (valid != accepted) throw std::runtime_error("fixture: " + entry.path().filename().string());
        ++count;
    }
    Check(count >= 8, "missing fixtures");
    std::ifstream input(directory / "valid-tree.json", std::ios::binary);
    auto original = ReadSnapshot(input);
    Check(original.nodes[0].sourceId == INT64_MAX, "int64 precision lost");
    original.nodes[1].rtf = std::string("{\\rtf1\0abc\xff}", 13);
    original.nodes[1].contentHash = Sha256(original.nodes[1].rtf);
    std::ostringstream output; WriteSnapshot(output, original);
    std::istringstream again(output.str());
    auto restored = ReadSnapshot(again);
    Check(restored.nodes[1].rtf == original.nodes[1].rtf, "binary RTF changed");
    Json base = Json::parse(output.str());
    for (const auto& field : {"source_id", "sequence", "title", "node_type", "created_time", "format_type", "content_hash", "rtf_base64"}) {
        auto invalid = base; invalid["nodes"][0][field] = nullptr;
        Reject([&] { std::istringstream s(invalid.dump()); ReadSnapshot(s); });
    }
    for (const auto& bad : {"01", "-1", "9223372036854775808", "1.0", "+2"}) {
        auto invalid = base; invalid["nodes"][0]["source_id"] = bad;
        Reject([&] { std::istringstream s(invalid.dump()); ReadSnapshot(s); });
    }
    auto invalid = base; invalid["nodes"][1]["content_hash"] = std::string(64, '0');
    Reject([&] { std::istringstream s(invalid.dump()); ReadSnapshot(s); });
    Limits small; small.maxJsonBytes = output.str().size() - 1;
    Reject([&] { std::istringstream s(output.str()); ReadSnapshot(s, small); });
    Reject([&] { std::ostringstream s; WriteSnapshot(s, original, small); });
    small = {}; small.maxNodes = 1;
    Reject([&] { std::istringstream s(output.str()); ReadSnapshot(s, small); });
    small = {}; small.maxDepth = 1;
    Reject([&] { std::istringstream s(output.str()); ReadSnapshot(s, small); });
    small = {}; small.maxNodeBytes = original.nodes[1].rtf.size() - 1;
    Reject([&] { std::istringstream s(output.str()); ReadSnapshot(s, small); });
    small = {}; small.maxVaultBytes = original.nodes[1].rtf.size() - 1;
    Reject([&] { std::istringstream s(output.str()); ReadSnapshot(s, small); });
    Check(Sha256("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "SHA-256 known vector");
    std::cout << "fixtures=" << count << " binary/int64/null/hash/size checks passed\n";
}
void Identity() {
    Snapshot a{"0123456789abcdef", "test", {Sample(1)}};
    Snapshot b = a; b.nodes[0].sourceId = 20;
    Check(Compare(a, b).empty(), "local integer ID caused false difference");
    b.nodes[0] = Sample(2); b.nodes[0].sourceId = 1;
    Check(Compare(a, b).size() == 2, "offline integer collision paired unrelated notes");
    b = a; b.nodes.push_back(Sample(2)); b.nodes[1].parentSyncId = a.nodes[0].syncId;
    Snapshot empty{a.uid, a.name, {}};
    auto result = PlanSubtree(empty, b, b.nodes[1].syncId);
    Check(result.nodes.size() == 2, "missing ancestor not added");
    auto c = b; c.nodes.push_back(Sample(3)); c.nodes[2].parentSyncId = a.nodes[0].syncId;
    result = PlanSubtree(c, b, b.nodes[0].syncId);
    Check(result.nodes.size() == 2, "target-only descendant not removed");
    c.nodes[1].parentSyncId.reset();
    c.nodes[2].parentSyncId = c.nodes[1].syncId;
    Reject([&] { PlanSubtree(c, b, b.nodes[0].syncId); });
    b.nodes[0].parentSyncId = b.nodes[1].syncId;
    Reject([&] { ValidateTree(b.nodes); });
    b.nodes[0].parentSyncId = Sample(9).syncId;
    Reject([&] { ValidateTree(b.nodes); });
    std::cout << "offline identity, ancestor, subtree deletion and outside-subtree guards passed\n";
}
class Discard : public std::streambuf {
public: size_t bytes = 0;
protected:
    std::streamsize xsputn(const char*, std::streamsize n) override { bytes += static_cast<size_t>(n); return n; }
    int_type overflow(int_type c) override { if (!traits_type::eq_int_type(c, traits_type::eof())) ++bytes; return traits_type::not_eof(c); }
};
void Scale() {
    // Streaming input generated in memory by a streambuf: never put plaintext
    // protocol packages on disk and never allocate an entire Base64 snapshot.
    struct Generated : std::streambuf {
        size_t index = 0;
        std::string chunk;
        int_type underflow() override {
            if (gptr() && gptr() < egptr()) return traits_type::to_int_type(*gptr());
            if (index == 0) chunk = "{\"protocol_version\":1,\"schema_version\":1,\"uid\":\"0123456789abcdef\",\"name\":\"scale\",\"nodes\":[";
            else if (index <= 20000) {
                auto n = Sample(static_cast<int>(index));
                // Eight maximum-size notes, plus 19,992 empty nodes.
                if (index <= 8) n.rtf.assign(16 * 1024 * 1024, 'x');
                n.contentHash = Sha256(n.rtf);
                Snapshot one{"0123456789abcdef", "", {std::move(n)}};
                std::ostringstream stream; WriteSnapshot(stream, one);
                chunk = stream.str();
                auto start = chunk.find("\"nodes\":[") + 9;
                chunk = chunk.substr(start, chunk.size() - start - 2);
                if (index != 1) chunk.insert(chunk.begin(), ',');
            } else if (index == 20001) chunk = "]}";
            else return traits_type::eof();
            ++index;
            setg(chunk.data(), chunk.data(), chunk.data() + chunk.size());
            return traits_type::to_int_type(*gptr());
        }
    } buffer;
    auto start = std::chrono::steady_clock::now();
    std::istream input(&buffer);
    size_t bytes = 0;
    auto snapshot = ReadSnapshot(input, {}, [&](const Node& node) { bytes += node.rtf.size(); });
    Check(snapshot.nodes.size() == 20000 && bytes == 128 * 1024 * 1024, "scale mismatch");
    PROCESS_MEMORY_COUNTERS counters{};
    Check(GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)) != FALSE, "memory measurement failed");
    std::cout << "scale nodes=20000 raw_bytes=" << bytes << " peak_working_set=" << counters.PeakWorkingSetSize
        << " elapsed_ms=" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count() << '\n';
}
int wmain(int argc, wchar_t** argv) {
    try {
        if (argc == 2 && std::wstring(argv[1]) == L"--scale") Scale();
        else { Check(argc == 2, "fixture directory required"); Fixtures(argv[1]); Identity(); }
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
