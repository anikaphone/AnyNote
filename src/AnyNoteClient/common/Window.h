#pragma once

#include <windows.h>
#include <string>

namespace anynote::common {

class Window {
public:
    Window();
    virtual ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    HWND GetHwnd() const noexcept { return m_hWnd; }

    bool Create(
        const std::wstring& className,
        const std::wstring& windowTitle,
        DWORD dwStyle,
        DWORD dwExStyle = 0,
        int x = CW_USEDEFAULT,
        int y = CW_USEDEFAULT,
        int width = CW_USEDEFAULT,
        int height = CW_USEDEFAULT,
        HWND hWndParent = nullptr,
        HMENU hMenu = nullptr,
        HINSTANCE hInstance = nullptr
    );

    void Show(int nCmdShow = SW_SHOW);
    void Update();
    void SetBounds(int x, int y, int width, int height, bool repaint = true);

protected:
    virtual LRESULT HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam);

    HWND m_hWnd = nullptr;

private:
    static LRESULT CALLBACK StaticWindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
};

} // namespace anynote::common
