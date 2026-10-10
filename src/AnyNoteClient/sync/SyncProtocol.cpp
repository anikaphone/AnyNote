#include "SyncProtocol.h"
#include <windows.h>
#include <bcrypt.h>
#include <wincrypt.h>
#include <nlohmann/json.hpp>
#include <array>
#include <charconv>
#include <limits>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace anynote::sync {
namespace {
using Json = nlohmann::json;
void Require(bool valid, const char* error) {
    if (!valid) throw std::runtime_error(error);
}
bool Hex(const std::string& text, size_t length) {
    return text.size() == length && text.find_first_not_of("0123456789abcdef") == std::string::npos;
}
int64_t Decimal(const Json& value, bool positive = false) {
    Require(value.is_string(), "INVALID_INTEGER");
    auto text = value.get<std::string>();
    Require(!text.empty() && (text == "0" || text[0] != '0') &&
        text.find_first_not_of("0123456789") == std::string::npos, "INVALID_INTEGER");
    int64_t result = 0;
    auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
    Require(parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() &&
        (!positive || result > 0), "INVALID_INTEGER");
    return result;
}
size_t Characters(const std::string& text) {
    // The JSON parser validates UTF-8 before invoking the SAX handler.
    size_t count = 0;
    for (unsigned char c : text) if ((c & 0xc0) != 0x80) ++count;
    return count;
}
int SmallInteger(const Json& value, int maximum) {
    Require(value.is_number_integer(), "INVALID_INTEGER");
    Require(value >= 0 && value <= maximum, "INVALID_INTEGER");
    return value.get<int>();
}
Node ParseNode(const Json& value, const Limits& limits) {
    static const std::set<std::string> fields = {"sync_id", "source_id", "parent_sync_id",
        "sequence", "title", "node_type", "created_time", "modified_time", "format_type",
        "content_hash", "rtf_base64"};
    Require(value.is_object() && value.size() == fields.size(), "INVALID_NODE");
    for (const auto& field : fields) Require(value.contains(field), "INVALID_NODE");
    Node node;
    node.syncId = value.at("sync_id").get<std::string>();
    Require(IsUuid(node.syncId), "INVALID_UUID");
    node.sourceId = Decimal(value.at("source_id"), true);
    if (!value.at("parent_sync_id").is_null()) {
        node.parentSyncId = value.at("parent_sync_id").get<std::string>();
        Require(IsUuid(*node.parentSyncId), "INVALID_UUID");
    }
    node.sequence = SmallInteger(value.at("sequence"), INT_MAX);
    node.title = value.at("title").get<std::string>();
    Require(Characters(node.title) <= 1024, "LIMIT_EXCEEDED");
    node.nodeType = SmallInteger(value.at("node_type"), 0);
    node.formatType = SmallInteger(value.at("format_type"), 1);
    Require(node.formatType == 1, "UNSUPPORTED_FORMAT");
    node.createdTime = Decimal(value.at("created_time"));
    node.modifiedTime = Decimal(value.at("modified_time"));
    node.contentHash = value.at("content_hash").get<std::string>();
    Require(Hex(node.contentHash, 64), "INVALID_HASH");
    node.rtf = DecodeBase64(value.at("rtf_base64").get<std::string>(), limits.maxNodeBytes);
    Require(Sha256(node.rtf) == node.contentHash, "HASH_MISMATCH");
    return node;
}
Json NodeJson(const Node& node) {
    return {{"sync_id", node.syncId}, {"source_id", std::to_string(node.sourceId)},
        {"parent_sync_id", node.parentSyncId ? Json(*node.parentSyncId) : Json(nullptr)},
        {"sequence", node.sequence}, {"title", node.title}, {"node_type", node.nodeType},
        {"created_time", std::to_string(node.createdTime)}, {"modified_time", std::to_string(node.modifiedTime)},
        {"format_type", node.formatType}, {"content_hash", node.contentHash},
        {"rtf_base64", EncodeBase64(node.rtf)}};
}

// Public SAX API only: retain one node DOM, not a full Base64 snapshot DOM.
class SnapshotSax : public nlohmann::json_sax<Json> {
public:
    SnapshotSax(const Limits& limits, const std::function<void(const Node&)>& sink, bool wrapped = false)
        : m_limits(limits), m_sink(sink), m_wrapped(wrapped) {}
    bool null() override { return Value(nullptr); }
    bool boolean(bool value) override { return Value(value); }
    bool number_integer(number_integer_t value) override { return Value(value); }
    bool number_unsigned(number_unsigned_t value) override { return Value(value); }
    bool number_float(number_float_t value, const string_t&) override { return Value(value); }
    bool string(string_t& value) override { return Value(std::move(value)); }
    bool binary(binary_t&) override { return false; }
    bool start_object(std::size_t) override { return Start(false); }
    bool start_array(std::size_t) override { return Start(true); }
    bool key(string_t& key) override {
        Require(!m_stack.empty() && !m_stack.back().array, "INVALID_JSON");
        Require(m_stack.back().keys.insert(key).second, "DUPLICATE_FIELD");
        m_stack.back().key = key;
        return true;
    }
    bool end_object() override { return End(); }
    bool end_array() override { return End(); }
    bool parse_error(std::size_t, const std::string&, const Json::exception&) override { return false; }
    Snapshot Finish() {
        if (m_wrapped) {
            m_envelope = m_header;
            Require(m_header.is_object() && m_header.contains("request_id") && IsUuid(m_header.at("request_id").get<std::string>()), "INVALID_RESPONSE");
            if (m_header.contains("error")) throw std::runtime_error(m_header.at("error").at("code").get<std::string>());
            const auto& data = m_header.at("data");
            m_header = data.at(data.contains("snapshot") ? "snapshot" : "source");
        }
        Require(m_done && m_header.is_object() && m_header.size() == 5 &&
            m_header.contains("protocol_version") && m_header.contains("schema_version") &&
            m_header.contains("uid") && m_header.contains("name") && m_header.contains("nodes"), "INVALID_SNAPSHOT");
        Require(SmallInteger(m_header.at("protocol_version"), 1) == 1 &&
            SmallInteger(m_header.at("schema_version"), 1) == 1, "UNSUPPORTED_VERSION");
        m_result.uid = m_header.at("uid").get<std::string>();
        m_result.name = m_header.at("name").get<std::string>();
        Require(Hex(m_result.uid, 16) && Characters(m_result.name) <= 128, "INVALID_SNAPSHOT");
        Require(m_header.at("nodes").is_array(), "INVALID_SNAPSHOT");
        ValidateTree(m_result.nodes, m_limits);
        return std::move(m_result);
    }
    Json Envelope() const { return m_envelope; }
private:
    struct Frame { Json value; std::set<std::string> keys; std::string key; bool array; bool nodes; };
    bool Start(bool array) {
        Require(m_stack.size() < (m_wrapped ? 6u : 4u), "INVALID_JSON_DEPTH");
        bool nodes = m_stack.size() == (m_wrapped ? 3u : 1u) && m_stack.back().key == "nodes";
        Require(!nodes || array, "INVALID_SNAPSHOT");
        m_stack.push_back({array ? Json::array() : Json::object(), {}, {}, array, nodes});
        return true;
    }
    bool Value(Json value) {
        if (m_stack.empty()) {
            Require(!m_done, "INVALID_JSON");
            m_header = std::move(value);
            m_done = true;
        } else if (m_stack.back().nodes) {
            Require(m_result.nodes.size() < m_limits.maxNodes, "LIMIT_EXCEEDED");
            Node node = ParseNode(value, m_limits);
            Require(node.rtf.size() <= m_limits.maxVaultBytes - m_bytes, "LIMIT_EXCEEDED");
            m_bytes += node.rtf.size();
            if (m_sink) { m_sink(node); node.rtf.clear(); }
            m_result.nodes.push_back(std::move(node));
        } else if (m_stack.back().array) {
            throw std::runtime_error("INVALID_JSON_ARRAY");
        } else {
            m_stack.back().value[m_stack.back().key] = std::move(value);
        }
        return true;
    }
    bool End() {
        Require(!m_stack.empty(), "INVALID_JSON");
        Json value = std::move(m_stack.back().value);
        m_stack.pop_back();
        return Value(std::move(value));
    }
    const Limits& m_limits;
    const std::function<void(const Node&)>& m_sink;
    std::vector<Frame> m_stack;
    Snapshot m_result;
    Json m_header;
    size_t m_bytes = 0;
    bool m_done = false;
    bool m_wrapped = false;
    Json m_envelope;
};

class BoundedInput : public std::streambuf {
public:
    BoundedInput(std::streambuf* source, size_t maximum) : m_source(source), m_remaining(maximum) {}
protected:
    int_type underflow() override {
        auto c = m_source->sgetc();
        Require(m_remaining != 0 || traits_type::eq_int_type(c, traits_type::eof()), "LIMIT_EXCEEDED");
        return c;
    }
    int_type uflow() override {
        auto c = underflow();
        if (!traits_type::eq_int_type(c, traits_type::eof())) { --m_remaining; m_source->sbumpc(); }
        return c;
    }
private:
    std::streambuf* m_source;
    size_t m_remaining;
};

class BoundedOutput : public std::streambuf {
public:
    BoundedOutput(std::streambuf* target, size_t maximum) : m_target(target), m_remaining(maximum) {}
protected:
    std::streamsize xsputn(const char* data, std::streamsize count) override {
        Require(count >= 0 && static_cast<size_t>(count) <= m_remaining, "LIMIT_EXCEEDED");
        auto written = m_target->sputn(data, count);
        m_remaining -= static_cast<size_t>(written);
        return written;
    }
    int_type overflow(int_type c) override {
        if (traits_type::eq_int_type(c, traits_type::eof())) return traits_type::not_eof(c);
        Require(m_remaining > 0, "LIMIT_EXCEEDED");
        auto result = m_target->sputc(traits_type::to_char_type(c));
        if (!traits_type::eq_int_type(result, traits_type::eof())) --m_remaining;
        return result;
    }
private:
    std::streambuf* m_target;
    size_t m_remaining;
};
} // namespace

bool IsUuid(const std::string& value) {
    if (value.size() != 36 || value[14] != '4' || std::string("89ab").find(value[19]) == std::string::npos) return false;
    std::string hex;
    for (size_t i = 0; i < value.size(); ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) { if (value[i] != '-') return false; }
        else hex += value[i];
    }
    return Hex(hex, 32);
}

std::string Sha256(const std::string& bytes) {
    Require(bytes.size() <= ULONG_MAX, "LIMIT_EXCEEDED");
    std::array<unsigned char, 32> digest{};
    Require(BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0,
        reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data())), static_cast<ULONG>(bytes.size()),
        digest.data(), static_cast<ULONG>(digest.size())) >= 0, "CRYPTO_FAILURE");
    static constexpr char hex[] = "0123456789abcdef";
    std::string result;
    for (auto c : digest) { result += hex[c >> 4]; result += hex[c & 15]; }
    return result;
}

std::string EncodeBase64(const std::string& bytes) {
    if (bytes.empty()) return {};
    Require(bytes.size() <= std::numeric_limits<DWORD>::max(), "LIMIT_EXCEEDED");
    DWORD size = 0;
    auto data = reinterpret_cast<const BYTE*>(bytes.data());
    Require(CryptBinaryToStringA(data, static_cast<DWORD>(bytes.size()), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
        nullptr, &size) != FALSE, "CRYPTO_FAILURE");
    std::string result(size, '\0');
    Require(CryptBinaryToStringA(data, static_cast<DWORD>(bytes.size()), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
        result.data(), &size) != FALSE, "CRYPTO_FAILURE");
    result.resize(size);
    return result;
}

std::string DecodeBase64(const std::string& text, size_t maximum) {
    if (text.empty()) return {};
    Require(text.size() <= ((maximum + 2) / 3) * 4 && text.size() <= std::numeric_limits<DWORD>::max() && text.size() % 4 == 0,
        "LIMIT_EXCEEDED");
    DWORD size = 0;
    Require(CryptStringToBinaryA(text.data(), static_cast<DWORD>(text.size()), CRYPT_STRING_BASE64,
        nullptr, &size, nullptr, nullptr) != FALSE && size <= maximum, "INVALID_BASE64");
    std::string result(size, '\0');
    Require(CryptStringToBinaryA(text.data(), static_cast<DWORD>(text.size()), CRYPT_STRING_BASE64,
        reinterpret_cast<BYTE*>(result.data()), &size, nullptr, nullptr) != FALSE, "INVALID_BASE64");
    result.resize(size);
    Require(EncodeBase64(result) == text, "INVALID_BASE64");
    return result;
}

void ValidateTree(const std::vector<Node>& nodes, const Limits& limits) {
    Require(nodes.size() <= limits.maxNodes, "LIMIT_EXCEEDED");
    std::unordered_map<std::string, const Node*> index;
    std::unordered_set<int64_t> ids;
    for (const auto& node : nodes) {
        Require(IsUuid(node.syncId) && index.emplace(node.syncId, &node).second, "DUPLICATE_SYNC_ID");
        Require(node.sourceId > 0 && ids.insert(node.sourceId).second, "DUPLICATE_SOURCE_ID");
    }
    for (const auto& node : nodes) {
        auto current = &node;
        std::unordered_set<std::string> path;
        for (;;) {
            Require(path.insert(current->syncId).second, "TREE_CYCLE");
            Require(path.size() <= limits.maxDepth, "LIMIT_EXCEEDED");
            if (!current->parentSyncId) break;
            auto parent = index.find(*current->parentSyncId);
            Require(parent != index.end(), "MISSING_PARENT");
            current = parent->second;
        }
    }
}

Snapshot ReadSnapshot(std::istream& input, const Limits& limits, const std::function<void(const Node&)>& stagingSink) {
    BoundedInput buffer(input.rdbuf(), limits.maxJsonBytes);
    std::istream bounded(&buffer);
    bounded.exceptions(std::ios::badbit);
    SnapshotSax sax(limits, stagingSink);
    Require(Json::sax_parse(bounded, &sax), "INVALID_JSON");
    return sax.Finish();
}

SnapshotResponse ReadSnapshotResponse(std::istream& input, const Limits& limits) {
    BoundedInput buffer(input.rdbuf(), limits.maxJsonBytes + 65536);
    std::istream bounded(&buffer); bounded.exceptions(std::ios::badbit);
    const std::function<void(const Node&)> sink;
    SnapshotSax sax(limits, sink, true);
    Require(Json::sax_parse(bounded, &sax), "INVALID_JSON");
    auto snapshot = sax.Finish();
    return {std::move(snapshot), sax.Envelope()};
}

void WriteSnapshot(std::ostream& target, const Snapshot& snapshot, const Limits& limits) {
    Require(target.good(), "WRITE_FAILED");
    BoundedOutput buffer(target.rdbuf(), limits.maxJsonBytes);
    std::ostream output(&buffer);
    output.exceptions(std::ios::badbit | std::ios::failbit);
    Require(Hex(snapshot.uid, 16) && Characters(snapshot.name) <= 128, "INVALID_SNAPSHOT");
    ValidateTree(snapshot.nodes, limits);
    size_t bytes = 0;
    // Validate before emitting any bytes; serialize one node at a time.
    for (const auto& node : snapshot.nodes) {
        ParseNode(NodeJson(node), limits);
        Require(node.rtf.size() <= limits.maxVaultBytes - bytes, "LIMIT_EXCEEDED");
        bytes += node.rtf.size();
    }
    output << "{\"protocol_version\":1,\"schema_version\":1,\"uid\":" << Json(snapshot.uid)
        << ",\"name\":" << Json(snapshot.name) << ",\"nodes\":[";
    bool first = true;
    for (const auto& node : snapshot.nodes) {
        if (!first) output << ',';
        first = false;
        output << NodeJson(node);
    }
    output << "]}";
    Require(output.good(), "WRITE_FAILED");
}
} // namespace anynote::sync
