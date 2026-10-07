#pragma once

#include <windows.h>
#include <commctrl.h>

namespace anynote::ui {

class Toolbar {
public:
    Toolbar() = default;
    ~Toolbar() = default;

    bool Initialize(HWND hParent, UINT controlId);
    HWND GetHwnd() const noexcept { return m_hWnd; }

    void SetBounds(int x, int y, int width, int height);
    int GetPreferredHeight() const {
        UINT dpi = m_hWnd ? GetDpiForWindow(m_hWnd) : 96;
        return MulDiv(36, dpi, 96);
    }

private:
    HWND m_hWnd = nullptr;
};

} // namespace anynote::ui
