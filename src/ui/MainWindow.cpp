#include "MainWindow.h"
#include "resource.h"
#include "common/StringUtils.h"
#include "common/SyntaxHighlighter.h"
#include <commctrl.h>
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace anynote::ui {

using anynote::utils::WideToRtf;

namespace {

struct CodeDialogContext {
    std::wstring initialCode;
    common::CodeLanguage selectedLang = common::CodeLanguage::Cpp;
    std::wstring resultText;
    bool ok = false;
};

INT_PTR CALLBACK CodeDialogProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_INITDIALOG: {
        SetWindowLongPtrW(hDlg, DWLP_USER, lParam);
        auto* pCtx = reinterpret_cast<CodeDialogContext*>(lParam);

        HWND hCombo = GetDlgItem(hDlg, IDC_CODE_LANG);
        const auto& languages = common::GetSupportedLanguages();
        for (const auto& langInfo : languages) {
            SendMessageW(hCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(langInfo.name));
        }
        SendMessageW(hCombo, CB_SETCURSEL, 0, 0);

        HWND hEdit = GetDlgItem(hDlg, IDC_CODE_TEXT);
        HFONT hFont = CreateFontW(
            -MulDiv(10, GetDpiForWindow(hDlg), 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas"
        );
        SetPropW(hDlg, L"CodeFont", reinterpret_cast<HANDLE>(hFont));
        SendMessageW(hEdit, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);

        if (pCtx && !pCtx->initialCode.empty()) {
            SetWindowTextW(hEdit, pCtx->initialCode.c_str());
        } else if (!languages.empty()) {
            SetWindowTextW(hEdit, languages[0].defaultTemplate);
        }
        SendMessageW(hEdit, EM_SETSEL, 0, -1);
        SetFocus(hEdit);
        return FALSE;
    }

    case WM_COMMAND: {
        WORD id = LOWORD(wParam);
        WORD code = HIWORD(wParam);

        if (id == IDC_CODE_LANG && code == CBN_SELCHANGE) {
            HWND hCombo = GetDlgItem(hDlg, IDC_CODE_LANG);
            int idx = static_cast<int>(SendMessageW(hCombo, CB_GETCURSEL, 0, 0));
            HWND hEdit = GetDlgItem(hDlg, IDC_CODE_TEXT);
            const auto& languages = common::GetSupportedLanguages();
            if (idx >= 0 && idx < static_cast<int>(languages.size())) {
                int textLen = GetWindowTextLengthW(hEdit);
                if (textLen == 0) {
                    SetWindowTextW(hEdit, languages[idx].defaultTemplate);
                } else {
                    std::wstring currentText(textLen + 1, L'\0');
                    GetWindowTextW(hEdit, currentText.data(), textLen + 1);
                    currentText.resize(textLen);
                    for (const auto& otherLang : languages) {
                        if (currentText == otherLang.defaultTemplate) {
                            SetWindowTextW(hEdit, languages[idx].defaultTemplate);
                            break;
                        }
                    }
                }
            }
            return TRUE;
        }

        if (id == IDOK) {
            auto* pCtx = reinterpret_cast<CodeDialogContext*>(GetWindowLongPtrW(hDlg, DWLP_USER));
            if (pCtx) {
                HWND hCombo = GetDlgItem(hDlg, IDC_CODE_LANG);
                int idx = static_cast<int>(SendMessageW(hCombo, CB_GETCURSEL, 0, 0));
                const auto& languages = common::GetSupportedLanguages();
                if (idx >= 0 && idx < static_cast<int>(languages.size())) {
                    pCtx->selectedLang = languages[idx].lang;
                }

                HWND hEdit = GetDlgItem(hDlg, IDC_CODE_TEXT);
                int len = GetWindowTextLengthW(hEdit);
                if (len > 0) {
                    std::wstring buf(len + 1, L'\0');
                    GetWindowTextW(hEdit, buf.data(), len + 1);
                    buf.resize(len);
                    pCtx->resultText = std::move(buf);
                }
                pCtx->ok = true;
            }
            EndDialog(hDlg, IDOK);
            return TRUE;
        }

        if (id == IDCANCEL) {
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;
    }

    case WM_NCDESTROY: {
        auto hFont = reinterpret_cast<HFONT>(RemovePropW(hDlg, L"CodeFont"));
        if (hFont) {
            DeleteObject(hFont);
        }
        break;
    }
    }
    return FALSE;
}

std::string BuildWelcomeNoteRtf() {
    std::string rtf;
    rtf += "{\\rtf1\\ansi\\deff0\\nouicompat";
    rtf += "{\\fonttbl{\\f0\\fnil\\fcharset134 Segoe UI;}{\\f1\\fnil\\fcharset0 Consolas;}}";
    rtf += "{\\colortbl ;\\red246\\green248\\blue250;\\red225\\green228\\blue232;\\red36\\green41\\blue47;\\red0\\green92\\blue197;\\red215\\green58\\blue73;\\red3\\green47\\blue98;\\red106\\green115\\blue125;\\red0\\green92\\blue197;\\red111\\green66\\blue193;\\red3\\green102\\blue214;}";
    rtf += "\\viewkind4\\uc1";
    rtf += "\\pard\\b\\fs28\\cf4 " + WideToRtf(L"欢迎使用 AnyNote - 基于 C++20 与原生 Win32 的分层树状笔记！") + "\\b0\\fs22\\par\\par";
    rtf += "\\cf3\\b " + WideToRtf(L"★ 特性高光：") + "\\b0\\par";
    rtf += WideToRtf(L"• ") + "\\b " + WideToRtf(L"原生富文本内核") + "\\b0 " + WideToRtf(L"：采用 Windows MSFTEDIT.DLL (RichEdit 5.0)，毫秒启动，极低内存；") + "\\par";
    rtf += WideToRtf(L"• ") + "\\b " + WideToRtf(L"剪贴板图片直接粘贴") + "\\b0 " + WideToRtf(L"：按 Win+Shift+S 截图，在编辑器中按 ") + "\\b Ctrl+V\\b0 " + WideToRtf(L" 即可直接粘贴截图；") + "\\par";
    rtf += WideToRtf(L"• ") + "\\b " + WideToRtf(L"原生代码框 (CodeBox)") + "\\b0 " + WideToRtf(L"：点击上方「代码块」或按 ") + "\\b Ctrl+K\\b0 " + WideToRtf(L"，支持多语言高亮、卡片底色与 Consolas 等宽排版；") + "\\par";
    rtf += WideToRtf(L"• ") + "\\b " + WideToRtf(L"单文件安全存储") + "\\b0 " + WideToRtf(L"：静态集成 SQLite3MC，支持 AES-256 全库透明加密。") + "\\par\\par";
    rtf += "\\pard\\cf3 " + WideToRtf(L"下方即为原生嵌入的代码块示例：") + "\\par\\par";


    // 嵌入示例代码卡片
    rtf += "\\trowd\\trgaph108\\trleft360";
    rtf += "\\clbrdrt\\brdrs\\brdrw15\\brdrcf2";
    rtf += "\\clbrdrb\\brdrs\\brdrw15\\brdrcf2";
    rtf += "\\clbrdrl\\brdrs\\brdrw40\\brdrcf10";
    rtf += "\\clbrdrr\\brdrs\\brdrw15\\brdrcf2";
    rtf += "\\clcbpat1\\cellx8600\n";
    rtf += "\\pard\\intbl\\sl240\\slmult1\\sb0\\sa0\\f1\\fs19\\cf3 ";
    rtf += "\\cf7 " + WideToRtf(L"// C++ 示例代码块") + "\\par";
    rtf += "\\cf5 #include\\cf3  <iostream>\\par\\par";
    rtf += "\\cf4 int\\cf3  main() {\\par";
    rtf += "    std::cout << \\cf6 \"Hello, AnyNote Native CodeBlock!\"\\cf3  << std::endl;\\par";
    rtf += "    \\cf4 return\\cf3  0;\\par";
    rtf += "}\\cell\\row\n";
    rtf += "\\pard\\f0\\fs22\\par\\cf3 " + WideToRtf(L"可以在此继续输入正文，或选中文本后按 ") + "\\b Ctrl+K\\b0 " + WideToRtf(L" 转换为代码块！") + "\\par";
    rtf += "}";
    return rtf;
}

std::string BuildCppArchitectureNoteRtf() {
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

    rtf += "\\trowd\\trgaph108\\trleft360";
    rtf += "\\clbrdrt\\brdrs\\brdrw15\\brdrcf2";
    rtf += "\\clbrdrb\\brdrs\\brdrw15\\brdrcf2";
    rtf += "\\clbrdrl\\brdrs\\brdrw40\\brdrcf10";
    rtf += "\\clbrdrr\\brdrs\\brdrw15\\brdrcf2";
    rtf += "\\clcbpat1\\cellx8600\n";
    rtf += "\\pard\\intbl\\sl240\\slmult1\\sb0\\sa0\\f1\\fs19\\cf3 ";
    rtf += "\\cf7 " + WideToRtf(L"// Window 抽象基类消息路由回调") + "\\par";
    rtf += "\\cf9 LRESULT\\cf3  \\cf9 CALLBACK\\cf3  Window::StaticWndProc(\\cf9 HWND\\cf3  hWnd, \\cf9 UINT\\cf3  uMsg, \\cf9 WPARAM\\cf3  wParam, \\cf9 LPARAM\\cf3  lParam) {\\par";
    rtf += "    \\cf4 auto\\cf3 * pThis = \\cf4 reinterpret_cast\\cf3 <Window*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));\\par";
    rtf += "    \\cf4 if\\cf3  (uMsg == WM_NCCREATE) {\\par";
    rtf += "        \\cf4 auto\\cf3 * pCreate = \\cf4 reinterpret_cast\\cf3 <CREATESTRUCTW*>(lParam);\\par";
    rtf += "        pThis = \\cf4 reinterpret_cast\\cf3 <Window*>(pCreate->lpCreateParams);\\par";
    rtf += "        \\cf4 if\\cf3  (pThis) {\\par";
    rtf += "            pThis->m_hWnd = hWnd;\\par";
    rtf += "            SetWindowLongPtrW(hWnd, GWLP_USERDATA, \\cf4 reinterpret_cast\\cf3 <\\cf9 LONG_PTR\\cf3 >(pThis));\\par";
    rtf += "        }\\par";
    rtf += "    }\\par";
    rtf += "    \\cf4 return\\cf3  pThis ? pThis->HandleMessage(uMsg, wParam, lParam) : DefWindowProcW(hWnd, uMsg, wParam, lParam);\\par";
    rtf += "}\\cell\\row\n";
    rtf += "\\pard\\f0\\fs22\\par\\cf3 " + WideToRtf(L"这种封装方式优雅兼顾面向对象与 Win32 原生性能。") + "\\par";
    rtf += "}";
    return rtf;
}

std::string BuildSqlSecurityNoteRtf() {
    std::string rtf;
    rtf += "{\\rtf1\\ansi\\deff0\\nouicompat";
    rtf += "{\\fonttbl{\\f0\\fnil\\fcharset134 Segoe UI;}{\\f1\\fnil\\fcharset0 Consolas;}}";
    rtf += "{\\colortbl ;\\red246\\green248\\blue250;\\red225\\green228\\blue232;\\red36\\green41\\blue47;\\red0\\green92\\blue197;\\red215\\green58\\blue73;\\red3\\green47\\blue98;\\red106\\green115\\blue125;\\red0\\green92\\blue197;\\red111\\green66\\blue193;\\red3\\green102\\blue214;}";
    rtf += "\\viewkind4\\uc1";
    rtf += "\\pard\\b\\fs26\\cf4 " + WideToRtf(L"【SQLite3MC 数据库与安全机制】") + "\\b0\\fs22\\par\\par";
    rtf += "\\cf3 " + WideToRtf(L"SQLite3 Multiple Ciphers 支持 AES-256-CBC, AES-256-GCM 等工业级高强度加密。") + "\\par";
    rtf += WideToRtf(L"数据库核心表结构定义如下：") + "\\par\\par";

    rtf += "\\trowd\\trgaph108\\trleft360";
    rtf += "\\clbrdrt\\brdrs\\brdrw15\\brdrcf2";
    rtf += "\\clbrdrb\\brdrs\\brdrw15\\brdrcf2";
    rtf += "\\clbrdrl\\brdrs\\brdrw40\\brdrcf10";
    rtf += "\\clbrdrr\\brdrs\\brdrw15\\brdrcf2";
    rtf += "\\clcbpat1\\cellx8600\n";
    rtf += "\\pard\\intbl\\sl240\\slmult1\\sb0\\sa0\\f1\\fs19\\cf3 ";
    rtf += "\\cf7 " + WideToRtf(L"-- 节点元数据表") + "\\par";
    rtf += "\\cf4 CREATE TABLE IF NOT EXISTS\\cf3  nodes (\\par";
    rtf += "    id            \\cf4 INTEGER PRIMARY KEY AUTOINCREMENT\\cf3 ,\\par";
    rtf += "    parent_id     \\cf4 INTEGER NOT NULL DEFAULT\\cf3  0,\\par";
    rtf += "    sequence      \\cf4 INTEGER NOT NULL DEFAULT\\cf3  0,\\par";
    rtf += "    title         \\cf4 TEXT NOT NULL\\cf3 ,\\par";
    rtf += "    created_time  \\cf4 INTEGER NOT NULL\\cf3 ,\\par";
    rtf += "    modified_time \\cf4 INTEGER NOT NULL\\cf3 \\par";
    rtf += ");\\par\\par";
    rtf += "\\cf7 " + WideToRtf(L"-- 正文 RTF 字节流存储表") + "\\par";
    rtf += "\\cf4 CREATE TABLE IF NOT EXISTS\\cf3  node_contents (\\par";
    rtf += "    node_id       \\cf4 INTEGER PRIMARY KEY\\cf3 ,\\par";
    rtf += "    format_type   \\cf4 INTEGER NOT NULL DEFAULT\\cf3  1,\\par";
    rtf += "    content_rtf   \\cf4 BLOB\\cf3 ,\\par";
    rtf += "    \\cf4 FOREIGN KEY\\cf3 (node_id) \\cf4 REFERENCES\\cf3  nodes(id) \\cf4 ON DELETE CASCADE\\cf3 \\par";
    rtf += ");\\cell\\row\n";
    rtf += "\\pard\\f0\\fs22\\par\\cf3 " + WideToRtf(L"全库加密时执行 ") + "\\b PRAGMA key = 'password'\\b0 " + WideToRtf(L" 即可实现透明加解密。") + "\\par";
    rtf += "}";
    return rtf;
}

} // namespace

static bool s_mainClassRegistered = false;

MainWindow::MainWindow() = default;

void MainWindow::RegisterClassIfNeeded(HINSTANCE hInstance) {
    if (s_mainClassRegistered) return;

    WNDCLASSEXW wc = {sizeof(WNDCLASSEXW)};
    wc.lpfnWndProc = [](HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) -> LRESULT {
        auto* pThis = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
        if (uMsg == WM_NCCREATE) {
            auto* pCreate = reinterpret_cast<CREATESTRUCTW*>(lParam);
            pThis = reinterpret_cast<MainWindow*>(pCreate->lpCreateParams);
            if (pThis) {
                pThis->m_hWnd = hWnd;
                SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
            }
        }
        if (pThis) {
            return pThis->HandleMessage(uMsg, wParam, lParam);
        }
        return DefWindowProcW(hWnd, uMsg, wParam, lParam);
    };
    wc.hInstance = hInstance;
    wc.lpszClassName = GetClassName();
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

    RegisterClassExW(&wc);
    s_mainClassRegistered = true;
}

bool MainWindow::Initialize(HINSTANCE hInstance, int nCmdShow) {
    RegisterClassIfNeeded(hInstance);

    HMENU hMenu = LoadMenuW(hInstance, MAKEINTRESOURCEW(IDR_MAIN_MENU));

    if (!Create(
        GetClassName(),
        L"AnyNote - 树状富文本笔记 (C++ & Win32)",
        WS_OVERLAPPEDWINDOW,
        0,
        CW_USEDEFAULT, CW_USEDEFAULT, 1200, 780,
        nullptr,
        hMenu,
        hInstance
    )) {
        return false;
    }

    RECT rcClient;
    GetClientRect(m_hWnd, &rcClient);
    int clientW = rcClient.right - rcClient.left;
    int clientH = rcClient.bottom - rcClient.top;

    UINT dpi = GetDpiForWindow(m_hWnd);
    int toolbarH = MulDiv(36, dpi, 96);
    int statusbarH = MulDiv(24, dpi, 96);

    // 1. 初始化顶部格式工具栏
    m_toolbar.Initialize(m_hWnd, IDC_MAIN_TOOLBAR);

    // 2. 初始化底部状态栏
    m_hStatusBar = CreateStatusWindowW(
        WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
        L"就绪",
        m_hWnd,
        IDC_MAIN_STATUSBAR
    );

    int statWidths[] = { 450, 650, -1 };
    SendMessageW(m_hStatusBar, SB_SETPARTS, 3, reinterpret_cast<LPARAM>(statWidths));
    SendMessageW(m_hStatusBar, SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(L"就绪 | 欢迎使用 AnyNote"));
    SendMessageW(m_hStatusBar, SB_SETTEXTW, 1, reinterpret_cast<LPARAM>(L"状态: 已保存"));
    SendMessageW(m_hStatusBar, SB_SETTEXTW, 2, reinterpret_cast<LPARAM>(L"加密: 未加密"));

    // 3. 初始化分割条
    m_splitter.Initialize(m_hWnd, m_splitterPos, toolbarH, clientH - toolbarH - statusbarH);

    // 4. 初始化左侧目录树
    m_treeView.Initialize(m_hWnd, 0, toolbarH, m_splitterPos, clientH - toolbarH - statusbarH, IDC_MAIN_TREEVIEW);
    m_treeView.PopulateSampleNodes();

    // 5. 初始化右侧富文本编辑器
    if (!m_richEditView.Initialize(
        m_hWnd,
        m_splitterPos + 5, toolbarH,
        clientW - m_splitterPos - 5, clientH - toolbarH - statusbarH,
        IDC_MAIN_RICHEDIT
    )) {
        return false;
    }

    // 加载当前树节点内容 (含原生嵌入代码块展示)
    m_activeItem = m_treeView.GetSelectedItem();
    if (m_activeItem) {
        LoadNoteForItem(m_activeItem);
        UpdateStatusBar(L"当前笔记: " + m_treeView.GetItemText(m_activeItem));
    } else {
        m_richEditView.StreamInRTF(BuildWelcomeNoteRtf());
    }

    LayoutChildren(clientW, clientH);
    Show(nCmdShow);
    Update();

    return true;
}

void MainWindow::LayoutChildren(int clientWidth, int clientHeight) {
    if (clientWidth <= 0 || clientHeight <= 0) return;

    UINT dpi = GetDpiForWindow(m_hWnd);
    int toolbarH = m_toolbar.GetPreferredHeight();
    int statusbarH = MulDiv(24, dpi, 96);
    int workH = std::max(100, clientHeight - toolbarH - statusbarH);

    m_toolbar.SetBounds(0, 0, clientWidth, toolbarH);

    m_splitterPos = std::clamp(m_splitterPos, 150, std::max(160, clientWidth - 250));
    m_treeView.SetBounds(0, toolbarH, m_splitterPos, workH);
    m_splitter.SetBounds(m_splitterPos, toolbarH, 5, workH);

    int editorW = std::max(50, clientWidth - m_splitterPos - 5);
    m_richEditView.SetBounds(m_splitterPos + 5, toolbarH, editorW, workH);

    if (m_hStatusBar) {
        SendMessageW(m_hStatusBar, WM_SIZE, 0, 0);
    }
}

void MainWindow::UpdateStatusBar(const std::wstring& text) {
    if (m_hStatusBar) {
        SendMessageW(m_hStatusBar, SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(text.c_str()));
    }
}

void MainWindow::ShowInsertCodeDialog() {
    CodeDialogContext ctx;
    ctx.initialCode = m_richEditView.GetSelectedText();

    INT_PTR res = DialogBoxParamW(
        GetModuleHandleW(nullptr),
        MAKEINTRESOURCEW(IDD_INSERT_CODE),
        m_hWnd,
        CodeDialogProc,
        reinterpret_cast<LPARAM>(&ctx)
    );

    if (res == IDOK && ctx.ok) {
        m_richEditView.InsertCodeBlock(ctx.resultText, ctx.selectedLang);
    }
}

void MainWindow::SaveActiveNote() {
    if (!m_activeItem || !m_richEditView.GetHwnd()) return;

    m_noteRtfByItem[m_activeItem] = m_richEditView.StreamOutRTF();
}

void MainWindow::LoadNoteForItem(HTREEITEM hItem) {
    if (!hItem || !m_richEditView.GetHwnd()) return;

    const auto saved = m_noteRtfByItem.find(hItem);
    if (saved != m_noteRtfByItem.end()) {
        if (!saved->second.empty() && m_richEditView.StreamInRTF(saved->second)) {
            return;
        }
        m_richEditView.SetText(L"");
        return;
    }

    switch (m_treeView.GetItemData(hItem)) {
    case 1:
        m_richEditView.StreamInRTF(BuildWelcomeNoteRtf());
        break;
    case 2:
        m_richEditView.SetText(
            L"【快速上手指南】\r\n\r\n"
            L"1. 在左侧树形目录中右键或使用菜单添加同级或子笔记；\r\n"
            L"2. 点击上方工具栏按钮或使用快捷键进行加粗 (Ctrl+B)、斜体 (Ctrl+I)；\r\n"
            L"3. 按 Ctrl+K 或点击「代码块」按钮，弹出代码框配置窗口，选择语言即可一键插入高亮代码块；\r\n"
            L"4. 任意复制微信/QQ/系统截图，在右侧编辑器中直接按 Ctrl+V 即可粘贴图片！"
        );
        break;
    case 5:
        m_richEditView.StreamInRTF(BuildCppArchitectureNoteRtf());
        break;
    case 6:
        m_richEditView.StreamInRTF(BuildSqlSecurityNoteRtf());
        break;
    case 7:
        m_richEditView.StreamInRTF(BuildWelcomeNoteRtf());
        break;
    default:
        // 新建或暂无示例内容的节点必须显示为空白，避免沿用上一节点内容。
        m_richEditView.SetText(L"");
        break;
    }
}

void MainWindow::OnTreeSelectionChanged(NMTREEVIEWW* pNmtv) {
    if (!pNmtv) return;

    if (!pNmtv->itemNew.hItem) {
        if (m_richEditView.GetHwnd()) {
            SaveActiveNote();
            m_richEditView.SetText(L"");
        }
        m_activeItem = nullptr;
        UpdateStatusBar(L"未选择笔记");
        return;
    }

    HTREEITEM newItem = pNmtv->itemNew.hItem;
    std::wstring title = m_treeView.GetItemText(newItem);
    UpdateStatusBar(L"当前笔记: " + title);

    // 树控件在窗口初始化时可能先发出选择通知，此时编辑器尚未创建。
    if (!m_richEditView.GetHwnd()) return;

    if (m_activeItem && m_activeItem != newItem) {
        SaveActiveNote();
    }
    LoadNoteForItem(newItem);
    m_activeItem = newItem;
}

LRESULT MainWindow::HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_SIZE: {
        int width = LOWORD(lParam);
        int height = HIWORD(lParam);
        LayoutChildren(width, height);
        return 0;
    }

    case WM_DPICHANGED: {
        const RECT* prcNewWindow = reinterpret_cast<const RECT*>(lParam);
        SetWindowPos(m_hWnd, nullptr,
            prcNewWindow->left, prcNewWindow->top,
            prcNewWindow->right - prcNewWindow->left,
            prcNewWindow->bottom - prcNewWindow->top,
            SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }

    case WM_SPLITTER_MOVED: {
        int newX = static_cast<int>(wParam);
        RECT rc;
        GetClientRect(m_hWnd, &rc);
        m_splitterPos = newX;
        LayoutChildren(rc.right - rc.left, rc.bottom - rc.top);
        return 0;
    }

    case WM_NOTIFY: {
        auto* pNmhdr = reinterpret_cast<NMHDR*>(lParam);
        if (pNmhdr && pNmhdr->idFrom == IDC_MAIN_TREEVIEW) {
            if (pNmhdr->code == TVN_SELCHANGEDW) {
                OnTreeSelectionChanged(reinterpret_cast<NMTREEVIEWW*>(lParam));
                return 0;
            } else if (pNmhdr->code == TVN_KEYDOWN) {
                auto* pTvKey = reinterpret_cast<NMTVKEYDOWN*>(lParam);
                if (pTvKey->wVKey == VK_DELETE) {
                    // 仅当焦点在左侧树控件且未在就地编辑文本时响应 Del 删除笔记
                    if (!TreeView_GetEditControl(m_treeView.GetHwnd())) {
                        SendMessageW(m_hWnd, WM_COMMAND, MAKEWPARAM(ID_FILE_DELETE_NOTE, 0), 0);
                        return 1;
                    }
                }
            } else if (pNmhdr->code == TVN_ENDLABELEDITW) {
                auto* pDispInfo = reinterpret_cast<NMTVDISPINFOW*>(lParam);
                if (pDispInfo->item.pszText && wcslen(pDispInfo->item.pszText) > 0) {
                    m_treeView.SetItemText(pDispInfo->item.hItem, pDispInfo->item.pszText);
                    UpdateStatusBar(L"当前笔记: " + std::wstring(pDispInfo->item.pszText));
                    return TRUE;
                }
                return FALSE;
            }
        }
        break;
    }

    case WM_COMMAND: {
        WORD cmdId = LOWORD(wParam);
        switch (cmdId) {
        // 富文本格式化命令
        case ID_FORMAT_BOLD:
            m_richEditView.ToggleBold();
            return 0;
        case ID_FORMAT_ITALIC:
            m_richEditView.ToggleItalic();
            return 0;
        case ID_FORMAT_UNDERLINE:
            m_richEditView.ToggleUnderline();
            return 0;
        case ID_FORMAT_STRIKE:
            m_richEditView.ToggleStrike();
            return 0;
        case ID_FORMAT_CODE_BLOCK:
            ShowInsertCodeDialog();
            return 0;
        case ID_FORMAT_BULLET_LIST:
            m_richEditView.InsertBulletList();
            return 0;
        case ID_FORMAT_NUMBER_LIST:
            m_richEditView.InsertNumberedList();
            return 0;

        // 编辑命令
        case ID_EDIT_UNDO:
            m_richEditView.Undo();
            return 0;
        case ID_EDIT_REDO:
            m_richEditView.Redo();
            return 0;
        case ID_EDIT_CUT:
            m_richEditView.Cut();
            return 0;
        case ID_EDIT_COPY:
            m_richEditView.Copy();
            return 0;
        case ID_EDIT_PASTE:
            m_richEditView.Paste();
            return 0;
        case ID_EDIT_SELECTALL:
            m_richEditView.SelectAll();
            return 0;

        // 插入当前时间戳
        case ID_INSERT_DATETIME: {
            auto now = std::chrono::system_clock::now();
            auto in_time_t = std::chrono::system_clock::to_time_t(now);
            std::tm tmNow;
            localtime_s(&tmNow, &in_time_t);
            std::wstringstream wss;
            wss << std::put_time(&tmNow, L"[%Y-%m-%d %H:%M:%S] ");
            SendMessageW(m_richEditView.GetHwnd(), EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(wss.str().c_str()));
            return 0;
        }

        // 插入图片演示提示
        case ID_INSERT_IMAGE:
            MessageBoxW(
                m_hWnd,
                L"💡 提示：Win32 RichEdit 原生支持图片剪贴板粘贴！\r\n\r\n你可以使用 Win+Shift+S 截图，然后在右侧编辑器中直接按 Ctrl+V 粘贴任意图片。",
                L"插入图片说明",
                MB_OK | MB_ICONINFORMATION
            );
            return 0;

        // 树节点增删
        case ID_FILE_NEW_NOTE: {
            HTREEITEM hCur = m_treeView.GetSelectedItem();
            HTREEITEM hParent = hCur ? TreeView_GetParent(m_treeView.GetHwnd(), hCur) : nullptr;
            HTREEITEM hNew = m_treeView.InsertNode(hParent, L"新建笔记", 100, true);
            if (hNew) m_treeView.SelectItem(hNew);
            return 0;
        }
        case ID_FILE_NEW_SUB_NOTE: {
            HTREEITEM hCur = m_treeView.GetSelectedItem();
            if (hCur) {
                HTREEITEM hNew = m_treeView.InsertNode(hCur, L"新建子笔记", 101, true);
                if (hNew) m_treeView.SelectItem(hNew);
            }
            return 0;
        }
        case ID_FILE_RENAME_NOTE: {
            HTREEITEM hCur = m_treeView.GetSelectedItem();
            if (hCur) {
                SetFocus(m_treeView.GetHwnd());
                TreeView_EditLabel(m_treeView.GetHwnd(), hCur);
            }
            return 0;
        }
        case ID_FILE_DELETE_NOTE: {
            HTREEITEM hCur = m_treeView.GetSelectedItem();
            if (hCur && MessageBoxW(m_hWnd, L"确定要删除当前选中的笔记吗？", L"确认删除", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                m_treeView.DeleteItem(hCur);
                m_noteRtfByItem.erase(hCur);
            }
            return 0;
        }

        case ID_FILE_SAVE:
            UpdateStatusBar(L"笔记本已保存 (内存/缓存)");
            if (m_hStatusBar) {
                SendMessageW(m_hStatusBar, SB_SETTEXTW, 1, reinterpret_cast<LPARAM>(L"状态: 已保存"));
            }
            return 0;

        case ID_SECURITY_ENCRYPT:
            MessageBoxW(
                m_hWnd,
                L"AnyNote 深度集成了 sqlite3mc (SQLite Multiple Ciphers)。\r\n在阶段 2 中将连接数据库全库 AES-256 加密管线！",
                L"笔记本安全加密",
                MB_OK | MB_ICONINFORMATION
            );
            return 0;

        case ID_HELP_ABOUT:
            MessageBoxW(
                m_hWnd,
                L"AnyNote v1.0.0 (Preview)\r\n\r\n基于 C++20 与原生 Win32 API 打造的高性能树状富文本笔记软件。\r\n• 编辑内核: Windows RichEdit 5.0\r\n• 数据存储: SQLite3MC (静态集成)",
                L"关于 AnyNote",
                MB_OK | MB_ICONINFORMATION
            );
            return 0;

        case ID_FILE_EXIT:
            DestroyWindow(m_hWnd);
            return 0;
        }
        break;
    }

    case WM_DESTROY:
        SaveActiveNote();
        PostQuitMessage(0);
        return 0;
    }

    return common::Window::HandleMessage(uMsg, wParam, lParam);
}

} // namespace anynote::ui
