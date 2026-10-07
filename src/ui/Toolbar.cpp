#include "Toolbar.h"
#include "resource.h"
#include <vector>

namespace anynote::ui {

bool Toolbar::Initialize(HWND hParent, UINT controlId) {
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

    // 定义按钮结构
    std::vector<TBBUTTON> buttons = {
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
    SendMessageW(m_hWnd, TB_AUTOSIZE, 0, 0);

    return true;
}

void Toolbar::SetBounds(int x, int y, int width, int height) {
    if (m_hWnd) {
        MoveWindow(m_hWnd, x, y, width, height, TRUE);
    }
}

} // namespace anynote::ui
