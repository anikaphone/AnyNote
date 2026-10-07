#pragma once

#include "common/Window.h"
#include "ui/Toolbar.h"
#include "ui/Splitter.h"
#include "ui/TreeView.h"
#include "ui/RichEditView.h"
#include <memory>
#include <string>
#include <unordered_map>

namespace anynote::ui {

class MainWindow : public common::Window {
public:
    MainWindow();
    ~MainWindow() override = default;

    bool Initialize(HINSTANCE hInstance, int nCmdShow);

    static const wchar_t* GetClassName() { return L"AnyNoteMainWindow"; }
    static void RegisterClassIfNeeded(HINSTANCE hInstance);

protected:
    LRESULT HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam) override;

private:
    void LayoutChildren(int clientWidth, int clientHeight);
    void UpdateStatusBar(const std::wstring& text);
    void OnTreeSelectionChanged(NMTREEVIEWW* pNmtv);
    void ShowInsertCodeDialog();
    void SaveActiveNote();
    void LoadNoteForItem(HTREEITEM hItem);

    Toolbar m_toolbar;
    Splitter m_splitter;
    TreeView m_treeView;
    RichEditView m_richEditView;
    HWND m_hStatusBar = nullptr;
    HTREEITEM m_activeItem = nullptr;
    std::unordered_map<HTREEITEM, std::string> m_noteRtfByItem;

    int m_splitterPos = 260; // 默认分割条 X 坐标
};

} // namespace anynote::ui
