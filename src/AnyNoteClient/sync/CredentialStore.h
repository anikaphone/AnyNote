#pragma once
#include <string>
namespace anynote::sync {
class CredentialStore {
public:
    static void Save(const std::wstring& path, const std::string& server, const std::string& account, const std::string& password);
    static std::string Load(const std::wstring& path, const std::string& server, const std::string& account);
    static void Erase(const std::wstring& path);
};
}
