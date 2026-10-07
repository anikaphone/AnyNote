#include "RichEditView.h"
#include <commctrl.h>
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

RichEditView::~RichEditView() {
    if (m_hWnd) {
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
                    ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_NOHIDESEL | ES_WANTRETURN;

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

    std::string outData;
    EDITSTREAM es = {};
    es.dwCookie = reinterpret_cast<DWORD_PTR>(&outData);
    es.pfnCallback = StreamOutCallback;

    SendMessageW(m_hWnd, EM_STREAMOUT, SF_RTF, reinterpret_cast<LPARAM>(&es));
    return outData;
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

} // namespace anynote::ui
