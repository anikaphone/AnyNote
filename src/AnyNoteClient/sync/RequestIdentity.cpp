#include "RequestIdentity.h"
#include "SyncProtocol.h"
#include <stdexcept>

namespace anynote::sync {
namespace {
void Encode(const nlohmann::json& value, std::string& result, size_t depth) {
    if (depth > 16 || result.size() > 192 * 1024 * 1024) throw std::runtime_error("LIMIT_EXCEEDED");
    if (value.is_null()) result += 'n';
    else if (value.is_boolean()) result += value.get<bool>() ? 't' : 'f';
    else if (value.is_number_integer()) {
        if (value.is_number_unsigned() && value.get<uint64_t>() > INT64_MAX) throw std::runtime_error("INVALID_INTEGER");
        result += 'i'; result += std::to_string(value.get<int64_t>()); result += ';';
    } else if (value.is_string()) {
        // dump checks UTF-8, without changing the bytes being hashed.
        (void)value.dump();
        const auto& text = value.get_ref<const std::string&>();
        if (text.size() > 192 * 1024 * 1024 - result.size()) throw std::runtime_error("LIMIT_EXCEEDED");
        result += 's'; result += std::to_string(text.size()); result += ':'; result += text;
    } else if (value.is_array()) {
        result += 'a'; result += std::to_string(value.size()); result += ':';
        for (const auto& item : value) Encode(item, result, depth + 1);
    } else if (value.is_object()) {
        result += 'o'; result += std::to_string(value.size()); result += ':';
        for (auto it = value.begin(); it != value.end(); ++it) {
            if (it.key() == "rtf_base64" || it.key() == "password" || it.key() == "token" || it.key() == "operation_id")
                throw std::runtime_error("INVALID_DESCRIPTOR");
            Encode(it.key(), result, depth + 1);
            Encode(it.value(), result, depth + 1);
        }
    } else throw std::runtime_error("INVALID_DESCRIPTOR");
    if (result.size() > 192 * 1024 * 1024) throw std::runtime_error("LIMIT_EXCEEDED");
}
}
std::string RequestHash(const nlohmann::json& descriptor) {
    std::string bytes = "AnyNote/request/v1\n";
    Encode(descriptor, bytes, 0);
    return Sha256(bytes);
}
}
