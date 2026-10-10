#pragma once
#include <nlohmann/json.hpp>
#include <string>

namespace anynote::sync {
// Hash a validated business descriptor, NOT the raw HTTP request. Snapshot
// descriptors contain metadata and verified content_hash, never Base64/RTF,
// credentials or operation_id. See protocol/request-identity.md.
std::string RequestHash(const nlohmann::json& descriptor);
}
