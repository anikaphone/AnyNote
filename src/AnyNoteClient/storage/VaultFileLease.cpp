#include "VaultFileLease.h"
#include <windows.h>
#include <filesystem>
#include <map>
#include <mutex>
#include <stdexcept>

namespace anynote::storage {
std::shared_ptr<VaultFileLease> VaultFileLease::Acquire(const std::wstring& path) {
    if (path == L":memory:") return {};
    auto normalized = std::filesystem::weakly_canonical(std::filesystem::absolute(path)).wstring();
    std::wstring key(normalized.size(), L'\0');
    if (!LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, normalized.data(), static_cast<int>(normalized.size()),
        key.data(), static_cast<int>(key.size()), nullptr, nullptr, 0)) throw std::runtime_error("VAULT_PATH_INVALID");
    static std::mutex mutex;
    static std::map<std::wstring, std::weak_ptr<VaultFileLease>> leases;
    std::lock_guard lock(mutex);
    for (auto it = leases.begin(); it != leases.end();) {
        if (it->second.expired()) it = leases.erase(it); else ++it;
    }
    if (auto found = leases.find(key); found != leases.end()) if (auto lease = found->second.lock()) return lease;
    auto lockPath = normalized + L".lock";
    HANDLE handle = CreateFileW(lockPath.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_HIDDEN, nullptr);
    if (handle == INVALID_HANDLE_VALUE) throw std::runtime_error("VAULT_LOCK_OPEN_FAILED");
    OVERLAPPED overlap{};
    if (!LockFileEx(handle, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &overlap)) {
        CloseHandle(handle);
        throw std::runtime_error("VAULT_IN_USE_BY_ANOTHER_PROCESS");
    }
    auto lease = std::shared_ptr<VaultFileLease>(new VaultFileLease(handle));
    leases[key] = lease;
    return lease;
}
VaultFileLease::~VaultFileLease() { CloseHandle(m_handle); }
}
