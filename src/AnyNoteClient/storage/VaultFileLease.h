#pragma once
#include <memory>
#include <string>

namespace anynote::storage {
// One OS byte-range lock per normalized path per process. Connections in the
// same process share the lease; another cooperating process cannot open a writer.
class VaultFileLease {
public:
    static std::shared_ptr<VaultFileLease> Acquire(const std::wstring& path);
    ~VaultFileLease();
private:
    explicit VaultFileLease(void* handle) : m_handle(handle) {}
    void* m_handle;
};
}
