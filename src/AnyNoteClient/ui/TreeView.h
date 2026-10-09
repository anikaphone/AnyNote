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
    void PrepareLabelEdit(HTREEITEM hItem);

    LPARAM GetItemData(HTREEITEM hItem) const;
    void SetItemData(HTREEITEM hItem, LPARAM data);

    HTREEITEM HitTest(POINT pt, UINT* pFlags = nullptr) const;
    RECT GetItemRect(HTREEITEM hItem, bool textOnly = false) const;
    void SetInsertMark(HTREEITEM hItem, bool after);
    void ClearInsertMark();
    void SetDropHighlight(HTREEITEM hItem);
    void ClearDropHighlight();
    void EnsureVisible(HTREEITEM hItem);
    HTREEITEM GetParentItem(HTREEITEM hItem) const;
    bool IsDescendant(HTREEITEM hParent, HTREEITEM hChild) const;

    void ExpandAll(bool expand = true);
    void PopulateSampleNodes();

private:
    HWND m_hWnd = nullptr;
    HFONT m_hFont = nullptr;
    HTREEITEM m_editItem = nullptr;
    static LRESULT CALLBACK LabelEditSubclassProc(HWND hWnd, UINT message, WPARAM wParam,
        LPARAM lParam, UINT_PTR subclassId, DWORD_PTR referenceData);
};

} // namespace anynote::ui
