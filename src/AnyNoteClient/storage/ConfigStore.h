#pragma once
#include <nlohmann/json.hpp>
#include <string>

namespace anynote::storage {
struct ConfigDocument {
    nlohmann::json value;
    std::string fingerprint;
    bool exists = false;
};
class ConfigStore {
public:
    static ConfigDocument Read(const std::wstring& path);
    static std::string Write(const std::wstring& path, const nlohmann::json& value,
        const std::string& expectedFingerprint);
    static nlohmann::json ReadIni(const std::wstring& path);
};
}
