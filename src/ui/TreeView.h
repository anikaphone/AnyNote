#pragma once

#include <windows.h>
#include <commctrl.h>
#include <string>

namespace anynote::ui {

class TreeView {
public:
    TreeView() = default;
    ~TreeView();

    bool Initialize(HWND hParent, int x, int y, int width, int height, UINT controlId);
    HWND GetHwnd() const noexcept { return m_hWnd; }

    void SetBounds(int x, int y, int width, int height, bool repaint = true);

    HTREEITEM InsertNode(HTREEITEM hParent, const std::wstring& text, LPARAM data = 0, bool expand = false);
    bool DeleteItem(HTREEITEM hItem);
    void ClearAll();

    HTREEITEM GetSelectedItem() const;
    void SelectItem(HTREEITEM hItem);

    std::wstring GetItemText(HTREEITEM hItem) const;
    void SetItemText(HTREEITEM hItem, const std::wstring& text);

    LPARAM GetItemData(HTREEITEM hItem) const;
    void SetItemData(HTREEITEM hItem, LPARAM data);

    void PopulateSampleNodes();

private:
    HWND m_hWnd = nullptr;
    HFONT m_hFont = nullptr;
};

} // namespace anynote::ui
