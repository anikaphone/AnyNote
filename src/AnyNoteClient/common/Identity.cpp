#include "Identity.h"
#include <windows.h>
#include <bcrypt.h>
#include <array>
#include <chrono>
#include <mutex>
#include <set>
#include <stdexcept>

namespace anynote::common {
namespace {
template<size_t N> std::array<unsigned char, N> Random() {
    std::array<unsigned char, N> bytes{};
    if (BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
        throw std::runtime_error("CRYPTO_FAILURE");
    return bytes;
}
template<size_t N> std::string Hex(const std::array<unsigned char, N>& bytes) {
    static const char digits[] = "0123456789abcdef";
    std::string result;
    for (auto byte : bytes) { result += digits[byte >> 4]; result += digits[byte & 15]; }
    return result;
}
}
std::string NewUuid() {
    auto bytes = Random<16>();
    bytes[6] = (bytes[6] & 15) | 0x40;
    bytes[8] = (bytes[8] & 63) | 0x80;
    auto hex = Hex(bytes);
    return hex.substr(0, 8) + '-' + hex.substr(8, 4) + '-' + hex.substr(12, 4) + '-' + hex.substr(16, 4) + '-' + hex.substr(20);
}
std::string NewVaultUid() {
    static std::mutex mutex;
    // Only the current millisecond needs collision tracking; avoid an unbounded registry.
    static int64_t previous = -1;
    static std::set<std::string> issued;
    std::lock_guard lock(mutex);
    for (int attempt = 0; attempt < 128; ++attempt) {
        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        if (now < 0 || now >= (int64_t{1} << 48)) throw std::runtime_error("INVALID_CLOCK");
        if (now < previous) throw std::runtime_error("CLOCK_MOVED_BACKWARDS");
        if (now > previous) { issued.clear(); previous = now; }
        auto bytes = Random<8>();
        for (int i = 0; i < 6; ++i) bytes[i] = static_cast<unsigned char>(now >> ((5 - i) * 8));
        auto uid = Hex(bytes);
        if (issued.insert(uid).second) return uid;
    }
    throw std::runtime_error("UID_COLLISION");
}
}
