#pragma once

#include "common/Window.h"
#include <windows.h>
#include <commctrl.h>
#include <string>
#include <functional>

namespace anynote::ui {

class RichEditView;

enum class FindTabMode {
    Find = 0,
    Replace = 1,
    Mark = 2
};

class FindReplaceDialog : public common::Window {
public:
    explicit FindReplaceDialog(RichEditView& richEditView);
    ~FindReplaceDialog() override;

    bool Initialize(HWND hParent);
    void ShowTab(FindTabMode mode);

    void SetStatusText(const std::wstring& text);

    void FindNext(bool forward = true);
    void CountMatches();
    void Replace();
    void ReplaceAll();
    void MarkAll();
    void ClearMarks();

    static const wchar_t* GetClassName() { return L"AnyNoteFindReplaceDialog"; }
    static void RegisterClassIfNeeded(HINSTANCE hInstance);

protected:
    LRESULT HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam) override;

private:
    void LayoutControls();
    void SwitchTab(FindTabMode mode);
    std::wstring GetFindText() const;
    std::wstring GetReplaceText() const;

    RichEditView& m_richEditView;
    FindTabMode m_currentTab = FindTabMode::Find;

    HFONT m_hFont = nullptr;
    HFONT m_hBoldFont = nullptr;
    HBRUSH m_hBgBrush = nullptr;

    HWND m_hTab = nullptr;

    // 通用输入与选项
    HWND m_hLabelFind = nullptr;
    HWND m_hEditFind = nullptr;
    HWND m_hLabelReplace = nullptr;
    HWND m_hEditReplace = nullptr;
    HWND m_hChkMatchCase = nullptr;
    HWND m_hChkWholeWord = nullptr;
    HWND m_hChkWrapAround = nullptr;
    HWND m_hStatus = nullptr;

    // 查找页特定
    HWND m_hGroupDirection = nullptr;
    HWND m_hRadioUp = nullptr;
    HWND m_hRadioDown = nullptr;

    // 动作按钮
    HWND m_hBtnFindNext = nullptr;
    HWND m_hBtnFindPrev = nullptr;
    HWND m_hBtnCount = nullptr;
    HWND m_hBtnReplace = nullptr;
    HWND m_hBtnReplaceAll = nullptr;
    HWND m_hBtnMarkAll = nullptr;
    HWND m_hBtnClearMarks = nullptr;
    HWND m_hBtnClose = nullptr;

    bool m_hasLastPos = false;
    POINT m_lastPos = { 0, 0 };

    static LRESULT CALLBACK EditSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);
};

} // namespace anynote::ui
