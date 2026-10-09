#include "OutlinePane.h"
#include "resource.h"
#include <commctrl.h>
#include <uxtheme.h>
#include <algorithm>

namespace anynote::ui {

static bool s_outlinePaneClassRegistered = false;

struct OutlineCloseBtnData {
    bool isHovered = false;
    bool isPressed = false;
    OutlinePane* pPane = nullptr;
};

static LRESULT CALLBACK OutlineCloseBtnSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
    UINT_PTR /*uIdSubclass*/, DWORD_PTR dwRefData) {
    auto* pData = reinterpret_cast<OutlineCloseBtnData*>(dwRefData);
    if (!pData) return DefSubclassProc(hWnd, uMsg, wParam, lParam);

    switch (uMsg) {
    case WM_MOUSEMOVE: {
        if (!pData->isHovered) {
            pData->isHovered = true;
            InvalidateRect(hWnd, nullptr, FALSE);
            TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, hWnd, 0 };
            TrackMouseEvent(&tme);
        }
        break;
    }
    case WM_MOUSELEAVE: {
        pData->isHovered = false;
        pData->isPressed = false;
        InvalidateRect(hWnd, nullptr, FALSE);
        break;
    }
    case WM_LBUTTONDOWN: {
        pData->isPressed = true;
        InvalidateRect(hWnd, nullptr, FALSE);
        break;
    }
    case WM_LBUTTONUP: {
        if (pData->isPressed) {
            pData->isPressed = false;
            InvalidateRect(hWnd, nullptr, FALSE);
            SendMessageW(GetParent(hWnd), WM_COMMAND,
                MAKEWPARAM(IDC_OUTLINE_BTN_CLOSE, BN_CLICKED), reinterpret_cast<LPARAM>(hWnd));
        }
        break;
    }
    case WM_NCDESTROY: {
        RemoveWindowSubclass(hWnd, OutlineCloseBtnSubclassProc, 1);
        delete pData;
        return 0;
    }
    }

    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

OutlinePane::OutlinePane() {
    m_hBgBrush = CreateSolidBrush(RGB(255, 255, 255));
    m_hHeaderBrush = CreateSolidBrush(RGB(247, 248, 250));
}

OutlinePane::~OutlinePane() {
    if (m_hHeaderFont) {
        DeleteObject(m_hHeaderFont);
        m_hHeaderFont = nullptr;
    }
    if (m_hEmptyFont) {
        DeleteObject(m_hEmptyFont);
        m_hEmptyFont = nullptr;
    }
    if (m_hBgBrush) {
        DeleteObject(m_hBgBrush);
        m_hBgBrush = nullptr;
    }
    if (m_hHeaderBrush) {
        DeleteObject(m_hHeaderBrush);
        m_hHeaderBrush = nullptr;
    }
}

void OutlinePane::RegisterClassIfNeeded(HINSTANCE hInstance) {
    if (s_outlinePaneClassRegistered) return;

    WNDCLASSEXW wc = {sizeof(WNDCLASSEXW)};
    wc.lpfnWndProc = [](HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) -> LRESULT {
        auto* pThis = reinterpret_cast<OutlinePane*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
        if (uMsg == WM_NCCREATE) {
            auto* pCreate = reinterpret_cast<CREATESTRUCTW*>(lParam);
            pThis = reinterpret_cast<OutlinePane*>(pCreate->lpCreateParams);
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
    s_outlinePaneClassRegistered = true;
}

bool OutlinePane::Initialize(HWND hParent) {
    RegisterClassIfNeeded(GetModuleHandleW(nullptr));

    if (!Create(
        GetClassName(),
        L"",
        WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
        0,
        0, 0, 220, 400,
        hParent,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_MAIN_OUTLINE_PANE)),
        GetModuleHandleW(nullptr)
    )) {
        return false;
    }

    UINT dpi = GetDpiForWindow(m_hWnd);

    m_hHeaderFont = CreateFontW(
        -MulDiv(9, dpi, 72), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
    );

    m_hEmptyFont = CreateFontW(
        -MulDiv(9, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
    );

    // 1. 关闭按钮
    m_hBtnClose = CreateWindowExW(
        0, L"BUTTON", L"",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        0, 0, 22, 22, m_hWnd,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_OUTLINE_BTN_CLOSE)),
        GetModuleHandleW(nullptr), nullptr
    );

    if (m_hBtnClose) {
        auto* pBtnData = new OutlineCloseBtnData();
        pBtnData->pPane = this;
        SetWindowSubclass(m_hBtnClose, OutlineCloseBtnSubclassProc, 1, reinterpret_cast<DWORD_PTR>(pBtnData));
    }

    // 2. 嵌套 TreeView 控件
    if (!m_treeView.Initialize(m_hWnd, 0, 30, 220, 370, IDC_OUTLINE_TREE)) {
        return false;
    }

    // 3. 空状态静态文本
    m_hStaticEmpty = CreateWindowExW(
        0, L"STATIC", L"当前笔记暂无大纲",
        WS_CHILD | WS_VISIBLE | SS_CENTER,
        0, 30, 220, 40, m_hWnd,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_OUTLINE_EMPTY_LABEL)),
        GetModuleHandleW(nullptr), nullptr
    );
    if (m_hStaticEmpty && m_hEmptyFont) {
        SendMessageW(m_hStaticEmpty, WM_SETFONT, reinterpret_cast<WPARAM>(m_hEmptyFont), TRUE);
    }

    ClearOutline();
    return true;
}

void OutlinePane::SetBounds(int x, int y, int width, int height, bool repaint) {
    if (m_hWnd) {
        MoveWindow(m_hWnd, x, y, width, height, repaint ? TRUE : FALSE);
        LayoutControls(width, height);
    }
}

void OutlinePane::ClearOutline() {
    m_items.clear();
    m_treeItems.clear();
    m_treeView.ClearAll();

    if (m_hStaticEmpty) {
        ShowWindow(m_hStaticEmpty, SW_SHOW);
    }
    if (m_treeView.GetHwnd()) {
        ShowWindow(m_treeView.GetHwnd(), SW_HIDE);
    }
}

void OutlinePane::SetOutlineItems(const std::vector<OutlineItem>& items) {
    if (m_items == items) {
        return;
    }

    m_items = items;
    m_treeItems.clear();

    HWND hTree = m_treeView.GetHwnd();
    if (hTree) {
        SendMessageW(hTree, WM_SETREDRAW, FALSE, 0);
    }
    m_treeView.ClearAll();

    if (items.empty()) {
        if (hTree) {
            SendMessageW(hTree, WM_SETREDRAW, TRUE, 0);
        }
        if (m_hStaticEmpty) ShowWindow(m_hStaticEmpty, SW_SHOW);
        if (hTree) ShowWindow(hTree, SW_HIDE);
        return;
    }

    if (m_hStaticEmpty) ShowWindow(m_hStaticEmpty, SW_HIDE);
    if (hTree) ShowWindow(hTree, SW_SHOW);

    m_treeItems.reserve(items.size());

    // 基于栈的层级构建算法，支持 H1~H4 的任意嵌套和跳级
    std::vector<std::pair<int, HTREEITEM>> hierarchy;
    for (size_t i = 0; i < items.size(); ++i) {
        const auto& item = items[i];

        while (!hierarchy.empty() && hierarchy.back().first >= item.level) {
            hierarchy.pop_back();
        }

        HTREEITEM hParent = hierarchy.empty() ? TVI_ROOT : hierarchy.back().second;
        HTREEITEM hItem = m_treeView.InsertNode(hParent, item.text, static_cast<LPARAM>(i), true);
        m_treeItems.push_back(hItem);
        hierarchy.emplace_back(item.level, hItem);
    }

    m_treeView.ExpandAll(true);

    if (hTree) {
        SendMessageW(hTree, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(hTree, nullptr, TRUE);
    }
}

void OutlinePane::SelectNearestItem(LONG charPos) {
    if (m_items.empty() || m_treeItems.empty()) return;

    int targetIdx = -1;
    for (int i = static_cast<int>(m_items.size()) - 1; i >= 0; --i) {
        if (m_items[i].charPos <= charPos) {
            targetIdx = i;
            break;
        }
    }

    if (targetIdx == -1) {
        targetIdx = 0;
    }

    if (targetIdx >= 0 && targetIdx < static_cast<int>(m_treeItems.size())) {
        HTREEITEM hItem = m_treeItems[targetIdx];
        if (hItem && hItem != m_treeView.GetSelectedItem()) {
            m_isInternalSelecting = true;
            m_treeView.SelectItem(hItem);
            m_treeView.EnsureVisible(hItem);
            m_isInternalSelecting = false;
        }
    }
}

void OutlinePane::LayoutControls(int width, int height) {
    if (width <= 0 || height <= 0) return;

    UINT dpi = GetDpiForWindow(m_hWnd);
    int headerH = MulDiv(30, dpi, 96);
    int btnSize = MulDiv(22, dpi, 96);
    int btnMarginRight = MulDiv(6, dpi, 96);
    int btnY = (headerH - btnSize) / 2;
    int btnX = width - btnMarginRight - btnSize;

    if (m_hBtnClose) {
        SetWindowPos(m_hBtnClose, nullptr, btnX, btnY, btnSize, btnSize, SWP_NOZORDER | SWP_NOACTIVATE);
    }

    int contentY = headerH;
    int contentH = std::max(0, height - headerH);

    m_treeView.SetBounds(0, contentY, width, contentH, true);

    if (m_hStaticEmpty) {
        int labelH = MulDiv(40, dpi, 96);
        int labelY = contentY + MulDiv(40, dpi, 96);
        SetWindowPos(m_hStaticEmpty, nullptr, MulDiv(10, dpi, 96), labelY,
            std::max(0, width - MulDiv(20, dpi, 96)), labelH, SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

LRESULT OutlinePane::HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_SIZE: {
        int width = LOWORD(lParam);
        int height = HIWORD(lParam);
        LayoutControls(width, height);
        InvalidateRect(m_hWnd, nullptr, FALSE);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(m_hWnd, &ps);

        RECT rcClient;
        GetClientRect(m_hWnd, &rcClient);

        UINT dpi = GetDpiForWindow(m_hWnd);
        int headerH = MulDiv(30, dpi, 96);

        // 1. 顶栏背景填充
        RECT rcHeader = { 0, 0, rcClient.right, headerH };
        FillRect(hdc, &rcHeader, m_hHeaderBrush);

        // 2. 顶栏标题 "大纲"
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(36, 41, 47));
        HGDIOBJ oldFont = SelectObject(hdc, m_hHeaderFont);
        RECT rcTitle = { MulDiv(12, dpi, 96), 0, rcClient.right - MulDiv(36, dpi, 96), headerH };
        DrawTextW(hdc, L"大纲", -1, &rcTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        SelectObject(hdc, oldFont);

        // 3. 顶栏底部分割线
        HPEN hPen = CreatePen(PS_SOLID, 1, RGB(226, 230, 234));
        HGDIOBJ oldPen = SelectObject(hdc, hPen);
        MoveToEx(hdc, 0, headerH - 1, nullptr);
        LineTo(hdc, rcClient.right, headerH - 1);
        SelectObject(hdc, oldPen);
        DeleteObject(hPen);

        // 4. 内容区背景填充 (在没有标题显示空状态时填充纯白)
        if (m_items.empty()) {
            RECT rcContent = { 0, headerH, rcClient.right, rcClient.bottom };
            FillRect(hdc, &rcContent, m_hBgBrush);
        }

        EndPaint(m_hWnd, &ps);
        return 0;
    }

    case WM_CTLCOLORSTATIC: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        HWND hStatic = reinterpret_cast<HWND>(lParam);
        SetBkMode(hdc, TRANSPARENT);
        if (hStatic == m_hStaticEmpty) {
            SetTextColor(hdc, RGB(140, 149, 159));
        }
        return reinterpret_cast<LRESULT>(m_hBgBrush);
    }

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        int code = HIWORD(wParam);
        if (id == IDC_OUTLINE_BTN_CLOSE && code == BN_CLICKED) {
            if (m_onClose) {
                m_onClose();
            }
            return 0;
        }
        break;
    }

    case WM_DRAWITEM: {
        auto* pDraw = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (pDraw && pDraw->CtlID == IDC_OUTLINE_BTN_CLOSE) {
            auto* pBtnData = reinterpret_cast<OutlineCloseBtnData*>(GetWindowLongPtrW(pDraw->hwndItem, GWLP_USERDATA));
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
                FillRect(hdc, &rc, m_hHeaderBrush);
            }

            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, isHover ? RGB(31, 41, 55) : RGB(107, 114, 128));
            HGDIOBJ oldFont = SelectObject(hdc, m_hHeaderFont);
            DrawTextW(hdc, L"✕", -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(hdc, oldFont);
            return TRUE;
        }
        break;
    }

    case WM_NOTIFY: {
        auto* pNmhdr = reinterpret_cast<NMHDR*>(lParam);
        if (pNmhdr && pNmhdr->idFrom == IDC_OUTLINE_TREE) {
            if (pNmhdr->code == TVN_SELCHANGEDW) {
                if (!m_isInternalSelecting) {
                    auto* pNmtv = reinterpret_cast<NMTREEVIEWW*>(lParam);
                    if (pNmtv && pNmtv->itemNew.hItem) {
                        LPARAM data = m_treeView.GetItemData(pNmtv->itemNew.hItem);
                        size_t idx = static_cast<size_t>(data);
                        if (idx < m_items.size() && m_onItemSelected) {
                            m_onItemSelected(m_items[idx].charPos);
                        }
                    }
                }
                return 0;
            } else if (pNmhdr->code == NM_CLICK) {
                // 处理在已选中节点上再次单击时的快速跳转
                POINT pt;
                GetCursorPos(&pt);
                ScreenToClient(m_treeView.GetHwnd(), &pt);
                UINT flags = 0;
                HTREEITEM hHit = m_treeView.HitTest(pt, &flags);
                if (hHit && (flags & (TVHT_ONITEMLABEL | TVHT_ONITEMICON))) {
                    if (!m_isInternalSelecting) {
                        LPARAM data = m_treeView.GetItemData(hHit);
                        size_t idx = static_cast<size_t>(data);
                        if (idx < m_items.size() && m_onItemSelected) {
                            m_onItemSelected(m_items[idx].charPos);
                        }
                    }
                }
            }
        }
        break;
    }
    }

    return common::Window::HandleMessage(uMsg, wParam, lParam);
}

} // namespace anynote::ui
