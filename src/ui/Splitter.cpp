#include "Splitter.h"

namespace anynote::ui {

static bool s_isRegistered = false;

Splitter::Splitter() {
    m_hCursor = LoadCursorW(nullptr, IDC_SIZEWE);
    m_hBgBrush = CreateSolidBrush(RGB(222, 226, 230));
}

void Splitter::RegisterClassIfNeeded(HINSTANCE hInstance) {
    if (s_isRegistered) return;

    WNDCLASSEXW wc = {sizeof(WNDCLASSEXW)};
    wc.lpfnWndProc = [](HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) -> LRESULT {
        auto* pThis = reinterpret_cast<Splitter*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
        if (uMsg == WM_NCCREATE) {
            auto* pCreate = reinterpret_cast<CREATESTRUCTW*>(lParam);
            pThis = reinterpret_cast<Splitter*>(pCreate->lpCreateParams);
            if (pThis) {
                pThis->m_hWnd = hWnd;
                SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
            }
        }
        if (pThis) {
            return pThis->HandleMessage(uMsg, wParam, lParam);
        }
        return DefWindowProcW(hWnd, uMsg, wParam, lParam);
    };
    wc.hInstance = hInstance;
    wc.lpszClassName = GetClassName();
    wc.hCursor = LoadCursorW(nullptr, IDC_SIZEWE);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);

    RegisterClassExW(&wc);
    s_isRegistered = true;
}

bool Splitter::Initialize(HWND hParent, int x, int y, int size, bool isHorizontal) {
    m_isHorizontal = isHorizontal;
    m_hCursor = LoadCursorW(nullptr, isHorizontal ? IDC_SIZENS : IDC_SIZEWE);
    RegisterClassIfNeeded(GetModuleHandleW(nullptr));
    int width = isHorizontal ? size : 5;
    int height = isHorizontal ? 5 : size;
    return Create(
        GetClassName(),
        L"",
        WS_CHILD | WS_VISIBLE,
        0,
        x, y, width, height,
        hParent,
        nullptr,
        GetModuleHandleW(nullptr)
    );
}

LRESULT Splitter::HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_SETCURSOR:
        if (m_hCursor) {
            SetCursor(m_hCursor);
            return TRUE;
        }
        break;

    case WM_LBUTTONDOWN:
        m_isDragging = true;
        SetCapture(m_hWnd);
        return 0;

    case WM_MOUSEMOVE:
        if (m_isDragging) {
            POINT pt;
            GetCursorPos(&pt);
            HWND hParent = GetParent(m_hWnd);
            if (hParent) {
                ScreenToClient(hParent, &pt);
                if (m_isHorizontal) {
                    SendMessageW(hParent, WM_SPLITTER_MOVED, static_cast<WPARAM>(pt.y), 1);
                } else {
                    SendMessageW(hParent, WM_SPLITTER_MOVED, static_cast<WPARAM>(pt.x), 0);
                }
            }
        }
        return 0;

    case WM_LBUTTONUP:
        if (m_isDragging) {
            m_isDragging = false;
            ReleaseCapture();
        }
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(m_hWnd, &ps);
        RECT rc;
        GetClientRect(m_hWnd, &rc);
        FillRect(hdc, &rc, m_hBgBrush);
        EndPaint(m_hWnd, &ps);
        return 0;
    }

    case WM_DESTROY:
        if (m_hBgBrush) {
            DeleteObject(m_hBgBrush);
            m_hBgBrush = nullptr;
        }
        break;
    }

    return common::Window::HandleMessage(uMsg, wParam, lParam);
}

} // namespace anynote::ui
