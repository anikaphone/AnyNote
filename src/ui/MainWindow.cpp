#include "MainWindow.h"
#include "resource.h"
#include "common/StringUtils.h"
#include "common/SyntaxHighlighter.h"
#include <commctrl.h>
#include <shlwapi.h>
#include <commdlg.h>
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace anynote::ui {

namespace {

std::wstring GetDefaultNotebookPath() {
    wchar_t exePath[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    PathRemoveFileSpecW(exePath);
    return std::wstring(exePath) + L"\\notebook.anynote";
}

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

struct PasswordPromptContext {
    std::string password;
    bool ok = false;
};

INT_PTR CALLBACK PasswordPromptDialogProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_INITDIALOG:
        SetWindowLongPtrW(hDlg, DWLP_USER, lParam);
        SetFocus(GetDlgItem(hDlg, IDC_PASSWORD_INPUT));
        return FALSE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK) {
            auto* pCtx = reinterpret_cast<PasswordPromptContext*>(GetWindowLongPtrW(hDlg, DWLP_USER));
            if (pCtx) {
                HWND hEdit = GetDlgItem(hDlg, IDC_PASSWORD_INPUT);
                int len = GetWindowTextLengthW(hEdit);
                if (len > 0) {
                    std::wstring wPwd(len + 1, L'\0');
                    GetWindowTextW(hEdit, wPwd.data(), len + 1);
                    wPwd.resize(len);
                    pCtx->password = anynote::utils::WideToUtf8(wPwd);
                }
                pCtx->ok = true;
            }
            EndDialog(hDlg, IDOK);
            return TRUE;
        } else if (LOWORD(wParam) == IDCANCEL) {
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

struct SetPasswordContext {
    std::string newPassword;
    bool removePassword = false;
    bool ok = false;
};

INT_PTR CALLBACK SetPasswordDialogProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_INITDIALOG:
        SetWindowLongPtrW(hDlg, DWLP_USER, lParam);
        SetFocus(GetDlgItem(hDlg, IDC_PASSWORD_NEW));
        return FALSE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK) {
            HWND hNew = GetDlgItem(hDlg, IDC_PASSWORD_NEW);
            HWND hConfirm = GetDlgItem(hDlg, IDC_PASSWORD_CONFIRM);

            int lenNew = GetWindowTextLengthW(hNew);
            int lenConfirm = GetWindowTextLengthW(hConfirm);

            std::wstring wNew(lenNew + 1, L'\0');
            GetWindowTextW(hNew, wNew.data(), lenNew + 1);
            wNew.resize(lenNew);

            std::wstring wConfirm(lenConfirm + 1, L'\0');
            GetWindowTextW(hConfirm, wConfirm.data(), lenConfirm + 1);
            wConfirm.resize(lenConfirm);

            if (wNew != wConfirm) {
                MessageBoxW(hDlg, L"两次输入的密码不一致，请核对后重新输入！", L"密码不一致", MB_OK | MB_ICONWARNING);
                SetFocus(hConfirm);
                return TRUE;
            }

            auto* pCtx = reinterpret_cast<SetPasswordContext*>(GetWindowLongPtrW(hDlg, DWLP_USER));
            if (pCtx) {
                if (wNew.empty()) {
                    if (MessageBoxW(hDlg, L"确定要解除密码加密，将笔记本转换为明文格式存储吗？", L"确认解除加密", MB_YESNO | MB_ICONQUESTION) != IDYES) {
                        return TRUE;
                    }
                    pCtx->removePassword = true;
                    pCtx->newPassword = "";
                } else {
                    pCtx->newPassword = anynote::utils::WideToUtf8(wNew);
                    pCtx->removePassword = false;
                }
                pCtx->ok = true;
            }
            EndDialog(hDlg, IDOK);
            return TRUE;
        } else if (LOWORD(wParam) == IDCANCEL) {
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
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

    // 5. 初始化右侧富文本编辑器
    if (!m_richEditView.Initialize(
        m_hWnd,
        m_splitterPos + 5, toolbarH,
        clientW - m_splitterPos - 5, clientH - toolbarH - statusbarH,
        IDC_MAIN_RICHEDIT
    )) {
        return false;
    }

    // 6. 打开默认便携笔记本并从数据库加载节点树
    OpenNotebook(GetDefaultNotebookPath(), "");

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

void MainWindow::UpdateEncryptionStatusUI() {
    if (!m_hStatusBar) return;

    if (m_db && m_db->IsEncrypted()) {
        SendMessageW(m_hStatusBar, SB_SETTEXTW, 2, reinterpret_cast<LPARAM>(L"加密: 已加密 (AES-256)"));
    } else {
        SendMessageW(m_hStatusBar, SB_SETTEXTW, 2, reinterpret_cast<LPARAM>(L"加密: 未加密"));
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

bool MainWindow::OpenNotebook(const std::wstring& filePath, const std::string& password) {
    if (m_activeNoteId > 0) {
        SaveActiveNote();
        m_activeNoteId = 0;
    }

    auto newDb = std::make_unique<storage::Database>();
    std::string currentPwd = password;

    bool opened = newDb->Open(filePath, currentPwd);
    while (!opened && newDb->GetLastError() == "ENCRYPTED_REQUIRES_PASSWORD") {
        PasswordPromptContext ctx;
        INT_PTR res = DialogBoxParamW(
            GetModuleHandleW(nullptr),
            MAKEINTRESOURCEW(IDD_ENTER_PASSWORD),
            m_hWnd,
            PasswordPromptDialogProc,
            reinterpret_cast<LPARAM>(&ctx)
        );

        if (res != IDOK || !ctx.ok) {
            return false;
        }

        currentPwd = ctx.password;
        opened = newDb->Open(filePath, currentPwd);
        if (!opened) {
            MessageBoxW(m_hWnd, L"密码不正确，无法解密该笔记本！请重新输入。", L"密码错误", MB_OK | MB_ICONERROR);
        }
    }

    if (!opened) {
        std::wstring err = anynote::utils::Utf8ToWide(newDb->GetLastError());
        MessageBoxW(m_hWnd, (L"无法打开笔记本文件：\r\n" + err).c_str(), L"打开失败", MB_OK | MB_ICONERROR);
        return false;
    }

    auto newRepo = std::make_unique<storage::NoteRepository>(*newDb);
    if (!newRepo->InitializeSchema()) {
        MessageBoxW(m_hWnd, L"初始化笔记本表结构失败！", L"数据库错误", MB_OK | MB_ICONERROR);
        return false;
    }

    if (newRepo->GetNodeCount() == 0) {
        newRepo->CreateDefaultWelcomeNotes();
    }

    m_db = std::move(newDb);
    m_repo = std::move(newRepo);
    m_currentNotebookPath = filePath;

    // 更新窗口标题
    std::wstring fileName = filePath;
    size_t slashPos = fileName.find_last_of(L"\\/");
    if (slashPos != std::wstring::npos) {
        fileName = fileName.substr(slashPos + 1);
    }
    std::wstring title = L"AnyNote - [" + fileName + L"]";
    SetWindowTextW(m_hWnd, title.c_str());

    UpdateEncryptionStatusUI();
    PopulateTreeViewFromDb();

    return true;
}

void MainWindow::PopulateTreeViewFromDb() {
    m_treeView.ClearAll();
    m_activeNoteId = 0;
    if (!m_repo) return;

    auto nodes = m_repo->GetAllNodes();
    if (nodes.empty()) return;

    std::unordered_map<int64_t, HTREEITEM> idToItem;
    std::vector<storage::NoteNode> remaining = std::move(nodes);

    bool insertedAny = true;
    while (!remaining.empty() && insertedAny) {
        insertedAny = false;
        std::vector<storage::NoteNode> nextRemaining;
        for (auto& node : remaining) {
            if (node.parentId == 0) {
                HTREEITEM hItem = m_treeView.InsertNode(nullptr, node.title, static_cast<LPARAM>(node.id), true);
                idToItem[node.id] = hItem;
                insertedAny = true;
            } else {
                auto it = idToItem.find(node.parentId);
                if (it != idToItem.end()) {
                    HTREEITEM hItem = m_treeView.InsertNode(it->second, node.title, static_cast<LPARAM>(node.id), true);
                    idToItem[node.id] = hItem;
                    insertedAny = true;
                } else {
                    nextRemaining.push_back(std::move(node));
                }
            }
        }
        remaining = std::move(nextRemaining);
    }

    // 孤儿节点（父节点不存在）安全挂在根目录下展示
    for (const auto& orphan : remaining) {
        HTREEITEM hItem = m_treeView.InsertNode(nullptr, orphan.title, static_cast<LPARAM>(orphan.id), true);
        idToItem[orphan.id] = hItem;
    }

    // 默认选中第一个根节点
    HTREEITEM hRoot = TreeView_GetRoot(m_treeView.GetHwnd());
    if (hRoot) {
        m_treeView.SelectItem(hRoot);
    }
}

void MainWindow::SaveActiveNote() {
    if (m_activeNoteId <= 0 || !m_repo || !m_richEditView.GetHwnd()) return;

    std::string rtf = m_richEditView.StreamOutRTF();
    m_repo->UpdateNoteContent(m_activeNoteId, rtf);

    if (m_hStatusBar) {
        SendMessageW(m_hStatusBar, SB_SETTEXTW, 1, reinterpret_cast<LPARAM>(L"状态: 已保存"));
    }
}

void MainWindow::LoadNoteForId(int64_t nodeId) {
    if (nodeId <= 0 || !m_repo || !m_richEditView.GetHwnd()) return;

    std::string rtf = m_repo->GetNoteContent(nodeId);
    if (!rtf.empty()) {
        m_richEditView.StreamInRTF(rtf);
    } else {
        m_richEditView.SetText(L"");
    }
}

void MainWindow::OnTreeSelectionChanged(NMTREEVIEWW* pNmtv) {
    if (!pNmtv) return;

    if (!pNmtv->itemNew.hItem) {
        if (m_richEditView.GetHwnd()) {
            SaveActiveNote();
            m_richEditView.SetText(L"");
        }
        m_activeNoteId = 0;
        UpdateStatusBar(L"未选择笔记");
        return;
    }

    HTREEITEM newItem = pNmtv->itemNew.hItem;
    int64_t newId = static_cast<int64_t>(m_treeView.GetItemData(newItem));
    std::wstring title = m_treeView.GetItemText(newItem);
    UpdateStatusBar(L"当前笔记: " + title);

    if (!m_richEditView.GetHwnd()) return;

    if (m_activeNoteId > 0 && m_activeNoteId != newId) {
        SaveActiveNote();
    }

    m_activeNoteId = newId;
    LoadNoteForId(newId);
}

LRESULT MainWindow::HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_EDITOR_FORMAT_CHANGED:
        m_toolbar.SetSelectedHeadingIndex(m_richEditView.GetCurrentHeadingLevel());
        return 0;

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
                    int64_t nodeId = static_cast<int64_t>(m_treeView.GetItemData(pDispInfo->item.hItem));
                    if (m_repo) {
                        m_repo->UpdateNoteTitle(nodeId, pDispInfo->item.pszText);
                    }
                    m_treeView.SetItemText(pDispInfo->item.hItem, pDispInfo->item.pszText);
                    UpdateStatusBar(L"当前笔记: " + std::wstring(pDispInfo->item.pszText));
                    return TRUE;
                }
                return FALSE;
            }
        } else if (pNmhdr && pNmhdr->idFrom == IDC_MAIN_RICHEDIT) {
            if (pNmhdr->code == EN_SELCHANGE) {
                int level = m_richEditView.GetCurrentHeadingLevel();
                m_toolbar.SetSelectedHeadingIndex(level);
                return 0;
            }
        }
        break;
    }

    case WM_COMMAND: {
        WORD cmdId = LOWORD(wParam);
        switch (cmdId) {
        // 工具栏标题下拉框事件
        case IDC_TOOLBAR_HEADING_COMBO: {
            if (HIWORD(wParam) == CBN_SELCHANGE) {
                int idx = m_toolbar.GetSelectedHeadingIndex();
                m_richEditView.ApplyHeading(idx);
                SetFocus(m_richEditView.GetHwnd());
                return 0;
            }
            break;
        }

        // 标题与正文样式命令
        case ID_FORMAT_HEADING_0:
            m_richEditView.ApplyHeading(0);
            m_toolbar.SetSelectedHeadingIndex(0);
            return 0;
        case ID_FORMAT_HEADING_1:
            m_richEditView.ApplyHeading(1);
            m_toolbar.SetSelectedHeadingIndex(1);
            return 0;
        case ID_FORMAT_HEADING_2:
            m_richEditView.ApplyHeading(2);
            m_toolbar.SetSelectedHeadingIndex(2);
            return 0;
        case ID_FORMAT_HEADING_3:
            m_richEditView.ApplyHeading(3);
            m_toolbar.SetSelectedHeadingIndex(3);
            return 0;
        case ID_FORMAT_HEADING_4:
            m_richEditView.ApplyHeading(4);
            m_toolbar.SetSelectedHeadingIndex(4);
            return 0;

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
            SaveActiveNote();
            HTREEITEM hCur = m_treeView.GetSelectedItem();
            HTREEITEM hParent = hCur ? TreeView_GetParent(m_treeView.GetHwnd(), hCur) : nullptr;
            int64_t parentId = hParent ? static_cast<int64_t>(m_treeView.GetItemData(hParent)) : 0;
            int64_t newId = m_repo ? m_repo->CreateNote(parentId, L"新建笔记", -1, 0, "") : 0;

            HTREEITEM hNew = m_treeView.InsertNode(hParent, L"新建笔记", static_cast<LPARAM>(newId), true);
            if (hNew) {
                m_treeView.SelectItem(hNew);
                SetFocus(m_treeView.GetHwnd());
                TreeView_EditLabel(m_treeView.GetHwnd(), hNew);
            }
            return 0;
        }
        case ID_FILE_NEW_SUB_NOTE: {
            SaveActiveNote();
            HTREEITEM hCur = m_treeView.GetSelectedItem();
            int64_t parentId = hCur ? static_cast<int64_t>(m_treeView.GetItemData(hCur)) : 0;
            int64_t newId = m_repo ? m_repo->CreateNote(parentId, L"新建子笔记", -1, 0, "") : 0;

            HTREEITEM hNew = m_treeView.InsertNode(hCur, L"新建子笔记", static_cast<LPARAM>(newId), true);
            if (hNew) {
                m_treeView.SelectItem(hNew);
                SetFocus(m_treeView.GetHwnd());
                TreeView_EditLabel(m_treeView.GetHwnd(), hNew);
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
            if (hCur && MessageBoxW(m_hWnd, L"确定要删除当前选中的笔记及其所有子笔记吗？此操作无法撤销。", L"确认删除", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                int64_t nodeId = static_cast<int64_t>(m_treeView.GetItemData(hCur));
                if (m_activeNoteId == nodeId) {
                    m_activeNoteId = 0;
                    m_richEditView.SetText(L"");
                }
                if (m_repo) {
                    m_repo->DeleteNote(nodeId);
                }
                m_treeView.DeleteItem(hCur);
            }
            return 0;
        }

        case ID_FILE_SAVE:
            SaveActiveNote();
            UpdateStatusBar(L"笔记本已保存至数据库");
            if (m_hStatusBar) {
                SendMessageW(m_hStatusBar, SB_SETTEXTW, 1, reinterpret_cast<LPARAM>(L"状态: 已保存"));
            }
            return 0;

        case ID_FILE_NEW_NOTEBOOK: {
            wchar_t szFile[MAX_PATH] = L"MyNotebook.anynote";
            OPENFILENAMEW ofn = { sizeof(OPENFILENAMEW) };
            ofn.hwndOwner = m_hWnd;
            ofn.lpstrFilter = L"AnyNote 笔记本 (*.anynote)\0*.anynote\0所有文件 (*.*)\0*.*\0";
            ofn.lpstrFile = szFile;
            ofn.nMaxFile = MAX_PATH;
            ofn.lpstrDefExt = L"anynote";
            ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;

            if (GetSaveFileNameW(&ofn)) {
                OpenNotebook(szFile, "");
            }
            return 0;
        }

        case ID_FILE_OPEN_NOTEBOOK: {
            wchar_t szFile[MAX_PATH] = {0};
            OPENFILENAMEW ofn = { sizeof(OPENFILENAMEW) };
            ofn.hwndOwner = m_hWnd;
            ofn.lpstrFilter = L"AnyNote 笔记本 (*.anynote)\0*.anynote\0所有文件 (*.*)\0*.*\0";
            ofn.lpstrFile = szFile;
            ofn.nMaxFile = MAX_PATH;
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

            if (GetOpenFileNameW(&ofn)) {
                OpenNotebook(szFile, "");
            }
            return 0;
        }

        case ID_SECURITY_ENCRYPT: {
            if (!m_db || !m_db->IsOpen()) {
                MessageBoxW(m_hWnd, L"尚未打开任何笔记本！", L"提示", MB_OK | MB_ICONWARNING);
                return 0;
            }
            SetPasswordContext ctx;
            INT_PTR res = DialogBoxParamW(
                GetModuleHandleW(nullptr),
                MAKEINTRESOURCEW(IDD_SET_PASSWORD),
                m_hWnd,
                SetPasswordDialogProc,
                reinterpret_cast<LPARAM>(&ctx)
            );
            if (res == IDOK && ctx.ok) {
                if (m_db->SetPassword(ctx.newPassword)) {
                    UpdateEncryptionStatusUI();
                    if (ctx.removePassword) {
                        MessageBoxW(m_hWnd, L"已成功解除密码，当前笔记本为未加密明文存储。", L"成功", MB_OK | MB_ICONINFORMATION);
                    } else {
                        MessageBoxW(m_hWnd, L"密码设置成功！全库已采用 AES-256 高强度加密。\r\n下次打开该文件时需要输入此密码。", L"成功", MB_OK | MB_ICONINFORMATION);
                    }
                } else {
                    std::wstring err = anynote::utils::Utf8ToWide(m_db->GetLastError());
                    MessageBoxW(m_hWnd, (L"修改密码失败：\r\n" + err).c_str(), L"错误", MB_OK | MB_ICONERROR);
                }
            }
            return 0;
        }

        case ID_HELP_ABOUT:
            MessageBoxW(
                m_hWnd,
                L"AnyNote v1.0.0 (Preview)\r\n\r\n基于 C++20 与原生 Win32 API 打造的高性能树状富文本笔记软件。\r\n• 编辑内核: Windows RichEdit 5.0 (MSFTEDIT.DLL)\r\n• 数据存储: SQLite3MC (AES-256 全库透明加密)\r\n• 特色特性: 代码框卡片、标题阶梯层级、便携零第三方依赖",
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

    case WM_CLOSE:
        SaveActiveNote();
        DestroyWindow(m_hWnd);
        return 0;

    case WM_DESTROY:
        SaveActiveNote();
        if (m_db) {
            m_db->Close();
        }
        PostQuitMessage(0);
        return 0;
    }

    return common::Window::HandleMessage(uMsg, wParam, lParam);
}

} // namespace anynote::ui
