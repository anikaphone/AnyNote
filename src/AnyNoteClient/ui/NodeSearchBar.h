#pragma once

#include "common/Window.h"
#include <windows.h>
#include <commctrl.h>
#include <string>
#include <functional>

namespace anynote::ui {

class NodeSearchBar : public common::Window {
public:
    using FilterChangedCallback = std::function<void(const std::wstring& keyword)>;
    using EnterPressedCallback = std::function<void()>;
    using CloseCallback = std::function<void()>;

    NodeSearchBar();
    ~NodeSearchBar() override;

    bool Initialize(HWND hParent);

    void ShowBar(bool show);
    bool IsBarVisible() const noexcept { return m_isVisible; }

    void FocusSearchBox();
    void SetMatchCount(int count, bool hasKeyword);
    std::wstring GetKeyword() const;
    void ClearKeyword();

    void SetBounds(int x, int y, int width, int height, bool repaint = true);

    void SetOnFilterChanged(FilterChangedCallback cb) { m_onFilterChanged = std::move(cb); }
    void SetOnEnterPressed(EnterPressedCallback cb) { m_onEnterPressed = std::move(cb); }
    void SetOnClose(CloseCallback cb) { m_onClose = std::move(cb); }

    static const wchar_t* GetClassName() { return L"AnyNoteNodeSearchBar"; }
    static void RegisterClassIfNeeded(HINSTANCE hInstance);

protected:
    LRESULT HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam) override;

private:
    void LayoutControls(int width, int height);

    FilterChangedCallback m_onFilterChanged;
    EnterPressedCallback m_onEnterPressed;
    CloseCallback m_onClose;

    bool m_isVisible = false;
    HFONT m_hFont = nullptr;
    HFONT m_hCountFont = nullptr;
    HBRUSH m_hBgBrush = nullptr;

    HWND m_hEdit = nullptr;
    HWND m_hStaticCount = nullptr;
    HWND m_hBtnClose = nullptr;

    int m_lastMatchCount = -1;
    bool m_hasKeyword = false;

    static LRESULT CALLBACK EditSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);
    static LRESULT CALLBACK CloseBtnSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);
};

} // namespace anynote::ui
