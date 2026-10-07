#include "RichEditView.h"
#include <commctrl.h>
#include <commdlg.h>
#include <windowsx.h>
#include <cwctype>
#include <vector>

namespace anynote::ui {

namespace {

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
    if (uMsg == WM_CHAR && wParam == VK_RETURN && pThis) {
        int curLevel = pThis->GetCurrentHeadingLevel();
        LRESULT res = DefSubclassProc(hWnd, uMsg, wParam, lParam);
        if (curLevel > 0) {
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
    LRESULT result = DefSubclassProc(hWnd, uMsg, wParam, lParam);
    switch (uMsg) {
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
                    WS_CLIPSIBLINGS;

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

    ApplyDefaultFormatting(true);
    return true;
}

void RichEditView::ApplyDefaultFormatting(bool allDocument) {
    CHARFORMAT2W cf = {sizeof(CHARFORMAT2W)};
    cf.dwMask = CFM_FACE | CFM_SIZE | CFM_COLOR | CFM_BACKCOLOR;
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
    }
}

void RichEditView::SetText(const std::wstring& text) {
    if (!m_hWnd) return;
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

bool RichEditView::InsertCodeBlock(std::wstring_view codeContent, common::CodeLanguage lang) {
    if (!m_hWnd) return false;

    std::string rtf = common::SyntaxHighlighter::GenerateRtfCodeBlock(codeContent, lang);
    bool ok = StreamInSelectionRTF(rtf);

    ApplyDefaultFormatting(false);
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
