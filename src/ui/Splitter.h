#pragma once

#include "common/Window.h"
#include <functional>

namespace anynote::ui {

constexpr UINT WM_SPLITTER_MOVED = WM_APP + 10;

class Splitter : public common::Window {
public:
    Splitter();
    ~Splitter() override {
        if (m_hBgBrush) {
            DeleteObject(m_hBgBrush);
            m_hBgBrush = nullptr;
        }
    }

    bool Initialize(HWND hParent, int x, int y, int size, bool isHorizontal = false);

    static const wchar_t* GetClassName() { return L"AnyNoteSplitter"; }
    static void RegisterClassIfNeeded(HINSTANCE hInstance);

protected:
    LRESULT HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam) override;

private:
    bool m_isDragging = false;
    bool m_isHorizontal = false;
    HCURSOR m_hCursor = nullptr;
    HBRUSH m_hBgBrush = nullptr;
};

} // namespace anynote::ui
