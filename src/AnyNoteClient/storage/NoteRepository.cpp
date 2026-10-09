#include "NoteRepository.h"
#include "common/StringUtils.h"
#include "common/SyntaxHighlighter.h"
#include <sqlite3.h>
#include <chrono>
#include <algorithm>
#include <charconv>
#include <cwctype>
#include <unordered_set>
#include <windows.h>

namespace anynote::storage {

using anynote::utils::WideToRtf;

namespace {

int64_t GetCurrentUnixTimestamp() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

std::string BuildDefaultCodeFragment(std::wstring_view code, common::CodeLanguage language) {
    // These welcome documents use the same font/color tables as the generator.
    // Reuse its complete row, including hidden language metadata and spacing.
    const auto rtf = common::SyntaxHighlighter::GenerateRtfCodeBlock(code, language);
    const auto start = rtf.find("\\trowd");
    return rtf.substr(start, rtf.size() - start - 1);
}

std::string BuildDefaultWelcomeRtf() {
    std::string rtf;
    rtf += "{\\rtf1\\ansi\\deff0\\nouicompat";
    rtf += "{\\fonttbl{\\f0\\fnil\\fcharset134 Segoe UI;}{\\f1\\fnil\\fcharset0 Consolas;}}";
    rtf += "{\\colortbl ;\\red246\\green248\\blue250;\\red225\\green228\\blue232;\\red36\\green41\\blue47;\\red0\\green92\\blue197;\\red215\\green58\\blue73;\\red3\\green47\\blue98;\\red106\\green115\\blue125;\\red0\\green92\\blue197;\\red111\\green66\\blue193;\\red3\\green102\\blue214;}";
    rtf += "\\viewkind4\\uc1";
    rtf += "\\pard\\b\\fs28\\cf4 " + WideToRtf(L"欢迎使用 AnyNote - 基于 C++20 与原生 Win32 的分层树状笔记！") + "\\b0\\fs22\\par\\par";
    rtf += "\\cf3\\b " + WideToRtf(L"★ 核心特性与架构高光：") + "\\b0\\par";
    rtf += WideToRtf(L"• ") + "\\b " + WideToRtf(L"多笔记本库管理 (Multi-Vault)") + "\\b0 " + WideToRtf(L"：左侧树顶部切换栏，支持多库即时切换、新建与统一管理，原生 INI 绿色存储；") + "\\par";
    rtf += WideToRtf(L"• ") + "\\b " + WideToRtf(L"目录树拖拽重排 (Drag & Drop)") + "\\b0 " + WideToRtf(L"：鼠标按住左侧笔记节点即可拖拽，自由调整同级排序与父子层级关系；") + "\\par";
    rtf += WideToRtf(L"• ") + "\\b " + WideToRtf(L"原生表格排版与编辑 (Tables)") + "\\b0 " + WideToRtf(L"：按 ") + "\\b Ctrl+Shift+T\\b0 " + WideToRtf(L" 快速插入 3×2 表格，光标置于表格内右键即可增删行与列；") + "\\par";
    rtf += WideToRtf(L"• ") + "\\b " + WideToRtf(L"全库多维搜索与浮动查找") + "\\b0 " + WideToRtf(L"：按 ") + "\\b Ctrl+Shift+F\\b0 " + WideToRtf(L" 呼出底部全库全文检索窗格，按 ") + "\\b Ctrl+F / Ctrl+H\\b0 " + WideToRtf(L" 唤起浮动查找替换；") + "\\par";
    rtf += WideToRtf(L"• ") + "\\b " + WideToRtf(L"原生富文本内核 (RichEdit 5.0)") + "\\b0 " + WideToRtf(L"：毫秒级疾速启动，极低内存，支持 1~4 级标题、粗体、斜体、列表等；") + "\\par";
    rtf += WideToRtf(L"• ") + "\\b " + WideToRtf(L"剪贴板图片直接粘贴") + "\\b0 " + WideToRtf(L"：按 Win+Shift+S 截图，在编辑器中直接按 ") + "\\b Ctrl+V\\b0 " + WideToRtf(L" 即可粘贴截图图片；") + "\\par";
    rtf += WideToRtf(L"• ") + "\\b " + WideToRtf(L"原生代码框 (CodeBox)") + "\\b0 " + WideToRtf(L"：按 ") + "\\b Ctrl+K\\b0 " + WideToRtf(L" 插入多语言语法高亮卡片与 Consolas 等宽排版；") + "\\par";
    rtf += WideToRtf(L"• ") + "\\b " + WideToRtf(L"工业级安全持久化") + "\\b0 " + WideToRtf(L"：基于 SQLite3MC 存储，支持 AES-256 全库透明加密。") + "\\par\\par";
    rtf += "\\pard\\cf3 " + WideToRtf(L"下方即为原生嵌入的代码块示例：") + "\\par\\par";

    // 嵌入示例代码卡片
    rtf += BuildDefaultCodeFragment(LR"code(// C++ 示例代码块
#include <iostream>

int main() {
    std::cout << "Hello, AnyNote Native CodeBlock!" << std::endl;
    return 0;
})code",
        common::CodeLanguage::Cpp);
    rtf += "\\pard\\f0\\fs22\\par\\cf3 " + WideToRtf(L"可以在此继续输入正文，或选中文本后按 ") + "\\b Ctrl+K\\b0 " + WideToRtf(L" 转换为代码块！") + "\\par";
    rtf += "}";
    return rtf;
}

std::string BuildDefaultQuickStartRtf() {
    std::string rtf;
    rtf += "{\\rtf1\\ansi\\deff0\\nouicompat";
    rtf += "{\\fonttbl{\\f0\\fnil\\fcharset134 Segoe UI;}}";
    rtf += "{\\colortbl ;\\red0\\green92\\blue197;\\red36\\green41\\blue47;}";
    rtf += "\\viewkind4\\uc1";
    rtf += "\\pard\\b\\fs26\\cf1 " + WideToRtf(L"【快速上手指南】") + "\\b0\\fs22\\par\\par";
    rtf += "\\cf2 " + WideToRtf(L"1. 多库切换：点击左侧目录树顶部的「📚 库名称 ▾」，可即时切换不同笔记库，或新建/管理笔记库；") + "\\par";
    rtf += WideToRtf(L"2. 笔记管理：在左侧树中右键添加同级笔记 (Ctrl+N) 或子笔记 (Ctrl+Shift+N)，按 F2 重命名，Del 删除；") + "\\par";
    rtf += WideToRtf(L"3. 拖拽排序：鼠标左键按住任意目录节点，直接拖动即可调整前后顺序或拖入其他节点成为子笔记；") + "\\par";
    rtf += WideToRtf(L"4. 格式排版：点击上方工具栏进行加粗 (Ctrl+B)、斜体 (Ctrl+I)、下划线 (Ctrl+U)、删除线及 1~4 级标题 (Ctrl+1~4)；") + "\\par";
    rtf += WideToRtf(L"5. 插入表格：按 Ctrl+Shift+T 插入 3×2 表格，光标置于单元格内右键可快捷增删行/列或删除表格；") + "\\par";
    rtf += WideToRtf(L"6. 插入代码块：按 Ctrl+K 弹出代码框对话框，选择语言即可插入高亮代码卡片；") + "\\par";
    rtf += WideToRtf(L"7. 粘贴图片：截屏 (Win+Shift+S) 或复制任意图片后，在右侧编辑器直接按 Ctrl+V 即可粘贴；") + "\\par";
    rtf += WideToRtf(L"8. 查找与全库搜索：按 Ctrl+F 唤起浮动查找框，按 Ctrl+Shift+F 打开底部全库搜索窗格快速定位；") + "\\par";
    rtf += WideToRtf(L"9. 安全加密：点击顶部菜单「安全」->「设置/修改笔记本密码」，即可为整库启用 AES-256 加密保护。") + "\\par";
    rtf += "}";
    return rtf;
}

std::string BuildDefaultFeaturesRtf() {
    std::string rtf;
    rtf += "{\\rtf1\\ansi\\deff0\\nouicompat";
    rtf += "{\\fonttbl{\\f0\\fnil\\fcharset134 Segoe UI;}}";
    rtf += "{\\colortbl ;\\red0\\green92\\blue197;\\red36\\green41\\blue47;\\red106\\green115\\blue125;}";
    rtf += "\\viewkind4\\uc1";
    rtf += "\\pard\\b\\fs26\\cf1 " + WideToRtf(L"【特性与常用快捷键清单】") + "\\b0\\fs22\\par\\par";
    rtf += "\\cf2\\b " + WideToRtf(L"• 笔记本库与树节点：") + "\\b0\\par";
    rtf += WideToRtf(L"  - 📚 顶栏切换栏 : 点击快速切换、新建、打开或管理多库") + "\\par";
    rtf += WideToRtf(L"  - Ctrl+N : 新建同级笔记") + "\\par";
    rtf += WideToRtf(L"  - Ctrl+Shift+N : 新建子笔记") + "\\par";
    rtf += WideToRtf(L"  - F2 : 重命名选中笔记") + "\\par";
    rtf += WideToRtf(L"  - Del : 删除选中笔记（焦点在目录树时）") + "\\par";
    rtf += WideToRtf(L"  - 鼠标拖拽 : 自由重排节点顺序与移动层级") + "\\par";
    rtf += WideToRtf(L"  - Ctrl+S : 保存当前笔记到数据库") + "\\par\\par";
    rtf += "\\b " + WideToRtf(L"• 文本排版与插入：") + "\\b0\\par";
    rtf += WideToRtf(L"  - Ctrl+0 : 恢复正文样式") + "\\par";
    rtf += WideToRtf(L"  - Ctrl+1 ~ 4 : 切换 1~4 级标题") + "\\par";
    rtf += WideToRtf(L"  - Ctrl+B / Ctrl+I / Ctrl+U : 粗体 / 斜体 / 下划线") + "\\par";
    rtf += WideToRtf(L"  - Ctrl+Shift+T : 插入原生表格 (3×2)") + "\\par";
    rtf += WideToRtf(L"  - Ctrl+K : 插入多语言高亮代码块") + "\\par";
    rtf += WideToRtf(L"  - Ctrl+V : 粘贴文本或剪贴板截图图片") + "\\par\\par";
    rtf += "\\b " + WideToRtf(L"• 查找与全库搜索：") + "\\b0\\par";
    rtf += WideToRtf(L"  - Ctrl+F : 打开查找对话框") + "\\par";
    rtf += WideToRtf(L"  - Ctrl+H : 打开替换对话框") + "\\par";
    rtf += WideToRtf(L"  - Ctrl+M : 打开标记对话框") + "\\par";
    rtf += WideToRtf(L"  - F3 / Shift+F3 : 查找下一个 / 上一个") + "\\par";
    rtf += WideToRtf(L"  - Ctrl+Shift+F : 打开/关闭全库搜索窗格") + "\\par\\par";
    rtf += "\\b " + WideToRtf(L"• 编辑与通用：") + "\\b0\\par";
    rtf += WideToRtf(L"  - Ctrl+Z / Ctrl+Y : 撤销 / 重做") + "\\par";
    rtf += WideToRtf(L"  - Ctrl+A : 全选正文") + "\\par";
    rtf += "}";
    return rtf;
}

std::string BuildDefaultCppRtf() {
    std::string rtf;
    rtf += "{\\rtf1\\ansi\\deff0\\nouicompat";
    rtf += "{\\fonttbl{\\f0\\fnil\\fcharset134 Segoe UI;}{\\f1\\fnil\\fcharset0 Consolas;}}";
    rtf += "{\\colortbl ;\\red246\\green248\\blue250;\\red225\\green228\\blue232;\\red36\\green41\\blue47;\\red0\\green92\\blue197;\\red215\\green58\\blue73;\\red3\\green47\\blue98;\\red106\\green115\\blue125;\\red0\\green92\\blue197;\\red111\\green66\\blue193;\\red3\\green102\\blue214;}";
    rtf += "\\viewkind4\\uc1";
    rtf += "\\pard\\b\\fs26\\cf4 " + WideToRtf(L"【现代 C++ 与 Win32 架构设计】") + "\\b0\\fs22\\par\\par";
    rtf += "\\cf3 " + WideToRtf(L"本工程采用现代 C++20 面向对象封装原生 Win32 API：") + "\\par";
    rtf += "• \\b " + WideToRtf(L"GWLP_USERDATA 绑定") + "\\b0 " + WideToRtf(L"：将 HWND 映射为 C++ 类对象指针，无全局变量；") + "\\par";
    rtf += "• \\b " + WideToRtf(L"Per-Monitor V2 DPI") + "\\b0 " + WideToRtf(L"：高分屏 4K 150%/200% 清晰不发虚；") + "\\par";
    rtf += "• \\b " + WideToRtf(L"零第三方 DLL 依赖") + "\\b0 " + WideToRtf(L"：全静态链接便携发行。") + "\\par\\par";
    rtf += "\\cf3 " + WideToRtf(L"消息分发核心代码封装实现如下：") + "\\par\\par";

    rtf += BuildDefaultCodeFragment(LR"code(// Window 抽象基类消息路由回调
LRESULT CALLBACK Window::StaticWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    auto* pThis = reinterpret_cast<Window*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    if (uMsg == WM_NCCREATE) {
        auto* pCreate = reinterpret_cast<CREATESTRUCTW*>(lParam);
        pThis = reinterpret_cast<Window*>(pCreate->lpCreateParams);
        if (pThis) {
            pThis->m_hWnd = hWnd;
            SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
        }
    }
    return pThis ? pThis->HandleMessage(uMsg, wParam, lParam) : DefWindowProcW(hWnd, uMsg, wParam, lParam);
})code",
        common::CodeLanguage::Cpp);
    rtf += "\\pard\\f0\\fs22\\par\\cf3 " + WideToRtf(L"这种封装方式优雅兼顾面向对象与 Win32 原生性能。") + "\\par";
    rtf += "}";
    return rtf;
}

std::string BuildDefaultSqliteRtf() {
    std::string rtf;
    rtf += "{\\rtf1\\ansi\\deff0\\nouicompat";
    rtf += "{\\fonttbl{\\f0\\fnil\\fcharset134 Segoe UI;}{\\f1\\fnil\\fcharset0 Consolas;}}";
    rtf += "{\\colortbl ;\\red246\\green248\\blue250;\\red225\\green228\\blue232;\\red36\\green41\\blue47;\\red0\\green92\\blue197;\\red215\\green58\\blue73;\\red3\\green47\\blue98;\\red106\\green115\\blue125;\\red0\\green92\\blue197;\\red111\\green66\\blue193;\\red3\\green102\\blue214;}";
    rtf += "\\viewkind4\\uc1";
    rtf += "\\pard\\b\\fs26\\cf4 " + WideToRtf(L"【SQLite3MC 数据库与安全机制】") + "\\b0\\fs22\\par\\par";
    rtf += "\\cf3 " + WideToRtf(L"SQLite3 Multiple Ciphers 支持 AES-256-CBC, AES-256-GCM 等工业级高强度加密。") + "\\par";
    rtf += WideToRtf(L"数据库核心表结构定义如下：") + "\\par\\par";

    rtf += BuildDefaultCodeFragment(LR"code(-- 节点元数据表
CREATE TABLE IF NOT EXISTS nodes (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    parent_id     INTEGER NOT NULL DEFAULT 0,
    sequence      INTEGER NOT NULL DEFAULT 0,
    title         TEXT NOT NULL,
    node_type     INTEGER NOT NULL DEFAULT 0,
    created_time  INTEGER NOT NULL,
    modified_time INTEGER NOT NULL
);

-- 正文 RTF 字节流存储表
CREATE TABLE IF NOT EXISTS node_contents (
    node_id       INTEGER PRIMARY KEY,
    format_type   INTEGER NOT NULL DEFAULT 1,
    content_rtf   BLOB,
    FOREIGN KEY (node_id) REFERENCES nodes(id) ON DELETE CASCADE
);)code",
        common::CodeLanguage::Sql);
    rtf += "\\pard\\f0\\fs22\\par\\cf3 " + WideToRtf(L"全库加密时通过 sqlite3_key / sqlite3_rekey 即可实现透明加解密。") + "\\par";
    rtf += "}";
    return rtf;
}

std::string BuildDefaultTodoRtf() {
    std::string rtf;
    rtf += "{\\rtf1\\ansi\\deff0\\nouicompat";
    rtf += "{\\fonttbl{\\f0\\fnil\\fcharset134 Segoe UI;}}";
    rtf += "{\\colortbl ;\\red0\\green92\\blue197;\\red36\\green41\\blue47;}";
    rtf += "\\viewkind4\\uc1";
    rtf += "\\pard\\b\\fs26\\cf1 " + WideToRtf(L"【AnyNote 核心能力与功能全貌】") + "\\b0\\fs22\\par\\par";
    rtf += "\\cf2 " + WideToRtf(L"[√] 现代 C++20 + 原生 Win32 骨架与 Per-Monitor V2 DPI 动态感知") + "\\par";
    rtf += WideToRtf(L"[√] RichEdit 5.0 原生富文本内核与 1~4 级标题层级系统") + "\\par";
    rtf += WideToRtf(L"[√] 嵌入式多语言代码块卡片与语法高亮 (CodeBox)") + "\\par";
    rtf += WideToRtf(L"[√] 剪贴板图片原生 OLE 粘贴支持 (Win+Shift+S 截图直接 Ctrl+V)") + "\\par";
    rtf += WideToRtf(L"[√] 3×2 原生表格插入与行/列快捷增删编辑 (Ctrl+Shift+T)") + "\\par";
    rtf += WideToRtf(L"[√] 左侧目录树鼠标拖拽重排与父子层级移动 (Drag & Drop)") + "\\par";
    rtf += WideToRtf(L"[√] 仿 Notepad++ 独立浮动查找/替换/标记对话框 (Ctrl+F / Ctrl+H)") + "\\par";
    rtf += WideToRtf(L"[√] 全库笔记多维全文检索窗格 (Ctrl+Shift+F)") + "\\par";
    rtf += WideToRtf(L"[√] SQLite3MC 单文件持久化存储与 AES-256 全库透明加密") + "\\par";
    rtf += WideToRtf(L"[√] 绿色便携多笔记本库管理 (Multi-Vault) 与 Windows 原生 INI 配置") + "\\par";
    rtf += "}";
    return rtf;
}

std::wstring ExtractPlainTextFromRtf(std::string_view rtf) {
    std::wstring out;
    out.reserve(rtf.size() / 2);

    int groupDepth = 0;
    int skipGroupDepth = -1;

    size_t i = 0;
    size_t len = rtf.size();
    UINT codePage = 1252;
    size_t headerEnd = std::min(len, size_t(512));
    for (size_t p = 0; p + 8 < headerEnd; ++p) {
        if (rtf.substr(p, 7) == "\\ansicpg") {
            size_t q = p + 7;
            UINT parsed = 0;
            while (q < headerEnd && isdigit(static_cast<unsigned char>(rtf[q]))) {
                parsed = parsed * 10 + static_cast<UINT>(rtf[q] - '0');
                ++q;
            }
            if (parsed != 0) codePage = parsed;
            break;
        }
    }

    auto decodeBytes = [codePage](const std::string& bytes, std::wstring& target) {
        if (bytes.empty()) return;
        int needed = MultiByteToWideChar(codePage, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
        if (needed <= 0) return;
        std::wstring decoded(static_cast<size_t>(needed), L'\0');
        MultiByteToWideChar(codePage, 0, bytes.data(), static_cast<int>(bytes.size()), decoded.data(), needed);
        target += decoded;
    };

    while (i < len) {
        char c = rtf[i];

        if (c == '{') {
            groupDepth++;
            i++;
            if (skipGroupDepth < 0 && i < len && rtf[i] == '\\') {
                size_t kwStart = i + 1;
                size_t kwEnd = kwStart;
                while (kwEnd < len && (isalpha(static_cast<unsigned char>(rtf[kwEnd])) || rtf[kwEnd] == '*')) {
                    kwEnd++;
                }
                std::string_view kw = rtf.substr(kwStart, kwEnd - kwStart);
                if (kw == "*" || kw == "fonttbl" || kw == "colortbl" || kw == "stylesheet" || 
                    kw == "info" || kw == "generator" || kw == "pict" || kw == "object") {
                    skipGroupDepth = groupDepth;
                }
            }
            continue;
        }

        if (c == '}') {
            if (skipGroupDepth == groupDepth) {
                skipGroupDepth = -1;
            }
            if (groupDepth > 0) groupDepth--;
            i++;
            continue;
        }

        if (skipGroupDepth > 0) {
            i++;
            continue;
        }

        if (c == '\\') {
            i++;
            if (i >= len) break;
            char nextChar = rtf[i];

            if (nextChar == '{' || nextChar == '}' || nextChar == '\\') {
                out.push_back(static_cast<wchar_t>(nextChar));
                i++;
            } else if (nextChar == '~') {
                out.push_back(L' ');
                i++;
            } else if (nextChar == '\'') {
                std::string bytes;
                while (i < len && rtf[i] == '\'' && i + 2 < len) {
                    i++;
                    auto hexVal = [](char h) -> int {
                        if (h >= '0' && h <= '9') return h - '0';
                        if (h >= 'a' && h <= 'f') return h - 'a' + 10;
                        if (h >= 'A' && h <= 'F') return h - 'A' + 10;
                        return 0;
                    };
                    int byteVal = (hexVal(rtf[i]) << 4) | hexVal(rtf[i + 1]);
                    bytes.push_back(static_cast<char>(byteVal));
                    i += 2;
                    if (i + 2 >= len || rtf[i] != '\'') break;
                }
                decodeBytes(bytes, out);
            } else if (isalpha(static_cast<unsigned char>(nextChar))) {
                size_t wordStart = i;
                while (i < len && isalpha(static_cast<unsigned char>(rtf[i]))) {
                    i++;
                }
                std::string_view word = rtf.substr(wordStart, i - wordStart);

                bool hasNum = false;
                long long numVal = 0;
                int sign = 1;
                if (i < len && rtf[i] == '-') {
                    sign = -1;
                    i++;
                }
                size_t numStart = i;
                while (i < len && isdigit(static_cast<unsigned char>(rtf[i]))) {
                    hasNum = true;
                    i++;
                }
                if (hasNum) {
                    long long parsed = 0;
                    auto res = std::from_chars(rtf.data() + numStart, rtf.data() + i, parsed);
                    if (res.ec == std::errc()) {
                        numVal = sign * parsed;
                    } else {
                        hasNum = false;
                    }
                }

                if (i < len && rtf[i] == ' ') {
                    i++;
                }

                if (word == "par" || word == "line") {
                    out.push_back(L'\n');
                } else if (word == "tab") {
                    out.push_back(L'\t');
                } else if (word == "u" && hasNum) {
                    uint16_t ucode = static_cast<uint16_t>(static_cast<int16_t>(numVal));
                    out.push_back(static_cast<wchar_t>(ucode));
                    if (i < len && rtf[i] != '\\' && rtf[i] != '{' && rtf[i] != '}') {
                        i++;
                    }
                }
            } else {
                i++;
            }
            continue;
        }

        if (c == '\r' || c == '\n') {
            i++;
            continue;
        }

        out.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));
        i++;
    }

    return out;
}

} // namespace

NoteRepository::NoteRepository(Database& db)
    : m_db(db) {
}

bool NoteRepository::InitializeSchema() {
    if (!m_db.IsOpen()) return false;

    const char* schemaSql = R"(
        CREATE TABLE IF NOT EXISTS notebook_meta (
            key   TEXT PRIMARY KEY,
            value TEXT
        );

        CREATE TABLE IF NOT EXISTS nodes (
            id            INTEGER PRIMARY KEY AUTOINCREMENT,
            parent_id     INTEGER NOT NULL DEFAULT 0,
            sequence      INTEGER NOT NULL DEFAULT 0,
            title         TEXT NOT NULL,
            node_type     INTEGER NOT NULL DEFAULT 0,
            created_time  INTEGER NOT NULL,
            modified_time INTEGER NOT NULL
        );

        CREATE INDEX IF NOT EXISTS idx_nodes_parent_seq ON nodes(parent_id, sequence);

        CREATE TABLE IF NOT EXISTS node_contents (
            node_id       INTEGER PRIMARY KEY,
            format_type   INTEGER NOT NULL DEFAULT 1,
            content_rtf   BLOB,
            plain_text    TEXT,
            FOREIGN KEY(node_id) REFERENCES nodes(id) ON DELETE CASCADE
        );
    )";

    if (!m_db.Execute(schemaSql)) {
        return false;
    }

    // 检查并自动升级既有数据库，保证具备 plain_text 列
    bool hasPlainTextCol = false;
    Database::Statement infoStmt;
    if (!infoStmt.Prepare(m_db, "PRAGMA table_info(node_contents);")) {
        return false;
    }
    while (infoStmt.Step() == SQLITE_ROW) {
        if (infoStmt.GetText(1) == "plain_text") {
            hasPlainTextCol = true;
            break;
        }
    }
    if (!hasPlainTextCol) {
        if (!m_db.Execute("ALTER TABLE node_contents ADD COLUMN plain_text TEXT;")) {
            return false;
        }
    }

    // 自动为已有旧笔记数据补充纯文本索引 (如果 plain_text 为空但 content_rtf 存在)
    Database::Statement backfillStmt;
    if (backfillStmt.Prepare(m_db, "SELECT node_id, content_rtf FROM node_contents WHERE content_rtf IS NOT NULL AND (plain_text IS NULL OR plain_text = '');")) {
        std::vector<std::pair<int64_t, std::string>> toMigrate;
        while (backfillStmt.Step() == SQLITE_ROW) {
            int64_t nid = backfillStmt.GetInt64(0);
            std::string rtf = backfillStmt.GetBlob(1);
            if (!rtf.empty()) {
                toMigrate.emplace_back(nid, std::move(rtf));
            }
        }
        if (!toMigrate.empty()) {
            if (!m_db.BeginTransaction()) {
                return false;
            }
            Database::Statement updateStmt;
            if (!updateStmt.Prepare(m_db, "UPDATE node_contents SET plain_text = ? WHERE node_id = ?;")) {
                m_db.Rollback();
                return false;
            }
            for (const auto& [nid, rtf] : toMigrate) {
                std::wstring plain = ExtractPlainTextFromRtf(rtf);
                if (!updateStmt.Reset() ||
                    !updateStmt.BindText(1, plain) ||
                    !updateStmt.BindInt64(2, nid) ||
                    updateStmt.Step() != SQLITE_DONE) {
                    m_db.Rollback();
                    return false;
                }
            }
            if (!m_db.Commit()) {
                m_db.Rollback();
                return false;
            }
        }
    } else {
        return false;
    }

    return true;
}

int64_t NoteRepository::GetNodeCount() {
    Database::Statement stmt;
    if (!stmt.Prepare(m_db, "SELECT count(*) FROM nodes;")) {
        return 0;
    }
    if (stmt.Step() == SQLITE_ROW) {
        return stmt.GetInt64(0);
    }
    return 0;
}

std::vector<NoteNode> NoteRepository::GetAllNodes() {
    std::vector<NoteNode> list;
    Database::Statement stmt;
    if (!stmt.Prepare(m_db, "SELECT id, parent_id, sequence, title, node_type, created_time, modified_time FROM nodes ORDER BY parent_id ASC, sequence ASC, id ASC;")) {
        return list;
    }

    while (stmt.Step() == SQLITE_ROW) {
        NoteNode node;
        node.id = stmt.GetInt64(0);
        node.parentId = stmt.GetInt64(1);
        node.sequence = stmt.GetInt(2);
        node.title = stmt.GetWideText(3);
        node.nodeType = stmt.GetInt(4);
        node.createdTime = stmt.GetInt64(5);
        node.modifiedTime = stmt.GetInt64(6);
        list.push_back(std::move(node));
    }
    return list;
}

std::optional<NoteNode> NoteRepository::GetNode(int64_t id) {
    Database::Statement stmt;
    if (!stmt.Prepare(m_db, "SELECT id, parent_id, sequence, title, node_type, created_time, modified_time FROM nodes WHERE id = ?;")) {
        return std::nullopt;
    }
    stmt.BindInt64(1, id);
    if (stmt.Step() == SQLITE_ROW) {
        NoteNode node;
        node.id = stmt.GetInt64(0);
        node.parentId = stmt.GetInt64(1);
        node.sequence = stmt.GetInt(2);
        node.title = stmt.GetWideText(3);
        node.nodeType = stmt.GetInt(4);
        node.createdTime = stmt.GetInt64(5);
        node.modifiedTime = stmt.GetInt64(6);
        return node;
    }
    return std::nullopt;
}

std::string NoteRepository::GetNoteContent(int64_t nodeId) {
    Database::Statement stmt;
    if (!stmt.Prepare(m_db, "SELECT content_rtf FROM node_contents WHERE node_id = ?;")) {
        return {};
    }
    stmt.BindInt64(1, nodeId);
    if (stmt.Step() == SQLITE_ROW) {
        return stmt.GetBlob(0);
    }
    return {};
}

std::wstring NoteRepository::GetNotePlainText(int64_t nodeId) {
    Database::Statement stmt;
    if (!stmt.Prepare(m_db, "SELECT plain_text FROM node_contents WHERE node_id = ?;")) {
        return {};
    }
    stmt.BindInt64(1, nodeId);
    if (stmt.Step() == SQLITE_ROW) {
        return stmt.GetWideText(0);
    }
    return {};
}

int64_t NoteRepository::CreateNote(int64_t parentId, const std::wstring& title, int sequence, int nodeType, const std::string& initialRtf, const std::wstring& initialPlainText) {
    if (!m_db.IsOpen()) return 0;

    if (sequence < 0) {
        Database::Statement seqStmt;
        if (seqStmt.Prepare(m_db, "SELECT COALESCE(MAX(sequence), -1) + 1 FROM nodes WHERE parent_id = ?;")) {
            seqStmt.BindInt64(1, parentId);
            if (seqStmt.Step() == SQLITE_ROW) {
                sequence = seqStmt.GetInt(0);
            } else {
                sequence = 0;
            }
        } else {
            sequence = 0;
        }
    }

    int64_t now = GetCurrentUnixTimestamp();

    if (!m_db.BeginTransaction()) return 0;

    Database::Statement insStmt;
    if (!insStmt.Prepare(m_db, "INSERT INTO nodes (parent_id, sequence, title, node_type, created_time, modified_time) VALUES (?, ?, ?, ?, ?, ?);")) {
        m_db.Rollback();
        return 0;
    }

    insStmt.BindInt64(1, parentId);
    insStmt.BindInt(2, sequence);
    insStmt.BindText(3, title);
    insStmt.BindInt(4, nodeType);
    insStmt.BindInt64(5, now);
    insStmt.BindInt64(6, now);

    if (insStmt.Step() != SQLITE_DONE) {
        m_db.Rollback();
        return 0;
    }

    int64_t newId = sqlite3_last_insert_rowid(m_db.GetRawHandle());

    Database::Statement cntStmt;
    if (!cntStmt.Prepare(m_db, "INSERT INTO node_contents (node_id, format_type, content_rtf, plain_text) VALUES (?, 1, ?, ?);")) {
        m_db.Rollback();
        return 0;
    }

    cntStmt.BindInt64(1, newId);
    if (!initialRtf.empty()) {
        cntStmt.BindBlob(2, initialRtf.data(), initialRtf.size());
    } else {
        cntStmt.BindNull(2);
    }
    if (!initialPlainText.empty()) {
        cntStmt.BindText(3, initialPlainText);
    } else {
        cntStmt.BindNull(3);
    }

    if (cntStmt.Step() != SQLITE_DONE) {
        m_db.Rollback();
        return 0;
    }

    if (!m_db.Commit()) {
        m_db.Rollback();
        return 0;
    }
    return newId;
}

bool NoteRepository::UpdateNoteTitle(int64_t nodeId, const std::wstring& title) {
    if (!m_db.IsOpen()) return false;

    int64_t now = GetCurrentUnixTimestamp();
    Database::Statement stmt;
    if (!stmt.Prepare(m_db, "UPDATE nodes SET title = ?, modified_time = ? WHERE id = ?;")) {
        return false;
    }
    stmt.BindText(1, title);
    stmt.BindInt64(2, now);
    stmt.BindInt64(3, nodeId);
    return stmt.Step() == SQLITE_DONE;
}

bool NoteRepository::UpdateNoteContent(int64_t nodeId, const std::string& rtfContent, const std::wstring& plainText) {
    if (!m_db.IsOpen()) return false;

    int64_t now = GetCurrentUnixTimestamp();
    if (!m_db.BeginTransaction()) return false;

    Database::Statement cntStmt;
    if (!cntStmt.Prepare(m_db,
        "INSERT INTO node_contents (node_id, format_type, content_rtf, plain_text) "
        "VALUES (?, 1, ?, ?) "
        "ON CONFLICT(node_id) DO UPDATE SET "
        "content_rtf = excluded.content_rtf, "
        "plain_text = excluded.plain_text;")) {
        m_db.Rollback();
        return false;
    }

    cntStmt.BindInt64(1, nodeId);
    if (!rtfContent.empty()) {
        cntStmt.BindBlob(2, rtfContent.data(), rtfContent.size());
    } else {
        cntStmt.BindNull(2);
    }
    if (!plainText.empty()) {
        cntStmt.BindText(3, plainText);
    } else {
        cntStmt.BindNull(3);
    }

    if (cntStmt.Step() != SQLITE_DONE) {
        m_db.Rollback();
        return false;
    }

    Database::Statement nodeStmt;
    if (!nodeStmt.Prepare(m_db, "UPDATE nodes SET modified_time = ? WHERE id = ?;")) {
        m_db.Rollback();
        return false;
    }
    if (!nodeStmt.BindInt64(1, now) || !nodeStmt.BindInt64(2, nodeId) || nodeStmt.Step() != SQLITE_DONE) {
        m_db.Rollback();
        return false;
    }

    if (!m_db.Commit()) {
        m_db.Rollback();
        return false;
    }
    return true;
}

static std::wstring EscapeSqlLikePattern(std::wstring_view input) {
    std::wstring escaped;
    escaped.reserve(input.size() + 4);
    for (wchar_t ch : input) {
        if (ch == L'%' || ch == L'_' || ch == L'\\') {
            escaped.push_back(L'\\');
        }
        escaped.push_back(ch);
    }
    return escaped;
}

std::vector<SearchResult> NoteRepository::SearchNotes(const std::wstring& keyword, bool matchCase, bool searchContent) {
    std::vector<SearchResult> results;
    if (!m_db.IsOpen() || keyword.empty()) return results;

    std::wstring needle = keyword;
    if (!matchCase) {
        std::transform(needle.begin(), needle.end(), needle.begin(), ::towlower);
    }

    std::wstring likePattern = L"%" + EscapeSqlLikePattern(keyword) + L"%";

    // 1. 标题匹配：利用 SQL LIKE 初筛，避免将全库所有标题拉回内存逐行遍历
    {
        Database::Statement stmt;
        if (stmt.Prepare(m_db, "SELECT id, title FROM nodes WHERE title LIKE ? ESCAPE '\\' ORDER BY sequence ASC, id ASC;")) {
            stmt.BindText(1, likePattern);
            while (stmt.Step() == SQLITE_ROW) {
                int64_t id = stmt.GetInt64(0);
                std::wstring title = stmt.GetWideText(1);
                std::wstring titleCmp = title;
                if (!matchCase) {
                    std::transform(titleCmp.begin(), titleCmp.end(), titleCmp.begin(), ::towlower);
                }
                auto pos = titleCmp.find(needle);
                if (pos != std::wstring::npos) {
                    SearchResult sr;
                    sr.nodeId = id;
                    sr.title = title;
                    sr.matchInTitle = true;
                    sr.snippet = L"【标题匹配】 " + title;
                    sr.matchOffsetInText = -1;
                    results.push_back(std::move(sr));
                }
            }
        }
    }

    // 2. 正文纯文本匹配：
    if (searchContent) {
        Database::Statement stmt;
        // 核心性能优化：完全排除大型 content_rtf 二进制 BLOB，并由 SQLite 在数据库引擎层进行 LIKE 初筛
        if (stmt.Prepare(m_db,
            "SELECT n.id, n.title, c.plain_text FROM nodes n "
            "JOIN node_contents c ON n.id = c.node_id "
            "WHERE c.plain_text IS NOT NULL AND c.plain_text != '' AND c.plain_text LIKE ? ESCAPE '\\';")) {
            stmt.BindText(1, likePattern);
            while (stmt.Step() == SQLITE_ROW) {
                int64_t id = stmt.GetInt64(0);
                std::wstring title = stmt.GetWideText(1);
                std::wstring content = stmt.GetWideText(2);
                if (content.empty()) continue;

                std::wstring contentCmp = content;
                if (!matchCase) {
                    std::transform(contentCmp.begin(), contentCmp.end(), contentCmp.begin(), ::towlower);
                }

                size_t pos = contentCmp.find(needle);
                if (pos != std::wstring::npos) {
                    SearchResult sr;
                    sr.nodeId = id;
                    sr.title = title;
                    sr.matchInTitle = false;
                    sr.matchOffsetInText = static_cast<int>(pos);

                    // 截取上下文摘要 (前后各约 25 个字符)
                    size_t start = (pos > 25) ? (pos - 25) : 0;
                    size_t end = std::min(content.size(), pos + needle.size() + 35);
                    std::wstring snippet = content.substr(start, end - start);
                    for (auto& ch : snippet) {
                        if (ch == L'\r' || ch == L'\n' || ch == L'\t') ch = L' ';
                    }
                    if (start > 0) snippet = L"..." + snippet;
                    if (end < content.size()) snippet = snippet + L"...";

                    sr.snippet = std::move(snippet);
                    results.push_back(std::move(sr));
                }
            }
        }

        // 冷数据保底兼容：仅对极罕见的 plain_text 为空但 content_rtf 存在的旧记录单独按需查询
        Database::Statement coldStmt;
        if (coldStmt.Prepare(m_db,
            "SELECT n.id, n.title, c.content_rtf FROM nodes n "
            "JOIN node_contents c ON n.id = c.node_id "
            "WHERE (c.plain_text IS NULL OR c.plain_text = '') AND c.content_rtf IS NOT NULL;")) {
            while (coldStmt.Step() == SQLITE_ROW) {
                int64_t id = coldStmt.GetInt64(0);
                std::string rtf = coldStmt.GetBlob(2);
                if (rtf.empty()) continue;
                std::wstring content = ExtractPlainTextFromRtf(rtf);
                if (content.empty()) continue;

                std::wstring contentCmp = content;
                if (!matchCase) {
                    std::transform(contentCmp.begin(), contentCmp.end(), contentCmp.begin(), ::towlower);
                }

                size_t pos = contentCmp.find(needle);
                if (pos != std::wstring::npos) {
                    SearchResult sr;
                    sr.nodeId = id;
                    sr.title = coldStmt.GetWideText(1);
                    sr.matchInTitle = false;
                    sr.matchOffsetInText = static_cast<int>(pos);

                    size_t start = (pos > 25) ? (pos - 25) : 0;
                    size_t end = std::min(content.size(), pos + needle.size() + 35);
                    std::wstring snippet = content.substr(start, end - start);
                    for (auto& ch : snippet) {
                        if (ch == L'\r' || ch == L'\n' || ch == L'\t') ch = L' ';
                    }
                    if (start > 0) snippet = L"..." + snippet;
                    if (end < content.size()) snippet = snippet + L"...";

                    sr.snippet = std::move(snippet);
                    results.push_back(std::move(sr));
                }
            }
        }
    }

    return results;
}

bool NoteRepository::UpdateNodeHierarchy(int64_t nodeId, int64_t newParentId, int newSequence) {
    if (!m_db.IsOpen()) return false;

    int64_t now = GetCurrentUnixTimestamp();
    Database::Statement stmt;
    if (!stmt.Prepare(m_db, "UPDATE nodes SET parent_id = ?, sequence = ?, modified_time = ? WHERE id = ?;")) {
        return false;
    }
    stmt.BindInt64(1, newParentId);
    stmt.BindInt(2, newSequence);
    stmt.BindInt64(3, now);
    stmt.BindInt64(4, nodeId);
    return stmt.Step() == SQLITE_DONE;
}

bool NoteRepository::IsDescendantOf(int64_t checkId, int64_t ancestorId) {
    if (checkId <= 0 || ancestorId <= 0) return false;
    if (checkId == ancestorId) return true;

    int64_t curId = checkId;
    std::unordered_set<int64_t> visited;
    while (curId > 0 && visited.insert(curId).second) {
        if (curId == ancestorId) return true;
        Database::Statement stmt;
        if (!stmt.Prepare(m_db, "SELECT parent_id FROM nodes WHERE id = ?;")) {
            break;
        }
        stmt.BindInt64(1, curId);
        if (stmt.Step() == SQLITE_ROW) {
            curId = stmt.GetInt64(0);
        } else {
            break;
        }
    }
    return false;
}

bool NoteRepository::ReorderSiblings(int64_t parentId) {
    Database::Statement stmt;
    if (!stmt.Prepare(m_db, "SELECT id FROM nodes WHERE parent_id = ? ORDER BY sequence ASC, id ASC;")) {
        return false;
    }
    if (!stmt.BindInt64(1, parentId)) {
        return false;
    }

    std::vector<int64_t> ids;
    int rc = SQLITE_OK;
    while ((rc = stmt.Step()) == SQLITE_ROW) {
        ids.push_back(stmt.GetInt64(0));
    }
    if (rc != SQLITE_DONE) {
        return false;
    }

    int seq = 0;
    Database::Statement upd;
    if (!upd.Prepare(m_db, "UPDATE nodes SET sequence = ? WHERE id = ?;")) {
        return false;
    }
    for (int64_t id : ids) {
        if (!upd.Reset() || !upd.BindInt(1, seq++) || !upd.BindInt64(2, id) || upd.Step() != SQLITE_DONE) {
            return false;
        }
    }
    return true;
}

bool NoteRepository::MoveNode(int64_t dragNodeId, int64_t targetNodeId, DropPosition position) {
    if (!m_db.IsOpen() || dragNodeId <= 0 || position == DropPosition::None) {
        return false;
    }

    auto dragNodeOpt = GetNode(dragNodeId);
    if (!dragNodeOpt.has_value()) return false;
    auto dragNode = dragNodeOpt.value();

    int64_t oldParentId = dragNode.parentId;
    int64_t newParentId = 0;
    int targetSeq = 0;

    if (position == DropPosition::AtRootEnd) {
        newParentId = 0;
        Database::Statement seqStmt;
        if (seqStmt.Prepare(m_db, "SELECT COALESCE(MAX(sequence), -1) + 1 FROM nodes WHERE parent_id = 0;")) {
            if (seqStmt.Step() == SQLITE_ROW) {
                targetSeq = seqStmt.GetInt(0);
            }
        }
    } else {
        if (targetNodeId <= 0 || dragNodeId == targetNodeId) {
            return false;
        }

        auto targetNodeOpt = GetNode(targetNodeId);
        if (!targetNodeOpt.has_value()) return false;
        auto targetNode = targetNodeOpt.value();

        if (position == DropPosition::AsChild) {
            newParentId = targetNode.id;
            Database::Statement seqStmt;
            if (seqStmt.Prepare(m_db, "SELECT COALESCE(MAX(sequence), -1) + 1 FROM nodes WHERE parent_id = ?;")) {
                seqStmt.BindInt64(1, newParentId);
                if (seqStmt.Step() == SQLITE_ROW) {
                    targetSeq = seqStmt.GetInt(0);
                }
            }
        } else if (position == DropPosition::Before) {
            newParentId = targetNode.parentId;
            targetSeq = targetNode.sequence;
        } else if (position == DropPosition::After) {
            newParentId = targetNode.parentId;
            targetSeq = targetNode.sequence + 1;
        }
    }

    // 严禁将节点移动到自己或自己的后代之下（防止循环依赖导致树损坏）
    if (IsDescendantOf(newParentId, dragNodeId)) {
        return false;
    }

    if (!m_db.BeginTransaction()) {
        return false;
    }

    int64_t now = GetCurrentUnixTimestamp();

    // 1. 先将当前节点 sequence 设为 -1，避免参与后续的兄弟节点移位判定
    Database::Statement tempStmt;
    if (!tempStmt.Prepare(m_db, "UPDATE nodes SET sequence = -1 WHERE id = ?;")) {
        m_db.Rollback();
        return false;
    }
    tempStmt.BindInt64(1, dragNodeId);
    if (tempStmt.Step() != SQLITE_DONE) {
        m_db.Rollback();
        return false;
    }

    // 2. 如果是插入到指定位置 (Before/After)，在新父节点下为待插入位置腾出空间
    if (position == DropPosition::Before || position == DropPosition::After) {
        Database::Statement shiftStmt;
        if (!shiftStmt.Prepare(m_db, "UPDATE nodes SET sequence = sequence + 1 WHERE parent_id = ? AND sequence >= ? AND id != ?;")) {
            m_db.Rollback();
            return false;
        }
        if (!shiftStmt.BindInt64(1, newParentId) || !shiftStmt.BindInt(2, targetSeq) || !shiftStmt.BindInt64(3, dragNodeId)) {
            m_db.Rollback();
            return false;
        }
        if (shiftStmt.Step() != SQLITE_DONE) {
            m_db.Rollback();
            return false;
        }
    }

    // 3. 更新被拖动节点的 parent_id, sequence, modified_time
    Database::Statement updStmt;
    if (!updStmt.Prepare(m_db, "UPDATE nodes SET parent_id = ?, sequence = ?, modified_time = ? WHERE id = ?;")) {
        m_db.Rollback();
        return false;
    }
    updStmt.BindInt64(1, newParentId);
    updStmt.BindInt(2, targetSeq);
    updStmt.BindInt64(3, now);
    updStmt.BindInt64(4, dragNodeId);
    if (updStmt.Step() != SQLITE_DONE) {
        m_db.Rollback();
        return false;
    }

    // 4. 分别规整旧父节点与新父节点下的序列号，保证连续性
    if (!ReorderSiblings(oldParentId)) {
        m_db.Rollback();
        return false;
    }
    if (newParentId != oldParentId) {
        if (!ReorderSiblings(newParentId)) {
            m_db.Rollback();
            return false;
        }
    }

    if (!m_db.Commit()) {
        m_db.Rollback();
        return false;
    }

    return true;
}

bool NoteRepository::DeleteNote(int64_t nodeId) {
    if (!m_db.IsOpen()) return false;

    if (!m_db.BeginTransaction()) return false;

    // 递归删除所有后代正文
    Database::Statement cntStmt;
    if (!cntStmt.Prepare(m_db,
        "WITH RECURSIVE to_delete AS ("
        "  SELECT ? AS id UNION ALL SELECT n.id FROM nodes n JOIN to_delete d ON n.parent_id = d.id"
        ") DELETE FROM node_contents WHERE node_id IN (SELECT id FROM to_delete);")) {
        m_db.Rollback();
        return false;
    }
    if (!cntStmt.BindInt64(1, nodeId) || cntStmt.Step() != SQLITE_DONE) {
        m_db.Rollback();
        return false;
    }

    // 递归删除所有后代节点
    Database::Statement nodeStmt;
    if (!nodeStmt.Prepare(m_db,
        "WITH RECURSIVE to_delete AS ("
        "  SELECT ? AS id UNION ALL SELECT n.id FROM nodes n JOIN to_delete d ON n.parent_id = d.id"
        ") DELETE FROM nodes WHERE id IN (SELECT id FROM to_delete);")) {
        m_db.Rollback();
        return false;
    }
    if (!nodeStmt.BindInt64(1, nodeId) || nodeStmt.Step() != SQLITE_DONE) {
        m_db.Rollback();
        return false;
    }

    if (!m_db.Commit()) {
        m_db.Rollback();
        return false;
    }
    return true;
}

bool NoteRepository::CreateDefaultWelcomeNotes() {
    if (!m_db.IsOpen()) return false;

    int64_t root1 = CreateNote(0, L"📌 欢迎使用 AnyNote", 0, 0, BuildDefaultWelcomeRtf(),
        L"欢迎使用 AnyNote - 基于 C++20 与原生 Win32 的分层树状笔记！\n特性高光：多笔记本库管理、目录树拖拽重排、原生表格排版、全库多维搜索、原生富文本内核、剪贴板图片直接粘贴、原生代码框、AES-256 全库加密存储。");
    if (root1 > 0) {
        CreateNote(root1, L"🚀 快速上手指南", 0, 0, BuildDefaultQuickStartRtf(),
            L"快速上手指南：1. 顶部切换栏多库管理；2. 目录树右键与拖拽调整层级；3. 粗体斜体与 1~4 级标题；4. Ctrl+Shift+T 插入表格与行列操作；5. Ctrl+K 插入代码块；6. 截图直接 Ctrl+V 粘贴图片；7. Ctrl+Shift+F 全库全文搜索；8. SQLite 自动保存与 AES-256 加密。");
        CreateNote(root1, L"💡 特性与快捷键说明", 1, 0, BuildDefaultFeaturesRtf(),
            L"特性与常用快捷键清单：Ctrl+N 新建笔记，Ctrl+Shift+N 新建子笔记，F2 重命名，Del 删除，拖拽重排，Ctrl+S 保存，Ctrl+Shift+T 插入表格，Ctrl+K 插入代码块，Ctrl+F 查找，Ctrl+H 替换，Ctrl+Shift+F 全库搜索。");
    }

    int64_t root2 = CreateNote(0, L"💻 开发与技术积累", 1, 0, "", L"现代软件工程架构设计与技术积累。");
    if (root2 > 0) {
        CreateNote(root2, L"📘 现代 C++ 与 Win32 架构", 0, 0, BuildDefaultCppRtf(),
            L"现代 C++ 与 Win32 架构设计：GWLP_USERDATA 绑定，Per-Monitor V2 DPI 高分屏，全静态链接零第三方 DLL 依赖。");
        CreateNote(root2, L"🔒 SQLite3MC 数据库与安全", 1, 0, BuildDefaultSqliteRtf(),
            L"SQLite3MC 数据库与安全机制：AES-256 全库透明加解密，nodes 树形表，node_contents 内容表，anynote.ini 原生便携配置。");
    }

    int64_t root3 = CreateNote(0, L"📝 随手记 / 待办", 2, 0, "", L"日常笔记与待办清单。");
    if (root3 > 0) {
        CreateNote(root3, L"计划清单", 0, 0, BuildDefaultTodoRtf(),
            L"AnyNote 核心能力一览：[√] Win32 骨架；[√] RichEdit 富文本；[√] 标题层级；[√] 表格排版；[√] 代码框；[√] 目录树拖拽重排；[√] 全库多维搜索；[√] SQLite3MC 加密存储；[√] 多笔记本库 (Multi-Vault)。");
    }

    return true;
}

bool NoteRepository::SetMeta(const std::string& key, const std::string& value) {
    if (!m_db.IsOpen()) return false;

    Database::Statement stmt;
    if (!stmt.Prepare(m_db, "INSERT INTO notebook_meta (key, value) VALUES (?, ?) ON CONFLICT(key) DO UPDATE SET value = excluded.value;")) {
        return false;
    }
    stmt.BindText(1, key);
    stmt.BindText(2, value);
    return stmt.Step() == SQLITE_DONE;
}

std::string NoteRepository::GetMeta(const std::string& key, const std::string& defaultValue) {
    if (!m_db.IsOpen()) return defaultValue;

    Database::Statement stmt;
    if (!stmt.Prepare(m_db, "SELECT value FROM notebook_meta WHERE key = ?;")) {
        return defaultValue;
    }
    stmt.BindText(1, key);
    if (stmt.Step() == SQLITE_ROW) {
        return stmt.GetText(0);
    }
    return defaultValue;
}

} // namespace anynote::storage
