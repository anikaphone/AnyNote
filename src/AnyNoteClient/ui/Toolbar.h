#pragma once

#include <windows.h>
#include <commctrl.h>
#include <string>

namespace anynote::ui {

class Toolbar {
public:
    Toolbar() = default;
    ~Toolbar();

    bool Initialize(HWND hParent, UINT controlId);
    HWND GetHwnd() const noexcept { return m_hWnd; }
    HWND GetHeadingComboHwnd() const noexcept { return m_hHeadingCombo; }

    void SetBounds(int x, int y, int width, int height);
    int GetPreferredHeight() const;

    void SetSelectedHeadingIndex(int index);
    int GetSelectedHeadingIndex() const;

    void SetOutlineButtonChecked(bool checked);
    void SetRealtimeStatus(bool checked, const std::wstring& text);

private:
    HWND m_hWnd = nullptr;
    HWND m_hParent = nullptr;
    HWND m_hHeadingCombo = nullptr;
    HWND m_hSyncStatus = nullptr;
    HFONT m_hFont = nullptr;
    UINT m_fontDpi = 0;
    HIMAGELIST m_hImageList = nullptr;
    UINT m_imageDpi = 0;

    void UpdateFontForDpi(UINT dpi);
    void UpdateImageListForDpi(UINT dpi);
};

} // namespace anynote::ui
