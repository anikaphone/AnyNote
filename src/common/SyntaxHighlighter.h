#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace anynote::common {

enum class CodeLanguage {
    Cpp,
    Python,
    JavaScript,
    Sql,
    Shell,
    Html,
    PlainText
};

struct LanguageInfo {
    CodeLanguage lang;
    const wchar_t* name;
    const wchar_t* defaultTemplate;
};

const std::vector<LanguageInfo>& GetSupportedLanguages();

class SyntaxHighlighter {
public:
    static std::string GenerateRtfCodeBlock(std::wstring_view code, CodeLanguage lang);
};

} // namespace anynote::common
