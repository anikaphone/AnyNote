#include "TreeView.h"
#include <uxtheme.h>
#include <functional>

namespace anynote::ui {

TreeView::~TreeView() {
    if (m_hFont) {
        DeleteObject(m_hFont);
        m_hFont = nullptr;
    }
}

bool TreeView::Initialize(HWND hParent, int x, int y, int width, int height, UINT controlId) {
    DWORD dwStyle = WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                    TVS_HASLINES | TVS_HASBUTTONS | TVS_LINESATROOT |
                    TVS_SHOWSELALWAYS | TVS_TRACKSELECT | TVS_EDITLABELS;

    m_hWnd = CreateWindowExW(
        WS_EX_CLIENTEDGE,
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

    // 启用现代 Windows 资源管理器视觉主题 (现代箭头折叠按钮)
    SetWindowTheme(m_hWnd, L"Explorer", nullptr);

    // 设置拖拽插入标记线的主题颜色 (Windows 现代蓝)
    SendMessageW(m_hWnd, TVM_SETINSERTMARKCOLOR, 0, static_cast<LPARAM>(RGB(0, 120, 215)));

    // 设置标准现代 UI 字体 (先清理可能已存在的旧字体句柄，防止 GDI 泄漏)
    if (m_hFont) {
        DeleteObject(m_hFont);
        m_hFont = nullptr;
    }

    NONCLIENTMETRICSW ncm = {sizeof(NONCLIENTMETRICSW)};
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0)) {
        m_hFont = CreateFontIndirectW(&ncm.lfMenuFont);
    } else {
        m_hFont = CreateFontW(
            -13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
        );
    }

    if (m_hFont) {
        SendMessageW(m_hWnd, WM_SETFONT, reinterpret_cast<WPARAM>(m_hFont), TRUE);
    }

    return true;
}

void TreeView::SetBounds(int x, int y, int width, int height, bool repaint) {
    if (m_hWnd) {
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
