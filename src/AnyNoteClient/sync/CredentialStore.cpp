#include "CredentialStore.h"
#include "SyncProtocol.h"
#include "storage/ConfigStore.h"
#include <windows.h>
#include <wincrypt.h>
#include <filesystem>
#include <stdexcept>

namespace anynote::sync {
namespace {
DATA_BLOB Blob(const std::string& value) { return {static_cast<DWORD>(value.size()), reinterpret_cast<BYTE*>(const_cast<char*>(value.data()))}; }
struct Protected { DATA_BLOB data{}; ~Protected() { if (data.pbData) { SecureZeroMemory(data.pbData, data.cbData); LocalFree(data.pbData); } } };
}
void CredentialStore::Save(const std::wstring& path, const std::string& server, const std::string& account, const std::string& password) {
    auto entropy = Sha256(server + "\n" + account); auto secret = Blob(password); auto binding = Blob(entropy); Protected encrypted;
    if (!CryptProtectData(&secret, L"AnyNote sync credentials", &binding, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &encrypted.data)) throw std::runtime_error("CREDENTIAL_ENCRYPT_FAILED");
    const auto previous = storage::ConfigStore::Read(path);
    storage::ConfigStore::Write(path, nlohmann::json{{"version", 1}, {"binding", entropy},
        {"ciphertext", EncodeBase64(std::string(reinterpret_cast<char*>(encrypted.data.pbData), encrypted.data.cbData))}}, previous.fingerprint);
    // A previous remembered password must not survive in the atomic writer's backup.
    std::error_code ec; std::filesystem::remove(path + L".bak", ec);
    if (ec) throw std::runtime_error("CREDENTIAL_BACKUP_DELETE_FAILED");
}
std::string CredentialStore::Load(const std::wstring& path, const std::string& server, const std::string& account) {
    auto document = storage::ConfigStore::Read(path); if (!document.exists) return {};
    auto entropy = Sha256(server + "\n" + account);
    if (document.value.at("version") != 1 || document.value.at("binding") != entropy) return {};
    auto ciphertext = DecodeBase64(document.value.at("ciphertext").get<std::string>(), 16384);
    auto data = Blob(ciphertext), binding = Blob(entropy); Protected decrypted;
    if (!CryptUnprotectData(&data, nullptr, &binding, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &decrypted.data)) throw std::runtime_error("CREDENTIAL_DECRYPT_FAILED");
    return std::string(reinterpret_cast<char*>(decrypted.data.pbData), decrypted.data.cbData);
}
void CredentialStore::Erase(const std::wstring& path) {
    for (const auto* suffix : {L"", L".bak"}) { std::error_code ec; std::filesystem::remove(path + suffix, ec); if (ec) throw std::runtime_error("CREDENTIAL_DELETE_FAILED"); }
}
}
