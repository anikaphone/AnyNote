#include "Toolbar.h"
#include "resource.h"
#include <ole2.h>
#include <gdiplus.h>
#include <uxtheme.h>
#include <vector>
#include <algorithm>

namespace anynote::ui {

namespace {

constexpr int kToolbarIconSize = 18;
constexpr int kToolbarButtonWidth = 28;
constexpr int kToolbarButtonHeight = 26;

LRESULT CALLBACK ToolbarSubclassProc(HWND hWnd, UINT message, WPARAM wParam,
    LPARAM lParam, UINT_PTR subclassId, DWORD_PTR) {
    if (message == WM_ERASEBKGND) {
        RECT rc = {};
        GetClientRect(hWnd, &rc);
        FillRect(reinterpret_cast<HDC>(wParam), &rc,
            static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
        return 1;
    }
    if (message == WM_PAINT) {
        LRESULT result = DefSubclassProc(hWnd, message, wParam, lParam);
        RECT rc = {};
        GetClientRect(hWnd, &rc);
        rc.top = rc.bottom - 1;
        HDC hdc = GetDC(hWnd);
        HBRUSH line = CreateSolidBrush(RGB(226, 230, 234));
        FillRect(hdc, &rc, line);
        DeleteObject(line);
        ReleaseDC(hWnd, hdc);
        return result;
    }
    if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(hWnd, ToolbarSubclassProc, subclassId);
    }
    return DefSubclassProc(hWnd, message, wParam, lParam);
}

void RenderFluentIcon(Gdiplus::Graphics& g, int iconIndex, int size) {
    using namespace Gdiplus;

    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(PixelOffsetModeHalf);
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);

    float scale = static_cast<float>(size) / 16.0f;
    Color colorMain(255, 36, 41, 47);   // Fluent 现代深色线框
    Color colorSub(255, 90, 96, 104);   // 次级线条颜色
    SolidBrush mainBrush(colorMain);
    Pen mainPen(colorMain, 1.4f * scale);
    mainPen.SetStartCap(LineCapRound);
    mainPen.SetEndCap(LineCapRound);
    mainPen.SetLineJoin(LineJoinRound);

    Pen subPen(colorSub, 1.2f * scale);
    subPen.SetStartCap(LineCapRound);
    subPen.SetEndCap(LineCapRound);

    FontFamily fontFamily(L"Segoe UI");
    FontFamily fallbackFamily(L"Arial");
    const FontFamily* pFamily = fontFamily.IsAvailable() ? &fontFamily : &fallbackFamily;

    switch (iconIndex) {
    case 0: { // 0: Bold
        Font font(pFamily, 11.5f * scale, FontStyleBold, UnitPixel);
        StringFormat fmt;
        fmt.SetAlignment(StringAlignmentCenter);
        fmt.SetLineAlignment(StringAlignmentCenter);
        RectF r(0.0f, -0.6f * scale, static_cast<float>(size), static_cast<float>(size));
        g.DrawString(L"B", 1, &font, r, &fmt, &mainBrush);
        break;
    }
    case 1: { // 1: Italic
        Font font(pFamily, 11.5f * scale, FontStyleItalic | FontStyleBold, UnitPixel);
        StringFormat fmt;
        fmt.SetAlignment(StringAlignmentCenter);
        fmt.SetLineAlignment(StringAlignmentCenter);
        RectF r(0.0f, -0.6f * scale, static_cast<float>(size), static_cast<float>(size));
        g.DrawString(L"I", 1, &font, r, &fmt, &mainBrush);
        break;
    }
    case 2: { // 2: Underline
        Font font(pFamily, 10.5f * scale, FontStyleBold, UnitPixel);
        StringFormat fmt;
        fmt.SetAlignment(StringAlignmentCenter);
        fmt.SetLineAlignment(StringAlignmentCenter);
        RectF r(0.0f, -1.8f * scale, static_cast<float>(size), static_cast<float>(size));
        g.DrawString(L"U", 1, &font, r, &fmt, &mainBrush);

        Pen linePen(colorMain, 1.5f * scale);
        linePen.SetStartCap(LineCapRound);
        linePen.SetEndCap(LineCapRound);
        float lineY = 14.0f * scale;
        g.DrawLine(&linePen, 2.5f * scale, lineY, 13.5f * scale, lineY);
        break;
    }
    case 3: { // 3: Code Block </>
        PointF leftPts[] = {
            { 4.8f * scale, 5.0f * scale },
            { 2.2f * scale, 8.0f * scale },
            { 4.8f * scale, 11.0f * scale }
        };
        g.DrawLines(&mainPen, leftPts, 3);

        g.DrawLine(&mainPen, 9.2f * scale, 4.0f * scale, 6.8f * scale, 12.0f * scale);

        PointF rightPts[] = {
            { 11.2f * scale, 5.0f * scale },
            { 13.8f * scale, 8.0f * scale },
            { 11.2f * scale, 11.0f * scale }
        };
        g.DrawLines(&mainPen, rightPts, 3);
        break;
    }
    case 4: { // 4: Bullet List
        float dotR = 1.15f * scale;
        float ys[] = { 4.0f * scale, 8.0f * scale, 12.0f * scale };
        for (float y : ys) {
            g.FillEllipse(&mainBrush, 2.8f * scale - dotR, y - dotR, dotR * 2.0f, dotR * 2.0f);
            g.DrawLine(&subPen, 5.8f * scale, y, 14.2f * scale, y);
        }
        break;
    }
    case 5: { // 5: Number List
        Font numFont(pFamily, 6.6f * scale, FontStyleBold, UnitPixel);
        StringFormat fmt;
        fmt.SetAlignment(StringAlignmentCenter);
        fmt.SetLineAlignment(StringAlignmentCenter);
        const wchar_t* nums[] = { L"1", L"2", L"3" };
        float ys[] = { 3.8f * scale, 8.0f * scale, 12.2f * scale };
        for (int i = 0; i < 3; ++i) {
            RectF numRect(0.2f * scale, ys[i] - 3.5f * scale, 5.2f * scale, 7.0f * scale);
            g.DrawString(nums[i], 1, &numFont, numRect, &fmt, &mainBrush);
            g.DrawLine(&subPen, 6.5f * scale, ys[i], 14.2f * scale, ys[i]);
        }
        break;
    }
    case 6: { // 6: Insert Image
        GraphicsPath path;
        float x = 1.8f * scale, y = 2.5f * scale, w = 12.4f * scale, h = 11.0f * scale, r = 1.5f * scale;
        path.AddArc(x, y, r * 2, r * 2, 180, 90);
        path.AddArc(x + w - r * 2, y, r * 2, r * 2, 270, 90);
        path.AddArc(x + w - r * 2, y + h - r * 2, r * 2, r * 2, 0, 90);
        path.AddArc(x, y + h - r * 2, r * 2, r * 2, 90, 90);
        path.CloseFigure();
        g.DrawPath(&mainPen, &path);

        float sunR = 1.1f * scale;
        g.FillEllipse(&mainBrush, 10.8f * scale - sunR, 5.5f * scale - sunR, sunR * 2.0f, sunR * 2.0f);

        PointF peak1[] = {
            { 2.8f * scale, 12.5f * scale },
            { 6.0f * scale, 7.8f * scale },
            { 9.5f * scale, 12.5f * scale }
        };
        g.DrawLines(&mainPen, peak1, 3);
        PointF peak2[] = {
            { 8.5f * scale, 12.5f * scale },
            { 11.2f * scale, 9.4f * scale },
            { 13.0f * scale, 12.5f * scale }
        };
        g.DrawLines(&mainPen, peak2, 3);
        break;
    }
    case 7: { // 7: Insert Table (Fluent 3x2 Grid)
        GraphicsPath path;
        float x = 2.0f * scale, y = 2.5f * scale, w = 12.0f * scale, h = 11.0f * scale, r = 1.2f * scale;
        path.AddArc(x, y, r * 2, r * 2, 180, 90);
        path.AddArc(x + w - r * 2, y, r * 2, r * 2, 270, 90);
        path.AddArc(x + w - r * 2, y + h - r * 2, r * 2, r * 2, 0, 90);
        path.AddArc(x, y + h - r * 2, r * 2, r * 2, 90, 90);
        path.CloseFigure();
        g.DrawPath(&mainPen, &path);

        // 表头水平分割线
        float splitY = 6.0f * scale;
        g.DrawLine(&mainPen, x, splitY, x + w, splitY);

        // 两条垂直列分割线
        float col1X = 6.0f * scale;
        float col2X = 10.0f * scale;
        g.DrawLine(&subPen, col1X, y, col1X, y + h);
        g.DrawLine(&subPen, col2X, y, col2X, y + h);
        break;
    }
    case 8: { // 8: Save
        PointF diskPts[] = {
            { 2.5f * scale, 2.5f * scale },
            { 11.5f * scale, 2.5f * scale },
            { 13.5f * scale, 4.5f * scale },
            { 13.5f * scale, 13.5f * scale },
            { 2.5f * scale, 13.5f * scale }
        };
        GraphicsPath diskPath;
        diskPath.AddPolygon(diskPts, 5);
        g.DrawPath(&mainPen, &diskPath);

        RectF shutter(5.0f * scale, 2.5f * scale, 5.5f * scale, 4.0f * scale);
        g.DrawRectangle(&mainPen, shutter);
        g.DrawLine(&mainPen, 6.8f * scale, 3.5f * scale, 6.8f * scale, 5.2f * scale);

        RectF label(4.5f * scale, 8.5f * scale, 7.0f * scale, 5.0f * scale);
        g.DrawRectangle(&mainPen, label);
        break;
    }
    case 9: { // 9: Outline / Table of Contents
        // H1 顶层标题 (左短横，右长横)
        g.DrawLine(&mainPen, 2.5f * scale, 3.5f * scale, 5.0f * scale, 3.5f * scale);
        g.DrawLine(&mainPen, 6.5f * scale, 3.5f * scale, 13.5f * scale, 3.5f * scale);
        // H2 二级标题 (缩进点与中长横)
        g.DrawLine(&subPen, 4.8f * scale, 7.5f * scale, 6.8f * scale, 7.5f * scale);
        g.DrawLine(&mainPen, 8.2f * scale, 7.5f * scale, 13.5f * scale, 7.5f * scale);
        // H3 三级标题 (深缩进点与短横)
        g.DrawLine(&subPen, 6.8f * scale, 11.5f * scale, 8.5f * scale, 11.5f * scale);
        g.DrawLine(&mainPen, 9.8f * scale, 11.5f * scale, 13.5f * scale, 11.5f * scale);
        break;
    }
    case 10: { // 10: Realtime Sync (Fluent 循环同步双箭头)
        RectF arcRect(2.8f * scale, 2.8f * scale, 10.4f * scale, 10.4f * scale);
        g.DrawArc(&mainPen, arcRect, 205.0f, 145.0f);
        PointF arrowTop[] = {
            { 10.6f * scale, 5.4f * scale },
            { 13.1f * scale, 7.8f * scale },
            { 14.4f * scale, 5.0f * scale }
        };
        g.DrawLines(&mainPen, arrowTop, 3);

        g.DrawArc(&mainPen, arcRect, 25.0f, 145.0f);
        PointF arrowBottom[] = {
            { 5.4f * scale, 10.6f * scale },
            { 2.9f * scale, 8.2f * scale },
            { 1.6f * scale, 11.0f * scale }
        };
        g.DrawLines(&mainPen, arrowBottom, 3);
        break;
    }
    }
}

HIMAGELIST CreateFluentToolbarImageList(UINT dpi) {
    int iconSize = MulDiv(kToolbarIconSize, dpi, 96);
    HIMAGELIST hImageList = ImageList_Create(iconSize, iconSize, ILC_COLOR32, 11, 0);
    if (!hImageList) return nullptr;

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = iconSize;
    bmi.bmiHeader.biHeight = -iconSize; // Top-down 32-bit DIB
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    for (int i = 0; i < 11; ++i) {
        void* pBits = nullptr;
        HBITMAP hBmp = CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &pBits, nullptr, 0);
        if (!hBmp) continue;

        {
            Gdiplus::Bitmap bitmap(iconSize, iconSize, iconSize * 4, PixelFormat32bppPARGB, static_cast<BYTE*>(pBits));
            Gdiplus::Graphics g(&bitmap);
            g.Clear(Gdiplus::Color(0, 0, 0, 0));
            RenderFluentIcon(g, i, iconSize);
            g.Flush();
        }

        ImageList_Add(hImageList, hBmp, nullptr);
        DeleteObject(hBmp);
    }

    return hImageList;
}

struct SyncStatusState {
    bool isHovered = false;
};

enum class SyncStateKind {
    Disabled,
    Syncing,
    Conflict,
    Normal
};

static SyncStateKind ClassifySyncState(const std::wstring& text) {
    if (text.find(L"已关闭") != std::wstring::npos || text.find(L"未开启") != std::wstring::npos) {
        return SyncStateKind::Disabled;
    }
    if (text.find(L"冲突") != std::wstring::npos || text.find(L"失败") != std::wstring::npos || text.find(L"暂停") != std::wstring::npos) {
        return SyncStateKind::Conflict;
    }
    if (text.find(L"正在") != std::wstring::npos || text.find(L"比较") != std::wstring::npos) {
        return SyncStateKind::Syncing;
    }
    return SyncStateKind::Normal;
}

static void AddRoundedRect(Gdiplus::GraphicsPath& path, Gdiplus::RectF bounds, float radius) {
    float diameter = radius * 2.0f;
    if (diameter > bounds.Width) diameter = bounds.Width;
    if (diameter > bounds.Height) diameter = bounds.Height;

    float right = bounds.X + bounds.Width;
    float bottom = bounds.Y + bounds.Height;

    path.AddArc(bounds.X, bounds.Y, diameter, diameter, 180.0f, 90.0f);
    path.AddArc(right - diameter, bounds.Y, diameter, diameter, 270.0f, 90.0f);
    path.AddArc(right - diameter, bottom - diameter, diameter, diameter, 0.0f, 90.0f);
    path.AddArc(bounds.X, bottom - diameter, diameter, diameter, 90.0f, 90.0f);
    path.CloseFigure();
}

static LRESULT CALLBACK SyncStatusSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
    UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    auto* pState = reinterpret_cast<SyncStatusState*>(dwRefData);

    switch (uMsg) {
    case WM_ERASEBKGND:
        return 1;

    case WM_SETTEXT: {
        LRESULT lr = DefSubclassProc(hWnd, uMsg, wParam, lParam);
        InvalidateRect(hWnd, nullptr, FALSE);
        return lr;
    }

    case WM_MOUSEMOVE: {
        if (pState && !pState->isHovered) {
            pState->isHovered = true;
            TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, hWnd, 0 };
            TrackMouseEvent(&tme);
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        break;
    }

    case WM_MOUSELEAVE: {
        if (pState && pState->isHovered) {
            pState->isHovered = false;
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        break;
    }

    case WM_SETCURSOR: {
        wchar_t buf[256] = {};
        GetWindowTextW(hWnd, buf, 256);
        auto stateKind = ClassifySyncState(buf);
        if (stateKind == SyncStateKind::Conflict) {
            SetCursor(LoadCursor(nullptr, IDC_HAND));
            return TRUE;
        }
        break;
    }

    case WM_LBUTTONDOWN:
        return 0;

    case WM_LBUTTONUP: {
        RECT rc;
        GetClientRect(hWnd, &rc);
        POINT pt = { static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam)) };
        if (PtInRect(&rc, pt)) {
            HWND hParent = GetParent(hWnd);
            if (hParent) {
                SendMessageW(hParent, WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(hWnd), STN_CLICKED), reinterpret_cast<LPARAM>(hWnd));
            }
        }
        return 0;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rc;
        GetClientRect(hWnd, &rc);
        int w = rc.right - rc.left;
        int h = rc.bottom - rc.top;

        if (w > 0 && h > 0) {
            HDC memDC = CreateCompatibleDC(hdc);
            HBITMAP memBmp = CreateCompatibleBitmap(hdc, w, h);
            HBITMAP oldBmp = static_cast<HBITMAP>(SelectObject(memDC, memBmp));

            // 背景使用与工具栏一致的纯白底色
            FillRect(memDC, &rc, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));

            wchar_t buf[256] = {};
            GetWindowTextW(hWnd, buf, 256);
            std::wstring text = buf;
            auto stateKind = ClassifySyncState(text);
            bool isHovered = pState ? pState->isHovered : false;

            UINT dpi = GetDpiForWindow(hWnd);
            int haloSize = MulDiv(10, dpi, 96);
            int dotSize = MulDiv(6, dpi, 96);
            int padX = MulDiv(10, dpi, 96);
            int dotGap = MulDiv(6, dpi, 96);

            Gdiplus::Color bgClr, borderClr, dotClr, haloClr;
            COLORREF textClr = RGB(45, 52, 60);

            switch (stateKind) {
            case SyncStateKind::Conflict:
                bgClr = isHovered ? Gdiplus::Color(255, 254, 235, 215) : Gdiplus::Color(255, 255, 244, 230);
                borderClr = isHovered ? Gdiplus::Color(255, 235, 140, 80) : Gdiplus::Color(255, 245, 170, 110);
                dotClr = Gdiplus::Color(255, 216, 59, 1);
                haloClr = Gdiplus::Color(65, 216, 59, 1);
                textClr = RGB(180, 50, 0);
                break;
            case SyncStateKind::Syncing:
                bgClr = isHovered ? Gdiplus::Color(255, 228, 240, 255) : Gdiplus::Color(255, 240, 246, 255);
                borderClr = isHovered ? Gdiplus::Color(255, 150, 195, 235) : Gdiplus::Color(255, 186, 216, 245);
                dotClr = Gdiplus::Color(255, 0, 120, 212);
                haloClr = Gdiplus::Color(65, 0, 120, 212);
                textClr = RGB(0, 102, 204);
                break;
            case SyncStateKind::Disabled:
                bgClr = isHovered ? Gdiplus::Color(255, 238, 240, 243) : Gdiplus::Color(255, 246, 248, 250);
                borderClr = isHovered ? Gdiplus::Color(255, 210, 215, 220) : Gdiplus::Color(255, 226, 230, 234);
                dotClr = Gdiplus::Color(255, 160, 168, 175);
                haloClr = Gdiplus::Color(45, 160, 168, 175);
                textClr = RGB(125, 133, 144);
                break;
            case SyncStateKind::Normal:
            default:
                bgClr = isHovered ? Gdiplus::Color(255, 235, 245, 238) : Gdiplus::Color(255, 244, 249, 245);
                borderClr = isHovered ? Gdiplus::Color(255, 175, 215, 185) : Gdiplus::Color(255, 205, 230, 212);
                dotClr = Gdiplus::Color(255, 16, 124, 65);
                haloClr = Gdiplus::Color(55, 16, 124, 65);
                textClr = RGB(40, 60, 45);
                break;
            }

            {
                using namespace Gdiplus;
                Graphics g(memDC);
                g.SetSmoothingMode(SmoothingModeAntiAlias);
                g.SetPixelOffsetMode(PixelOffsetModeHalf);

                // 胶囊外框
                RectF pillBounds(0.5f, 0.5f, static_cast<float>(w) - 1.0f, static_cast<float>(h) - 1.0f);
                float radius = (static_cast<float>(h) - 1.0f) / 2.0f;
                GraphicsPath path;
                AddRoundedRect(path, pillBounds, radius);

                SolidBrush bgBrush(bgClr);
                g.FillPath(&bgBrush, &path);

                Pen borderPen(borderClr, 1.0f);
                g.DrawPath(&borderPen, &path);

                // 指示灯圆点与光晕
                float cx = static_cast<float>(padX) + static_cast<float>(haloSize) / 2.0f;
                float cy = static_cast<float>(h) / 2.0f;

                SolidBrush haloBrush(haloClr);
                g.FillEllipse(&haloBrush, cx - haloSize / 2.0f, cy - haloSize / 2.0f,
                    static_cast<float>(haloSize), static_cast<float>(haloSize));

                SolidBrush dotBrush(dotClr);
                g.FillEllipse(&dotBrush, cx - dotSize / 2.0f, cy - dotSize / 2.0f,
                    static_cast<float>(dotSize), static_cast<float>(dotSize));
            }

            // 绘制文字
            HFONT hFont = reinterpret_cast<HFONT>(SendMessageW(hWnd, WM_GETFONT, 0, 0));
            HFONT oldFont = hFont ? static_cast<HFONT>(SelectObject(memDC, hFont)) : nullptr;
            SetBkMode(memDC, TRANSPARENT);
            SetTextColor(memDC, textClr);

            RECT textRc;
            textRc.left = padX + haloSize + dotGap;
            textRc.top = 0;
            textRc.right = w - padX;
            textRc.bottom = h;

            DrawTextW(memDC, text.c_str(), -1, &textRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

            if (oldFont) {
                SelectObject(memDC, oldFont);
            }

            BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);
            SelectObject(memDC, oldBmp);
            DeleteObject(memBmp);
            DeleteDC(memDC);
        }

        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_NCDESTROY:
        delete pState;
        RemoveWindowSubclass(hWnd, SyncStatusSubclassProc, uIdSubclass);
        break;
    }

    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

} // namespace

Toolbar::~Toolbar() {
    if (m_hFont) {
        DeleteObject(m_hFont);
        m_hFont = nullptr;
    }
    if (m_hImageList) {
        ImageList_Destroy(m_hImageList);
        m_hImageList = nullptr;
    }
}

int Toolbar::GetPreferredHeight() const {
    UINT dpi = m_hWnd ? GetDpiForWindow(m_hWnd) : 96;
    return MulDiv(30, dpi, 96);
}

bool Toolbar::Initialize(HWND hParent, UINT controlId) {
    m_hParent = hParent;
    UINT dpi = GetDpiForWindow(hParent);

    DWORD dwStyle = WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | TBSTYLE_FLAT | TBSTYLE_LIST | TBSTYLE_TOOLTIPS | TBSTYLE_CUSTOMERASE |
                    CCS_NODIVIDER | CCS_NORESIZE;

    m_hWnd = CreateWindowExW(
        0,
        TOOLBARCLASSNAME,
        nullptr,
        dwStyle,
        0, 0, 0, 0,
        hParent,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(controlId)),
        GetModuleHandleW(nullptr),
        nullptr
    );

    if (!m_hWnd) {
        return false;
    }

    // 禁用工具栏的主题底板，否则主题绘制会覆盖白色擦除背景。
    SetWindowTheme(m_hWnd, L"", L"");
    SetWindowSubclass(m_hWnd, ToolbarSubclassProc, 1, 0);

    SendMessageW(m_hWnd, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
    SendMessageW(m_hWnd, TB_SETEXTENDEDSTYLE, 0, TBSTYLE_EX_HIDECLIPPEDBUTTONS | TBSTYLE_EX_DOUBLEBUFFER | TBSTYLE_EX_MIXEDBUTTONS);

    UpdateImageListForDpi(dpi);

    // 定义按钮结构 (第 0 项为 ComboBox 占位分隔符)
    std::vector<TBBUTTON> buttons = {
        { 0, 0, TBSTATE_ENABLED, BTNS_SEP, {0}, 0, -1 },
        { 0, 0, TBSTATE_ENABLED, BTNS_SEP, {0}, 0, -1 },
        { 0, ID_FORMAT_BOLD, TBSTATE_ENABLED, BTNS_BUTTON, {0}, 0, -1 },
        { 1, ID_FORMAT_ITALIC, TBSTATE_ENABLED, BTNS_BUTTON, {0}, 0, -1 },
        { 2, ID_FORMAT_UNDERLINE, TBSTATE_ENABLED, BTNS_BUTTON, {0}, 0, -1 },
        { 0, 0, 0, BTNS_SEP, {0}, 0, -1 },
        { 3, ID_FORMAT_CODE_BLOCK, TBSTATE_ENABLED, BTNS_BUTTON, {0}, 0, -1 },
        { 4, ID_FORMAT_BULLET_LIST, TBSTATE_ENABLED, BTNS_BUTTON, {0}, 0, -1 },
        { 5, ID_FORMAT_NUMBER_LIST, TBSTATE_ENABLED, BTNS_BUTTON, {0}, 0, -1 },
        { 0, 0, 0, BTNS_SEP, {0}, 0, -1 },
        { 6, ID_INSERT_IMAGE, TBSTATE_ENABLED, BTNS_BUTTON, {0}, 0, -1 },
        { 7, ID_INSERT_TABLE, TBSTATE_ENABLED, BTNS_BUTTON, {0}, 0, -1 },
        { 0, 0, 0, BTNS_SEP, {0}, 0, -1 },
        { 8, ID_FILE_SAVE, TBSTATE_ENABLED, BTNS_BUTTON, {0}, 0, -1 },
        { 0, 0, 0, BTNS_SEP, {0}, 0, -1 },
        { 9, ID_VIEW_OUTLINE, TBSTATE_ENABLED, BTNS_CHECK, {0}, 0, -1 },
        { 0, 0, 0, BTNS_SEP, {0}, 0, -1 },
        { 10, ID_REALTIME_SYNC, TBSTATE_ENABLED, BTNS_CHECK, {0}, 0, -1 }
    };

    SendMessageW(m_hWnd, TB_ADDBUTTONSW, buttons.size(), reinterpret_cast<LPARAM>(buttons.data()));

    // 设置第 0 项占位分隔符的宽度 (96px 随 DPI 缩放)
    TBBUTTONINFO tbbi = { sizeof(TBBUTTONINFO) };
    tbbi.dwMask = TBIF_SIZE | TBIF_BYINDEX;
    tbbi.cx = static_cast<WORD>(MulDiv(96, dpi, 96));
    SendMessageW(m_hWnd, TB_SETBUTTONINFO, 0, reinterpret_cast<LPARAM>(&tbbi));

    SendMessageW(m_hWnd, TB_AUTOSIZE, 0, 0);

    // 创建内嵌样式 ComboBox，以 hParent 为父窗口以便主窗口直接接收 CBN_SELCHANGE
    m_hHeadingCombo = CreateWindowExW(
        0,
        WC_COMBOBOXW,
        L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        4, 4, tbbi.cx, MulDiv(180, dpi, 96),
        hParent,
        reinterpret_cast<HMENU>(IDC_TOOLBAR_HEADING_COMBO),
        GetModuleHandleW(nullptr),
        nullptr
    );

    if (m_hHeadingCombo) {
        SetWindowTheme(m_hHeadingCombo, L"CFD", nullptr);
        UpdateFontForDpi(dpi);
        SendMessageW(m_hHeadingCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"正文"));
        SendMessageW(m_hHeadingCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"标题 1"));
        SendMessageW(m_hHeadingCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"标题 2"));
        SendMessageW(m_hHeadingCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"标题 3"));
        SendMessageW(m_hHeadingCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"标题 4"));
        SendMessageW(m_hHeadingCombo, CB_SETCURSEL, 0, 0);
    }

    m_hSyncStatus = CreateWindowExW(0, L"STATIC", L"实时同步已关闭", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | SS_NOTIFY,
        0, 0, 0, 0, hParent, reinterpret_cast<HMENU>(IDC_REALTIME_STATUS), GetModuleHandleW(nullptr), nullptr);
    if (m_hSyncStatus) {
        auto* pState = new SyncStatusState();
        SetWindowSubclass(m_hSyncStatus, SyncStatusSubclassProc, 2, reinterpret_cast<DWORD_PTR>(pState));
        SendMessageW(m_hSyncStatus, WM_SETFONT, reinterpret_cast<WPARAM>(m_hFont), TRUE);
    }
    return true;
}

void Toolbar::SetRealtimeStatus(bool checked, const std::wstring& text) {
    if (m_realtimeChecked == checked && m_syncStatusText == text) {
        return;
    }

    if (m_realtimeChecked != checked) {
        m_realtimeChecked = checked;
        if (m_hWnd) {
            SendMessageW(m_hWnd, TB_CHECKBUTTON, ID_REALTIME_SYNC, MAKELONG(checked ? TRUE : FALSE, 0));
        }
    }

    if (m_syncStatusText != text) {
        m_syncStatusText = text;
        if (m_hSyncStatus) {
            SetWindowTextW(m_hSyncStatus, text.c_str());
            LayoutSyncStatus();
        }
    }
}

void Toolbar::UpdateImageListForDpi(UINT dpi) {
    if (m_hImageList && m_imageDpi == dpi) return;

    HIMAGELIST hNewList = CreateFluentToolbarImageList(dpi);
    if (!hNewList) return;

    if (m_hWnd) {
        int iconSize = MulDiv(kToolbarIconSize, dpi, 96);
        int btnW = MulDiv(kToolbarButtonWidth, dpi, 96);
        int btnH = MulDiv(kToolbarButtonHeight, dpi, 96);
        SendMessageW(m_hWnd, TB_SETIMAGELIST, 0, reinterpret_cast<LPARAM>(hNewList));
        SendMessageW(m_hWnd, TB_SETBITMAPSIZE, 0, MAKELONG(iconSize, iconSize));
        SendMessageW(m_hWnd, TB_SETBUTTONSIZE, 0, MAKELONG(btnW, btnH));
    }

    if (m_hImageList) {
        ImageList_Destroy(m_hImageList);
    }
    m_hImageList = hNewList;
    m_imageDpi = dpi;
}

void Toolbar::SetBounds(int x, int y, int width, int height) {
    if (m_hWnd) {
        m_lastX = x;
        m_lastY = y;
        m_lastWidth = width;
        m_lastHeight = height;

        UINT dpi = GetDpiForWindow(m_hParent);
        UpdateFontForDpi(dpi);
        UpdateImageListForDpi(dpi);

        TBBUTTONINFO separator = { sizeof(TBBUTTONINFO) };
        separator.dwMask = TBIF_SIZE | TBIF_BYINDEX;
        separator.cx = static_cast<WORD>(MulDiv(96, dpi, 96));
        SendMessageW(m_hWnd, TB_SETBUTTONINFOW, 0, reinterpret_cast<LPARAM>(&separator));
        MoveWindow(m_hWnd, x, y, width, height, TRUE);

        LayoutSyncStatus();

        // 同步定位 ComboBox
        if (m_hHeadingCombo) {
            RECT rcSep = {};
            if (SendMessageW(m_hWnd, TB_GETITEMRECT, 0, reinterpret_cast<LPARAM>(&rcSep))) {
                POINT pt = { rcSep.left, rcSep.top };
                MapWindowPoints(m_hWnd, m_hParent, &pt, 1);
                RECT comboRect = {};
                GetWindowRect(m_hHeadingCombo, &comboRect);
                int comboH = comboRect.bottom - comboRect.top;
                int inset = MulDiv(4, dpi, 96);
                int comboW = rcSep.right - rcSep.left - inset * 2;
                int topY = y + (height - comboH) / 2;
                SetWindowPos(m_hHeadingCombo, HWND_TOP, pt.x + inset, topY,
                    comboW, MulDiv(180, dpi, 96), SWP_NOACTIVATE);
            }
        }
    }
}

void Toolbar::LayoutSyncStatus() {
    if (!m_hSyncStatus || !m_hWnd) return;

    UINT dpi = GetDpiForWindow(m_hParent);
    UpdateFontForDpi(dpi);

    // 获取实时同步按钮右边界，避免挤压重叠
    RECT last = {};
    SendMessageW(m_hWnd, TB_GETRECT, ID_REALTIME_SYNC, reinterpret_cast<LPARAM>(&last));
    int statusLeft = last.right + MulDiv(12, dpi, 96);

    wchar_t buf[256] = {};
    GetWindowTextW(m_hSyncStatus, buf, 256);
    std::wstring text = buf;

    int textWidth = 0;
    HDC hdc = GetDC(m_hSyncStatus);
    if (hdc) {
        HFONT oldFont = m_hFont ? static_cast<HFONT>(SelectObject(hdc, m_hFont)) : nullptr;
        RECT calcRc = {};
        DrawTextW(hdc, text.c_str(), -1, &calcRc, DT_CALCRECT | DT_SINGLELINE);
        textWidth = calcRc.right - calcRc.left;
        if (oldFont) {
            SelectObject(hdc, oldFont);
        }
        ReleaseDC(m_hSyncStatus, hdc);
    }

    int haloSize = MulDiv(10, dpi, 96);
    int dotGap = MulDiv(6, dpi, 96);
    int padX = MulDiv(10, dpi, 96);
    int pillW = padX * 2 + haloSize + dotGap + textWidth;
    int pillH = MulDiv(22, dpi, 96);
    int rightMargin = MulDiv(12, dpi, 96);

    int pillX = m_lastX + m_lastWidth - rightMargin - pillW;
    int minX = m_lastX + statusLeft;

    if (pillX < minX) {
        pillX = minX;
        pillW = (std::max)(0, (m_lastX + m_lastWidth - rightMargin) - minX);
    }

    int pillY = m_lastY + (m_lastHeight - pillH) / 2;

    RECT curRc = {};
    GetWindowRect(m_hSyncStatus, &curRc);
    POINT pt = { curRc.left, curRc.top };
    ScreenToClient(m_hParent, &pt);
    int curW = curRc.right - curRc.left;
    int curH = curRc.bottom - curRc.top;

    if (pt.x != pillX || pt.y != pillY || curW != pillW || curH != pillH) {
        SetWindowPos(m_hSyncStatus, HWND_TOP, pillX, pillY, pillW, pillH, SWP_NOACTIVATE | SWP_NOCOPYBITS);
    }
    InvalidateRect(m_hSyncStatus, nullptr, FALSE);
}

void Toolbar::UpdateFontForDpi(UINT dpi) {
    if (m_hFont && m_fontDpi == dpi) return;

    HFONT newFont = CreateFontW(
        -MulDiv(9, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
    );
    if (!newFont) return;

    if (m_hHeadingCombo) {
        SendMessageW(m_hHeadingCombo, WM_SETFONT, reinterpret_cast<WPARAM>(newFont), TRUE);
    }
    if (m_hSyncStatus) {
        SendMessageW(m_hSyncStatus, WM_SETFONT, reinterpret_cast<WPARAM>(newFont), TRUE);
    }
    if (m_hFont) DeleteObject(m_hFont);
    m_hFont = newFont;
    m_fontDpi = dpi;
}

void Toolbar::SetSelectedHeadingIndex(int index) {
    if (m_hHeadingCombo && index >= 0 && index <= 4) {
        int cur = static_cast<int>(SendMessageW(m_hHeadingCombo, CB_GETCURSEL, 0, 0));
        if (cur != index) {
            SendMessageW(m_hHeadingCombo, CB_SETCURSEL, index, 0);
        }
    }
}

int Toolbar::GetSelectedHeadingIndex() const {
    if (m_hHeadingCombo) {
        return static_cast<int>(SendMessageW(m_hHeadingCombo, CB_GETCURSEL, 0, 0));
    }
    return 0;
}

void Toolbar::SetOutlineButtonChecked(bool checked) {
    if (m_hWnd) {
        SendMessageW(m_hWnd, TB_CHECKBUTTON, ID_VIEW_OUTLINE, MAKELPARAM(checked ? TRUE : FALSE, 0));
    }
}

} // namespace anynote::ui
