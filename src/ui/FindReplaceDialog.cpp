#include "FindReplaceDialog.h"
#include "RichEditView.h"
#include <commctrl.h>
#include <algorithm>

namespace anynote::ui {

constexpr UINT IDC_FRD_TAB = 4001;
constexpr UINT IDC_FRD_LABEL_FIND = 4002;
constexpr UINT IDC_FRD_EDIT_FIND = 4003;
constexpr UINT IDC_FRD_LABEL_REPLACE = 4004;
constexpr UINT IDC_FRD_EDIT_REPLACE = 4005;
constexpr UINT IDC_FRD_CHK_CASE = 4006;
constexpr UINT IDC_FRD_CHK_WORD = 4007;
constexpr UINT IDC_FRD_CHK_WRAP = 4008;
constexpr UINT IDC_FRD_GROUP_DIR = 4009;
constexpr UINT IDC_FRD_RADIO_UP = 4010;
constexpr UINT IDC_FRD_RADIO_DOWN = 4011;
constexpr UINT IDC_FRD_BTN_FIND_NEXT = 4012;
constexpr UINT IDC_FRD_BTN_FIND_PREV = 4013;
constexpr UINT IDC_FRD_BTN_COUNT = 4014;
constexpr UINT IDC_FRD_BTN_REPLACE = 4015;
constexpr UINT IDC_FRD_BTN_REPLACE_ALL = 4016;
constexpr UINT IDC_FRD_BTN_MARK_ALL = 4017;
constexpr UINT IDC_FRD_BTN_CLEAR_MARKS = 4018;
constexpr UINT IDC_FRD_BTN_CLOSE = 4019;
constexpr UINT IDC_FRD_STATIC_STATUS = 4020;

static bool s_findDialogClassRegistered = false;

FindReplaceDialog::FindReplaceDialog(RichEditView& richEditView)
    : m_richEditView(richEditView) {
}

FindReplaceDialog::~FindReplaceDialog() {
    if (m_hFont) {
        DeleteObject(m_hFont);
        m_hFont = nullptr;
    }
    if (m_hBoldFont) {
        DeleteObject(m_hBoldFont);
        m_hBoldFont = nullptr;
    }
    if (m_hBgBrush) {
        DeleteObject(m_hBgBrush);
        m_hBgBrush = nullptr;
    }
}

void FindReplaceDialog::RegisterClassIfNeeded(HINSTANCE hInstance) {
    if (s_findDialogClassRegistered) return;

    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
    wc.lpfnWndProc = [](HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) -> LRESULT {
        auto* pThis = reinterpret_cast<FindReplaceDialog*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
        if (uMsg == WM_NCCREATE) {
            auto* pCreate = reinterpret_cast<CREATESTRUCTW*>(lParam);
            pThis = reinterpret_cast<FindReplaceDialog*>(pCreate->lpCreateParams);
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
    s_findDialogClassRegistered = true;
}

bool FindReplaceDialog::Initialize(HWND hParent) {
    RegisterClassIfNeeded(GetModuleHandleW(nullptr));

    UINT dpi = GetDpiForWindow(hParent);
    int dlgW = MulDiv(475, dpi, 96);
    int dlgH = MulDiv(255, dpi, 96);

    DWORD dwStyle = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
    DWORD dwExStyle = WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT;

    RECT rcParent;
    GetWindowRect(hParent, &rcParent);
    int initX = rcParent.left + (rcParent.right - rcParent.left - dlgW) / 2 + MulDiv(60, dpi, 96);
    int initY = rcParent.top + (rcParent.bottom - rcParent.top - dlgH) / 3;

    if (!Create(
        GetClassName(),
        L"查找",
        dwStyle,
        dwExStyle,
        initX, initY, dlgW, dlgH,
        hParent,
        nullptr,
        GetModuleHandleW(nullptr)
    )) {
        return false;
    }

    m_hFont = CreateFontW(
        -MulDiv(9, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
    );

    m_hBoldFont = CreateFontW(
        -MulDiv(9, dpi, 72), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
    );

    m_hBgBrush = (HBRUSH)(COLOR_BTNFACE + 1);

    // 1. 顶部 TabControl 选项卡
    m_hTab = CreateWindowExW(
        0, WC_TABCONTROL, L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | TCS_TABS,
        0, 0, 10, 10, m_hWnd,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_FRD_TAB)),
        GetModuleHandleW(nullptr), nullptr
    );

    TCITEMW tie = { TCIF_TEXT };
    wchar_t tab1[] = L"查找";
    tie.pszText = tab1;
    TabCtrl_InsertItem(m_hTab, 0, &tie);
    wchar_t tab2[] = L"替换";
    tie.pszText = tab2;
    TabCtrl_InsertItem(m_hTab, 1, &tie);
    wchar_t tab3[] = L"标记";
    tie.pszText = tab3;
    TabCtrl_InsertItem(m_hTab, 2, &tie);

    // 2. 查找目标
    m_hLabelFind = CreateWindowExW(
        0, L"STATIC", L"查找目标(&F):",
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
        0, 0, 10, 10, m_hWnd,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_FRD_LABEL_FIND)),
        GetModuleHandleW(nullptr), nullptr
    );

    m_hEditFind = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        0, 0, 10, 10, m_hWnd,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_FRD_EDIT_FIND)),
        GetModuleHandleW(nullptr), nullptr
    );
    SetWindowSubclass(m_hEditFind, EditSubclassProc, 1, reinterpret_cast<DWORD_PTR>(this));

    // 3. 替换为 (替换页专属)
    m_hLabelReplace = CreateWindowExW(
        0, L"STATIC", L"替换为(&E):",
        WS_CHILD | SS_LEFT | SS_CENTERIMAGE,
        0, 0, 10, 10, m_hWnd,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_FRD_LABEL_REPLACE)),
        GetModuleHandleW(nullptr), nullptr
    );

    m_hEditReplace = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL,
        0, 0, 10, 10, m_hWnd,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_FRD_EDIT_REPLACE)),
        GetModuleHandleW(nullptr), nullptr
    );
    SetWindowSubclass(m_hEditReplace, EditSubclassProc, 2, reinterpret_cast<DWORD_PTR>(this));

    // 4. 匹配选项复选框
    m_hChkMatchCase = CreateWindowExW(
        0, L"BUTTON", L"区分大小写(&C)",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        0, 0, 10, 10, m_hWnd,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_FRD_CHK_CASE)),
        GetModuleHandleW(nullptr), nullptr
    );

    m_hChkWholeWord = CreateWindowExW(
        0, L"BUTTON", L"全词匹配(&W)",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        0, 0, 10, 10, m_hWnd,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_FRD_CHK_WORD)),
        GetModuleHandleW(nullptr), nullptr
    );

    m_hChkWrapAround = CreateWindowExW(
        0, L"BUTTON", L"循环查找(&R)",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        0, 0, 10, 10, m_hWnd,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_FRD_CHK_WRAP)),
        GetModuleHandleW(nullptr), nullptr
    );
    SendMessageW(m_hChkWrapAround, BM_SETCHECK, BST_CHECKED, 0); // 默认开启循环查找

    // 5. 查找方向单选框分组 (查找页专属)
    m_hGroupDirection = CreateWindowExW(
        0, L"BUTTON", L"方向",
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        0, 0, 10, 10, m_hWnd,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_FRD_GROUP_DIR)),
        GetModuleHandleW(nullptr), nullptr
    );

    m_hRadioUp = CreateWindowExW(
        0, L"BUTTON", L"向上(&U)",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON,
        0, 0, 10, 10, m_hWnd,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_FRD_RADIO_UP)),
        GetModuleHandleW(nullptr), nullptr
    );

    m_hRadioDown = CreateWindowExW(
        0, L"BUTTON", L"向下(&D)",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON,
        0, 0, 10, 10, m_hWnd,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_FRD_RADIO_DOWN)),
        GetModuleHandleW(nullptr), nullptr
    );
    SendMessageW(m_hRadioDown, BM_SETCHECK, BST_CHECKED, 0); // 默认向下查找

    // 6. 状态提示文本
    m_hStatus = CreateWindowExW(
        0, L"STATIC", L"就绪",
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
        0, 0, 10, 10, m_hWnd,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_FRD_STATIC_STATUS)),
        GetModuleHandleW(nullptr), nullptr
    );

    // 7. 动作按钮
    m_hBtnFindNext = CreateWindowExW(
        0, L"BUTTON", L"查找下一个(&N)",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
        0, 0, 10, 10, m_hWnd,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_FRD_BTN_FIND_NEXT)),
        GetModuleHandleW(nullptr), nullptr
    );

    m_hBtnFindPrev = CreateWindowExW(
        0, L"BUTTON", L"查找上一个(&P)",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        0, 0, 10, 10, m_hWnd,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_FRD_BTN_FIND_PREV)),
        GetModuleHandleW(nullptr), nullptr
    );

    m_hBtnCount = CreateWindowExW(
        0, L"BUTTON", L"计数(&O)",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        0, 0, 10, 10, m_hWnd,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_FRD_BTN_COUNT)),
        GetModuleHandleW(nullptr), nullptr
    );

    m_hBtnReplace = CreateWindowExW(
        0, L"BUTTON", L"替换(&R)",
        WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
        0, 0, 10, 10, m_hWnd,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_FRD_BTN_REPLACE)),
        GetModuleHandleW(nullptr), nullptr
    );

    m_hBtnReplaceAll = CreateWindowExW(
        0, L"BUTTON", L"全部替换(&A)",
        WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
        0, 0, 10, 10, m_hWnd,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_FRD_BTN_REPLACE_ALL)),
        GetModuleHandleW(nullptr), nullptr
    );

    m_hBtnMarkAll = CreateWindowExW(
        0, L"BUTTON", L"全部标记(&M)",
        WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
        0, 0, 10, 10, m_hWnd,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_FRD_BTN_MARK_ALL)),
        GetModuleHandleW(nullptr), nullptr
    );

    m_hBtnClearMarks = CreateWindowExW(
        0, L"BUTTON", L"清除所有标记(&L)",
        WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
        0, 0, 10, 10, m_hWnd,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_FRD_BTN_CLEAR_MARKS)),
        GetModuleHandleW(nullptr), nullptr
    );

    m_hBtnClose = CreateWindowExW(
        0, L"BUTTON", L"关闭(&X)",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        0, 0, 10, 10, m_hWnd,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_FRD_BTN_CLOSE)),
        GetModuleHandleW(nullptr), nullptr
    );

    // 设置字体
    HWND allControls[] = {
        m_hTab, m_hLabelFind, m_hEditFind, m_hLabelReplace, m_hEditReplace,
        m_hChkMatchCase, m_hChkWholeWord, m_hChkWrapAround,
        m_hGroupDirection, m_hRadioUp, m_hRadioDown, m_hStatus,
        m_hBtnFindNext, m_hBtnFindPrev, m_hBtnCount,
        m_hBtnReplace, m_hBtnReplaceAll,
        m_hBtnMarkAll, m_hBtnClearMarks, m_hBtnClose
    };
    for (HWND hCtl : allControls) {
        if (hCtl) {
            SendMessageW(hCtl, WM_SETFONT, reinterpret_cast<WPARAM>(m_hFont), TRUE);
        }
    }

    LayoutControls();
    SwitchTab(FindTabMode::Find);

    return true;
}

void FindReplaceDialog::LayoutControls() {
    UINT dpi = GetDpiForWindow(m_hWnd);

    RECT rcClient;
    GetClientRect(m_hWnd, &rcClient);
    int clientW = rcClient.right - rcClient.left;
    int clientH = rcClient.bottom - rcClient.top;

    int padX = MulDiv(10, dpi, 96);
    int tabH = MulDiv(28, dpi, 96);
    MoveWindow(m_hTab, padX, MulDiv(4, dpi, 96), clientW - padX * 2, tabH, TRUE);

    // 左侧输入与选项区
    int labelW = MulDiv(75, dpi, 96);
    int ctlH = MulDiv(23, dpi, 96);
    int editW = MulDiv(230, dpi, 96);

    int yFind = MulDiv(38, dpi, 96);
    MoveWindow(m_hLabelFind, MulDiv(14, dpi, 96), yFind, labelW, ctlH, TRUE);
    MoveWindow(m_hEditFind, MulDiv(90, dpi, 96), yFind, editW, ctlH, TRUE);

    int yReplace = MulDiv(66, dpi, 96);
    MoveWindow(m_hLabelReplace, MulDiv(14, dpi, 96), yReplace, labelW, ctlH, TRUE);
    MoveWindow(m_hEditReplace, MulDiv(90, dpi, 96), yReplace, editW, ctlH, TRUE);

    // 复选框选项
    int optY = MulDiv(96, dpi, 96);
    int chkW = MulDiv(130, dpi, 96);
    int chkH = MulDiv(20, dpi, 96);
    MoveWindow(m_hChkMatchCase, MulDiv(14, dpi, 96), optY, chkW, chkH, TRUE);
    MoveWindow(m_hChkWholeWord, MulDiv(14, dpi, 96), optY + MulDiv(22, dpi, 96), chkW, chkH, TRUE);
    MoveWindow(m_hChkWrapAround, MulDiv(14, dpi, 96), optY + MulDiv(44, dpi, 96), chkW, chkH, TRUE);

    // 查找方向单选框分组
    int grpX = MulDiv(170, dpi, 96);
    int grpW = MulDiv(150, dpi, 96);
    int grpH = MulDiv(64, dpi, 96);
    MoveWindow(m_hGroupDirection, grpX, optY - MulDiv(4, dpi, 96), grpW, grpH, TRUE);
    MoveWindow(m_hRadioUp, grpX + MulDiv(14, dpi, 96), optY + MulDiv(14, dpi, 96), MulDiv(110, dpi, 96), chkH, TRUE);
    MoveWindow(m_hRadioDown, grpX + MulDiv(14, dpi, 96), optY + MulDiv(36, dpi, 96), MulDiv(110, dpi, 96), chkH, TRUE);

    // 状态栏
    int statusY = clientH - MulDiv(26, dpi, 96);
    MoveWindow(m_hStatus, MulDiv(14, dpi, 96), statusY, MulDiv(306, dpi, 96), MulDiv(22, dpi, 96), TRUE);

    // 右侧按钮列
    int btnW = MulDiv(116, dpi, 96);
    int btnH = MulDiv(25, dpi, 96);
    int btnX = clientW - btnW - MulDiv(12, dpi, 96);

    int btnY1 = MulDiv(37, dpi, 96);
    int btnY2 = MulDiv(67, dpi, 96);
    int btnY3 = MulDiv(97, dpi, 96);
    int btnY4 = MulDiv(127, dpi, 96);
    int btnYClose = clientH - btnH - MulDiv(8, dpi, 96);

    MoveWindow(m_hBtnFindNext, btnX, btnY1, btnW, btnH, TRUE);
    MoveWindow(m_hBtnFindPrev, btnX, btnY2, btnW, btnH, TRUE);
    MoveWindow(m_hBtnCount, btnX, btnY3, btnW, btnH, TRUE);

    MoveWindow(m_hBtnReplace, btnX, btnY2, btnW, btnH, TRUE);
    MoveWindow(m_hBtnReplaceAll, btnX, btnY3, btnW, btnH, TRUE);

    MoveWindow(m_hBtnMarkAll, btnX, btnY2, btnW, btnH, TRUE);
    MoveWindow(m_hBtnClearMarks, btnX, btnY3, btnW, btnH, TRUE);

    MoveWindow(m_hBtnClose, btnX, btnYClose, btnW, btnH, TRUE);
}

void FindReplaceDialog::SwitchTab(FindTabMode mode) {
    m_currentTab = mode;
    TabCtrl_SetCurSel(m_hTab, static_cast<int>(mode));

    // 根据 Tab 模式显隐控件
    bool isFind = (mode == FindTabMode::Find);
    bool isReplace = (mode == FindTabMode::Replace);
    bool isMark = (mode == FindTabMode::Mark);

    ShowWindow(m_hLabelReplace, isReplace ? SW_SHOW : SW_HIDE);
    ShowWindow(m_hEditReplace, isReplace ? SW_SHOW : SW_HIDE);

    ShowWindow(m_hGroupDirection, isFind ? SW_SHOW : SW_HIDE);
    ShowWindow(m_hRadioUp, isFind ? SW_SHOW : SW_HIDE);
    ShowWindow(m_hRadioDown, isFind ? SW_SHOW : SW_HIDE);

    ShowWindow(m_hBtnFindPrev, isFind ? SW_SHOW : SW_HIDE);
    ShowWindow(m_hBtnCount, isFind ? SW_SHOW : SW_HIDE);

    ShowWindow(m_hBtnReplace, isReplace ? SW_SHOW : SW_HIDE);
    ShowWindow(m_hBtnReplaceAll, isReplace ? SW_SHOW : SW_HIDE);

    ShowWindow(m_hBtnMarkAll, isMark ? SW_SHOW : SW_HIDE);
    ShowWindow(m_hBtnClearMarks, isMark ? SW_SHOW : SW_HIDE);

    switch (mode) {
    case FindTabMode::Find:
        SetWindowTextW(m_hWnd, L"查找");
        SetWindowTextW(m_hBtnFindNext, L"查找下一个(&N)");
        break;
    case FindTabMode::Replace:
        SetWindowTextW(m_hWnd, L"替换");
        SetWindowTextW(m_hBtnFindNext, L"查找下一个(&N)");
        break;
    case FindTabMode::Mark:
        SetWindowTextW(m_hWnd, L"标记");
        SetWindowTextW(m_hBtnFindNext, L"查找下一个(&N)");
        break;
    }

    SetStatusText(L"就绪");
}

void FindReplaceDialog::ShowTab(FindTabMode mode) {
    if (!m_hWnd) return;

    SwitchTab(mode);

    // 尝试拉取当前编辑器选中的单词自动填入
    std::wstring sel = m_richEditView.GetSelectedText();
    if (!sel.empty() && sel.size() < 80 && sel.find(L'\r') == std::wstring::npos && sel.find(L'\n') == std::wstring::npos) {
        SetWindowTextW(m_hEditFind, sel.c_str());
    }

    if (!IsWindowVisible(m_hWnd)) {
        ShowWindow(m_hWnd, SW_SHOW);
    }
    SetForegroundWindow(m_hWnd);

    SendMessageW(m_hEditFind, EM_SETSEL, 0, -1);
    SetFocus(m_hEditFind);
}

void FindReplaceDialog::SetStatusText(const std::wstring& text) {
    if (m_hStatus) {
        SetWindowTextW(m_hStatus, text.c_str());
    }
}

std::wstring FindReplaceDialog::GetFindText() const {
    int len = GetWindowTextLengthW(m_hEditFind);
    if (len <= 0) return {};
    std::wstring buf(len + 1, L'\0');
    int copied = GetWindowTextW(m_hEditFind, buf.data(), len + 1);
    buf.resize(copied);
    return buf;
}

std::wstring FindReplaceDialog::GetReplaceText() const {
    int len = GetWindowTextLengthW(m_hEditReplace);
    if (len <= 0) return {};
    std::wstring buf(len + 1, L'\0');
    int copied = GetWindowTextW(m_hEditReplace, buf.data(), len + 1);
    buf.resize(copied);
    return buf;
}

void FindReplaceDialog::FindNext(bool forward) {
    std::wstring text = GetFindText();
    if (text.empty()) {
        SetStatusText(L"请输入查找目标");
        return;
    }

    bool matchCase = (SendMessageW(m_hChkMatchCase, BM_GETCHECK, 0, 0) == BST_CHECKED);
    bool matchWord = (SendMessageW(m_hChkWholeWord, BM_GETCHECK, 0, 0) == BST_CHECKED);
    bool wrapAround = (SendMessageW(m_hChkWrapAround, BM_GETCHECK, 0, 0) == BST_CHECKED);

    if (m_currentTab == FindTabMode::Find) {
        bool isUp = (SendMessageW(m_hRadioUp, BM_GETCHECK, 0, 0) == BST_CHECKED);
        forward = !isUp;
    }

    bool found = m_richEditView.FindAndSelect(text, forward, matchCase, matchWord, wrapAround);
    if (found) {
        SetStatusText(L"找到目标文本");
    } else {
        SetStatusText(L"找不到目标文本: \"" + text + L"\"");
    }
}

void FindReplaceDialog::CountMatches() {
    std::wstring text = GetFindText();
    if (text.empty()) {
        SetStatusText(L"请输入查找目标");
        return;
    }

    bool matchCase = (SendMessageW(m_hChkMatchCase, BM_GETCHECK, 0, 0) == BST_CHECKED);
    bool matchWord = (SendMessageW(m_hChkWholeWord, BM_GETCHECK, 0, 0) == BST_CHECKED);

    int count = m_richEditView.CountMatches(text, matchCase, matchWord);
    if (count == 0) {
        SetStatusText(L"计数：无匹配项");
    } else {
        SetStatusText(L"计数：找到 " + std::to_wstring(count) + L" 处匹配项");
    }
}

void FindReplaceDialog::Replace() {
    std::wstring findText = GetFindText();
    if (findText.empty()) {
        SetStatusText(L"请输入查找目标");
        return;
    }

    std::wstring replaceText = GetReplaceText();
    bool matchCase = (SendMessageW(m_hChkMatchCase, BM_GETCHECK, 0, 0) == BST_CHECKED);
    bool matchWord = (SendMessageW(m_hChkWholeWord, BM_GETCHECK, 0, 0) == BST_CHECKED);
    bool wrapAround = (SendMessageW(m_hChkWrapAround, BM_GETCHECK, 0, 0) == BST_CHECKED);

    bool replaced = m_richEditView.ReplaceCurrent(findText, replaceText, true, matchCase, matchWord, wrapAround);
    if (replaced) {
        SetStatusText(L"替换成功并已移动到下一处");
    } else {
        SetStatusText(L"未找到更多可替换项");
    }
}

void FindReplaceDialog::ReplaceAll() {
    std::wstring findText = GetFindText();
    if (findText.empty()) {
        SetStatusText(L"请输入查找目标");
        return;
    }

    std::wstring replaceText = GetReplaceText();
    bool matchCase = (SendMessageW(m_hChkMatchCase, BM_GETCHECK, 0, 0) == BST_CHECKED);
    bool matchWord = (SendMessageW(m_hChkWholeWord, BM_GETCHECK, 0, 0) == BST_CHECKED);

    int count = m_richEditView.ReplaceAll(findText, replaceText, matchCase, matchWord);
    if (count == 0) {
        SetStatusText(L"未找到任何匹配项以执行替换");
    } else {
        SetStatusText(L"全部替换完成：共替换 " + std::to_wstring(count) + L" 处");
    }
}

void FindReplaceDialog::MarkAll() {
    std::wstring text = GetFindText();
    if (text.empty()) {
        SetStatusText(L"请输入查找目标");
        return;
    }

    bool matchCase = (SendMessageW(m_hChkMatchCase, BM_GETCHECK, 0, 0) == BST_CHECKED);
    bool matchWord = (SendMessageW(m_hChkWholeWord, BM_GETCHECK, 0, 0) == BST_CHECKED);

    int count = m_richEditView.MarkAll(text, matchCase, matchWord);
    if (count == 0) {
        SetStatusText(L"标记：无匹配项");
    } else {
        SetStatusText(L"已高亮标记 " + std::to_wstring(count) + L" 处匹配项");
    }
}

void FindReplaceDialog::ClearMarks() {
    m_richEditView.ClearMarks();
    SetStatusText(L"已清除所有高亮标记");
}

LRESULT CALLBACK FindReplaceDialog::EditSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    auto* pThis = reinterpret_cast<FindReplaceDialog*>(dwRefData);
    if (uMsg == WM_KEYDOWN) {
        if (wParam == VK_RETURN) {
            if (pThis) {
                if (pThis->m_currentTab == FindTabMode::Find) {
                    if (GetKeyState(VK_SHIFT) & 0x8000) {
                        pThis->FindNext(false);
                    } else {
                        pThis->FindNext(true);
                    }
                } else if (pThis->m_currentTab == FindTabMode::Replace) {
                    pThis->Replace();
                } else if (pThis->m_currentTab == FindTabMode::Mark) {
                    pThis->MarkAll();
                }
            }
            return 0;
        } else if (wParam == VK_ESCAPE) {
            if (pThis) {
                ShowWindow(pThis->m_hWnd, SW_HIDE);
                SetFocus(pThis->m_richEditView.GetHwnd());
            }
            return 0;
        }
    }
    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

LRESULT FindReplaceDialog::HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_MOVE: {
        RECT rc;
        GetWindowRect(m_hWnd, &rc);
        m_lastPos.x = rc.left;
        m_lastPos.y = rc.top;
        m_hasLastPos = true;
        break;
    }

    case WM_SIZE:
        LayoutControls();
        return 0;

    case WM_NOTIFY: {
        auto* pNmhdr = reinterpret_cast<NMHDR*>(lParam);
        if (pNmhdr && pNmhdr->idFrom == IDC_FRD_TAB && pNmhdr->code == TCN_SELCHANGE) {
            int sel = TabCtrl_GetCurSel(m_hTab);
            SwitchTab(static_cast<FindTabMode>(sel));
            SendMessageW(m_hEditFind, EM_SETSEL, 0, -1);
            SetFocus(m_hEditFind);
            return 0;
        }
        break;
    }

    case WM_CTLCOLORSTATIC: {
        HDC hdcStatic = reinterpret_cast<HDC>(wParam);
        SetBkMode(hdcStatic, TRANSPARENT);
        if (reinterpret_cast<HWND>(lParam) == m_hStatus) {
            SetTextColor(hdcStatic, RGB(0, 92, 197)); // 柔和深蓝状态色
            SelectObject(hdcStatic, m_hBoldFont);
        } else {
            SetTextColor(hdcStatic, RGB(36, 41, 47));
        }
        return reinterpret_cast<LRESULT>(m_hBgBrush);
    }

    case WM_COMMAND: {
        WORD id = LOWORD(wParam);
        switch (id) {
        case IDC_FRD_BTN_FIND_NEXT:
            FindNext(true);
            return 0;
        case IDC_FRD_BTN_FIND_PREV:
            FindNext(false);
            return 0;
        case IDC_FRD_BTN_COUNT:
            CountMatches();
            return 0;
        case IDC_FRD_BTN_REPLACE:
            Replace();
            return 0;
        case IDC_FRD_BTN_REPLACE_ALL:
            ReplaceAll();
            return 0;
        case IDC_FRD_BTN_MARK_ALL:
            MarkAll();
            return 0;
        case IDC_FRD_BTN_CLEAR_MARKS:
            ClearMarks();
            return 0;
        case IDC_FRD_BTN_CLOSE:
        case IDCANCEL:
            ShowWindow(m_hWnd, SW_HIDE);
            SetFocus(m_richEditView.GetHwnd());
            return 0;
        case IDOK:
            if (m_currentTab == FindTabMode::Find) {
                FindNext(true);
            } else if (m_currentTab == FindTabMode::Replace) {
                Replace();
            } else if (m_currentTab == FindTabMode::Mark) {
                MarkAll();
            }
            return 0;
        }
        break;
    }

    case WM_CLOSE:
        ShowWindow(m_hWnd, SW_HIDE);
        SetFocus(m_richEditView.GetHwnd());
        return 0;
    }

    return common::Window::HandleMessage(uMsg, wParam, lParam);
}

} // namespace anynote::ui
