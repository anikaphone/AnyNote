#include "RichEditView.h"
#include "resource.h"
#include <commctrl.h>
#include <commdlg.h>
#include <windowsx.h>
#include <cwctype>
#include <vector>
#include <memory>
#include <filesystem>
#include <gdiplus.h>
#include <tom.h>
#include <wrl/client.h>

namespace anynote::ui {

namespace {

int GetEncoderClsid(const WCHAR* format, CLSID* pClsid) {
    UINT num = 0;
    UINT size = 0;
    Gdiplus::GetImageEncodersSize(&num, &size);
    if (size == 0) return -1;

    std::vector<BYTE> buffer(size);
    auto* pImageCodecInfo = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buffer.data());
    Gdiplus::GetImageEncoders(num, size, pImageCodecInfo);

    for (UINT j = 0; j < num; ++j) {
        if (wcscmp(pImageCodecInfo[j].MimeType, format) == 0) {
            *pClsid = pImageCodecInfo[j].Clsid;
            return static_cast<int>(j);
        }
    }
    return -1;
}

struct StreamInCookie {
    const char* data;
    size_t size;
    size_t offset = 0;
};

DWORD CALLBACK StreamInCallback(DWORD_PTR dwCookie, LPBYTE pbBuff, LONG cb, LONG* pcb) {
    auto* pCookie = reinterpret_cast<StreamInCookie*>(dwCookie);
    if (!pCookie || pCookie->offset >= pCookie->size) {
        *pcb = 0;
        return 0;
    }

    LONG toCopy = static_cast<LONG>(std::min(static_cast<size_t>(cb), pCookie->size - pCookie->offset));
    memcpy(pbBuff, pCookie->data + pCookie->offset, toCopy);
    pCookie->offset += toCopy;
    *pcb = toCopy;
    return 0;
}

DWORD CALLBACK StreamOutCallback(DWORD_PTR dwCookie, LPBYTE pbBuff, LONG cb, LONG* pcb) {
    auto* pOut = reinterpret_cast<std::string*>(dwCookie);
    if (!pOut) {
        *pcb = 0;
        return 0;
    }

    pOut->append(reinterpret_cast<const char*>(pbBuff), static_cast<size_t>(cb));
    *pcb = cb;
    return 0;
}

} // namespace


static LRESULT CALLBACK RichEditSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    auto* pThis = reinterpret_cast<RichEditView*>(dwRefData);
    if (uMsg == WM_KEYDOWN && wParam == VK_TAB && pThis) {
        if (pThis->GetCodeBlockAtCursor()) {
            SendMessageW(hWnd, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"    "));
            return 0;
        }
        if (pThis->IsCursorInTable()) {
            bool isShift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            if (isShift) {
                pThis->NavigateTableCell(false);
            } else {
                bool moved = pThis->NavigateTableCell(true);
                if (!moved) {
                    // 位于表格末尾单元格，按 Tab 自动追加新行并跳入新行首个单元格
                    if (pThis->InsertTableRow(true)) {
                        pThis->NavigateTableCell(true);
                    }
                }
            }
            return 0; // 拦截默认 Tab 键行为，防止跳焦或破坏表格
        }
    }
    if (uMsg == WM_CHAR && wParam == VK_TAB && pThis && pThis->IsCursorInTable()) {
        // TranslateMessage can still emit WM_CHAR after custom cell navigation.
        return 0;
    }
    if (uMsg == WM_CHAR && wParam == VK_RETURN && pThis) {
        const bool inCode = pThis->GetCodeBlockAtCursor();
        int curLevel = pThis->GetCurrentHeadingLevel();
        LRESULT res = DefSubclassProc(hWnd, uMsg, wParam, lParam);
        if (inCode) {
            pThis->RefreshCodeBlockLayout();
            pThis->UpdateHoverBarPosition();
        }
        if (curLevel > 0 && !pThis->IsCursorInTable()) {
            pThis->ApplyHeading(0);
        }
        return res;
    }
    if (uMsg == WM_NCDESTROY) {
        RemoveWindowSubclass(hWnd, RichEditSubclassProc, uIdSubclass);
    }
    if (uMsg == WM_CONTEXTMENU) {
        int xPos = GET_X_LPARAM(lParam);
        int yPos = GET_Y_LPARAM(lParam);
        if (xPos != -1 || yPos != -1) {
            POINT ptClient = { xPos, yPos };
            ScreenToClient(hWnd, &ptClient);
            POINTL ptl = { ptClient.x, ptClient.y };
            LRESULT charPos = SendMessageW(hWnd, EM_CHARFROMPOS, 0, reinterpret_cast<LPARAM>(&ptl));
            if (charPos >= 0) {
                CHARRANGE cr = {};
                SendMessageW(hWnd, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&cr));
                if (charPos < cr.cpMin || charPos > cr.cpMax) {
                    CHARRANGE newCr = { static_cast<LONG>(charPos), static_cast<LONG>(charPos) };
                    SendMessageW(hWnd, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&newCr));
                }
            }
        }
        SendMessageW(GetParent(hWnd), WM_CONTEXTMENU, reinterpret_cast<WPARAM>(hWnd), lParam);
        return 0;
    }
    if (uMsg == WM_MOUSEMOVE && pThis) {
        POINT ptClient = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        POINTL ptl = { ptClient.x, ptClient.y };
        LRESULT charPos = SendMessageW(hWnd, EM_CHARFROMPOS, 0, reinterpret_cast<LPARAM>(&ptl));
        pThis->OnMouseMove(static_cast<long>(charPos), ptClient);

        TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT) };
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = hWnd;
        TrackMouseEvent(&tme);
    }

    if (uMsg == WM_MOUSELEAVE && pThis) {
        pThis->OnMouseLeave();
    }

    const bool shouldUpdateHoverBar = pThis &&
        (uMsg == WM_VSCROLL || uMsg == WM_HSCROLL || uMsg == WM_MOUSEWHEEL || uMsg == WM_SIZE);
    const bool shouldRepaintAfterScroll = pThis &&
        (uMsg == WM_VSCROLL || uMsg == WM_HSCROLL || uMsg == WM_MOUSEWHEEL);
    LRESULT result = DefSubclassProc(hWnd, uMsg, wParam, lParam);
    if (shouldRepaintAfterScroll) {
        // RichEdit scrolls by moving pixels and may leave the custom code-block
        // frame layer out of sync at the newly exposed bottom edge.  Invalidate
        // the complete editor so both RichEdit and the overlay repaint from a
        // clean backing surface; resizing naturally did this before.
        RedrawWindow(hWnd, nullptr, nullptr,
            RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    }
    if (uMsg == WM_PAINT && pThis) pThis->PaintCodeBlockFrames();
    if (shouldUpdateHoverBar) {
        pThis->UpdateHoverBarPosition();
    }
    const bool shouldRefreshCodeLayout = pThis &&
        (uMsg == WM_CHAR || uMsg == WM_SETTEXT || uMsg == WM_CUT ||
         uMsg == WM_PASTE || uMsg == WM_CLEAR || uMsg == WM_UNDO ||
         uMsg == EM_UNDO || uMsg == EM_REDO || uMsg == EM_REPLACESEL ||
         uMsg == EM_STREAMIN || uMsg == EM_SETTEXTEX || uMsg == EM_PASTESPECIAL ||
         uMsg == EM_SETCHARFORMAT || uMsg == EM_SETPARAFORMAT ||
         (uMsg == WM_KEYDOWN && (wParam == VK_DELETE ||
          (wParam == VK_INSERT && (GetKeyState(VK_SHIFT) & 0x8000) != 0) ||
          ((GetKeyState(VK_CONTROL) & 0x8000) != 0 &&
           (wParam == 'X' || wParam == 'V' || wParam == 'Z' || wParam == 'Y')))));
    if (shouldRefreshCodeLayout) {
        // Normalize only after the complete edit, without adding layout to undo history.
        pThis->RefreshCodeBlockLayout();
        pThis->UpdateHoverBarPosition();
    }
    switch (uMsg) {
    case WM_KEYUP:
    case WM_LBUTTONUP:
        if (pThis) {
            pThis->OnSelChange();
        }
        break;
    case WM_CHAR:
    case WM_KEYDOWN:
    case WM_SETTEXT:
    case WM_CUT:
    case WM_PASTE:
    case WM_CLEAR:
    case WM_UNDO:
    case EM_UNDO:
    case EM_REDO:
    case EM_REPLACESEL:
    case EM_STREAMIN:
    case EM_SETTEXTEX:
    case EM_PASTESPECIAL:
    case EM_SETCHARFORMAT:
    case EM_SETPARAFORMAT:
        // 在完整操作结束后查询格式，避免读到加载/格式化中的临时选区。
        PostMessageW(GetParent(hWnd), WM_EDITOR_FORMAT_CHANGED, 0, 0);
        break;
    }
    return result;
}

RichEditView::~RichEditView() {
    if (m_hWnd) {
        RemoveWindowSubclass(m_hWnd, RichEditSubclassProc, 1);
        DestroyWindow(m_hWnd);
        m_hWnd = nullptr;
    }
    if (m_richEditModule) {
        FreeLibrary(m_richEditModule);
        m_richEditModule = nullptr;
    }
}

bool RichEditView::Initialize(HWND hParent, int x, int y, int width, int height, UINT controlId) {
    if (m_hWnd) {
        return false;
    }

    HMODULE richEditModule = LoadLibraryW(L"msftedit.dll");
    if (!richEditModule) {
        return false;
    }

    DWORD dwStyle = WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL |
                    ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_NOHIDESEL | ES_WANTRETURN |
                    WS_CLIPSIBLINGS | WS_CLIPCHILDREN;

    m_hWnd = CreateWindowExW(
        0,
        MSFTEDIT_CLASS, // "RichEdit50W"
        L"",
        dwStyle,
        x, y, width, height,
        hParent,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(controlId)),
        GetModuleHandleW(nullptr),
        nullptr
    );

    if (!m_hWnd) {
        FreeLibrary(richEditModule);
        return false;
    }

    m_richEditModule = richEditModule;

    // 挂载子类化过程以处理标题回车换行自动恢复正文
    SetWindowSubclass(m_hWnd, RichEditSubclassProc, 1, reinterpret_cast<DWORD_PTR>(this));

    // 设置内边距 (Padding): 左右各 16 像素，顶部更舒适
    SendMessageW(m_hWnd, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELONG(16, 16));

    // 启用换行与链接检测
    SendMessageW(m_hWnd, EM_SETEVENTMASK, 0, ENM_CHANGE | ENM_SELCHANGE | ENM_LINK);

    // 默认开启高级 RTF 语言选项
    SendMessageW(m_hWnd, EM_SETLANGOPTIONS, 0, 0);

    // 初始化单例代码块悬浮控制条
    m_hoverBar.Initialize(m_hWnd);
    m_hoverBar.SetOnLanguageChanged([this](common::CodeLanguage newLang) {
        CodeBlockInfo info;
        long activeTable = m_hoverBar.GetActiveTableStart();
        if (activeTable >= 0 && GetCodeBlockAt(activeTable, &info)) {
            SwitchCodeBlockLanguage(info, newLang);
        }
    });
    m_hoverBar.SetOnCopyClicked([this]() {
        CodeBlockInfo info;
        long activeTable = m_hoverBar.GetActiveTableStart();
        if (activeTable >= 0 && GetCodeBlockAt(activeTable, &info)) {
            if (CopyCodeBlockText(info)) {
                SendMessageW(GetParent(m_hWnd), WM_CODEBLOCK_COPIED, 0, 0);
                return true;
            }
        }
        return false;
    });

    ApplyDefaultFormatting(true);
    return true;
}

void RichEditView::ApplyDefaultFormatting(bool allDocument) {
    CHARFORMAT2W cf = {sizeof(CHARFORMAT2W)};
    cf.dwMask = CFM_FACE | CFM_SIZE | CFM_COLOR | CFM_BACKCOLOR | CFM_BOLD | CFM_ITALIC | CFM_UNDERLINE | CFM_STRIKEOUT;
    cf.yHeight = 220; // 11pt (20 twips = 1pt)
    cf.crTextColor = RGB(33, 37, 41);
    cf.crBackColor = RGB(255, 255, 255);
    cf.dwEffects = CFE_AUTOBACKCOLOR;
    wcscpy_s(cf.szFaceName, L"Segoe UI");

    SendMessageW(m_hWnd, EM_SETCHARFORMAT, allDocument ? SCF_ALL : SCF_SELECTION, reinterpret_cast<LPARAM>(&cf));

    // 设置默认段落行距与间距
    if (allDocument) {
        PARAFORMAT2 pf = {sizeof(PARAFORMAT2)};
        pf.dwMask = PFM_SPACEAFTER;
        pf.dySpaceAfter = 100; // 段后间距
        SendMessageW(m_hWnd, EM_SETPARAFORMAT, 0, reinterpret_cast<LPARAM>(&pf));
    }
}

void RichEditView::SetBounds(int x, int y, int width, int height, bool repaint) {
    if (m_hWnd) {
        MoveWindow(m_hWnd, x, y, width, height, repaint ? TRUE : FALSE);

        // 现代笔记文档呼吸感边距：左右留白 28px、顶部留白 18px (随 DPI 缩放)
        UINT dpi = GetDpiForWindow(m_hWnd);
        int marginX = MulDiv(28, dpi, 96);
        int topMargin = MulDiv(18, dpi, 96);
        int bottomMargin = MulDiv(20, dpi, 96);

        SendMessageW(m_hWnd, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELONG(marginX, marginX));

        RECT rc;
        GetClientRect(m_hWnd, &rc);
        rc.left += marginX;
        rc.top += topMargin;
        rc.right = std::max(rc.left + 50, rc.right - marginX);
        rc.bottom = std::max(rc.top + 50, rc.bottom - bottomMargin);
        SendMessageW(m_hWnd, EM_SETRECTNP, 0, reinterpret_cast<LPARAM>(&rc));
        RefreshCodeBlockLayout();
        UpdateHoverBarPosition();
    }
}

void RichEditView::SetText(const std::wstring& text) {
    if (!m_hWnd) return;
    m_hoverBar.Hide();
    SetWindowTextW(m_hWnd, text.c_str());
}

std::wstring RichEditView::GetText() const {
    if (!m_hWnd) return {};

    GETTEXTLENGTHEX gtl = {};
    gtl.flags = GTL_DEFAULT | GTL_NUMCHARS;
    gtl.codepage = 1200; // UTF-16
    LRESULT len = SendMessageW(m_hWnd, EM_GETTEXTLENGTHEX, reinterpret_cast<WPARAM>(&gtl), 0);
    if (len <= 0) return {};

    std::wstring text(static_cast<size_t>(len) + 1, L'\0');
    GETTEXTEX gt = {};
    gt.cb = static_cast<DWORD>((len + 1) * sizeof(wchar_t));
    gt.flags = GT_DEFAULT;
    gt.codepage = 1200; // UTF-16
    SendMessageW(m_hWnd, EM_GETTEXTEX, reinterpret_cast<WPARAM>(&gt), reinterpret_cast<LPARAM>(text.data()));
    text.resize(static_cast<size_t>(len));
    return text;
}

std::wstring RichEditView::GetSelectedText() const {
    if (!m_hWnd) return {};

    CHARRANGE cr = {};
    SendMessageW(m_hWnd, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&cr));
    if (cr.cpMax <= cr.cpMin) return {};

    LONG len = cr.cpMax - cr.cpMin;
    std::wstring text(static_cast<size_t>(len) + 1, L'\0');
    SendMessageW(m_hWnd, EM_GETSELTEXT, 0, reinterpret_cast<LPARAM>(text.data()));
    text.resize(static_cast<size_t>(len));
    return text;
}

bool RichEditView::StreamInRTF(std::string_view rtfData) {
    if (!m_hWnd || rtfData.empty()) return false;

    m_hoverBar.Hide();
    StreamInCookie cookie{rtfData.data(), rtfData.size()};
    EDITSTREAM es = {};
    es.dwCookie = reinterpret_cast<DWORD_PTR>(&cookie);
    es.pfnCallback = StreamInCallback;

    SendMessageW(m_hWnd, EM_STREAMIN, SF_RTF, reinterpret_cast<LPARAM>(&es));
    return (es.dwError == 0);
}

bool RichEditView::StreamInSelectionRTF(std::string_view rtfData) {
    if (!m_hWnd || rtfData.empty()) return false;

    StreamInCookie cookie{rtfData.data(), rtfData.size()};
    EDITSTREAM es = {};
    es.dwCookie = reinterpret_cast<DWORD_PTR>(&cookie);
    es.pfnCallback = StreamInCallback;

    SendMessageW(m_hWnd, EM_STREAMIN, SFF_SELECTION | SF_RTF, reinterpret_cast<LPARAM>(&es));
    return (es.dwError == 0);
}

std::string RichEditView::StreamOutRTF() const {
    if (!m_hWnd) return {};

    // 确保临时高亮标记不会污染保存到数据库的 RTF 格式
    if (!m_markedRanges.empty()) {
        const_cast<RichEditView*>(this)->ClearMarks();
    }

    std::string outData;
    EDITSTREAM es = {};
    es.dwCookie = reinterpret_cast<DWORD_PTR>(&outData);
    es.pfnCallback = StreamOutCallback;

    SendMessageW(m_hWnd, EM_STREAMOUT, SF_RTF, reinterpret_cast<LPARAM>(&es));
    return outData;
}

void RichEditView::ApplyHeading(int level) {
    if (!m_hWnd) return;

    // 1. 获取当前选区
    CHARRANGE crOrig = {};
    SendMessageW(m_hWnd, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&crOrig));

    // 2. 按逻辑段落边界扩展选区，自动折行不构成段落边界。
    // RAWTEXT 保留对象及表格标记，保证文本下标与 RichEdit 字符位置一致。
    GETTEXTLENGTHEX lengthInfo = {};
    lengthInfo.flags = GTL_PRECISE | GTL_NUMCHARS;
    lengthInfo.codepage = 1200;
    LONG length = static_cast<LONG>(SendMessageW(m_hWnd, EM_GETTEXTLENGTHEX,
        reinterpret_cast<WPARAM>(&lengthInfo), 0));
    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    GETTEXTEX textInfo = {};
    textInfo.cb = static_cast<DWORD>(text.size() * sizeof(wchar_t));
    textInfo.flags = GT_RAWTEXT;
    textInfo.codepage = 1200;
    LONG copied = static_cast<LONG>(SendMessageW(m_hWnd, EM_GETTEXTEX,
        reinterpret_cast<WPARAM>(&textInfo), reinterpret_cast<LPARAM>(text.data())));
    text.resize(static_cast<size_t>(copied));

    LONG start = std::min(crOrig.cpMin, copied);
    LONG end = std::min(crOrig.cpMax > crOrig.cpMin ? crOrig.cpMax - 1 : crOrig.cpMin, copied);
    auto isParagraphBreak = [](wchar_t ch) { return ch == L'\r' || ch == L'\n'; };
    while (start > 0 && !isParagraphBreak(text[start - 1])) --start;
    while (end < copied && !isParagraphBreak(text[end])) ++end;
    CHARRANGE paragraphRange = { start, end };
    SendMessageW(m_hWnd, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&paragraphRange));

    // 3. 设置字符格式
    CHARFORMAT2W cf = {sizeof(CHARFORMAT2W)};
    cf.dwMask = CFM_FACE | CFM_SIZE | CFM_COLOR | CFM_BOLD | CFM_BACKCOLOR;
    cf.crBackColor = RGB(255, 255, 255);
    cf.dwEffects = CFE_AUTOBACKCOLOR;
    wcscpy_s(cf.szFaceName, L"Segoe UI");

    // 4. 设置段落格式 (段前/段后间距)
    PARAFORMAT2 pf = {sizeof(PARAFORMAT2)};
    pf.dwMask = PFM_SPACEBEFORE | PFM_SPACEAFTER;

    switch (level) {
    case 1:
        cf.yHeight = 360; // 18pt
        cf.dwEffects |= CFE_BOLD;
        cf.crTextColor = RGB(0, 92, 197); // 经典主题品蓝
        pf.dySpaceBefore = 160;
        pf.dySpaceAfter = 100;
        break;
    case 2:
        cf.yHeight = 300; // 15pt
        cf.dwEffects |= CFE_BOLD;
        cf.crTextColor = RGB(3, 102, 214); // 经典主题蓝
        pf.dySpaceBefore = 120;
        pf.dySpaceAfter = 80;
        break;
    case 3:
        cf.yHeight = 260; // 13pt
        cf.dwEffects |= CFE_BOLD;
        cf.crTextColor = RGB(36, 41, 47); // 深炭灰粗体
        pf.dySpaceBefore = 80;
        pf.dySpaceAfter = 60;
        break;
    case 4:
        cf.yHeight = 220; // 11pt
        cf.dwEffects |= CFE_BOLD;
        cf.crTextColor = RGB(36, 41, 47); // 深炭灰粗体
        pf.dySpaceBefore = 60;
        pf.dySpaceAfter = 40;
        break;
    case 0:
    default:
        cf.yHeight = 220; // 11pt
        cf.crTextColor = RGB(33, 37, 41); // 常规深黑
        pf.dySpaceBefore = 0;
        pf.dySpaceAfter = 100;
        break;
    }

    SendMessageW(m_hWnd, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&cf));
    SendMessageW(m_hWnd, EM_SETPARAFORMAT, 0, reinterpret_cast<LPARAM>(&pf));

    // 恢复原来的光标或选区
    SendMessageW(m_hWnd, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&crOrig));
}

int RichEditView::GetCurrentHeadingLevel() const {
    if (!m_hWnd) return 0;

    CHARFORMAT2W cf = {sizeof(CHARFORMAT2W)};
    SendMessageW(m_hWnd, EM_GETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&cf));

    if (cf.yHeight >= 340) {
        return 1;
    } else if (cf.yHeight >= 280) {
        return 2;
    } else if (cf.yHeight >= 250) {
        return 3;
    } else if (cf.yHeight <= 230 && (cf.dwEffects & CFE_BOLD) && cf.yHeight > 0) {
        PARAFORMAT2 pf = {sizeof(PARAFORMAT2)};
        SendMessageW(m_hWnd, EM_GETPARAFORMAT, 0, reinterpret_cast<LPARAM>(&pf));
        if (pf.dySpaceBefore > 0) {
            return 4;
        }
    }
    return 0;
}

void RichEditView::ToggleBold() {
    if (!m_hWnd) return;
    CHARFORMAT2W cf = {sizeof(CHARFORMAT2W)};
    SendMessageW(m_hWnd, EM_GETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&cf));

    cf.dwMask = CFM_BOLD;
    cf.dwEffects ^= CFE_BOLD;
    SendMessageW(m_hWnd, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&cf));
}

void RichEditView::ToggleItalic() {
    if (!m_hWnd) return;
    CHARFORMAT2W cf = {sizeof(CHARFORMAT2W)};
    SendMessageW(m_hWnd, EM_GETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&cf));

    cf.dwMask = CFM_ITALIC;
    cf.dwEffects ^= CFE_ITALIC;
    SendMessageW(m_hWnd, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&cf));
}

void RichEditView::ToggleUnderline() {
    if (!m_hWnd) return;
    CHARFORMAT2W cf = {sizeof(CHARFORMAT2W)};
    SendMessageW(m_hWnd, EM_GETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&cf));

    cf.dwMask = CFM_UNDERLINE;
    cf.dwEffects ^= CFE_UNDERLINE;
    SendMessageW(m_hWnd, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&cf));
}

void RichEditView::ToggleStrike() {
    if (!m_hWnd) return;
    CHARFORMAT2W cf = {sizeof(CHARFORMAT2W)};
    SendMessageW(m_hWnd, EM_GETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&cf));

    cf.dwMask = CFM_STRIKEOUT;
    cf.dwEffects ^= CFE_STRIKEOUT;
    SendMessageW(m_hWnd, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&cf));
}

void RichEditView::SetTextColor(COLORREF color) {
    if (!m_hWnd) return;
    CHARFORMAT2W cf = {sizeof(CHARFORMAT2W)};
    cf.dwMask = CFM_COLOR;
    cf.crTextColor = color;
    SendMessageW(m_hWnd, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&cf));
}

void RichEditView::SetHighlightColor(COLORREF color) {
    if (!m_hWnd) return;
    CHARFORMAT2W cf = {sizeof(CHARFORMAT2W)};
    cf.dwMask = CFM_BACKCOLOR;
    cf.crBackColor = color;
    cf.dwEffects &= ~CFE_AUTOBACKCOLOR;
    SendMessageW(m_hWnd, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&cf));
}

void RichEditView::InsertBulletList() {
    if (!m_hWnd) return;
    PARAFORMAT2 pf = {sizeof(PARAFORMAT2)};
    SendMessageW(m_hWnd, EM_GETPARAFORMAT, 0, reinterpret_cast<LPARAM>(&pf));

    pf.dwMask = PFM_NUMBERING | PFM_OFFSET;
    if (pf.wNumbering == PFN_BULLET) {
        pf.wNumbering = 0; // 取消项目符号
        pf.dxOffset = 0;
    } else {
        pf.wNumbering = PFN_BULLET;
        pf.dxOffset = 280; // 缩进
    }
    SendMessageW(m_hWnd, EM_SETPARAFORMAT, 0, reinterpret_cast<LPARAM>(&pf));
}

void RichEditView::InsertNumberedList() {
    if (!m_hWnd) return;
    PARAFORMAT2 pf = {sizeof(PARAFORMAT2)};
    SendMessageW(m_hWnd, EM_GETPARAFORMAT, 0, reinterpret_cast<LPARAM>(&pf));

    pf.dwMask = PFM_NUMBERING | PFM_OFFSET;
    if (pf.wNumbering == PFN_ARABIC) {
        pf.wNumbering = 0;
        pf.dxOffset = 0;
    } else {
        pf.wNumbering = PFN_ARABIC;
        pf.dxOffset = 280;
    }
    SendMessageW(m_hWnd, EM_SETPARAFORMAT, 0, reinterpret_cast<LPARAM>(&pf));
}

bool RichEditView::InsertImageFromFile(const std::wstring& filePath) {
    if (!m_hWnd) return false;
    if (!std::filesystem::exists(filePath)) return false;

    std::unique_ptr<Gdiplus::Bitmap> pBmp(Gdiplus::Bitmap::FromFile(filePath.c_str()));
    if (!pBmp || pBmp->GetLastStatus() != Gdiplus::Ok) {
        return false;
    }

    UINT origW = pBmp->GetWidth();
    UINT origH = pBmp->GetHeight();
    if (origW == 0 || origH == 0) return false;

    RECT rcClient;
    GetClientRect(m_hWnd, &rcClient);
    UINT dpi = GetDpiForWindow(m_hWnd);
    if (dpi == 0) dpi = 96;

    int marginX = MulDiv(28, dpi, 96);
    int maxDispW = (rcClient.right - rcClient.left) - marginX * 2 - MulDiv(30, dpi, 96);
    if (maxDispW < MulDiv(200, dpi, 96)) {
        maxDispW = MulDiv(650, dpi, 96);
    }

    UINT targetW = origW;
    UINT targetH = origH;
    if (targetW > static_cast<UINT>(maxDispW)) {
        targetH = static_cast<UINT>((static_cast<uint64_t>(origH) * maxDispW) / origW);
        targetW = static_cast<UINT>(maxDispW);
        if (targetH == 0) targetH = 1;
    }

    UINT picwgoal = (targetW * 1440) / dpi;
    UINT pichgoal = (targetH * 1440) / dpi;

    IStream* pStream = nullptr;
    if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &pStream))) {
        return false;
    }

    CLSID clsidPng = {};
    if (GetEncoderClsid(L"image/png", &clsidPng) < 0) {
        clsidPng = { 0x557cf406, 0x1a04, 0x11d3, { 0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e } };
    }

    Gdiplus::Status st = pBmp->Save(pStream, &clsidPng, nullptr);
    if (st != Gdiplus::Ok) {
        pStream->Release();
        return false;
    }

    STATSTG stat;
    if (FAILED(pStream->Stat(&stat, STATFLAG_NONAME))) {
        pStream->Release();
        return false;
    }

    ULONG sizeInBytes = static_cast<ULONG>(stat.cbSize.QuadPart);
    std::vector<BYTE> buffer(sizeInBytes);
    LARGE_INTEGER liZero = {};
    pStream->Seek(liZero, STREAM_SEEK_SET, nullptr);
    ULONG readBytes = 0;
    pStream->Read(buffer.data(), sizeInBytes, &readBytes);
    pStream->Release();

    if (readBytes == 0) return false;

    std::string hexData;
    hexData.reserve(readBytes * 2 + (readBytes / 64) + 64);
    static const char hexDigits[] = "0123456789abcdef";
    for (ULONG i = 0; i < readBytes; ++i) {
        BYTE b = buffer[i];
        hexData.push_back(hexDigits[(b >> 4) & 0x0F]);
        hexData.push_back(hexDigits[b & 0x0F]);
        if ((i + 1) % 64 == 0) {
            hexData.push_back('\n');
        }
    }

    std::string rtf = "{\\rtf1\\ansi\\deff0{\\pict\\pngblip\\picw";
    rtf += std::to_string(origW);
    rtf += "\\pich";
    rtf += std::to_string(origH);
    rtf += "\\picwgoal";
    rtf += std::to_string(picwgoal);
    rtf += "\\pichgoal";
    rtf += std::to_string(pichgoal);
    rtf += "\n";
    rtf += hexData;
    rtf += "}\\par\n}";

    bool ok = StreamInSelectionRTF(rtf);
    if (!ok) {
        HBITMAP hBmp = nullptr;
        if (pBmp->GetHBITMAP(Gdiplus::Color::White, &hBmp) == Gdiplus::Ok && hBmp) {
            bool clipboardOwnsBitmap = false;
            if (OpenClipboard(m_hWnd)) {
                EmptyClipboard();
                clipboardOwnsBitmap = SetClipboardData(CF_BITMAP, hBmp) != nullptr;
                CloseClipboard();
                if (clipboardOwnsBitmap) {
                    SendMessageW(m_hWnd, WM_PASTE, 0, 0);
                    ok = true;
                }
            }
            if (!clipboardOwnsBitmap) {
                DeleteObject(hBmp);
            }
        }
    }

    ApplyDefaultFormatting(false);
    PostMessageW(GetParent(m_hWnd), WM_EDITOR_FORMAT_CHANGED, 0, 0);
    return ok;
}

bool RichEditView::InsertCodeBlock(std::wstring_view codeContent, common::CodeLanguage lang) {
    if (!m_hWnd) return false;

    CHARRANGE crBefore = {};
    SendMessageW(m_hWnd, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&crBefore));

    std::wstring effectiveContent(codeContent);
    if (effectiveContent.empty()) {
        effectiveContent = L"\n\n";
    }

    std::string rtf = common::SyntaxHighlighter::GenerateRtfCodeBlock(effectiveContent, lang);
    bool ok = StreamInSelectionRTF(rtf);

    if (ok) {
        CodeBlockInfo info;
        if (GetCodeBlockAt(crBefore.cpMin, &info) ||
            GetCodeBlockAt(crBefore.cpMin + 1, &info) ||
            GetCodeBlockAt(crBefore.cpMin + 2, &info)) {
            CHARRANGE crTarget = { info.codeStart, info.codeStart };
            SendMessageW(m_hWnd, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&crTarget));
            RECT rcBlock = GetCodeBlockRect(info);
            m_hoverBar.AttachToCodeBlock(info, rcBlock);
        }
    }

    ApplyCodeTypingFormat();
    PostMessageW(GetParent(m_hWnd), WM_EDITOR_FORMAT_CHANGED, 0, 0);
    return ok;
}

void RichEditView::ApplyCodeTypingFormat() {
    CHARFORMAT2W cf{sizeof(cf)};
    cf.dwMask = CFM_FACE | CFM_SIZE | CFM_BOLD | CFM_ITALIC | CFM_HIDDEN | CFM_BACKCOLOR;
    cf.dwEffects = CFE_AUTOBACKCOLOR;
    cf.yHeight = 220;
    wcscpy_s(cf.szFaceName, L"Consolas");
    SendMessageW(m_hWnd, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&cf));
}

bool RichEditView::GetCodeBlockAtCursor(CodeBlockInfo* outInfo) const {
    if (!m_hWnd) return false;
    CHARRANGE cr = {};
    SendMessageW(m_hWnd, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&cr));
    return GetCodeBlockAt(cr.cpMin, outInfo);
}

bool RichEditView::GetCodeBlockAt(long charPos, CodeBlockInfo* outInfo) const {
    if (!m_hWnd) return false;
    IUnknown* pUnk = nullptr;
    SendMessageW(m_hWnd, EM_GETOLEINTERFACE, 0, reinterpret_cast<LPARAM>(&pUnk));
    if (!pUnk) return false;

    ITextDocument2* pDoc2 = nullptr;
    bool found = false;
    if (SUCCEEDED(pUnk->QueryInterface(__uuidof(ITextDocument2), reinterpret_cast<void**>(&pDoc2)))) {
        ITextRange2* pRange = nullptr;
        if (SUCCEEDED(pDoc2->Range2(charPos, charPos, &pRange))) {
            long delta = 0;
            if (SUCCEEDED(pRange->Expand(tomTable, &delta)) && delta > 0) {
                long tStart = 0, tEnd = 0;
                pRange->GetStart(&tStart);
                pRange->GetEnd(&tEnd);

                BSTR bstr = nullptr;
                pRange->GetText(&bstr);
                if (bstr) {
                    std::wstring tableText = bstr;
                    SysFreeString(bstr);

                    // Adjacent code blocks can be returned as one TOM table range
                    // when no paragraph separates their RTF. Narrow the range to
                    // the row containing the requested character before parsing or
                    // editing it.
                    const long relativePos = std::clamp(charPos - tStart, 0L,
                        static_cast<long>(tableText.size()));
                    long rowStartOffset = relativePos;
                    while (rowStartOffset > 0 &&
                           tableText[static_cast<size_t>(rowStartOffset)] != static_cast<wchar_t>(0xfff9)) {
                        --rowStartOffset;
                    }
                    long rowEndOffset = relativePos;
                    while (rowEndOffset < static_cast<long>(tableText.size()) &&
                           tableText[static_cast<size_t>(rowEndOffset)] != static_cast<wchar_t>(0xfffb)) {
                        ++rowEndOffset;
                    }
                    if (rowStartOffset < rowEndOffset &&
                        tableText[static_cast<size_t>(rowStartOffset)] == static_cast<wchar_t>(0xfff9)) {
                        const long rowEndExclusive = std::min(
                            static_cast<long>(tableText.size()), rowEndOffset + 1);
                        tStart += rowStartOffset;
                        tEnd = tStart + (rowEndExclusive - rowStartOffset);
                        tableText = tableText.substr(
                            static_cast<size_t>(rowStartOffset),
                            static_cast<size_t>(rowEndExclusive - rowStartOffset));
                    }

                    // 1. 优先检测现代无污染单单元格代码块标记 [lang:<tag>]
                    size_t tagPos = tableText.find(L"[lang:");
                    if (tagPos != std::wstring::npos) {
                        size_t tagEnd = tableText.find(L']', tagPos);
                        if (tagEnd != std::wstring::npos) {
                            found = true;
                            if (outInfo) {
                                outInfo->isCodeBlock = true;
                                outInfo->tableStart = tStart;
                                outInfo->tableEnd = tEnd;

                                std::wstring langTag = tableText.substr(tagPos + 6, tagEnd - (tagPos + 6));
                                outInfo->currentLang = common::StringToCodeLanguage(langTag);

                                size_t codeStartIdx = tagEnd + 1;
                                outInfo->codeStart = tStart + static_cast<long>(codeStartIdx);

                                size_t codeEndIdx = tableText.rfind(static_cast<wchar_t>(0x07));
                                if (codeEndIdx == std::wstring::npos || codeEndIdx <= codeStartIdx) {
                                    codeEndIdx = tableText.rfind(static_cast<wchar_t>(0xfffb));
                                }
                                if (codeEndIdx == std::wstring::npos || codeEndIdx <= codeStartIdx) {
                                    codeEndIdx = tableText.size();
                                }
                                outInfo->codeEnd = tStart + static_cast<long>(codeEndIdx);

                                std::wstring rawCode = tableText.substr(codeStartIdx, codeEndIdx - codeStartIdx);
                                std::wstring cleanCode;
                                cleanCode.reserve(rawCode.size());
                                for (size_t i = 0; i < rawCode.size(); ++i) {
                                    if (rawCode[i] == L'\r') {
                                        cleanCode += L"\r\n";
                                        if (i + 1 < rawCode.size() && rawCode[i + 1] == L'\n') {
                                            i++;
                                        }
                                    } else if (rawCode[i] != static_cast<wchar_t>(0x07) &&
                                               rawCode[i] != static_cast<wchar_t>(0xfffb) &&
                                               rawCode[i] != static_cast<wchar_t>(0xfff9)) {
                                        cleanCode += rawCode[i];
                                    }
                                }
                                outInfo->codeText = std::move(cleanCode);
                            }
                        }
                    }

                    // 2. 向后兼容上一代旧版本笔记中的双行代码块 (顶栏带 [ 复制 ] 和 [ ▾ ])
                    if (!found) {
                        bool hasCopy = (tableText.find(L"复制") != std::wstring::npos);
                        bool hasArrow = (tableText.find(L"\u25be") != std::wstring::npos ||
                                         tableText.find(L"▾") != std::wstring::npos);

                        if (hasCopy && hasArrow) {
                            found = true;
                            if (outInfo) {
                                outInfo->isCodeBlock = true;
                                outInfo->tableStart = tStart;
                                outInfo->tableEnd = tEnd;

                                size_t cell1EndIdx = tableText.find(static_cast<wchar_t>(0x07));
                                size_t cell2EndIdx = (cell1EndIdx != std::wstring::npos) ?
                                    tableText.find(static_cast<wchar_t>(0x07), cell1EndIdx + 1) : std::wstring::npos;

                                if (cell1EndIdx != std::wstring::npos && cell2EndIdx != std::wstring::npos) {
                                    std::wstring cell1Text = tableText.substr(0, cell1EndIdx);
                                    outInfo->currentLang = common::CodeLanguage::PlainText;
                                    if (cell1Text.find(L"C++") != std::wstring::npos || cell1Text.find(L"C / C++") != std::wstring::npos) {
                                        outInfo->currentLang = common::CodeLanguage::Cpp;
                                    } else if (cell1Text.find(L"Python") != std::wstring::npos) {
                                        outInfo->currentLang = common::CodeLanguage::Python;
                                    } else if (cell1Text.find(L"JavaScript") != std::wstring::npos || cell1Text.find(L"TypeScript") != std::wstring::npos) {
                                        outInfo->currentLang = common::CodeLanguage::JavaScript;
                                    } else if (cell1Text.find(L"SQL") != std::wstring::npos) {
                                        outInfo->currentLang = common::CodeLanguage::Sql;
                                    } else if (cell1Text.find(L"Shell") != std::wstring::npos || cell1Text.find(L"Bash") != std::wstring::npos) {
                                        outInfo->currentLang = common::CodeLanguage::Shell;
                                    } else if (cell1Text.find(L"HTML") != std::wstring::npos || cell1Text.find(L"XML") != std::wstring::npos) {
                                        outInfo->currentLang = common::CodeLanguage::Html;
                                    } else {
                                        outInfo->currentLang = common::CodeLanguage::PlainText;
                                    }

                                    size_t codeStartIdx = cell2EndIdx + 1;
                                    while (codeStartIdx < tableText.size() &&
                                           (tableText[codeStartIdx] == 0xfffb ||
                                            tableText[codeStartIdx] == 0xfff9 ||
                                            tableText[codeStartIdx] == L'\r' ||
                                            tableText[codeStartIdx] == L'\n')) {
                                        codeStartIdx++;
                                    }
                                    outInfo->codeStart = tStart + static_cast<long>(codeStartIdx);

                                    size_t codeEndIdx = tableText.find(static_cast<wchar_t>(0x07), codeStartIdx);
                                    if (codeEndIdx == std::wstring::npos) {
                                        codeEndIdx = tableText.find(static_cast<wchar_t>(0xfffb), codeStartIdx);
                                    }
                                    if (codeEndIdx == std::wstring::npos) {
                                        codeEndIdx = tableText.size();
                                    }
                                    outInfo->codeEnd = tStart + static_cast<long>(codeEndIdx);

                                    std::wstring rawCode = tableText.substr(codeStartIdx, codeEndIdx - codeStartIdx);
                                    std::wstring cleanCode;
                                    cleanCode.reserve(rawCode.size());
                                    for (size_t i = 0; i < rawCode.size(); ++i) {
                                        if (rawCode[i] == L'\r') {
                                            cleanCode += L"\r\n";
                                            if (i + 1 < rawCode.size() && rawCode[i + 1] == L'\n') {
                                                i++;
                                            }
                                        } else {
                                            cleanCode += rawCode[i];
                                        }
                                    }
                                    outInfo->codeText = std::move(cleanCode);
                                }
                            }
                        }
                    }
                }
            }
            pRange->Release();
        }
        pDoc2->Release();
    }
    pUnk->Release();
    return found;
}

bool RichEditView::CopyCodeBlockText(const CodeBlockInfo& info) const {
    if (!m_hWnd) return false;
    if (!OpenClipboard(m_hWnd)) return false;
    const std::wstring& text = info.codeText;
    size_t byteCount = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL hGlob = GlobalAlloc(GMEM_MOVEABLE, byteCount);
    bool copied = false;
    if (hGlob) {
        void* pMem = GlobalLock(hGlob);
        if (pMem) {
            memcpy(pMem, text.c_str(), byteCount);
            GlobalUnlock(hGlob);
            copied = EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, hGlob) != nullptr;
        }
        if (!copied) GlobalFree(hGlob);
    }
    CloseClipboard();
    return copied;
}

bool RichEditView::SwitchCodeBlockLanguage(const CodeBlockInfo& info, common::CodeLanguage newLang) {
    if (!m_hWnd || !info.isCodeBlock) return false;

    std::string newRtf = common::SyntaxHighlighter::GenerateRtfCodeBlock(info.codeText, newLang);

    CHARRANGE cr = { info.tableStart, info.tableEnd };
    SendMessageW(m_hWnd, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&cr));

    bool ok = StreamInSelectionRTF(newRtf);
    if (ok) {
        CodeBlockInfo newInfo;
        if (GetCodeBlockAt(info.tableStart, &newInfo)) {
            CHARRANGE crTarget = { newInfo.codeStart, newInfo.codeStart };
            SendMessageW(m_hWnd, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&crTarget));
            RECT rcBlock = GetCodeBlockRect(newInfo);
            m_hoverBar.AttachToCodeBlock(newInfo, rcBlock);
        }
        ApplyCodeTypingFormat();
        PostMessageW(GetParent(m_hWnd), WM_EDITOR_FORMAT_CHANGED, 0, 0);
        SendMessageW(GetParent(m_hWnd), WM_CODEBLOCK_LANG_CHANGED, static_cast<WPARAM>(newLang), 0);
    }
    return ok;
}

void RichEditView::RefreshCodeBlockLayout() {
    if (!m_hWnd || m_updatingCodeLayout) return;
    using Microsoft::WRL::ComPtr;
    ComPtr<IUnknown> ole;
    SendMessageW(m_hWnd, EM_GETOLEINTERFACE, 0, reinterpret_cast<LPARAM>(ole.GetAddressOf()));
    ComPtr<ITextDocument2> doc;
    if (!ole || FAILED(ole.As(&doc))) return;
    ComPtr<ITextRange2> search;
    if (FAILED(doc->Range2(0, 0, &search))) return;
    long length = 0;
    search->GetStoryLength(&length);
    search->SetRange(0, length);
    BSTR story = nullptr;
    search->GetText(&story);
    const std::wstring text = story ? std::wstring(story, SysStringLen(story)) : std::wstring();
    SysFreeString(story);
    if (text.find(L"[lang:") == std::wstring::npos) return;
    RECT format;
    SendMessageW(m_hWnd, EM_GETRECT, 0, reinterpret_cast<LPARAM>(&format));
    const UINT dpi = std::max(96U, GetDpiForWindow(m_hWnd));
    const long width = std::max(600, MulDiv(format.right - format.left - 2, 1440, dpi));
    const LRESULT modified = SendMessageW(m_hWnd, EM_GETMODIFY, 0, 0);
    const LRESULT events = SendMessageW(m_hWnd, EM_GETEVENTMASK, 0, 0);
    m_updatingCodeLayout = true;
    SendMessageW(m_hWnd, EM_SETEVENTMASK, 0, 0);
    long freezeCount = 0;
    doc->Freeze(&freezeCount);
    doc->Undo(tomSuspend, nullptr);
    long next = 0;
    while (next < static_cast<long>(text.size())) {
        const size_t match = text.find(L"[lang:", static_cast<size_t>(next));
        if (match == std::wstring::npos) break;
        const long start = static_cast<long>(match);
        next = start + 6;
        CodeBlockInfo info;
        if (!GetCodeBlockAt(start, &info)) continue;
        next = std::max(next, info.tableEnd);
        ComPtr<ITextRange2> range;
        ComPtr<ITextRow> row;
        if (FAILED(doc->Range2(info.codeStart, info.codeStart, &range)) ||
            FAILED(range->GetRow(&row))) continue;
        long cells = 0;
        row->Reset(tomRowUpdate);
        row->GetCellCount(&cells);
        if (cells != 1) continue; // Leave legacy multi-cell tables intact.
        row->SetCellIndex(0);
        row->SetIndent(0);
        row->SetCellMargin(240);
        row->SetCellWidth(width);
        row->SetCellColorBack(RGB(246, 248, 250));
        row->SetCellShading(0);
        row->SetCellBorderColors(RGB(225, 228, 232), RGB(225, 228, 232),
            RGB(225, 228, 232), RGB(225, 228, 232));
        row->SetCellBorderWidths(15, 15, 15, 15);
        row->Apply(1, tomRowApplyDefault);

        range->SetRange(info.codeStart, info.codeEnd);
        ComPtr<ITextFont> font;
        if (SUCCEEDED(range->GetFont(&font))) {
            BSTR face = SysAllocString(L"Consolas");
            font->SetName(face);
            SysFreeString(face);
            font->SetSize(11);
            font->SetBackColor(tomAutoColor);
        }
        ComPtr<ITextPara> para;
        if (SUCCEEDED(range->GetPara(&para))) {
            para->SetSpaceBefore(0);
            para->SetSpaceAfter(0);
            para->SetLineSpacing(tomLineSpaceMultiple, 1.25f);
        }
        range->SetRange(info.codeStart, info.codeStart);
        para.Reset();
        if (SUCCEEDED(range->GetPara(&para))) para->SetSpaceBefore(10);
        range->SetRange(std::max(info.codeStart, info.codeEnd - 1), std::max(info.codeStart, info.codeEnd - 1));
        para.Reset();
        if (SUCCEEDED(range->GetPara(&para))) para->SetSpaceAfter(10);
    }
    doc->Undo(tomResume, nullptr);
    doc->Unfreeze(&freezeCount);
    SendMessageW(m_hWnd, EM_SETMODIFY, modified, 0);
    SendMessageW(m_hWnd, EM_SETEVENTMASK, 0, events);
    m_updatingCodeLayout = false;
    InvalidateRect(m_hWnd, nullptr, FALSE);
}

void RichEditView::PaintCodeBlockFrames() {
    if (m_updatingCodeLayout) return;
    const int firstLine = static_cast<int>(SendMessageW(m_hWnd, EM_GETFIRSTVISIBLELINE, 0, 0));
    const int lines = static_cast<int>(SendMessageW(m_hWnd, EM_GETLINECOUNT, 0, 0));
    HDC dc = GetDC(m_hWnd);
    const int saved = SaveDC(dc);
    RECT format;
    SendMessageW(m_hWnd, EM_GETRECT, 0, reinterpret_cast<LPARAM>(&format));
    // RichEdit's formatting rectangle excludes the reserved bottom margin and
    // scrollbar area.  Keep the overlay inside that same viewport: drawing to
    // client.bottom leaves stale rounded borders/text in the editor's bottom
    // margin after scrolling a code block.
    IntersectClipRect(dc, format.left, format.top, format.right, format.bottom);
    long lastTableEnd = -1;
    const int radius = MulDiv(8, GetDpiForWindow(m_hWnd), 96);
    for (int line = firstLine; line < lines; ++line) {
        const long pos = static_cast<long>(SendMessageW(m_hWnd, EM_LINEINDEX, line, 0));
        POINTL point{};
        SendMessageW(m_hWnd, EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&point), pos);
        if (point.y > format.bottom) break;
        if (pos < lastTableEnd) continue;
        CodeBlockInfo info;
        if (!GetCodeBlockAt(pos, &info)) continue;
        lastTableEnd = info.tableEnd;
        RECT block = GetCodeBlockRect(info);
        // Mask only the rounded corners. Native RichEdit continues to paint
        // the text, selection, caret and cell background normally.
        HRGN outer = CreateRectRgn(block.left - 1, block.top - 1, block.right + 1, block.bottom + 1);
        HRGN rounded = CreateRoundRectRgn(block.left, block.top, block.right + 1, block.bottom + 1,
            radius * 2, radius * 2);
        CombineRgn(outer, outer, rounded, RGN_DIFF);
        FillRgn(dc, outer, reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(225, 228, 232));
        HGDIOBJ oldPen = SelectObject(dc, pen);
        HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
        RoundRect(dc, block.left, block.top, block.right, block.bottom, radius * 2, radius * 2);
        SelectObject(dc, oldBrush);
        SelectObject(dc, oldPen);
        DeleteObject(pen);
        DeleteObject(outer);
        DeleteObject(rounded);
    }
    RestoreDC(dc, saved);
    ReleaseDC(m_hWnd, dc);
}

RECT RichEditView::GetCodeBlockRect(const CodeBlockInfo& info) const {
    RECT rc = { 0, 0, 0, 0 };
    if (!m_hWnd || !info.isCodeBlock) return rc;
    using Microsoft::WRL::ComPtr;
    const UINT dpi = std::max(96U, GetDpiForWindow(m_hWnd));
    long margin = 240, rowWidth = 0;
    ComPtr<IUnknown> ole;
    SendMessageW(m_hWnd, EM_GETOLEINTERFACE, 0, reinterpret_cast<LPARAM>(ole.GetAddressOf()));
    ComPtr<ITextDocument2> doc;
    ComPtr<ITextRange2> range;
    if (ole && SUCCEEDED(ole.As(&doc))) {
        ComPtr<ITextRange2> cellRange;
        ComPtr<ITextRow> row;
        if (SUCCEEDED(doc->Range2(info.codeStart, info.codeStart, &cellRange)) &&
            SUCCEEDED(cellRange->GetRow(&row))) {
            row->Reset(tomRowUpdate);
            row->GetCellMargin(&margin);
            long cells = 0;
            row->GetCellCount(&cells);
            for (long cell = 0; cell < cells; ++cell) {
                long width = 0;
                row->SetCellIndex(cell);
                if (SUCCEEDED(row->GetCellWidth(&width))) rowWidth += width;
            }
        }
        if (SUCCEEDED(doc->Range2(info.tableStart, info.tableEnd, &range))) {
            // S_FALSE means no point was returned. Offscreen endpoints still
            // belong to the same cell and must keep its actual border geometry.
            const long flags = tomClientCoord | tomAllowOffClient;
            if (range->GetPoint(tomStart | TA_TOP | TA_LEFT | flags, &rc.left, &rc.top) == S_OK &&
                range->GetPoint(tomEnd | TA_BOTTOM | TA_RIGHT | flags, &rc.right, &rc.bottom) == S_OK) {
                rc.left -= MulDiv(margin, dpi, 1440);
                if (rc.right > rc.left && rc.bottom > rc.top) return rc;
            }
        }
    }

    // EM_POSFROMCHAR also reports the text inset. Reuse the row metadata even
    // when TOM cannot return the rendered endpoints; negative x is valid while scrolling.
    POINTL ptStart{}, ptEnd{};
    SendMessageW(m_hWnd, EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&ptStart), info.tableStart);
    SendMessageW(m_hWnd, EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&ptEnd), std::max(info.tableStart, info.tableEnd - 1));
    rc.left = ptStart.x - MulDiv(margin, dpi, 1440);
    rc.top = ptStart.y;
    if (rowWidth > 0) {
        // Match TOM's outer edge, including the native one-pixel border.
        rc.right = rc.left + MulDiv(rowWidth, dpi, 1440) + 1;
    } else {
        RECT format;
        SendMessageW(m_hWnd, EM_GETRECT, 0, reinterpret_cast<LPARAM>(&format));
        rc.right = std::max(rc.left + 1, format.right);
    }
    rc.bottom = ptEnd.y + MulDiv(36, dpi, 96);

    return rc;
}

void RichEditView::OnMouseMove(long charPos, POINT /*ptClient*/) {
    if (!m_hWnd) return;
    // A queued editor mouse event must not steal the target from its child bar.
    if (m_hoverBar.IsInteracting()) return;

    CodeBlockInfo info;
    if (charPos >= 0 && GetCodeBlockAt(charPos, &info)) {
        RECT rcBlock = GetCodeBlockRect(info);
        m_hoverBar.AttachToCodeBlock(info, rcBlock);
    } else {
        // 鼠标移出代码块时，若光标仍在某个代码块内，保持悬浮条吸附在光标所在代码块
        CodeBlockInfo cursorInfo;
        if (GetCodeBlockAtCursor(&cursorInfo)) {
            RECT rcBlock = GetCodeBlockRect(cursorInfo);
            m_hoverBar.AttachToCodeBlock(cursorInfo, rcBlock);
        } else {
            // 光标也不在代码块内，且鼠标不在悬浮条自身之上 -> 隐藏
            if (!m_hoverBar.IsMouseOver()) {
                m_hoverBar.Hide();
            }
        }
    }
}

void RichEditView::OnMouseLeave() {
    if (!m_hWnd) return;
    // Entering the child bar also sends WM_MOUSELEAVE to RichEdit. Keep the
    // hovered block instead of moving back to the block containing the caret.
    if (m_hoverBar.IsInteracting()) return;

    // 鼠标离开 RichEdit 控件：若光标仍停留在代码块内，悬浮条继续显示；否则隐藏
    CodeBlockInfo cursorInfo;
    if (GetCodeBlockAtCursor(&cursorInfo)) {
        RECT rcBlock = GetCodeBlockRect(cursorInfo);
        m_hoverBar.AttachToCodeBlock(cursorInfo, rcBlock);
    } else {
        if (!m_hoverBar.IsMouseOver()) {
            m_hoverBar.Hide();
        }
    }
}

void RichEditView::OnSelChange() {
    if (!m_hWnd) return;
    if (m_hoverBar.IsInteracting()) return;

    CodeBlockInfo cursorInfo;
    if (GetCodeBlockAtCursor(&cursorInfo)) {
        RECT rcBlock = GetCodeBlockRect(cursorInfo);
        m_hoverBar.AttachToCodeBlock(cursorInfo, rcBlock);
    } else {
        // 光标不在代码块内：检查鼠标是否当前正悬停在某个代码块上
        POINT ptCursor;
        GetCursorPos(&ptCursor);
        ScreenToClient(m_hWnd, &ptCursor);
        POINTL ptl = { ptCursor.x, ptCursor.y };
        LRESULT hoverCharPos = SendMessageW(m_hWnd, EM_CHARFROMPOS, 0, reinterpret_cast<LPARAM>(&ptl));

        CodeBlockInfo hoverInfo;
        if (hoverCharPos >= 0 && GetCodeBlockAt(static_cast<long>(hoverCharPos), &hoverInfo)) {
            RECT rcBlock = GetCodeBlockRect(hoverInfo);
            m_hoverBar.AttachToCodeBlock(hoverInfo, rcBlock);
        } else {
            if (!m_hoverBar.IsMouseOver()) {
                m_hoverBar.Hide();
            }
        }
    }
}

void RichEditView::UpdateHoverBarPosition() {
    if (!m_hWnd) return;

    long activeTable = m_hoverBar.GetActiveTableStart();
    if (activeTable < 0) return;

    CodeBlockInfo info;
    if (!GetCodeBlockAt(activeTable, &info)) {
        m_hoverBar.Hide();
        return;
    }

    RECT rcBlock = GetCodeBlockRect(info);
    RECT rcClient;
    GetClientRect(m_hWnd, &rcClient);

    UINT dpi = GetDpiForWindow(m_hWnd);
    if (dpi == 0) dpi = 96;
    int minVisibleH = MulDiv(16, dpi, 96);

    // 视口判断：完全滚出可视区域或残余高度过小时隐藏
    if (rcBlock.bottom <= minVisibleH || rcBlock.top >= rcClient.bottom) {
        m_hoverBar.Hide();
    } else {
        m_hoverBar.UpdatePosition(rcBlock);
    }
}

bool RichEditView::IsCursorInTable() const {
    if (!m_hWnd) return false;
    PARAFORMAT2 pf{ sizeof(PARAFORMAT2) };
    SendMessageW(m_hWnd, EM_GETPARAFORMAT, 0, reinterpret_cast<LPARAM>(&pf));
    return (pf.dwMask & PFM_TABLE) && (pf.wEffects & PFE_TABLE);
}

bool RichEditView::InsertTable(int rows, int cols) {
    if (!m_hWnd || rows <= 0 || cols <= 0) return false;

    // 确保插入前重置为默认正文字体样式，避免继承标题大字号
    ApplyDefaultFormatting(false);

    IUnknown* pUnk = nullptr;
    SendMessageW(m_hWnd, EM_GETOLEINTERFACE, 0, reinterpret_cast<LPARAM>(&pUnk));
    if (!pUnk) return false;

    ITextDocument2* pDoc2 = nullptr;
    bool ok = false;
    if (SUCCEEDED(pUnk->QueryInterface(__uuidof(ITextDocument2), reinterpret_cast<void**>(&pDoc2)))) {
        ITextSelection* pSel = nullptr;
        if (SUCCEEDED(pDoc2->GetSelection(&pSel))) {
            long cpMin = 0;
            pSel->GetStart(&cpMin);
            ITextRange2* pRange2 = nullptr;
            if (SUCCEEDED(pDoc2->Range2(cpMin, cpMin, &pRange2))) {
                ITextRow* pRow = nullptr;
                if (SUCCEEDED(pRange2->GetRow(&pRow))) {
                    pRow->SetIndent(144);
                    pRow->SetCellCount(cols);
                    int perColWidth = std::max(1200, 7200 / cols);
                    for (int c = 0; c < cols; ++c) {
                        pRow->SetCellIndex(c);
                        pRow->SetCellWidth(perColWidth);
                        pRow->SetCellBorderWidths(15, 15, 15, 15);
                        pRow->SetCellBorderColors(RGB(226, 230, 234), RGB(226, 230, 234), RGB(226, 230, 234), RGB(226, 230, 234));
                    }
                    if (SUCCEEDED(pRow->Insert(rows))) {
                        ok = true;
                        pSel->SetRange(cpMin, cpMin);
                        long delta = 0;
                        pSel->Expand(tomTable, &delta);
                        ApplyDefaultFormatting(false);
                        pSel->SetRange(cpMin + 2, cpMin + 2);
                    }
                    pRow->Release();
                }
                pRange2->Release();
            }
            pSel->Release();
        }
        pDoc2->Release();
    }
    pUnk->Release();
    SetFocus(m_hWnd);
    return ok;
}

bool RichEditView::NavigateTableCell(bool forward) {
    if (!m_hWnd) return false;
    IUnknown* pUnk = nullptr;
    SendMessageW(m_hWnd, EM_GETOLEINTERFACE, 0, reinterpret_cast<LPARAM>(&pUnk));
    if (!pUnk) return false;

    ITextDocument* pDoc = nullptr;
    bool success = false;
    if (SUCCEEDED(pUnk->QueryInterface(__uuidof(ITextDocument), reinterpret_cast<void**>(&pDoc)))) {
        ITextSelection* pSel = nullptr;
        if (SUCCEEDED(pDoc->GetSelection(&pSel))) {
            long delta = 0;
            HRESULT hr = pSel->Move(tomCell, forward ? 1 : -1, &delta);
            success = (SUCCEEDED(hr) && delta != 0);
            pSel->Release();
        }
        pDoc->Release();
    }
    pUnk->Release();
    return success;
}

bool RichEditView::InsertTableRow(bool below) {
    if (!m_hWnd || !IsCursorInTable()) return false;
    IUnknown* pUnk = nullptr;
    SendMessageW(m_hWnd, EM_GETOLEINTERFACE, 0, reinterpret_cast<LPARAM>(&pUnk));
    if (!pUnk) return false;

    ITextDocument2* pDoc2 = nullptr;
    bool ok = false;
    if (SUCCEEDED(pUnk->QueryInterface(__uuidof(ITextDocument2), reinterpret_cast<void**>(&pDoc2)))) {
        ITextSelection* pSel = nullptr;
        if (SUCCEEDED(pDoc2->GetSelection(&pSel))) {
            long curCp = 0;
            pSel->GetStart(&curCp);

            long storyLen = 0;
            pSel->GetStoryLength(&storyLen);

            ITextRange* pDocRange = nullptr;
            pDoc2->Range(0, storyLen, &pDocRange);
            BSTR text = nullptr;
            pDocRange->GetText(&text);

            int len = SysStringLen(text);
            int rowStart = curCp;
            while (rowStart > 0 && (unsigned short)text[rowStart] != 0xfff9) {
                rowStart--;
            }
            int rowEnd = curCp;
            while (rowEnd < len && (unsigned short)text[rowEnd] != 0xfffb) {
                rowEnd++;
            }

            int numCells = 0;
            for (int i = rowStart; i < rowEnd; ++i) {
                if ((unsigned short)text[i] == 0x07) {
                    numCells++;
                }
            }
            if (numCells == 0) numCells = 3;

            int insPos = below ? (rowEnd + 2) : rowStart;
            ITextRange2* pInsRange = nullptr;
            if (SUCCEEDED(pDoc2->Range2(insPos, insPos, &pInsRange))) {
                ITextRow* pRow = nullptr;
                if (SUCCEEDED(pInsRange->GetRow(&pRow))) {
                    pRow->SetIndent(144);
                    pRow->SetCellCount(numCells);
                    int perColWidth = std::max(1200, 7200 / numCells);
                    for (int c = 0; c < numCells; ++c) {
                        pRow->SetCellIndex(c);
                        pRow->SetCellWidth(perColWidth);
                        pRow->SetCellBorderWidths(15, 15, 15, 15);
                        pRow->SetCellBorderColors(RGB(226, 230, 234), RGB(226, 230, 234), RGB(226, 230, 234), RGB(226, 230, 234));
                    }
                    if (SUCCEEDED(pRow->Insert(1))) {
                        ok = true;
                        pSel->SetRange(insPos + 2, insPos + 2);
                    }
                    pRow->Release();
                }
                pInsRange->Release();
            }

            SysFreeString(text);
            pDocRange->Release();
            pSel->Release();
        }
        pDoc2->Release();
    }
    pUnk->Release();
    return ok;
}

bool RichEditView::DeleteTableRow() {
    if (!m_hWnd || !IsCursorInTable()) return false;
    IUnknown* pUnk = nullptr;
    SendMessageW(m_hWnd, EM_GETOLEINTERFACE, 0, reinterpret_cast<LPARAM>(&pUnk));
    if (!pUnk) return false;

    ITextDocument2* pDoc2 = nullptr;
    bool ok = false;
    if (SUCCEEDED(pUnk->QueryInterface(__uuidof(ITextDocument2), reinterpret_cast<void**>(&pDoc2)))) {
        ITextSelection* pSel = nullptr;
        if (SUCCEEDED(pDoc2->GetSelection(&pSel))) {
            long curCp = 0;
            pSel->GetStart(&curCp);

            long storyLen = 0;
            pSel->GetStoryLength(&storyLen);

            ITextRange* pDocRange = nullptr;
            pDoc2->Range(0, storyLen, &pDocRange);
            BSTR text = nullptr;
            pDocRange->GetText(&text);

            int len = SysStringLen(text);
            int rowStart = curCp;
            while (rowStart > 0 && (unsigned short)text[rowStart] != 0xfff9) {
                rowStart--;
            }
            int rowEnd = curCp;
            while (rowEnd < len && (unsigned short)text[rowEnd] != 0xfffb) {
                rowEnd++;
            }

            // Count rows only in the table containing the caret.
            long tableStart = rowStart;
            long tableEnd = rowEnd;
            long savedStart = 0;
            pSel->GetStart(&savedStart);
            long tableDelta = 0;
            if (SUCCEEDED(pSel->Expand(tomTable, &tableDelta))) {
                pSel->GetStart(&tableStart);
                pSel->GetEnd(&tableEnd);
                pSel->SetRange(savedStart, savedStart);
            }
            int tableRows = 0;
            for (long i = std::max(0L, tableStart); i < std::min(static_cast<long>(len), tableEnd); ++i) {
                if ((unsigned short)text[i] == 0xfff9) ++tableRows;
            }

            if (tableRows <= 1) {
                SysFreeString(text);
                pDocRange->Release();
                pSel->Release();
                pDoc2->Release();
                pUnk->Release();
                return DeleteTable();
            }

            ITextRange* pDelRange = nullptr;
            int delEnd = (rowEnd + 2 <= len) ? (rowEnd + 2) : rowEnd;
            if (SUCCEEDED(pDoc2->Range(rowStart, delEnd, &pDelRange))) {
                long delta = 0;
                pDelRange->Delete(tomCharacter, 0, &delta);
                pDelRange->Release();
                ok = true;
            }

            SysFreeString(text);
            pDocRange->Release();
            pSel->Release();
        }
        pDoc2->Release();
    }
    pUnk->Release();
    return ok;
}

bool RichEditView::DeleteTable() {
    if (!m_hWnd || !IsCursorInTable()) return false;
    IUnknown* pUnk = nullptr;
    SendMessageW(m_hWnd, EM_GETOLEINTERFACE, 0, reinterpret_cast<LPARAM>(&pUnk));
    if (!pUnk) return false;

    ITextDocument2* pDoc2 = nullptr;
    bool ok = false;
    if (SUCCEEDED(pUnk->QueryInterface(__uuidof(ITextDocument2), reinterpret_cast<void**>(&pDoc2)))) {
        ITextSelection* pSel = nullptr;
        if (SUCCEEDED(pDoc2->GetSelection(&pSel))) {
            long delta = 0;
            if (SUCCEEDED(pSel->Expand(tomTable, &delta))) {
                pSel->Delete(tomCharacter, 0, &delta);
                ok = true;
            }
            pSel->Release();
        }
        pDoc2->Release();
    }
    pUnk->Release();
    return ok;
}

bool RichEditView::InsertTableColumn(bool right) {
    if (!m_hWnd || !IsCursorInTable()) return false;
    IUnknown* pUnk = nullptr;
    SendMessageW(m_hWnd, EM_GETOLEINTERFACE, 0, reinterpret_cast<LPARAM>(&pUnk));
    if (!pUnk) return false;

    ITextDocument2* pDoc2 = nullptr;
    bool ok = false;
    if (SUCCEEDED(pUnk->QueryInterface(__uuidof(ITextDocument2), reinterpret_cast<void**>(&pDoc2)))) {
        ITextSelection* pSel = nullptr;
        if (SUCCEEDED(pDoc2->GetSelection(&pSel))) {
            long curCp = 0;
            pSel->GetStart(&curCp);

            long storyLen = 0;
            pSel->GetStoryLength(&storyLen);

            long tableDelta = 0, tableStart = 0, tableEnd = 0;
            if (FAILED(pSel->Expand(tomTable, &tableDelta))) { pSel->Release(); pDoc2->Release(); pUnk->Release(); return false; }
            pSel->GetStart(&tableStart); pSel->GetEnd(&tableEnd); pSel->SetRange(curCp, curCp);
            ITextRange* pDocRange = nullptr;
            pDoc2->Range(0, storyLen, &pDocRange);
            BSTR text = nullptr;
            pDocRange->GetText(&text);
            int len = SysStringLen(text);

            std::vector<int> rows;
            for (int i = static_cast<int>(tableStart); i < tableEnd && i < len; ++i)
                if ((unsigned short)text[i] == 0xfff9) rows.push_back(i);
            int targetCol = 0;
            for (int i = static_cast<int>(tableStart); i < curCp && i < len; ++i)
                if ((unsigned short)text[i] == 0x07) ++targetCol;
            for (auto it = rows.rbegin(); it != rows.rend(); ++it) {
                ITextRange2* pRowRange = nullptr; ITextRow* pRow = nullptr;
                if (SUCCEEDED(pDoc2->Range2(*it, *it, &pRowRange)) && SUCCEEDED(pRowRange->GetRow(&pRow))) {
                    long count = 0;
                    if (SUCCEEDED(pRow->GetCellCount(&count)) && count > 0) {
                        pRow->SetCellIndex(std::min(count - 1, static_cast<long>(targetCol + (right ? 1 : 0))));
                        if (SUCCEEDED(pRow->SetCellCount(count + 1)) && SUCCEEDED(pRow->Apply(1, tomCellStructureChangeOnly))) ok = true;
                    }
                    pRow->Release();
                }
                if (pRowRange) pRowRange->Release();
            }

            SysFreeString(text);
            pDocRange->Release();
            pSel->Release();
        }
        pDoc2->Release();
    }
    pUnk->Release();
    return ok;
}

bool RichEditView::DeleteTableColumn() {
    if (!m_hWnd || !IsCursorInTable()) return false;
    IUnknown* pUnk = nullptr;
    SendMessageW(m_hWnd, EM_GETOLEINTERFACE, 0, reinterpret_cast<LPARAM>(&pUnk));
    if (!pUnk) return false;

    ITextDocument2* pDoc2 = nullptr;
    bool ok = false;
    if (SUCCEEDED(pUnk->QueryInterface(__uuidof(ITextDocument2), reinterpret_cast<void**>(&pDoc2)))) {
        ITextSelection* pSel = nullptr;
        if (SUCCEEDED(pDoc2->GetSelection(&pSel))) {
            long curCp = 0;
            pSel->GetStart(&curCp);

            long storyLen = 0;
            pSel->GetStoryLength(&storyLen);

            long tableDelta = 0, tableStart = 0, tableEnd = 0;
            if (FAILED(pSel->Expand(tomTable, &tableDelta))) { pSel->Release(); pDoc2->Release(); pUnk->Release(); return false; }
            pSel->GetStart(&tableStart); pSel->GetEnd(&tableEnd); pSel->SetRange(curCp, curCp);
            ITextRange* pDocRange = nullptr;
            pDoc2->Range(0, storyLen, &pDocRange);
            BSTR text = nullptr;
            pDocRange->GetText(&text);
            int len = SysStringLen(text);

            std::vector<int> rows;
            for (int i = static_cast<int>(tableStart); i < tableEnd && i < len; ++i)
                if ((unsigned short)text[i] == 0xfff9) rows.push_back(i);
            int curCol = 0;
            for (int i = static_cast<int>(tableStart); i < curCp && i < len; ++i)
                if ((unsigned short)text[i] == 0x07) ++curCol;
            for (auto it = rows.rbegin(); it != rows.rend(); ++it) {
                ITextRange2* pRowRange = nullptr; ITextRow* pRow = nullptr;
                if (SUCCEEDED(pDoc2->Range2(*it, *it, &pRowRange)) && SUCCEEDED(pRowRange->GetRow(&pRow))) {
                    long count = 0;
                    if (SUCCEEDED(pRow->GetCellCount(&count)) && count > 1 && curCol < count) {
                        pRow->SetCellIndex(curCol);
                        if (SUCCEEDED(pRow->SetCellCount(count - 1)) && SUCCEEDED(pRow->Apply(1, tomCellStructureChangeOnly))) ok = true;
                    }
                    pRow->Release();
                }
                if (pRowRange) pRowRange->Release();
            }

            SysFreeString(text);
            pDocRange->Release();
            pSel->Release();
        }
        pDoc2->Release();
    }
    pUnk->Release();
    return ok;
}


void RichEditView::Undo() {
    if (m_hWnd) SendMessageW(m_hWnd, EM_UNDO, 0, 0);
}

void RichEditView::Redo() {
    if (m_hWnd) SendMessageW(m_hWnd, EM_REDO, 0, 0);
}

void RichEditView::Cut() {
    if (m_hWnd) SendMessageW(m_hWnd, WM_CUT, 0, 0);
}

void RichEditView::Copy() {
    if (m_hWnd) SendMessageW(m_hWnd, WM_COPY, 0, 0);
}

void RichEditView::Paste() {
    if (m_hWnd) SendMessageW(m_hWnd, WM_PASTE, 0, 0);
}

void RichEditView::SelectAll() {
    if (m_hWnd) {
        CHARRANGE cr = {0, -1};
        SendMessageW(m_hWnd, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&cr));
    }
}

std::wstring RichEditView::GetPlainText() const {
    if (!m_hWnd) return {};
    GETTEXTLENGTHEX lengthInfo = {};
    lengthInfo.flags = GTL_PRECISE | GTL_NUMCHARS;
    lengthInfo.codepage = 1200;
    LONG length = static_cast<LONG>(SendMessageW(m_hWnd, EM_GETTEXTLENGTHEX, reinterpret_cast<WPARAM>(&lengthInfo), 0));
    if (length <= 0) return {};

    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    GETTEXTEX textInfo = {};
    textInfo.cb = static_cast<DWORD>(text.size() * sizeof(wchar_t));
    textInfo.flags = GT_DEFAULT;
    textInfo.codepage = 1200;
    LONG copied = static_cast<LONG>(SendMessageW(m_hWnd, EM_GETTEXTEX, reinterpret_cast<WPARAM>(&textInfo), reinterpret_cast<LPARAM>(text.data())));
    text.resize(static_cast<size_t>(copied));

    // Code block language tags are hidden RTF metadata. RichEdit still exposes
    // them through plain text, so remove them before indexing and persistence.
    size_t searchPos = 0;
    while ((searchPos = text.find(L"[lang:", searchPos)) != std::wstring::npos) {
        size_t endPos = text.find(L']', searchPos + 6);
        if (endPos == std::wstring::npos) break;
        text.erase(searchPos, endPos - searchPos + 1);
    }
    return text;
}

bool RichEditView::SelectRange(LONG start, LONG end) {
    if (!m_hWnd) return false;
    CHARRANGE cr = {start, end};
    SendMessageW(m_hWnd, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&cr));
    SendMessageW(m_hWnd, EM_SCROLLCARET, 0, 0);
    return true;
}

bool RichEditView::FindAndSelect(const std::wstring& text, bool forward, bool matchCase, bool wholeWord, bool wrapAround) {
    if (!m_hWnd || text.empty()) return false;

    CHARRANGE crCurr = {};
    SendMessageW(m_hWnd, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&crCurr));

    GETTEXTLENGTHEX lengthInfo = { GTL_PRECISE | GTL_NUMCHARS, 1200 };
    LONG docLen = static_cast<LONG>(SendMessageW(m_hWnd, EM_GETTEXTLENGTHEX, reinterpret_cast<WPARAM>(&lengthInfo), 0));

    DWORD flags = 0;
    if (forward) flags |= FR_DOWN;
    if (matchCase) flags |= FR_MATCHCASE;
    if (wholeWord) flags |= FR_WHOLEWORD;

    FINDTEXTEXW ft = {};
    ft.lpstrText = text.c_str();

    if (forward) {
        ft.chrg.cpMin = crCurr.cpMax;
        ft.chrg.cpMax = docLen;
        LRESULT res = SendMessageW(m_hWnd, EM_FINDTEXTEXW, flags, reinterpret_cast<LPARAM>(&ft));
        if (res == -1 && wrapAround && crCurr.cpMax > 0) {
            ft.chrg.cpMin = 0;
            ft.chrg.cpMax = crCurr.cpMax;
            res = SendMessageW(m_hWnd, EM_FINDTEXTEXW, flags, reinterpret_cast<LPARAM>(&ft));
        }
        if (res != -1) {
            SendMessageW(m_hWnd, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&ft.chrgText));
            SendMessageW(m_hWnd, EM_SCROLLCARET, 0, 0);
            return true;
        }
    } else {
        ft.chrg.cpMin = crCurr.cpMin;
        ft.chrg.cpMax = 0;
        LRESULT res = SendMessageW(m_hWnd, EM_FINDTEXTEXW, flags, reinterpret_cast<LPARAM>(&ft));
        if (res == -1 && wrapAround && crCurr.cpMin < docLen) {
            ft.chrg.cpMin = docLen;
            ft.chrg.cpMax = crCurr.cpMin;
            res = SendMessageW(m_hWnd, EM_FINDTEXTEXW, flags, reinterpret_cast<LPARAM>(&ft));
        }
        if (res != -1) {
            SendMessageW(m_hWnd, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&ft.chrgText));
            SendMessageW(m_hWnd, EM_SCROLLCARET, 0, 0);
            return true;
        }
    }

    return false;
}

int RichEditView::CountMatches(const std::wstring& text, bool matchCase, bool wholeWord) const {
    if (!m_hWnd || text.empty()) return 0;

    GETTEXTLENGTHEX lengthInfo = { GTL_PRECISE | GTL_NUMCHARS, 1200 };
    LONG docLen = static_cast<LONG>(SendMessageW(m_hWnd, EM_GETTEXTLENGTHEX, reinterpret_cast<WPARAM>(&lengthInfo), 0));
    if (docLen <= 0) return 0;

    DWORD flags = FR_DOWN;
    if (matchCase) flags |= FR_MATCHCASE;
    if (wholeWord) flags |= FR_WHOLEWORD;

    int count = 0;
    LONG curPos = 0;
    FINDTEXTEXW ft = {};
    ft.lpstrText = text.c_str();

    while (curPos < docLen) {
        ft.chrg.cpMin = curPos;
        ft.chrg.cpMax = docLen;
        LRESULT res = SendMessageW(m_hWnd, EM_FINDTEXTEXW, flags, reinterpret_cast<LPARAM>(&ft));
        if (res == -1) break;
        count++;
        curPos = ft.chrgText.cpMax;
        if (ft.chrgText.cpMin == ft.chrgText.cpMax) curPos++;
    }

    return count;
}

bool RichEditView::ReplaceCurrent(const std::wstring& findText, const std::wstring& replaceText, bool forward, bool matchCase, bool wholeWord, bool wrapAround) {
    if (!m_hWnd || findText.empty()) return false;

    // 检查当前选区是否精确匹配查找内容
    std::wstring sel = GetSelectedText();
    bool isMatch = false;
    if (sel.size() == findText.size()) {
        if (matchCase) {
            isMatch = (sel == findText);
        } else {
            isMatch = (_wcsicmp(sel.c_str(), findText.c_str()) == 0);
        }
    }

    if (isMatch) {
        bool validWord = true;
        if (wholeWord) {
            CHARRANGE selected = {};
            SendMessageW(m_hWnd, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&selected));
            auto isWordChar = [](wchar_t ch) {
                return std::iswalnum(ch) || ch == L'_';
            };
            std::wstring document = GetText();
            validWord = (selected.cpMin <= 0 || !isWordChar(document[static_cast<size_t>(selected.cpMin - 1)])) &&
                        (selected.cpMax >= static_cast<LONG>(document.size()) || !isWordChar(document[static_cast<size_t>(selected.cpMax)]));
        }
        if (validWord) {
            SendMessageW(m_hWnd, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(replaceText.c_str()));
        }
    }

    return FindAndSelect(findText, forward, matchCase, wholeWord, wrapAround);
}

int RichEditView::ReplaceAll(const std::wstring& findText, const std::wstring& replaceText, bool matchCase, bool wholeWord) {
    if (!m_hWnd || findText.empty()) return 0;

    ClearMarks();

    CHARRANGE crOrig = {};
    SendMessageW(m_hWnd, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&crOrig));

    SendMessageW(m_hWnd, WM_SETREDRAW, FALSE, 0);

    DWORD flags = FR_DOWN;
    if (matchCase) flags |= FR_MATCHCASE;
    if (wholeWord) flags |= FR_WHOLEWORD;

    int count = 0;
    LONG curPos = 0;
    FINDTEXTEXW ft = {};
    ft.lpstrText = findText.c_str();

    while (true) {
        GETTEXTLENGTHEX lengthInfo = { GTL_PRECISE | GTL_NUMCHARS, 1200 };
        LONG docLen = static_cast<LONG>(SendMessageW(m_hWnd, EM_GETTEXTLENGTHEX, reinterpret_cast<WPARAM>(&lengthInfo), 0));
        if (curPos >= docLen) break;

        ft.chrg.cpMin = curPos;
        ft.chrg.cpMax = docLen;
        LRESULT res = SendMessageW(m_hWnd, EM_FINDTEXTEXW, flags, reinterpret_cast<LPARAM>(&ft));
        if (res == -1) break;

        SendMessageW(m_hWnd, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&ft.chrgText));
        SendMessageW(m_hWnd, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(replaceText.c_str()));
        count++;

        CHARRANGE crAfter = {};
        SendMessageW(m_hWnd, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&crAfter));
        curPos = crAfter.cpMax;
    }

    SendMessageW(m_hWnd, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&crOrig));
    SendMessageW(m_hWnd, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(m_hWnd, nullptr, TRUE);

    return count;
}

int RichEditView::MarkAll(const std::wstring& text, bool matchCase, bool wholeWord) {
    if (!m_hWnd || text.empty()) return 0;

    ClearMarks();

    GETTEXTLENGTHEX lengthInfo = { GTL_PRECISE | GTL_NUMCHARS, 1200 };
    LONG docLen = static_cast<LONG>(SendMessageW(m_hWnd, EM_GETTEXTLENGTHEX, reinterpret_cast<WPARAM>(&lengthInfo), 0));
    if (docLen <= 0) return 0;

    CHARRANGE crOrig = {};
    SendMessageW(m_hWnd, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&crOrig));

    SendMessageW(m_hWnd, WM_SETREDRAW, FALSE, 0);

    DWORD flags = FR_DOWN;
    if (matchCase) flags |= FR_MATCHCASE;
    if (wholeWord) flags |= FR_WHOLEWORD;

    LONG curPos = 0;
    FINDTEXTEXW ft = {};
    ft.lpstrText = text.c_str();

    CHARFORMAT2W cf = { sizeof(CHARFORMAT2W) };
    cf.dwMask = CFM_BACKCOLOR;
    cf.crBackColor = RGB(255, 255, 128); // 经典 Notepad++ 荧光黄
    cf.dwEffects = 0;

    while (curPos < docLen) {
        ft.chrg.cpMin = curPos;
        ft.chrg.cpMax = docLen;
        LRESULT res = SendMessageW(m_hWnd, EM_FINDTEXTEXW, flags, reinterpret_cast<LPARAM>(&ft));
        if (res == -1) break;

        m_markedRanges.push_back(ft.chrgText);

        SendMessageW(m_hWnd, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&ft.chrgText));
        CHARFORMAT2W original = { sizeof(CHARFORMAT2W) };
        SendMessageW(m_hWnd, EM_GETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&original));
        m_markedFormats.push_back(original);
        SendMessageW(m_hWnd, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&cf));

        curPos = ft.chrgText.cpMax;
        if (ft.chrgText.cpMin == ft.chrgText.cpMax) curPos++;
    }

    SendMessageW(m_hWnd, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&crOrig));
    SendMessageW(m_hWnd, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(m_hWnd, nullptr, TRUE);

    return static_cast<int>(m_markedRanges.size());
}

void RichEditView::ClearMarks() {
    if (!m_hWnd || m_markedRanges.empty()) return;

    CHARRANGE crOrig = {};
    SendMessageW(m_hWnd, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&crOrig));

    SendMessageW(m_hWnd, WM_SETREDRAW, FALSE, 0);

    for (size_t i = 0; i < m_markedRanges.size(); ++i) {
        const auto& cr = m_markedRanges[i];
        SendMessageW(m_hWnd, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&cr));
        if (i < m_markedFormats.size()) {
            auto format = m_markedFormats[i];
            format.dwMask = CFM_BACKCOLOR;
            format.dwEffects &= CFE_AUTOBACKCOLOR;
            SendMessageW(m_hWnd, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&format));
        }
    }

    m_markedRanges.clear();
    m_markedFormats.clear();

    SendMessageW(m_hWnd, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&crOrig));
    SendMessageW(m_hWnd, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(m_hWnd, nullptr, TRUE);
}

} // namespace anynote::ui
