#include "SearchPane.h"
#include "storage/NoteRepository.h"
#include <commctrl.h>
#include <uxtheme.h>

namespace anynote::ui {

constexpr UINT IDC_SP_LABEL = 3201;
constexpr UINT IDC_SP_EDIT = 3202;
constexpr UINT IDC_SP_BTN_SEARCH = 3203;
constexpr UINT IDC_SP_CHK_CASE = 3204;
constexpr UINT IDC_SP_CHK_CONTENT = 3205;
constexpr UINT IDC_SP_STATIC_COUNT = 3206;
constexpr UINT IDC_SP_BTN_CLOSE = 3207;
constexpr UINT IDC_SP_LIST_RESULTS = 3208;

static bool s_searchPaneClassRegistered = false;

SearchPane::SearchPane() {
    m_hBgBrush = CreateSolidBrush(RGB(246, 248, 250));
}

SearchPane::~SearchPane() {
    if (m_hFont) {
        DeleteObject(m_hFont);
        m_hFont = nullptr;
    }
    if (m_hBgBrush) {
        DeleteObject(m_hBgBrush);
        m_hBgBrush = nullptr;
    }
}

void SearchPane::RegisterClassIfNeeded(HINSTANCE hInstance) {
    if (s_searchPaneClassRegistered) return;

    WNDCLASSEXW wc = {sizeof(WNDCLASSEXW)};
    wc.lpfnWndProc = [](HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) -> LRESULT {
        auto* pThis = reinterpret_cast<SearchPane*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
        if (uMsg == WM_NCCREATE) {
            auto* pCreate = reinterpret_cast<CREATESTRUCTW*>(lParam);
            pThis = reinterpret_cast<SearchPane*>(pCreate->lpCreateParams);
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
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);

    RegisterClassExW(&wc);
    s_searchPaneClassRegistered = true;
}

bool SearchPane::Initialize(HWND hParent, storage::NoteRepository* repo) {
    m_repo = repo;
    RegisterClassIfNeeded(GetModuleHandleW(nullptr));

    if (!Create(
        GetClassName(),
        L"",
        WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
        0,
        0, 0, 100, 100,
        hParent,
        nullptr,
        GetModuleHandleW(nullptr)
    )) {
        return false;
    }

    UINT dpi = GetDpiForWindow(m_hWnd);
    m_hFont = CreateFontW(
        -MulDiv(9, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
    );

    // 1. 标题标签
    m_hLabel = CreateWindowExW(
        0, L"STATIC", L"全库搜索:",
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
        0, 0, 10, 10, m_hWnd,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_SP_LABEL)),
        GetModuleHandleW(nullptr), nullptr
    );

    // 2. 输入框
    m_hEdit = CreateWindowExW(
        0, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | WS_BORDER,
        0, 0, 10, 10, m_hWnd,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_SP_EDIT)),
        GetModuleHandleW(nullptr), nullptr
    );
    SetWindowSubclass(m_hEdit, EditSubclassProc, 1, reinterpret_cast<DWORD_PTR>(this));
    SendMessageW(m_hEdit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELONG(6, 6));

    // 3. 搜索按钮
    m_hBtnSearch = CreateWindowExW(
        0, L"BUTTON", L"搜索",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
        0, 0, 10, 10, m_hWnd,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_SP_BTN_SEARCH)),
        GetModuleHandleW(nullptr), nullptr
    );

    // 4. 区分大小写复选框
    m_hChkCase = CreateWindowExW(
        0, L"BUTTON", L"区分大小写",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        0, 0, 10, 10, m_hWnd,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_SP_CHK_CASE)),
        GetModuleHandleW(nullptr), nullptr
    );

    // 5. 搜索正文复选框 (默认勾选)
    m_hChkContent = CreateWindowExW(
        0, L"BUTTON", L"包含正文",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        0, 0, 10, 10, m_hWnd,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_SP_CHK_CONTENT)),
        GetModuleHandleW(nullptr), nullptr
    );
    SendMessageW(m_hChkContent, BM_SETCHECK, BST_CHECKED, 0);

    // 6. 统计标签
    m_hStaticCount = CreateWindowExW(
        0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
        0, 0, 10, 10, m_hWnd,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_SP_STATIC_COUNT)),
        GetModuleHandleW(nullptr), nullptr
    );

    // 7. 关闭按钮
    m_hBtnClose = CreateWindowExW(
        0, L"BUTTON", L"✕",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        0, 0, 10, 10, m_hWnd,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_SP_BTN_CLOSE)),
        GetModuleHandleW(nullptr), nullptr
    );

    // 8. 结果列表 ListView (移除 WS_EX_CLIENTEDGE，移除 LVS_EX_GRIDLINES)
    m_hListResults = CreateWindowExW(
        0, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        0, 0, 10, 10, m_hWnd,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_SP_LIST_RESULTS)),
        GetModuleHandleW(nullptr), nullptr
    );

    SetWindowTheme(m_hListResults, L"Explorer", nullptr);
    ListView_SetExtendedListViewStyle(m_hListResults, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    ListView_SetBkColor(m_hListResults, RGB(255, 255, 255));
    ListView_SetTextBkColor(m_hListResults, RGB(255, 255, 255));
    ListView_SetTextColor(m_hListResults, RGB(33, 37, 41));

    // 初始化列
    LVCOLUMNW lvc = {};
    lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;

    lvc.pszText = const_cast<LPWSTR>(L"笔记标题");
    lvc.cx = MulDiv(180, dpi, 96);
    ListView_InsertColumn(m_hListResults, 0, &lvc);

    lvc.pszText = const_cast<LPWSTR>(L"匹配位置");
    lvc.cx = MulDiv(80, dpi, 96);
    ListView_InsertColumn(m_hListResults, 1, &lvc);

    lvc.pszText = const_cast<LPWSTR>(L"上下文摘要");
    lvc.cx = MulDiv(450, dpi, 96);
    ListView_InsertColumn(m_hListResults, 2, &lvc);

    // 应用字体
    HWND children[] = { m_hLabel, m_hEdit, m_hBtnSearch, m_hChkCase, m_hChkContent, m_hStaticCount, m_hBtnClose, m_hListResults };
    for (HWND hChild : children) {
        SendMessageW(hChild, WM_SETFONT, reinterpret_cast<WPARAM>(m_hFont), TRUE);
    }

    return true;
}

void SearchPane::LayoutControls(int width, int height) {
    if (width <= 0 || height <= 0) return;

    UINT dpi = GetDpiForWindow(m_hWnd);
    int pad = MulDiv(6, dpi, 96);
    int barH = MulDiv(28, dpi, 96);

    int curX = pad;
    int labelW = MulDiv(60, dpi, 96);
    MoveWindow(m_hLabel, curX, pad, labelW, barH, TRUE);
    curX += labelW + MulDiv(4, dpi, 96);

    int editW = MulDiv(220, dpi, 96);
    MoveWindow(m_hEdit, curX, pad, editW, barH, TRUE);
    curX += editW + MulDiv(5, dpi, 96);

    int btnW = MulDiv(60, dpi, 96);
    MoveWindow(m_hBtnSearch, curX, pad, btnW, barH, TRUE);
    curX += btnW + MulDiv(8, dpi, 96);

    int chkW = MulDiv(85, dpi, 96);
    MoveWindow(m_hChkCase, curX, pad, chkW, barH, TRUE);
    curX += chkW + MulDiv(4, dpi, 96);

    int chkContentW = MulDiv(75, dpi, 96);
    MoveWindow(m_hChkContent, curX, pad, chkContentW, barH, TRUE);
    curX += chkContentW + MulDiv(10, dpi, 96);

    int closeBtnW = MulDiv(26, dpi, 96);
    int countW = std::max(50, width - curX - closeBtnW - pad * 2);
    MoveWindow(m_hStaticCount, curX, pad, countW, barH, TRUE);

    MoveWindow(m_hBtnClose, width - closeBtnW - pad, pad, closeBtnW, barH, TRUE);

    // 列表区域铺满剩余空间
    int listY = barH + pad * 2;
    int listH = std::max(10, height - listY - pad);
    MoveWindow(m_hListResults, pad, listY, width - pad * 2, listH, TRUE);
}

void SearchPane::ShowPane(bool show) {
    m_isVisible = show;
    ShowWindow(m_hWnd, show ? SW_SHOW : SW_HIDE);
    if (show) {
        FocusSearchBox();
    }
}

void SearchPane::FocusSearchBox() {
    if (m_hEdit) {
        SetFocus(m_hEdit);
        SendMessageW(m_hEdit, EM_SETSEL, 0, -1);
    }
}

void SearchPane::PerformSearch() {
    int len = GetWindowTextLengthW(m_hEdit);
    if (len <= 0) {
        SetWindowTextW(m_hStaticCount, L"请输入搜索关键字");
        ListView_DeleteAllItems(m_hListResults);
        m_results.clear();
        return;
    }

    std::wstring text(len + 1, L'\0');
    GetWindowTextW(m_hEdit, text.data(), len + 1);
    text.resize(len);

    bool matchCase = (SendMessageW(m_hChkCase, BM_GETCHECK, 0, 0) == BST_CHECKED);
    bool matchContent = (SendMessageW(m_hChkContent, BM_GETCHECK, 0, 0) == BST_CHECKED);

    ListView_DeleteAllItems(m_hListResults);
    m_results.clear();

    if (m_repo) {
        m_results = m_repo->SearchNotes(text, matchCase, matchContent);
    }

    for (size_t i = 0; i < m_results.size(); ++i) {
        LVITEMW item = {};
        item.mask = LVIF_TEXT | LVIF_PARAM;
        item.iItem = static_cast<int>(i);
        item.iSubItem = 0;
        item.pszText = const_cast<LPWSTR>(m_results[i].title.c_str());
        item.lParam = static_cast<LPARAM>(i);
        ListView_InsertItem(m_hListResults, &item);

        std::wstring matchType = m_results[i].matchInTitle ? L"标题" : L"正文";
        ListView_SetItemText(m_hListResults, static_cast<int>(i), 1, const_cast<LPWSTR>(matchType.c_str()));
        ListView_SetItemText(m_hListResults, static_cast<int>(i), 2, const_cast<LPWSTR>(m_results[i].snippet.c_str()));
    }

    std::wstring countStr = L"共找到 " + std::to_wstring(m_results.size()) + L" 条结果";
    SetWindowTextW(m_hStaticCount, countStr.c_str());
}

std::wstring SearchPane::GetSearchKeyword() const {
    if (!m_hEdit) return L"";
    int len = GetWindowTextLengthW(m_hEdit);
    if (len <= 0) return L"";
    std::wstring text(len + 1, L'\0');
    GetWindowTextW(m_hEdit, text.data(), len + 1);
    text.resize(len);
    return text;
}

bool SearchPane::IsMatchCase() const {
    if (!m_hChkCase) return false;
    return SendMessageW(m_hChkCase, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

LRESULT CALLBACK SearchPane::EditSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    auto* pThis = reinterpret_cast<SearchPane*>(dwRefData);
    if (uMsg == WM_KEYDOWN) {
        if (wParam == VK_RETURN) {
            if (pThis) {
                pThis->PerformSearch();
            }
            return 0;
        } else if (wParam == VK_ESCAPE) {
            if (pThis) {
                if (pThis->m_onClose) {
                    pThis->m_onClose();
                } else {
                    pThis->ShowPane(false);
                }
            }
            return 0;
        }
    }
    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

LRESULT SearchPane::HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_SIZE: {
        int width = LOWORD(lParam);
        int height = HIWORD(lParam);
        LayoutControls(width, height);
        return 0;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(m_hWnd, &ps);
        RECT rc;
        GetClientRect(m_hWnd, &rc);
        FillRect(hdc, &rc, m_hBgBrush);

        // 顶部 1px 精细分界线，衔接上方编辑器
        HBRUSH hLine = CreateSolidBrush(RGB(226, 230, 234));
        RECT lineRc = { rc.left, rc.top, rc.right, rc.top + 1 };
        FillRect(hdc, &lineRc, hLine);
        DeleteObject(hLine);

        EndPaint(m_hWnd, &ps);
        return 0;
    }

    case WM_CTLCOLORSTATIC: {
        HDC hdcStatic = reinterpret_cast<HDC>(wParam);
        SetBkMode(hdcStatic, TRANSPARENT);
        SetTextColor(hdcStatic, RGB(73, 80, 87));
        return reinterpret_cast<LRESULT>(m_hBgBrush);
    }

    case WM_COMMAND: {
        WORD id = LOWORD(wParam);
        if (id == IDC_SP_BTN_SEARCH) {
            PerformSearch();
            return 0;
        } else if (id == IDC_SP_BTN_CLOSE) {
            if (m_onClose) {
                m_onClose();
            } else {
                ShowPane(false);
            }
            return 0;
        }
        break;
    }

    case WM_NOTIFY: {
        auto* pNmhdr = reinterpret_cast<NMHDR*>(lParam);
        if (pNmhdr && pNmhdr->idFrom == IDC_SP_LIST_RESULTS) {
            if (pNmhdr->code == NM_DBLCLK || pNmhdr->code == NM_RETURN) {
                int sel = ListView_GetNextItem(m_hListResults, -1, LVNI_SELECTED);
                if (sel >= 0 && sel < static_cast<int>(m_results.size())) {
                    if (m_onResultSelected) {
                        m_onResultSelected(m_results[sel]);
                    }
                }
                return 0;
            }
        }
        break;
    }
    }

    return common::Window::HandleMessage(uMsg, wParam, lParam);
}

} // namespace anynote::ui
