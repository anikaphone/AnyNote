#pragma once

#include <windows.h>
#include <string>
#include <string_view>

namespace anynote::utils {

inline std::wstring Utf8ToWide(std::string_view utf8Str) {
    if (utf8Str.empty()) return {};
    int count = MultiByteToWideChar(CP_UTF8, 0, utf8Str.data(), static_cast<int>(utf8Str.size()), nullptr, 0);
    if (count <= 0) return {};
    std::wstring result(count, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8Str.data(), static_cast<int>(utf8Str.size()), result.data(), count);
    return result;
}

inline std::string WideToUtf8(std::wstring_view wideStr) {
    if (wideStr.empty()) return {};
    int count = WideCharToMultiByte(CP_UTF8, 0, wideStr.data(), static_cast<int>(wideStr.size()), nullptr, 0, nullptr, nullptr);
    if (count <= 0) return {};
    std::string result(count, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wideStr.data(), static_cast<int>(wideStr.size()), result.data(), count, nullptr, nullptr);
    return result;
}

inline std::string WideToRtf(std::wstring_view text) {
    std::string res;
    res.reserve(text.size() * 2);
    for (wchar_t wc : text) {
        if (wc == L'\\') {
            res += "\\\\";
        } else if (wc == L'{') {
            res += "\\{";
        } else if (wc == L'}') {
            res += "\\}";
        } else if (wc == L'\r') {
            // 忽略 \r
        } else if (wc == L'\n') {
            res += "\\par\n";
        } else if (wc < 128) {
            res += static_cast<char>(wc);
        } else {
            // RTF Unicode sequence \uN? where N is signed 16-bit
            short s = static_cast<short>(wc);
            res += "\\u" + std::to_string(s) + "?";
        }
    }
    return res;
}

} // namespace anynote::utils
