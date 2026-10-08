#include "SyntaxHighlighter.h"
#include <unordered_set>
#include <cwctype>
#include <sstream>

namespace anynote::common {

static const std::vector<LanguageInfo> s_languages = {
    { CodeLanguage::Cpp, L"C / C++", L"#include <iostream>\n\nint main() {\n    std::cout << \"Hello AnyNote!\" << std::endl;\n    return 0;\n}" },
    { CodeLanguage::Python, L"Python", L"def hello_anynote(name: str) -> None:\n    print(f\"Hello {name} from AnyNote!\")\n\nif __name__ == '__main__':\n    hello_anynote(\"Developer\")" },
    { CodeLanguage::JavaScript, L"JavaScript / TypeScript", L"function greet(user) {\n    console.log(`Welcome to AnyNote, ${user}!`);\n}\n\ngreet(\"Developer\");" },
    { CodeLanguage::Sql, L"SQL", L"SELECT n.id, n.title, c.content_rtf\nFROM nodes n\nJOIN node_contents c ON n.id = c.node_id\nWHERE n.parent_id = 0\nORDER BY n.sequence ASC;" },
    { CodeLanguage::Shell, L"Shell / Bash", L"#!/usr/bin/env bash\necho \"Building AnyNote portable executable...\"\ncmake --build build --config Release\necho \"Done!\"" },
    { CodeLanguage::Html, L"HTML / XML", L"<!DOCTYPE html>\n<html>\n<head>\n    <title>AnyNote</title>\n</head>\n<body>\n    <h1>Hello AnyNote</h1>\n</body>\n</html>" },
    { CodeLanguage::PlainText, L"纯文本 (Plain Text)", L"// 纯文本代码段落\nKey: Value\nStatus: OK" }
};

const std::vector<LanguageInfo>& GetSupportedLanguages() {
    return s_languages;
}

const wchar_t* CodeLanguageToString(CodeLanguage lang) {
    switch (lang) {
    case CodeLanguage::Cpp:        return L"Cpp";
    case CodeLanguage::Python:     return L"Python";
    case CodeLanguage::JavaScript: return L"JavaScript";
    case CodeLanguage::Sql:        return L"Sql";
    case CodeLanguage::Shell:      return L"Shell";
    case CodeLanguage::Html:       return L"Html";
    case CodeLanguage::PlainText:
    default:                       return L"PlainText";
    }
}

CodeLanguage StringToCodeLanguage(std::wstring_view str) {
    if (_wcsicmp(std::wstring(str).c_str(), L"Cpp") == 0 || _wcsicmp(std::wstring(str).c_str(), L"C++") == 0) {
        return CodeLanguage::Cpp;
    }
    if (_wcsicmp(std::wstring(str).c_str(), L"Python") == 0) {
        return CodeLanguage::Python;
    }
    if (_wcsicmp(std::wstring(str).c_str(), L"JavaScript") == 0 || _wcsicmp(std::wstring(str).c_str(), L"TypeScript") == 0 || _wcsicmp(std::wstring(str).c_str(), L"JS") == 0) {
        return CodeLanguage::JavaScript;
    }
    if (_wcsicmp(std::wstring(str).c_str(), L"Sql") == 0) {
        return CodeLanguage::Sql;
    }
    if (_wcsicmp(std::wstring(str).c_str(), L"Shell") == 0 || _wcsicmp(std::wstring(str).c_str(), L"Bash") == 0) {
        return CodeLanguage::Shell;
    }
    if (_wcsicmp(std::wstring(str).c_str(), L"Html") == 0 || _wcsicmp(std::wstring(str).c_str(), L"Xml") == 0) {
        return CodeLanguage::Html;
    }
    return CodeLanguage::PlainText;
}

const wchar_t* GetLanguageShortName(CodeLanguage lang) {
    switch (lang) {
    case CodeLanguage::Cpp:        return L"C / C++";
    case CodeLanguage::Python:     return L"Python";
    case CodeLanguage::JavaScript: return L"JavaScript";
    case CodeLanguage::Sql:        return L"SQL";
    case CodeLanguage::Shell:      return L"Shell";
    case CodeLanguage::Html:       return L"HTML";
    case CodeLanguage::PlainText:
    default:                       return L"纯文本";
    }
}

namespace {

enum class TokenType {
    Default,
    Keyword,
    Directive,
    String,
    Comment,
    Number,
    Type
};

struct Token {
    TokenType type;
    std::wstring text;
};

bool IsIdentStart(wchar_t c) {
    return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || c == L'_';
}

bool IsIdentChar(wchar_t c) {
    return IsIdentStart(c) || (c >= L'0' && c <= L'9');
}

std::wstring ToLower(std::wstring_view sv) {
    std::wstring s;
    s.reserve(sv.size());
    for (wchar_t c : sv) {
        s.push_back(static_cast<wchar_t>(std::towlower(c)));
    }
    return s;
}

const std::unordered_set<std::wstring>& GetCppKeywords() {
    static const std::unordered_set<std::wstring> kw = {
        L"if", L"else", L"for", L"while", L"do", L"switch", L"case", L"default",
        L"break", L"continue", L"return", L"try", L"catch", L"throw", L"class",
        L"struct", L"enum", L"union", L"template", L"typename", L"public",
        L"private", L"protected", L"virtual", L"override", L"constexpr", L"nullptr",
        L"true", L"false", L"using", L"namespace", L"new", L"delete", L"sizeof",
        L"this", L"inline", L"operator", L"friend", L"typedef", L"static", L"const",
        L"volatile", L"mutable", L"auto", L"explicit", L"noexcept", L"decltype",
        L"concept", L"requires", L"co_await", L"co_return", L"co_yield"
    };
    return kw;
}

const std::unordered_set<std::wstring>& GetCppTypes() {
    static const std::unordered_set<std::wstring> types = {
        L"int", L"char", L"wchar_t", L"float", L"double", L"void", L"bool",
        L"short", L"long", L"signed", L"unsigned", L"size_t", L"int8_t", L"int16_t",
        L"int32_t", L"int64_t", L"uint8_t", L"uint16_t", L"uint32_t", L"uint64_t",
        L"string", L"wstring", L"string_view", L"wstring_view", L"vector", L"map",
        L"set", L"unordered_map", L"unordered_set", L"unique_ptr", L"shared_ptr",
        L"HWND", L"HINSTANCE", L"LRESULT", L"WPARAM", L"LPARAM", L"UINT", L"BOOL",
        L"DWORD", L"HANDLE", L"RECT", L"POINT"
    };
    return types;
}

const std::unordered_set<std::wstring>& GetPythonKeywords() {
    static const std::unordered_set<std::wstring> kw = {
        L"def", L"class", L"if", L"elif", L"else", L"for", L"while", L"break",
        L"continue", L"return", L"yield", L"import", L"from", L"as", L"try",
        L"except", L"finally", L"raise", L"with", L"pass", L"lambda", L"True",
        L"False", L"None", L"and", L"or", L"not", L"in", L"is", L"async",
        L"await", L"global", L"nonlocal", L"assert", L"del"
    };
    return kw;
}

const std::unordered_set<std::wstring>& GetJsKeywords() {
    static const std::unordered_set<std::wstring> kw = {
        L"function", L"class", L"const", L"let", L"var", L"if", L"else", L"for",
        L"while", L"do", L"switch", L"case", L"default", L"break", L"continue",
        L"return", L"try", L"catch", L"finally", L"throw", L"new", L"typeof",
        L"instanceof", L"import", L"export", L"from", L"async", L"await",
        L"true", L"false", L"null", L"undefined", L"this", L"yield", L"super",
        L"debugger", L"delete", L"in", L"of"
    };
    return kw;
}

const std::unordered_set<std::wstring>& GetSqlKeywords() {
    static const std::unordered_set<std::wstring> kw = {
        L"select", L"from", L"where", L"insert", L"into", L"update", L"set",
        L"delete", L"create", L"table", L"drop", L"alter", L"index", L"join",
        L"inner", L"left", L"right", L"outer", L"full", L"cross", L"on", L"group",
        L"by", L"order", L"having", L"limit", L"offset", L"and", L"or", L"not",
        L"null", L"primary", L"key", L"foreign", L"references", L"values", L"as",
        L"distinct", L"count", L"sum", L"avg", L"min", L"max", L"like", L"in",
        L"between", L"is", L"case", L"when", L"then", L"else", L"end", L"union",
        L"all", L"view", L"trigger", L"pragma", L"autoincrement", L"cascade"
    };
    return kw;
}

const std::unordered_set<std::wstring>& GetShellKeywords() {
    static const std::unordered_set<std::wstring> kw = {
        L"if", L"then", L"else", L"elif", L"fi", L"for", L"in", L"do", L"done",
        L"while", L"case", L"esac", L"export", L"local", L"function", L"return",
        L"exit", L"echo", L"cd", L"ls", L"mkdir", L"rm", L"cp", L"mv", L"cat",
        L"grep", L"chmod", L"chown", L"sudo", L"source"
    };
    return kw;
}

std::vector<Token> Tokenize(std::wstring_view text, CodeLanguage lang) {
    std::vector<Token> tokens;
    size_t i = 0;
    size_t n = text.size();

    while (i < n) {
        wchar_t c = text[i];

        // 1. 注释检测
        if ((lang == CodeLanguage::Cpp || lang == CodeLanguage::JavaScript) && c == L'/' && i + 1 < n && text[i + 1] == L'/') {
            size_t start = i;
            while (i < n && text[i] != L'\n') ++i;
            tokens.push_back({ TokenType::Comment, std::wstring(text.substr(start, i - start)) });
            continue;
        }
        if ((lang == CodeLanguage::Cpp || lang == CodeLanguage::JavaScript || lang == CodeLanguage::Sql) && c == L'/' && i + 1 < n && text[i + 1] == L'*') {
            size_t start = i;
            i += 2;
            while (i + 1 < n && !(text[i] == L'*' && text[i + 1] == L'/')) ++i;
            if (i + 1 < n) i += 2;
            tokens.push_back({ TokenType::Comment, std::wstring(text.substr(start, i - start)) });
            continue;
        }
        if ((lang == CodeLanguage::Python || lang == CodeLanguage::Shell) && c == L'#') {
            size_t start = i;
            while (i < n && text[i] != L'\n') ++i;
            tokens.push_back({ TokenType::Comment, std::wstring(text.substr(start, i - start)) });
            continue;
        }
        if (lang == CodeLanguage::Sql && c == L'-' && i + 1 < n && text[i + 1] == L'-') {
            size_t start = i;
            while (i < n && text[i] != L'\n') ++i;
            tokens.push_back({ TokenType::Comment, std::wstring(text.substr(start, i - start)) });
            continue;
        }
        if (lang == CodeLanguage::Html && c == L'<' && i + 3 < n && text.substr(i, 4) == L"<!--") {
            size_t start = i;
            i += 4;
            while (i + 2 < n && text.substr(i, 3) != L"-->") ++i;
            if (i + 2 < n) i += 3;
            tokens.push_back({ TokenType::Comment, std::wstring(text.substr(start, i - start)) });
            continue;
        }

        // 2. 字符串检测
        if (c == L'"' || c == L'\'' || (c == L'`' && lang == CodeLanguage::JavaScript)) {
            wchar_t quote = c;
            size_t start = i++;
            while (i < n) {
                if (text[i] == L'\\' && i + 1 < n) {
                    i += 2; // 转义
                } else if (text[i] == quote) {
                    ++i;
                    break;
                } else if (text[i] == L'\n') {
                    // Python 三引号支持可跨行，单引号到行尾截止
                    break;
                } else {
                    ++i;
                }
            }
            tokens.push_back({ TokenType::String, std::wstring(text.substr(start, i - start)) });
            continue;
        }

        // 3. 预处理指令 / 装饰器
        if (lang == CodeLanguage::Cpp && c == L'#' && (i == 0 || text[i - 1] == L'\n' || std::iswspace(text[i - 1]))) {
            size_t start = i++;
            while (i < n && IsIdentChar(text[i])) ++i;
            tokens.push_back({ TokenType::Directive, std::wstring(text.substr(start, i - start)) });
            continue;
        }
        if (lang == CodeLanguage::Python && c == L'@') {
            size_t start = i++;
            while (i < n && IsIdentChar(text[i])) ++i;
            tokens.push_back({ TokenType::Directive, std::wstring(text.substr(start, i - start)) });
            continue;
        }

        // 4. 数字
        if (c >= L'0' && c <= L'9' && (i == 0 || !IsIdentChar(text[i - 1]))) {
            size_t start = i++;
            while (i < n && (IsIdentChar(text[i]) || text[i] == L'.')) ++i;
            tokens.push_back({ TokenType::Number, std::wstring(text.substr(start, i - start)) });
            continue;
        }

        // 5. 标识符与关键字
        if (IsIdentStart(c)) {
            size_t start = i++;
            while (i < n && IsIdentChar(text[i])) ++i;
            std::wstring word(text.substr(start, i - start));
            TokenType type = TokenType::Default;

            if (lang == CodeLanguage::Cpp) {
                if (GetCppKeywords().contains(word)) type = TokenType::Keyword;
                else if (GetCppTypes().contains(word)) type = TokenType::Type;
            } else if (lang == CodeLanguage::Python) {
                if (GetPythonKeywords().contains(word)) type = TokenType::Keyword;
            } else if (lang == CodeLanguage::JavaScript) {
                if (GetJsKeywords().contains(word)) type = TokenType::Keyword;
            } else if (lang == CodeLanguage::Sql) {
                if (GetSqlKeywords().contains(ToLower(word))) type = TokenType::Keyword;
            } else if (lang == CodeLanguage::Shell) {
                if (GetShellKeywords().contains(word)) type = TokenType::Keyword;
            }

            tokens.push_back({ type, std::move(word) });
            continue;
        }

        // 6. 其他普通符号或空白
        tokens.push_back({ TokenType::Default, std::wstring(1, c) });
        ++i;
    }

    return tokens;
}

void AppendEscapedRtf(std::string& rtf, const std::wstring& text) {
    for (wchar_t wc : text) {
        if (wc == L'\\') {
            rtf += "\\\\";
        } else if (wc == L'{') {
            rtf += "\\{";
        } else if (wc == L'}') {
            rtf += "\\}";
        } else if (wc == L'\r') {
            // 忽略 \r
        } else if (wc == L'\n') {
            rtf += "\\par\\intbl\\sl240\\slmult1\\sb40\\sa40\\f1\\fs19\\cf3 ";
        } else if (wc < 128) {
            rtf += static_cast<char>(wc);
        } else {
            // RTF Unicode sequence \uN? where N is signed 16-bit
            short s = static_cast<short>(wc);
            rtf += "\\u" + std::to_string(s) + "?";
        }
    }
}

} // namespace

std::string SyntaxHighlighter::GenerateRtfCodeBlock(std::wstring_view code, CodeLanguage lang) {
    // 颜色表说明:
    // \cf1  : 浅灰卡片背景 (RGB 246, 248, 250)
    // \cf2  : 边框浅灰 (RGB 225, 228, 232)
    // \cf3  : 代码正文深炭灰 (RGB 36, 41, 47)
    // \cf4  : 关键字经典蓝 (RGB 0, 92, 197)
    // \cf5  : 预处理指令/特殊关键词砖红 (RGB 215, 58, 73)
    // \cf6  : 字符串/字面量暗蓝 (RGB 3, 47, 98)
    // \cf7  : 注释灰绿 (RGB 106, 115, 125)
    // \cf8  : 数字青蓝 (RGB 0, 92, 197)
    // \cf9  : 内置类型紫罗兰 (RGB 111, 66, 193)
    // \cf10 : 左侧重音装饰条品蓝 (RGB 3, 102, 214)

    std::string rtf;
    rtf.reserve(4096 + code.size() * 3);

    rtf += "{\\rtf1\\ansi\\deff0\\nouicompat";
    rtf += "{\\fonttbl{\\f0\\fnil\\fcharset134 Segoe UI;}{\\f1\\fnil\\fcharset0 Consolas;}}";
    rtf += "{\\colortbl ;\\red246\\green248\\blue250;\\red225\\green228\\blue232;\\red36\\green41\\blue47;\\red0\\green92\\blue197;\\red215\\green58\\blue73;\\red3\\green47\\blue98;\\red106\\green115\\blue125;\\red0\\green92\\blue197;\\red111\\green66\\blue193;\\red3\\green102\\blue214;}";
    rtf += "\\viewkind4\\uc1\n";

    // 单行单单元格纯净代码卡片 (设置最小行高 720 twips 避免单行卡片过于扁平)
    rtf += "\\trowd\\trgaph108\\trleft360\\trrh720";
    rtf += "\\clbrdrt\\brdrs\\brdrw15\\brdrcf2";
    rtf += "\\clbrdrb\\brdrs\\brdrw15\\brdrcf2";
    rtf += "\\clbrdrl\\brdrs\\brdrw40\\brdrcf10";
    rtf += "\\clbrdrr\\brdrs\\brdrw15\\brdrcf2";
    rtf += "\\clcbpat1\\cellx8600\n";

    // 单元格内首部嵌入不可见的语言元数据标记 (\\v 为 RTF 隐藏文本，屏幕占用 0 像素)
    const wchar_t* langCode = CodeLanguageToString(lang);
    std::wstring langTag = L"[lang:" + std::wstring(langCode) + L"]";

    rtf += "\\pard\\intbl\\sl240\\slmult1\\sb60\\sa60\\f1\\fs19\\cf3 ";
    rtf += "{\\v ";
    AppendEscapedRtf(rtf, langTag);
    rtf += "\\v0}";

    std::wstring_view effectiveCode = code;
    if (effectiveCode.empty()) {
        effectiveCode = L"\n\n";
    }

    auto tokens = Tokenize(effectiveCode, lang);
    for (const auto& token : tokens) {
        int colorIndex = 3; // 默认深炭灰
        switch (token.type) {
        case TokenType::Keyword:   colorIndex = 4; break;
        case TokenType::Directive: colorIndex = 5; break;
        case TokenType::String:    colorIndex = 6; break;
        case TokenType::Comment:   colorIndex = 7; break;
        case TokenType::Number:    colorIndex = 8; break;
        case TokenType::Type:      colorIndex = 9; break;
        default:                   colorIndex = 3; break;
        }

        rtf += "\\cf" + std::to_string(colorIndex) + " ";
        AppendEscapedRtf(rtf, token.text);
    }

    // 闭合单元格并闭合行
    rtf += "\\cell\\row\n";
    rtf += "}";

    return rtf;
}

} // namespace anynote::common
