#pragma once

#include "common/Window.h"
#include "storage/NoteModel.h"
#include <windows.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <functional>

namespace anynote::storage {
class NoteRepository;
}

namespace anynote::ui {

class SearchPane : public common::Window {
public:
    using ResultSelectedCallback = std::function<void(const storage::SearchResult& result)>;
    using CloseCallback = std::function<void()>;

    SearchPane();
    ~SearchPane() override;

    bool Initialize(HWND hParent, storage::NoteRepository* repo);

    void SetRepository(storage::NoteRepository* repo) { m_repo = repo; }
    void SetOnResultSelected(ResultSelectedCallback cb) { m_onResultSelected = std::move(cb); }
    void SetOnClose(CloseCallback cb) { m_onClose = std::move(cb); }

    void ShowPane(bool show);
    bool IsPaneVisible() const noexcept { return m_isVisible; }

    void FocusSearchBox();
    void PerformSearch();
    std::wstring GetSearchKeyword() const;
    bool IsMatchCase() const;

    static const wchar_t* GetClassName() { return L"AnyNoteSearchPane"; }
    static void RegisterClassIfNeeded(HINSTANCE hInstance);

protected:
    LRESULT HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam) override;

private:
    void LayoutControls(int width, int height);

    storage::NoteRepository* m_repo = nullptr;
    ResultSelectedCallback m_onResultSelected;
    CloseCallback m_onClose;
    std::vector<storage::SearchResult> m_results;

    bool m_isVisible = false;
    HFONT m_hFont = nullptr;
    HBRUSH m_hBgBrush = nullptr;

    HWND m_hLabel = nullptr;
    HWND m_hEdit = nullptr;
    HWND m_hBtnSearch = nullptr;
    HWND m_hChkCase = nullptr;
    HWND m_hChkContent = nullptr;
    HWND m_hStaticCount = nullptr;
    HWND m_hBtnClose = nullptr;
    HWND m_hListResults = nullptr;

    static LRESULT CALLBACK EditSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);
};

} // namespace anynote::ui
