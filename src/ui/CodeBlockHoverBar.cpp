#include "CodeBlockHoverBar.h"
#include "RichEditView.h"
#include "resource.h"
#include <windowsx.h>
#include <algorithm>

namespace anynote::ui {

namespace {

constexpr UINT_PTR TIMER_COPIED_RESET = 1001;

} // namespace

CodeBlockHoverBar::~CodeBlockHoverBar() {
    if (m_hWnd && IsWindow(m_hWnd)) {
        DestroyWindow(m_hWnd);
        m_hWnd = nullptr;
    }
}

bool CodeBlockHoverBar::Initialize(HWND hParent) {
    m_hParent = hParent;
    HINSTANCE hInstance = GetModuleHandleW(nullptr);

    if (!s_classRegistered) {
        WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
        wc.lpfnWndProc = WndProc;
        wc.hInstance = hInstance;
        wc.lpszClassName = L"AnyNoteCodeBlockHoverBar";
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        RegisterClassExW(&wc);
        s_classRegistered = true;
    }

    m_hWnd = CreateWindowExW(
        0,
        L"AnyNoteCodeBlockHoverBar",
        L"",
        WS_CHILD | WS_CLIPSIBLINGS,
        0, 0, 10, 10,
        hParent,
        nullptr,
        hInstance,
        this
    );

    return m_hWnd != nullptr;
}

LRESULT CALLBACK CodeBlockHoverBar::WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    auto* pThis = reinterpret_cast<CodeBlockHoverBar*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    if (uMsg == WM_NCCREATE) {
        auto* pCreate = reinterpret_cast<CREATESTRUCTW*>(lParam);
        pThis = reinterpret_cast<CodeBlockHoverBar*>(pCreate->lpCreateParams);
        if (pThis) {
            pThis->m_hWnd = hWnd;
            SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
        }
    }
    if (pThis) {
        return pThis->HandleMessage(uMsg, wParam, lParam);
    }
    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

int CodeBlockHoverBar::CalculateWidth(UINT dpi) const {
    const wchar_t* langName = common::GetLanguageShortName(m_currentLang);
    size_t len = wcslen(langName);
    int langWidth = MulDiv(static_cast<int>(len * 8 + 26), dpi, 96);
    langWidth = std::max(langWidth, MulDiv(58, dpi, 96));
    int copyWidth = MulDiv(56, dpi, 96);
    return langWidth + copyWidth;
}

void CodeBlockHoverBar::AttachToCodeBlock(const CodeBlockInfo& info, const RECT& rcBlockInParent) {
    const bool targetChanged = m_activeTableStart != info.tableStart;
    const bool languageChanged = m_currentLang != info.currentLang;
    if (targetChanged) {
        ResetCopiedState();
        m_hoverLang = false;
        m_hoverCopy = false;
    }
    m_activeTableStart = info.tableStart;
    m_currentLang = info.currentLang;
    UpdatePosition(rcBlockInParent);
    if (targetChanged || languageChanged) {
        InvalidateRect(m_hWnd, nullptr, FALSE);
    }
}

void CodeBlockHoverBar::UpdatePosition(const RECT& rcBlockInParent) {
    if (!m_hWnd || !m_hParent) return;

    UINT dpi = GetDpiForWindow(m_hWnd);
    if (dpi == 0) dpi = 96;

    int barW = CalculateWidth(dpi);
    int barH = MulDiv(24, dpi, 96);

    int marginX = MulDiv(8, dpi, 96);
    int marginY = MulDiv(4, dpi, 96);

    int barX = rcBlockInParent.right - barW - marginX;
    int barY = rcBlockInParent.top + marginY;

    // 视口上方吸附：若代码块头部滚出视口，悬浮条固定在可视区域顶部
    int minViewportY = MulDiv(4, dpi, 96);
    if (barY < minViewportY) {
        barY = minViewportY;
    }

    // 保证悬浮条底边不超出代码块底边
    if (barY + barH > rcBlockInParent.bottom - marginY) {
        if (rcBlockInParent.bottom - rcBlockInParent.top >= barH) {
            barY = rcBlockInParent.bottom - barH - marginY;
        }
    }

    RECT current;
    GetWindowRect(m_hWnd, &current);
    MapWindowPoints(HWND_DESKTOP, m_hParent, reinterpret_cast<POINT*>(&current), 2);
    const bool sizeChanged = current.right - current.left != barW ||
        current.bottom - current.top != barH;
    const bool positionChanged = current.left != barX || current.top != barY;
    if (!sizeChanged && !positionChanged && IsWindowVisible(m_hWnd)) return;

    UINT flags = SWP_SHOWWINDOW | SWP_NOACTIVATE;
    if (!sizeChanged) flags |= SWP_NOSIZE;
    if (!positionChanged) flags |= SWP_NOMOVE;
    SetWindowPos(
        m_hWnd,
        HWND_TOP,
        barX, barY, barW, barH,
        flags
    );

    // 设置圆角窗口区域剪裁，彻底剔除 4 个黑色外角
    if (sizeChanged) {
        int corner = MulDiv(5, dpi, 96);
        HRGN hRgn = CreateRoundRectRgn(0, 0, barW + 1, barH + 1, corner * 2, corner * 2);
        if (hRgn && !SetWindowRgn(m_hWnd, hRgn, TRUE)) {
            DeleteObject(hRgn);
        }
    }

    InvalidateRect(m_hWnd, nullptr, FALSE);
}

void CodeBlockHoverBar::Hide() {
    if (m_hWnd && IsWindowVisible(m_hWnd)) {
        ShowWindow(m_hWnd, SW_HIDE);
    }
    m_activeTableStart = -1;
    m_hoverLang = false;
    m_hoverCopy = false;
}

bool CodeBlockHoverBar::IsVisible() const {
    return m_hWnd && IsWindowVisible(m_hWnd);
}

bool CodeBlockHoverBar::IsMouseOver() const {
    if (!m_hWnd || !IsWindowVisible(m_hWnd)) return false;
    POINT pt;
    GetCursorPos(&pt);
    RECT rc;
    GetWindowRect(m_hWnd, &rc);
    return PtInRect(&rc, pt) != FALSE;
}

void CodeBlockHoverBar::ResetCopiedState() {
    if (m_isCopied) {
        m_isCopied = false;
        KillTimer(m_hWnd, TIMER_COPIED_RESET);
        if (m_hWnd && IsWindow(m_hWnd)) {
            InvalidateRect(m_hWnd, nullptr, FALSE);
        }
    }
}

LRESULT CodeBlockHoverBar::HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(m_hWnd, &ps);
        Paint(hdc);
        EndPaint(m_hWnd, &ps);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1; // 双缓冲接管背景擦除，彻底杜绝闪烁

    case WM_SETCURSOR: {
        SetCursor(LoadCursorW(nullptr, IDC_HAND));
        return TRUE;
    }

    case WM_MOUSEMOVE: {
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        OnMouseMove(x, y);

        TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT) };
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = m_hWnd;
        TrackMouseEvent(&tme);
        return 0;
    }

    case WM_MOUSELEAVE: {
        OnMouseLeave();
        return 0;
    }

    case WM_LBUTTONUP: {
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        OnLButtonUp(x, y);
        return 0;
    }

    case WM_TIMER: {
        if (wParam == TIMER_COPIED_RESET) {
            ResetCopiedState();
            return 0;
        }
        break;
    }

    case WM_DESTROY: {
        KillTimer(m_hWnd, TIMER_COPIED_RESET);
        break;
    }
    }

    return DefWindowProcW(m_hWnd, uMsg, wParam, lParam);
}

void CodeBlockHoverBar::OnMouseMove(int x, int /*y*/) {
    RECT rc;
    GetClientRect(m_hWnd, &rc);
    UINT dpi = GetDpiForWindow(m_hWnd);
    if (dpi == 0) dpi = 96;

    int copyWidth = MulDiv(56, dpi, 96);
    int sepX = rc.right - copyWidth;

    bool newHoverLang = (x < sepX);
    bool newHoverCopy = (x >= sepX);

    if (newHoverLang != m_hoverLang || newHoverCopy != m_hoverCopy) {
        m_hoverLang = newHoverLang;
        m_hoverCopy = newHoverCopy;
        InvalidateRect(m_hWnd, nullptr, FALSE);
    }
}

void CodeBlockHoverBar::OnMouseLeave() {
    if (m_hoverLang || m_hoverCopy) {
        m_hoverLang = false;
        m_hoverCopy = false;
        InvalidateRect(m_hWnd, nullptr, FALSE);
    }
}

void CodeBlockHoverBar::OnLButtonUp(int x, int /*y*/) {
    RECT rc;
    GetClientRect(m_hWnd, &rc);
    UINT dpi = GetDpiForWindow(m_hWnd);
    if (dpi == 0) dpi = 96;

    int copyWidth = MulDiv(56, dpi, 96);
    int sepX = rc.right - copyWidth;

    if (x < sepX) {
        // 点击语言切换区域 -> 弹出语言下拉菜单
        HMENU hMenu = CreatePopupMenu();
        if (hMenu) {
            const auto& languages = common::GetSupportedLanguages();
            for (size_t i = 0; i < languages.size(); ++i) {
                UINT flags = MF_STRING;
                if (languages[i].lang == m_currentLang) {
                    flags |= MF_CHECKED;
                }
                AppendMenuW(hMenu, flags, ID_CODE_LANG_BASE + i, languages[i].name);
            }

            RECT rcWindow;
            GetWindowRect(m_hWnd, &rcWindow);

            SetForegroundWindow(m_hWnd);
            m_languageMenuOpen = true;
            int selected = TrackPopupMenu(
                hMenu,
                TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
                rcWindow.left, rcWindow.bottom + 2,
                0, m_hWnd, nullptr
            );
            m_languageMenuOpen = false;
            DestroyMenu(hMenu);

            if (selected >= ID_CODE_LANG_BASE && selected < ID_CODE_LANG_BASE + static_cast<int>(languages.size())) {
                size_t idx = static_cast<size_t>(selected - ID_CODE_LANG_BASE);
                common::CodeLanguage newLang = languages[idx].lang;
                if (newLang != m_currentLang) {
                    m_currentLang = newLang;
                    if (m_onLangChanged) {
                        m_onLangChanged(newLang);
                    }
                    InvalidateRect(m_hWnd, nullptr, FALSE);
                }
            }
        }
    } else {
        // 点击复制区域 -> 触发复制代码并展示反馈
        if (m_onCopyClicked) {
            m_onCopyClicked();
        }
        m_isCopied = true;
        SetTimer(m_hWnd, TIMER_COPIED_RESET, 1500, nullptr);
        InvalidateRect(m_hWnd, nullptr, FALSE);
    }
}

void CodeBlockHoverBar::Paint(HDC hdc) {
    RECT rc;
    GetClientRect(m_hWnd, &rc);
    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;
    if (width <= 0 || height <= 0) return;

    UINT dpi = GetDpiForWindow(m_hWnd);
    if (dpi == 0) dpi = 96;

    // 内存双缓冲
    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBmp = CreateCompatibleBitmap(hdc, width, height);
    HGDIOBJ oldBmp = SelectObject(memDC, memBmp);

    // 首先填充代码卡片同款背景底色，彻底杜绝外围黑色像素残留
    HBRUSH cardBgBrush = CreateSolidBrush(RGB(246, 248, 250));
    FillRect(memDC, &rc, cardBgBrush);
    DeleteObject(cardBgBrush);

    // 1. 胶囊底板背景与微阴影细边框 (Fluent 纯白卡片)
    HBRUSH bgBrush = CreateSolidBrush(RGB(255, 255, 255));
    HPEN borderPen = CreatePen(PS_SOLID, 1, RGB(218, 222, 228));
    HGDIOBJ oldBrush = SelectObject(memDC, bgBrush);
    HGDIOBJ oldPen = SelectObject(memDC, borderPen);

    int corner = MulDiv(5, dpi, 96);
    RoundRect(memDC, 0, 0, width, height, corner * 2, corner * 2);

    int copyWidth = MulDiv(56, dpi, 96);
    int sepX = width - copyWidth;

    // 2. 语言按钮悬停高亮
    if (m_hoverLang) {
        RECT rcLangBg = { 1, 1, sepX - 1, height - 1 };
        HBRUSH hoverBrush = CreateSolidBrush(RGB(242, 244, 247));
        FillRect(memDC, &rcLangBg, hoverBrush);
        DeleteObject(hoverBrush);
    }

    // 3. 复制按钮悬停高亮
    if (m_hoverCopy) {
        RECT rcCopyBg = { sepX + 1, 1, width - 1, height - 1 };
        COLORREF hoverColor = m_isCopied ? RGB(235, 248, 240) : RGB(240, 245, 255);
        HBRUSH hoverBrush = CreateSolidBrush(hoverColor);
        FillRect(memDC, &rcCopyBg, hoverBrush);
        DeleteObject(hoverBrush);
    }

    // 4. 中间竖直分隔细线
    HPEN sepPen = CreatePen(PS_SOLID, 1, RGB(225, 228, 232));
    SelectObject(memDC, sepPen);
    int padY = MulDiv(5, dpi, 96);
    MoveToEx(memDC, sepX, padY, nullptr);
    LineTo(memDC, sepX, height - padY);
    DeleteObject(sepPen);

    // 5. 文字渲染
    HFONT hFont = CreateFontW(
        -MulDiv(9, dpi, 72), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
    );
    HGDIOBJ oldFont = SelectObject(memDC, hFont);
    SetBkMode(memDC, TRANSPARENT);

    // 左侧语言名称 + ▾
    std::wstring langStr = std::wstring(common::GetLanguageShortName(m_currentLang)) + L" \u25be";
    RECT rcLangText = { MulDiv(8, dpi, 96), 0, sepX - MulDiv(4, dpi, 96), height };
    SetTextColor(memDC, RGB(66, 74, 83));
    DrawTextW(memDC, langStr.c_str(), -1, &rcLangText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    // 右侧复制文字 / 已复制反馈
    RECT rcCopyText = { sepX, 0, width, height };
    if (m_isCopied) {
        SetTextColor(memDC, RGB(26, 127, 55)); // 优雅成功绿
        DrawTextW(memDC, L"\u2714 \u5df2\u590d\u5236", -1, &rcCopyText, DT_CENTER | DT_VCENTER | DT_SINGLELINE); // ✔ 已复制
    } else {
        SetTextColor(memDC, RGB(9, 105, 218)); // 经典 Fluent 品蓝
        DrawTextW(memDC, L"\u29c9 \u590d\u5236", -1, &rcCopyText, DT_CENTER | DT_VCENTER | DT_SINGLELINE); // ⧉ 复制
    }

    // 拷回目标 DC
    BitBlt(hdc, 0, 0, width, height, memDC, 0, 0, SRCCOPY);

    // 释放资源
    SelectObject(memDC, oldFont);
    DeleteObject(hFont);
    SelectObject(memDC, oldBrush);
    DeleteObject(bgBrush);
    SelectObject(memDC, oldPen);
    DeleteObject(borderPen);
    SelectObject(memDC, oldBmp);
    DeleteObject(memBmp);
    DeleteDC(memDC);
}

} // namespace anynote::ui
