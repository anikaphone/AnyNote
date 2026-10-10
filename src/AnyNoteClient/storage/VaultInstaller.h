#pragma once
#include "SnapshotService.h"
#include "VaultManager.h"

namespace anynote::storage {
// Journal contains paths/identity only, never a password or plaintext content.
class VaultInstaller {
public:
    static sync::Snapshot AsNew(sync::Snapshot snapshot);
    // Returns the recovery journal path if registration must be retried, empty on success.
    static std::filesystem::path Install(VaultManager& manager, const sync::Snapshot& snapshot,
        const std::filesystem::path& destination, const std::wstring& displayName, const std::string& password = "");
    static void Recover(VaultManager& manager, const std::filesystem::path& journal, const std::string& password = "");
};
}
