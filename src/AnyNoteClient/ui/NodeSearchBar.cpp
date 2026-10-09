#include "NodeSearchBar.h"
#include "resource.h"
#include <commctrl.h>
#include <uxtheme.h>
#include <algorithm>

namespace anynote::ui {

static bool s_nodeSearchBarClassRegistered = false;

struct CloseBtnData {
    bool isHovered = false;
    bool isPressed = false;
    NodeSearchBar* pBar = nullptr;
};

NodeSearchBar::NodeSearchBar() {
    m_hBgBrush = CreateSolidBrush(RGB(247, 248, 250));
}

NodeSearchBar::~NodeSearchBar() {
    if (m_hFont) {
        DeleteObject(m_hFont);
        m_hFont = nullptr;
    }
    if (m_hCountFont) {
        DeleteObject(m_hCountFont);
        m_hCountFont = nullptr;
    }
    if (m_hBgBrush) {
        DeleteObject(m_hBgBrush);
        m_hBgBrush = nullptr;
    }
}

void NodeSearchBar::RegisterClassIfNeeded(HINSTANCE hInstance) {
    if (s_nodeSearchBarClassRegistered) return;

    WNDCLASSEXW wc = {sizeof(WNDCLASSEXW)};
    wc.lpfnWndProc = [](HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) -> LRESULT {
        auto* pThis = reinterpret_cast<NodeSearchBar*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
        if (uMsg == WM_NCCREATE) {
            auto* pCreate = reinterpret_cast<CREATESTRUCTW*>(lParam);
            pThis = reinterpret_cast<NodeSearchBar*>(pCreate->lpCreateParams);
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
    wc.hbrBackground = nullptr;

    RegisterClassExW(&wc);
    s_nodeSearchBarClassRegistered = true;
}

bool NodeSearchBar::Initialize(HWND hParent) {
    RegisterClassIfNeeded(GetModuleHandleW(nullptr));

    if (!Create(
        GetClassName(),
        L"",
        WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
        0,
        0, 0, 100, 34,
        hParent,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_MAIN_NODE_SEARCH_BAR)),
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

    m_hCountFont = CreateFontW(
        -MulDiv(8, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
    );

    // 1. 搜索输入框
    m_hEdit = CreateWindowExW(
        0, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | WS_BORDER,
        0, 0, 10, 10, m_hWnd,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_NODE_SEARCH_EDIT)),
        GetModuleHandleW(nullptr), nullptr
    );

    if (m_hEdit) {
        SendMessageW(m_hEdit, WM_SETFONT, reinterpret_cast<WPARAM>(m_hFont), TRUE);
        SendMessageW(m_hEdit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELONG(4, 4));
        SendMessageW(m_hEdit, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"搜索节点名称..."));
        SetWindowSubclass(m_hEdit, EditSubclassProc, 1, reinterpret_cast<DWORD_PTR>(this));
    }

    // 2. 匹配计数标签
    m_hStaticCount = CreateWindowExW(
        0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_RIGHT | SS_CENTERIMAGE,
        0, 0, 10, 10, m_hWnd,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_NODE_SEARCH_COUNT)),
        GetModuleHandleW(nullptr), nullptr
    );

    if (m_hStaticCount) {
        SendMessageW(m_hStaticCount, WM_SETFONT, reinterpret_cast<WPARAM>(m_hCountFont), TRUE);
    }

    // 3. 关闭按钮 (✕)
    m_hBtnClose = CreateWindowExW(
        0, L"BUTTON", L"",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        0, 0, 10, 10, m_hWnd,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_NODE_SEARCH_CLOSE)),
        GetModuleHandleW(nullptr), nullptr
    );

    if (m_hBtnClose) {
        auto* pBtnData = new CloseBtnData();
        pBtnData->pBar = this;
        SetWindowSubclass(m_hBtnClose, CloseBtnSubclassProc, 1, reinterpret_cast<DWORD_PTR>(pBtnData));
    }

    return true;
}

void NodeSearchBar::ShowBar(bool show) {
    m_isVisible = show;
    if (m_hWnd) {
        ShowWindow(m_hWnd, show ? SW_SHOW : SW_HIDE);
        if (show) {
            FocusSearchBox();
        }
    }
}

void NodeSearchBar::FocusSearchBox() {
    if (m_hEdit && IsWindow(m_hEdit)) {
        SetFocus(m_hEdit);
        SendMessageW(m_hEdit, EM_SETSEL, 0, -1);
    }
}

void NodeSearchBar::SetMatchCount(int count, bool hasKeyword) {
    m_lastMatchCount = count;
    m_hasKeyword = hasKeyword;
    if (!m_hStaticCount) return;

    if (!hasKeyword) {
        SetWindowTextW(m_hStaticCount, L"");
    } else if (count > 0) {
        std::wstring text = std::to_wstring(count) + L" 项";
        SetWindowTextW(m_hStaticCount, text.c_str());
    } else {
        SetWindowTextW(m_hStaticCount, L"无匹配");
    }
    InvalidateRect(m_hStaticCount, nullptr, TRUE);
}

std::wstring NodeSearchBar::GetKeyword() const {
    if (!m_hEdit) return {};
    int len = GetWindowTextLengthW(m_hEdit);
    if (len <= 0) return {};
    std::wstring text(len + 1, L'\0');
    GetWindowTextW(m_hEdit, text.data(), len + 1);
    text.resize(len);
    return text;
}

void NodeSearchBar::ClearKeyword() {
    if (m_hEdit) {
        SetWindowTextW(m_hEdit, L"");
    }
    SetMatchCount(0, false);
}

void NodeSearchBar::SetBounds(int x, int y, int width, int height, bool repaint) {
    if (m_hWnd) {
        MoveWindow(m_hWnd, x, y, width, height, repaint ? TRUE : FALSE);
    }
}

void NodeSearchBar::LayoutControls(int width, int height) {
    if (width <= 0 || height <= 0) return;

    UINT dpi = GetDpiForWindow(m_hWnd);
    int padX = MulDiv(6, dpi, 96);
    int btnSize = MulDiv(20, dpi, 96);
    int countW = MulDiv(52, dpi, 96);
    int editH = MulDiv(24, dpi, 96);

    int btnX = width - padX - btnSize;
    int btnY = (height - btnSize) / 2;

    int countX = btnX - countW - MulDiv(3, dpi, 96);
    int countY = (height - btnSize) / 2;

    int editX = padX;
    int editY = (height - editH) / 2;
    int editW = std::max(40, countX - editX - MulDiv(4, dpi, 96));

    if (m_hEdit) {
        SetWindowPos(m_hEdit, nullptr, editX, editY, editW, editH, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if (m_hStaticCount) {
        SetWindowPos(m_hStaticCount, nullptr, countX, countY, countW, btnSize, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if (m_hBtnClose) {
        SetWindowPos(m_hBtnClose, nullptr, btnX, btnY, btnSize, btnSize, SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

LRESULT NodeSearchBar::HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_SIZE: {
        LayoutControls(LOWORD(lParam), HIWORD(lParam));
        return 0;
    }

    case WM_COMMAND: {
        WORD id = LOWORD(wParam);
        WORD code = HIWORD(wParam);

        if (id == IDC_NODE_SEARCH_EDIT && code == EN_CHANGE) {
            if (m_onFilterChanged) {
                m_onFilterChanged(GetKeyword());
            }
            return 0;
        }

        if (id == IDC_NODE_SEARCH_CLOSE) {
            if (m_onClose) {
                m_onClose();
            }
            return 0;
        }
        break;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(m_hWnd, &ps);

        RECT rcClient;
        GetClientRect(m_hWnd, &rcClient);

        // 背景填充
        FillRect(hdc, &rcClient, m_hBgBrush);

        // 顶部精致浅灰分割线 (RGB 226, 232, 240)
        HPEN hPen = CreatePen(PS_SOLID, 1, RGB(226, 232, 240));
        HGDIOBJ oldPen = SelectObject(hdc, hPen);
        MoveToEx(hdc, 0, 0, nullptr);
        LineTo(hdc, rcClient.right, 0);
        SelectObject(hdc, oldPen);
        DeleteObject(hPen);

        EndPaint(m_hWnd, &ps);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_CTLCOLORSTATIC: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        HWND hStatic = reinterpret_cast<HWND>(lParam);
        SetBkMode(hdc, TRANSPARENT);
        if (hStatic == m_hStaticCount) {
            if (m_hasKeyword && m_lastMatchCount == 0) {
                SetTextColor(hdc, RGB(220, 38, 38)); // 柔和深红
            } else {
                SetTextColor(hdc, RGB(100, 116, 139)); // 现代石板灰
            }
        }
        return reinterpret_cast<LRESULT>(m_hBgBrush);
    }

    case WM_CTLCOLOREDIT: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        SetTextColor(hdc, RGB(33, 37, 41));
        SetBkColor(hdc, RGB(255, 255, 255));
        return reinterpret_cast<LRESULT>(GetStockObject(WHITE_BRUSH));
    }

    case WM_DRAWITEM: {
        auto* pDraw = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (pDraw && pDraw->CtlID == IDC_NODE_SEARCH_CLOSE) {
            auto* pBtnData = reinterpret_cast<CloseBtnData*>(GetWindowLongPtrW(pDraw->hwndItem, GWLP_USERDATA));
            bool isHover = pBtnData ? pBtnData->isHovered : false;
            bool isPressed = (pDraw->itemState & ODS_SELECTED) || (pBtnData && pBtnData->isPressed);

            HDC hdc = pDraw->hDC;
            RECT rc = pDraw->rcItem;

            if (isPressed) {
                HBRUSH hPress = CreateSolidBrush(RGB(209, 213, 219));
                FillRect(hdc, &rc, hPress);
                DeleteObject(hPress);
            } else if (isHover) {
                HBRUSH hHover = CreateSolidBrush(RGB(229, 231, 235));
                FillRect(hdc, &rc, hHover);
                DeleteObject(hHover);
            } else {
                FillRect(hdc, &rc, m_hBgBrush);
            }

            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, isHover ? RGB(31, 41, 55) : RGB(107, 114, 128));
            HGDIOBJ oldFont = SelectObject(hdc, m_hFont);
            DrawTextW(hdc, L"✕", -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(hdc, oldFont);
            return TRUE;
        }
        break;
    }
    }

    return common::Window::HandleMessage(uMsg, wParam, lParam);
}

LRESULT CALLBACK NodeSearchBar::EditSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR /*uIdSubclass*/, DWORD_PTR dwRefData) {
    auto* pThis = reinterpret_cast<NodeSearchBar*>(dwRefData);
    if (!pThis) return DefSubclassProc(hWnd, uMsg, wParam, lParam);

    switch (uMsg) {
    case WM_KEYDOWN: {
        if (wParam == VK_RETURN || wParam == VK_DOWN) {
            if (pThis->m_onEnterPressed) {
                pThis->m_onEnterPressed();
            }
            return 0;
        }
        if (wParam == VK_ESCAPE) {
            if (pThis->m_onClose) {
                pThis->m_onClose();
            }
            return 0;
        }
        break;
    }
    case WM_CHAR: {
        if (wParam == VK_RETURN) {
            return 0; // 阻止单行编辑框回车发出提示音
        }
        break;
    }
    }

    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK NodeSearchBar::CloseBtnSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    auto* pData = reinterpret_cast<CloseBtnData*>(dwRefData);
    switch (uMsg) {
    case WM_MOUSEMOVE: {
        if (pData && !pData->isHovered) {
            pData->isHovered = true;
            TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, hWnd, 0 };
            TrackMouseEvent(&tme);
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        break;
    }
    case WM_MOUSELEAVE: {
        if (pData) {
            pData->isHovered = false;
            pData->isPressed = false;
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        break;
    }
    case WM_LBUTTONDOWN: {
        if (pData) {
            pData->isPressed = true;
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        break;
    }
    case WM_LBUTTONUP: {
        if (pData && pData->isPressed) {
            pData->isPressed = false;
            InvalidateRect(hWnd, nullptr, FALSE);
            if (pData->pBar && pData->pBar->m_onClose) {
                pData->pBar->m_onClose();
            }
            return 0;
        }
        break;
    }
    case WM_NCDESTROY: {
        delete pData;
        RemoveWindowSubclass(hWnd, CloseBtnSubclassProc, uIdSubclass);
        break;
    }
    }
    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

} // namespace anynote::ui
