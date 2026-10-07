#include "Toolbar.h"
#include "resource.h"
#include <vector>

namespace anynote::ui {

Toolbar::~Toolbar() {
    if (m_hFont) {
        DeleteObject(m_hFont);
        m_hFont = nullptr;
    }
}

int Toolbar::GetPreferredHeight() const {
    UINT dpi = m_hWnd ? GetDpiForWindow(m_hWnd) : 96;
    return MulDiv(38, dpi, 96);
}

bool Toolbar::Initialize(HWND hParent, UINT controlId) {
    m_hParent = hParent;
    UINT dpi = GetDpiForWindow(hParent);

    DWORD dwStyle = WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_TOOLTIPS | 
                    CCS_NODIVIDER | CCS_NORESIZE | TBSTYLE_LIST;

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

    SendMessageW(m_hWnd, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
    SendMessageW(m_hWnd, TB_SETEXTENDEDSTYLE, 0, TBSTYLE_EX_MIXEDBUTTONS | TBSTYLE_EX_HIDECLIPPEDBUTTONS);

    // 添加文本字符串
    const wchar_t* buttonStrings = 
        L"粗体\0"
        L"斜体\0"
        L"下划线\0"
        L"代码块\0"
        L"项目列表\0"
        L"编号列表\0"
        L"插入图片\0"
        L"保存\0\0";

    INT_PTR strIndex = SendMessageW(m_hWnd, TB_ADDSTRINGW, 0, reinterpret_cast<LPARAM>(buttonStrings));

    // 定义按钮结构 (第 0 项为 ComboBox 占位分隔符)
    std::vector<TBBUTTON> buttons = {
        { 0, 0, TBSTATE_ENABLED, BTNS_SEP, {0}, 0, 0 },
        { 0, 0, TBSTATE_ENABLED, BTNS_SEP, {0}, 0, 0 },
        { I_IMAGENONE, ID_FORMAT_BOLD, TBSTATE_ENABLED, BTNS_BUTTON | BTNS_SHOWTEXT, {0}, 0, strIndex + 0 },
        { I_IMAGENONE, ID_FORMAT_ITALIC, TBSTATE_ENABLED, BTNS_BUTTON | BTNS_SHOWTEXT, {0}, 0, strIndex + 1 },
        { I_IMAGENONE, ID_FORMAT_UNDERLINE, TBSTATE_ENABLED, BTNS_BUTTON | BTNS_SHOWTEXT, {0}, 0, strIndex + 2 },
        { 0, 0, 0, BTNS_SEP, {0}, 0, 0 },
        { I_IMAGENONE, ID_FORMAT_CODE_BLOCK, TBSTATE_ENABLED, BTNS_BUTTON | BTNS_SHOWTEXT, {0}, 0, strIndex + 3 },
        { I_IMAGENONE, ID_FORMAT_BULLET_LIST, TBSTATE_ENABLED, BTNS_BUTTON | BTNS_SHOWTEXT, {0}, 0, strIndex + 4 },
        { I_IMAGENONE, ID_FORMAT_NUMBER_LIST, TBSTATE_ENABLED, BTNS_BUTTON | BTNS_SHOWTEXT, {0}, 0, strIndex + 5 },
        { 0, 0, 0, BTNS_SEP, {0}, 0, 0 },
        { I_IMAGENONE, ID_INSERT_IMAGE, TBSTATE_ENABLED, BTNS_BUTTON | BTNS_SHOWTEXT, {0}, 0, strIndex + 6 },
        { 0, 0, 0, BTNS_SEP, {0}, 0, 0 },
        { I_IMAGENONE, ID_FILE_SAVE, TBSTATE_ENABLED, BTNS_BUTTON | BTNS_SHOWTEXT, {0}, 0, strIndex + 7 }
    };

    SendMessageW(m_hWnd, TB_ADDBUTTONSW, buttons.size(), reinterpret_cast<LPARAM>(buttons.data()));

    // 设置第 0 项占位分隔符的宽度 (96px)
    TBBUTTONINFO tbbi = { sizeof(TBBUTTONINFO) };
    tbbi.dwMask = TBIF_SIZE;
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
        UpdateFontForDpi(dpi);
        SendMessageW(m_hHeadingCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"正文"));
        SendMessageW(m_hHeadingCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"标题 1"));
        SendMessageW(m_hHeadingCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"标题 2"));
        SendMessageW(m_hHeadingCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"标题 3"));
        SendMessageW(m_hHeadingCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"标题 4"));
        SendMessageW(m_hHeadingCombo, CB_SETCURSEL, 0, 0);
    }

    return true;
}

void Toolbar::SetBounds(int x, int y, int width, int height) {
    if (m_hWnd) {
        UINT dpi = GetDpiForWindow(m_hParent);
        UpdateFontForDpi(dpi);
        TBBUTTONINFO separator = { sizeof(TBBUTTONINFO) };
        separator.dwMask = TBIF_SIZE | TBIF_BYINDEX;
        separator.cx = static_cast<WORD>(MulDiv(96, dpi, 96));
        SendMessageW(m_hWnd, TB_SETBUTTONINFOW, 0, reinterpret_cast<LPARAM>(&separator));
        MoveWindow(m_hWnd, x, y, width, height, TRUE);

        // 同步定位 ComboBox
        if (m_hHeadingCombo) {
            RECT rcSep = {};
            if (SendMessageW(m_hWnd, TB_GETITEMRECT, 0, reinterpret_cast<LPARAM>(&rcSep))) {
                POINT pt = { rcSep.left, rcSep.top };
                MapWindowPoints(m_hWnd, m_hParent, &pt, 1);
                int comboH = MulDiv(24, dpi, 96);
                int comboW = rcSep.right - rcSep.left;
                int topY = pt.y + ((rcSep.bottom - rcSep.top) - comboH) / 2;
                MoveWindow(m_hHeadingCombo, pt.x + 2, topY, comboW, MulDiv(180, dpi, 96), TRUE);
            }
        }
    }
}

void Toolbar::UpdateFontForDpi(UINT dpi) {
    if (!m_hHeadingCombo || (m_hFont && m_fontDpi == dpi)) return;

    HFONT newFont = CreateFontW(
        -MulDiv(9, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
    );
    if (!newFont) return;

    SendMessageW(m_hHeadingCombo, WM_SETFONT, reinterpret_cast<WPARAM>(newFont), TRUE);
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

} // namespace anynote::ui
