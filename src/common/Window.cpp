#include "Window.h"

namespace anynote::common {

Window::Window() = default;

Window::~Window() {
    if (m_hWnd && IsWindow(m_hWnd)) {
        DestroyWindow(m_hWnd);
        m_hWnd = nullptr;
    }
}

bool Window::Create(
    const std::wstring& className,
    const std::wstring& windowTitle,
    DWORD dwStyle,
    DWORD dwExStyle,
    int x,
    int y,
    int width,
    int height,
    HWND hWndParent,
    HMENU hMenu,
    HINSTANCE hInstance
) {
    if (m_hWnd) {
        return false;
    }

    if (!hInstance) {
        hInstance = GetModuleHandleW(nullptr);
    }

    // 将 this 作为 lpParam 传递给 CreateWindowExW
    HWND hWnd = CreateWindowExW(
        dwExStyle,
        className.c_str(),
        windowTitle.c_str(),
        dwStyle,
        x, y, width, height,
        hWndParent,
        hMenu,
        hInstance,
        this
    );

    return (hWnd != nullptr);
}

void Window::Show(int nCmdShow) {
    if (m_hWnd) {
        ShowWindow(m_hWnd, nCmdShow);
    }
}

void Window::Update() {
    if (m_hWnd) {
        UpdateWindow(m_hWnd);
    }
}

void Window::SetBounds(int x, int y, int width, int height, bool repaint) {
    if (m_hWnd) {
        MoveWindow(m_hWnd, x, y, width, height, repaint ? TRUE : FALSE);
    }
}

LRESULT CALLBACK Window::StaticWindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    Window* pThis = nullptr;

    if (uMsg == WM_NCCREATE) {
        auto* pCreate = reinterpret_cast<CREATESTRUCTW*>(lParam);
        pThis = reinterpret_cast<Window*>(pCreate->lpCreateParams);
        if (pThis) {
            pThis->m_hWnd = hWnd;
            SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
        }
    } else {
        pThis = reinterpret_cast<Window*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    }

    if (pThis) {
        LRESULT result = pThis->HandleMessage(uMsg, wParam, lParam);
        if (uMsg == WM_NCDESTROY) {
            SetWindowLongPtrW(hWnd, GWLP_USERDATA, 0);
            pThis->m_hWnd = nullptr;
        }
        return result;
    }

    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

LRESULT Window::HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam) {
    return DefWindowProcW(m_hWnd, uMsg, wParam, lParam);
}

} // namespace anynote::common
