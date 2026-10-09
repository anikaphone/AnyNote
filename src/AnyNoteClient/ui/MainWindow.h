#pragma once

#include "common/Window.h"
#include "ui/Toolbar.h"
#include "ui/Splitter.h"
#include "ui/TreeView.h"
#include "ui/RichEditView.h"
#include "ui/FindReplaceDialog.h"
#include "ui/SearchPane.h"
#include "ui/NodeSearchBar.h"
#include "ui/OutlinePane.h"
#include "storage/Database.h"
#include "storage/NoteRepository.h"
#include "storage/VaultManager.h"
#include <memory>
#include <string>

namespace anynote::ui {

class MainWindow : public common::Window {
public:
    explicit MainWindow(const std::wstring& iniPath = L"");
    ~MainWindow() override = default;

    bool Initialize(HINSTANCE hInstance, int nCmdShow);
    HWND GetFindReplaceDialogHwnd() const noexcept { return m_findReplaceDialog.GetHwnd(); }
    std::wstring GetActiveVaultDisplayName() const { return m_vaultManager.GetActiveVaultDisplayName(); }
    void ShowVaultMenu();
    bool SwitchToVault(const std::wstring& path, bool createIfMissing = false);
    RichEditView& GetRichEditView() noexcept { return m_richEditView; }
    const RichEditView& GetRichEditView() const noexcept { return m_richEditView; }

    static const wchar_t* GetClassName() { return L"AnyNoteMainWindow"; }
    static void RegisterClassIfNeeded(HINSTANCE hInstance);

protected:
    LRESULT HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam) override;

private:
    void LayoutChildren(int clientWidth, int clientHeight);
    void UpdateStatusBar(const std::wstring& text);
    void OnTreeSelectionChanged(NMTREEVIEWW* pNmtv);
    void ShowInsertCodeDialog();
    void OnInsertCodeBlock();
    void OnInsertImageFromFile();

    common::CodeLanguage LoadLastCodeLanguage();
    void SaveLastCodeLanguage(common::CodeLanguage lang);

    bool OpenNotebook(const std::wstring& filePath, const std::string& password = "", bool createWelcomeIfEmpty = false);
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

    // 目录树搜索相关操作
    void ShowNodeSearch(bool show);
    void FilterTreeNodes(const std::wstring& keyword);

    // 大纲目录相关操作
    void ToggleOutlinePane();
    void ShowOutlinePane(bool show);
    void UpdateOutline();
    void SyncOutlineSelection();

    // 窗口置顶相关操作
    void ToggleAlwaysOnTop();
    void SetAlwaysOnTop(bool enable);

    // 多笔记本库相关操作
    void UpdateVaultBarUI();
    void OnNewVault();
    void OnOpenExternalVault();
    void OnManageVaults();

    HWND m_hVaultBar = nullptr;
    storage::VaultManager m_vaultManager;

    Toolbar m_toolbar;
    Splitter m_splitter;
    TreeView m_treeView;
    RichEditView m_richEditView;
    FindReplaceDialog m_findReplaceDialog;
    SearchPane m_searchPane;
    NodeSearchBar m_nodeSearchBar;
    Splitter m_searchSplitter;
    OutlinePane m_outlinePane;
    Splitter m_outlineSplitter;
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
    int m_outlineWidth = 220; // 默认右侧大纲面板宽度
    bool m_isSearchPaneVisible = false; // 全库搜索窗格是否显示
    bool m_isNodeSearchVisible = false; // 目录树节点搜索栏是否显示
    bool m_isOutlineVisible = false;    // 大纲面板是否显示
    bool m_isFilteringTree = false;     // 是否正在执行树节点过滤
    bool m_isUpdatingOutline = false;   // 是否正在更新大纲
    bool m_isAlwaysOnTop = false;       // 窗口是否置顶

    common::CodeLanguage m_lastCodeLanguage = common::CodeLanguage::PlainText;
};

} // namespace anynote::ui
