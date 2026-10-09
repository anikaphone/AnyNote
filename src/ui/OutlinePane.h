#pragma once

#include "common/Window.h"
#include "ui/TreeView.h"
#include "ui/RichEditView.h"
#include <windows.h>
#include <vector>
#include <functional>

namespace anynote::ui {

class OutlinePane : public common::Window {
public:
    using ItemSelectedCallback = std::function<void(LONG charPos)>;
    using CloseCallback = std::function<void()>;

    OutlinePane();
    ~OutlinePane() override;

    bool Initialize(HWND hParent);

    void SetBounds(int x, int y, int width, int height, bool repaint = true);

    void SetOutlineItems(const std::vector<OutlineItem>& items);
    void ClearOutline();

    void SelectNearestItem(LONG charPos);

    void SetOnItemSelected(ItemSelectedCallback cb) { m_onItemSelected = std::move(cb); }
    void SetOnClose(CloseCallback cb) { m_onClose = std::move(cb); }

    static const wchar_t* GetClassName() { return L"AnyNoteOutlinePane"; }
    static void RegisterClassIfNeeded(HINSTANCE hInstance);

protected:
    LRESULT HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam) override;

private:
    void LayoutControls(int width, int height);

    ItemSelectedCallback m_onItemSelected;
    CloseCallback m_onClose;

    TreeView m_treeView;
    HWND m_hBtnClose = nullptr;
    HWND m_hStaticEmpty = nullptr;

    HFONT m_hHeaderFont = nullptr;
    HFONT m_hEmptyFont = nullptr;
    HBRUSH m_hBgBrush = nullptr;
    HBRUSH m_hHeaderBrush = nullptr;

    std::vector<OutlineItem> m_items;
    std::vector<HTREEITEM> m_treeItems;

    bool m_isInternalSelecting = false;
};

} // namespace anynote::ui
