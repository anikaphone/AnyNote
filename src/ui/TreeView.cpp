#include "TreeView.h"
#include <uxtheme.h>
#include <functional>
#include <algorithm>

namespace anynote::ui {

TreeView::~TreeView() {
    if (m_hFont) {
        DeleteObject(m_hFont);
        m_hFont = nullptr;
    }
}

bool TreeView::Initialize(HWND hParent, int x, int y, int width, int height, UINT controlId) {
    // 现代 Fluent Explorer 风格：保留 TVS_HASBUTTONS 与 TVS_LINESATROOT 以显示现代折叠三角箭头
    // 不加 TVS_HASLINES 避免点阵虚线，不加 TVS_EX_FADEINOUTEXPANDOS 以保证折叠/展开三角标记常驻清晰可见
    DWORD dwStyle = WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                    TVS_HASBUTTONS | TVS_LINESATROOT | TVS_SHOWSELALWAYS | TVS_EDITLABELS | TVS_NONEVENHEIGHT;

    m_hWnd = CreateWindowExW(
        0, // 移除 WS_EX_CLIENTEDGE 3D 凹陷老旧黑边框
        WC_TREEVIEW,
        L"",
        dwStyle,
        x, y, width, height,
        hParent,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(controlId)),
        GetModuleHandleW(nullptr),
        nullptr
    );

    if (!m_hWnd) {
        return false;
    }

    // 启用现代 Windows 资源管理器视觉主题 (现代三角折叠箭头、扁平高亮圆角胶囊矩形)
    SetWindowTheme(m_hWnd, L"Explorer", nullptr);

    // 启用现代扩展样式：双缓冲防闪烁、自动水平滚动 (三角折叠箭头常驻显示，不自动淡出隐形)
    TreeView_SetExtendedStyle(
        m_hWnd,
        TVS_EX_DOUBLEBUFFER | TVS_EX_AUTOHSCROLL,
        TVS_EX_DOUBLEBUFFER | TVS_EX_AUTOHSCROLL
    );

    // 设置侧边栏现代轻量浅灰底色与深色正文字体，形成自然的侧边栏面板层次感
    COLORREF bgSidebar = RGB(247, 248, 250);
    COLORREF textSidebar = RGB(33, 37, 41);
    TreeView_SetBkColor(m_hWnd, bgSidebar);
    TreeView_SetTextColor(m_hWnd, textSidebar);

    UINT dpi = GetDpiForWindow(m_hWnd);

    // 设置符合现代设计语言的呼吸感行高与层级缩进
    TreeView_SetItemHeight(m_hWnd, MulDiv(28, dpi, 96));
    TreeView_SetIndent(m_hWnd, MulDiv(18, dpi, 96));

    // 设置拖拽插入标记线的主题颜色 (Windows 现代蓝)
    SendMessageW(m_hWnd, TVM_SETINSERTMARKCOLOR, 0, static_cast<LPARAM>(RGB(0, 120, 215)));

    // 设置标准现代 UI 字体 (DPI 自适应 Segoe UI)
    if (m_hFont) {
        DeleteObject(m_hFont);
        m_hFont = nullptr;
    }

    m_hFont = CreateFontW(
        -MulDiv(10, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
    );

    if (m_hFont) {
        SendMessageW(m_hWnd, WM_SETFONT, reinterpret_cast<WPARAM>(m_hFont), TRUE);
    }

    return true;
}

void TreeView::SetBounds(int x, int y, int width, int height, bool repaint) {
    if (m_hWnd) {
        UINT dpi = GetDpiForWindow(m_hWnd);
        TreeView_SetItemHeight(m_hWnd, MulDiv(28, dpi, 96));
        TreeView_SetIndent(m_hWnd, MulDiv(18, dpi, 96));
        MoveWindow(m_hWnd, x, y, width, height, repaint ? TRUE : FALSE);
    }
}

HTREEITEM TreeView::InsertNode(HTREEITEM hParent, const std::wstring& text, LPARAM data, bool expand) {
    if (!m_hWnd) return nullptr;

    TVINSERTSTRUCTW tvis = {};
    tvis.hParent = hParent ? hParent : TVI_ROOT;
    tvis.hInsertAfter = TVI_LAST;
    tvis.item.mask = TVIF_TEXT | TVIF_PARAM;
    tvis.item.pszText = const_cast<LPWSTR>(text.c_str());
    tvis.item.lParam = data;

    HTREEITEM hItem = TreeView_InsertItem(m_hWnd, &tvis);
    if (hItem && expand) {
        TreeView_Expand(m_hWnd, hItem, TVE_EXPAND);
    }
    return hItem;
}

bool TreeView::DeleteItem(HTREEITEM hItem) {
    if (!m_hWnd || !hItem) return false;
    return TreeView_DeleteItem(m_hWnd, hItem) != FALSE;
}

void TreeView::ClearAll() {
    if (m_hWnd) {
        TreeView_DeleteAllItems(m_hWnd);
    }
}

HTREEITEM TreeView::GetSelectedItem() const {
    if (!m_hWnd) return nullptr;
    return TreeView_GetSelection(m_hWnd);
}

void TreeView::SelectItem(HTREEITEM hItem) {
    if (m_hWnd && hItem) {
        TreeView_SelectItem(m_hWnd, hItem);
    }
}

std::wstring TreeView::GetItemText(HTREEITEM hItem) const {
    if (!m_hWnd || !hItem) return {};

    // 使用足够大的缓冲区以避免长标题被截断 (原 260 字符有截断风险)
    wchar_t buffer[1024] = {0};
    TVITEMW tvi = {};
    tvi.mask = TVIF_TEXT;
    tvi.hItem = hItem;
    tvi.pszText = buffer;
    tvi.cchTextMax = static_cast<int>(std::size(buffer));

    if (TreeView_GetItem(m_hWnd, &tvi)) {
        return buffer;
    }
    return {};
}

void TreeView::SetItemText(HTREEITEM hItem, const std::wstring& text) {
    if (!m_hWnd || !hItem) return;

    TVITEMW tvi = {};
    tvi.mask = TVIF_TEXT;
    tvi.hItem = hItem;
    tvi.pszText = const_cast<LPWSTR>(text.c_str());

    TreeView_SetItem(m_hWnd, &tvi);
}

void TreeView::PrepareLabelEdit(HTREEITEM hItem) {
    HWND edit = TreeView_GetEditControl(m_hWnd);
    if (!edit || !hItem) return;

    m_editItem = hItem;
    SetWindowSubclass(edit, LabelEditSubclassProc, 1, reinterpret_cast<DWORD_PTR>(this));
    SendMessageW(edit, WM_SETFONT, SendMessageW(m_hWnd, WM_GETFONT, 0, 0), FALSE);
    SendMessageW(edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELONG(1, 1));
    RECT rect = GetItemRect(hItem, true);
    SetWindowPos(edit, nullptr, rect.left, rect.top, rect.right - rect.left,
        rect.bottom - rect.top, SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

LRESULT CALLBACK TreeView::LabelEditSubclassProc(HWND hWnd, UINT message, WPARAM wParam,
    LPARAM lParam, UINT_PTR subclassId, DWORD_PTR referenceData) {
    auto* tree = reinterpret_cast<TreeView*>(referenceData);
    if (message == WM_WINDOWPOSCHANGING && tree->m_editItem) {
        // 原生树控件会按字体高度反复调整编辑框；始终与节点标签的行框对齐。
        RECT label = tree->GetItemRect(tree->m_editItem, true);
        auto* position = reinterpret_cast<WINDOWPOS*>(lParam);
        position->x = label.left;
        position->y = label.top;
        position->cy = label.bottom - label.top;

        int length = GetWindowTextLengthW(hWnd);
        std::wstring text(static_cast<size_t>(length) + 1, L'\0');
        GetWindowTextW(hWnd, text.data(), length + 1);
        HDC hdc = GetDC(hWnd);
        HGDIOBJ oldFont = SelectObject(hdc, reinterpret_cast<HFONT>(SendMessageW(hWnd, WM_GETFONT, 0, 0)));
        SIZE textSize = {};
        GetTextExtentPoint32W(hdc, text.data(), length, &textSize);
        SelectObject(hdc, oldFont);
        ReleaseDC(hWnd, hdc);
        position->cx = std::max(label.right - label.left, textSize.cx + MulDiv(4, GetDpiForWindow(hWnd), 96));
        position->flags &= ~(SWP_NOMOVE | SWP_NOSIZE);
    } else if (message == WM_NCCALCSIZE) {
        LRESULT result = DefSubclassProc(hWnd, message, wParam, lParam);
        RECT* client = wParam ? &reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam)->rgrc[0]
                             : reinterpret_cast<RECT*>(lParam);
        HDC hdc = GetDC(hWnd);
        HGDIOBJ oldFont = SelectObject(hdc, reinterpret_cast<HFONT>(SendMessageW(hWnd, WM_GETFONT, 0, 0)));
        TEXTMETRICW metrics = {};
        GetTextMetricsW(hdc, &metrics);
        SelectObject(hdc, oldFont);
        ReleaseDC(hWnd, hdc);
        // 单行 EDIT 不支持 EM_SETRECT，使用上下非客户区留白保持文字基线不变。
        int padding = std::max(0L, client->bottom - client->top - metrics.tmHeight);
        client->top += padding / 2;
        client->bottom -= padding - padding / 2;
        return result;
    } else if (message == WM_NCPAINT) {
        RECT windowRect = {}, clientRect = {};
        GetWindowRect(hWnd, &windowRect);
        GetClientRect(hWnd, &clientRect);
        MapWindowPoints(hWnd, nullptr, reinterpret_cast<POINT*>(&clientRect), 2);
        OffsetRect(&clientRect, -windowRect.left, -windowRect.top);
        RECT frame = { 0, 0, windowRect.right - windowRect.left, windowRect.bottom - windowRect.top };
        HDC hdc = GetWindowDC(hWnd);
        ExcludeClipRect(hdc, clientRect.left, clientRect.top, clientRect.right, clientRect.bottom);
        FillRect(hdc, &frame, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
        HBRUSH border = CreateSolidBrush(RGB(0, 120, 215));
        FrameRect(hdc, &frame, border);
        DeleteObject(border);
        ReleaseDC(hWnd, hdc);
        return 0;
    } else if (message == WM_NCDESTROY) {
        tree->m_editItem = nullptr;
        RemoveWindowSubclass(hWnd, LabelEditSubclassProc, subclassId);
    }
    return DefSubclassProc(hWnd, message, wParam, lParam);
}

LPARAM TreeView::GetItemData(HTREEITEM hItem) const {
    if (!m_hWnd || !hItem) return 0;

    TVITEMW tvi = {};
    tvi.mask = TVIF_PARAM;
    tvi.hItem = hItem;

    if (TreeView_GetItem(m_hWnd, &tvi)) {
        return tvi.lParam;
    }
    return 0;
}

void TreeView::SetItemData(HTREEITEM hItem, LPARAM data) {
    if (!m_hWnd || !hItem) return;

    TVITEMW tvi = {};
    tvi.mask = TVIF_PARAM;
    tvi.hItem = hItem;
    tvi.lParam = data;

    TreeView_SetItem(m_hWnd, &tvi);
}

HTREEITEM TreeView::HitTest(POINT pt, UINT* pFlags) const {
    if (!m_hWnd) return nullptr;
    TVHITTESTINFO ht = {};
    ht.pt = pt;
    HTREEITEM hItem = TreeView_HitTest(m_hWnd, &ht);
    if (pFlags) *pFlags = ht.flags;
    return hItem;
}

RECT TreeView::GetItemRect(HTREEITEM hItem, bool textOnly) const {
    RECT rc = {};
    if (!m_hWnd || !hItem) return rc;
    *(reinterpret_cast<HTREEITEM*>(&rc)) = hItem;
    TreeView_GetItemRect(m_hWnd, hItem, &rc, textOnly ? TRUE : FALSE);
    return rc;
}

void TreeView::SetInsertMark(HTREEITEM hItem, bool after) {
    if (m_hWnd) {
        SendMessageW(m_hWnd, TVM_SETINSERTMARK, after ? TRUE : FALSE, reinterpret_cast<LPARAM>(hItem));
    }
}

void TreeView::ClearInsertMark() {
    if (m_hWnd) {
        SendMessageW(m_hWnd, TVM_SETINSERTMARK, 0, 0);
    }
}

void TreeView::SetDropHighlight(HTREEITEM hItem) {
    if (m_hWnd) {
        TreeView_SelectDropTarget(m_hWnd, hItem);
    }
}

void TreeView::ClearDropHighlight() {
    if (m_hWnd) {
        TreeView_SelectDropTarget(m_hWnd, nullptr);
    }
}

void TreeView::EnsureVisible(HTREEITEM hItem) {
    if (m_hWnd && hItem) {
        TreeView_EnsureVisible(m_hWnd, hItem);
    }
}

HTREEITEM TreeView::GetParentItem(HTREEITEM hItem) const {
    if (!m_hWnd || !hItem) return nullptr;
    return TreeView_GetParent(m_hWnd, hItem);
}

bool TreeView::IsDescendant(HTREEITEM hParent, HTREEITEM hChild) const {
    if (!m_hWnd || !hParent || !hChild) return false;
    HTREEITEM hCur = hChild;
    while (hCur) {
        if (hCur == hParent) return true;
        hCur = TreeView_GetParent(m_hWnd, hCur);
    }
    return false;
}

void TreeView::ExpandAll(bool expand) {
    if (!m_hWnd) return;
    SendMessageW(m_hWnd, WM_SETREDRAW, FALSE, 0);

    std::function<void(HTREEITEM)> recurse = [&](HTREEITEM hItem) {
        while (hItem) {
            TreeView_Expand(m_hWnd, hItem, expand ? TVE_EXPAND : TVE_COLLAPSE);
            HTREEITEM hChild = TreeView_GetChild(m_hWnd, hItem);
            if (hChild) {
                recurse(hChild);
            }
            hItem = TreeView_GetNextSibling(m_hWnd, hItem);
        }
    };
    recurse(TreeView_GetRoot(m_hWnd));

    SendMessageW(m_hWnd, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(m_hWnd, nullptr, TRUE);
}

void TreeView::PopulateSampleNodes() {
    ClearAll();

    HTREEITEM hRoot1 = InsertNode(nullptr, L"📌 欢迎使用 AnyNote", 1, true);
    InsertNode(hRoot1, L"🚀 快速上手指南", 2);
    InsertNode(hRoot1, L"💡 特性与快捷键说明", 3);

    HTREEITEM hRoot2 = InsertNode(nullptr, L"💻 开发与技术积累", 4, true);
    InsertNode(hRoot2, L"📘 现代 C++ 与 Win32 架构", 5);
    InsertNode(hRoot2, L"🔒 SQLite3MC 数据库与安全", 6);
    InsertNode(hRoot2, L"🎨 RichEdit 富文本与代码块", 7);

    HTREEITEM hRoot3 = InsertNode(nullptr, L"📝 随手记 / 待办", 8, false);
    InsertNode(hRoot3, L"计划清单", 9);

    if (hRoot1) {
        SelectItem(hRoot1);
    }
}

} // namespace anynote::ui
