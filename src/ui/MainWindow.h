#pragma once

#include "common/Window.h"
#include "ui/Toolbar.h"
#include "ui/Splitter.h"
#include "ui/TreeView.h"
#include "ui/RichEditView.h"
#include "ui/FindReplaceDialog.h"
#include "ui/SearchPane.h"
#include "storage/Database.h"
#include "storage/NoteRepository.h"
#include <memory>
#include <string>

namespace anynote::ui {

class MainWindow : public common::Window {
public:
    MainWindow();
    ~MainWindow() override = default;

    bool Initialize(HINSTANCE hInstance, int nCmdShow);
    HWND GetFindReplaceDialogHwnd() const noexcept { return m_findReplaceDialog.GetHwnd(); }

    static const wchar_t* GetClassName() { return L"AnyNoteMainWindow"; }
    static void RegisterClassIfNeeded(HINSTANCE hInstance);

protected:
    LRESULT HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam) override;

private:
    void LayoutChildren(int clientWidth, int clientHeight);
    void UpdateStatusBar(const std::wstring& text);
    void OnTreeSelectionChanged(NMTREEVIEWW* pNmtv);
    void ShowInsertCodeDialog();

    bool OpenNotebook(const std::wstring& filePath, const std::string& password = "");
    void PopulateTreeViewFromDb(int64_t selectNodeId = 0);
    bool SelectNodeById(int64_t nodeId);
    bool SaveActiveNote();
    void LoadNoteForId(int64_t nodeId);
    void UpdateEncryptionStatusUI();
    void CancelDragOperation();

    void OpenFindReplaceDialog(FindTabMode mode);
    void ToggleSearchPane();
    void OnSearchResultSelected(const storage::SearchResult& result);
    void ShowTreeContextMenu(int xScreen, int yScreen);
    void ShowEditorContextMenu(int xScreen, int yScreen);

    Toolbar m_toolbar;
    Splitter m_splitter;
    TreeView m_treeView;
    RichEditView m_richEditView;
    FindReplaceDialog m_findReplaceDialog;
    SearchPane m_searchPane;
    Splitter m_searchSplitter;
    HWND m_hStatusBar = nullptr;

    std::unique_ptr<storage::Database> m_db;
    std::unique_ptr<storage::NoteRepository> m_repo;
    int64_t m_activeNoteId = 0;
    std::wstring m_currentNotebookPath;
    bool m_revertingTreeSelection = false;

    // 目录树拖拽重排与移动状态
    bool m_isDragging = false;
    HTREEITEM m_hDragItem = nullptr;
    HTREEITEM m_hDropTarget = nullptr;
    storage::DropPosition m_dropPosition = storage::DropPosition::None;
    HIMAGELIST m_hDragImageList = nullptr;

    int m_splitterPos = 260; // 默认左侧分割条 X 坐标
    int m_searchPaneHeight = 180; // 默认下方全库搜索窗格高度
    bool m_isSearchPaneVisible = false; // 全库搜索窗格是否显示
};

} // namespace anynote::ui
