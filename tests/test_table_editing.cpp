#include "ui/RichEditView.h"
#include <commctrl.h>
#include <commdlg.h>
#include <windowsx.h>
#include <tom.h>
#include <wrl/client.h>
#include <iostream>
#include <stdexcept>

namespace anynote::ui {
struct TableEditorGeometryTest {
    static std::vector<RECT> Rectangles(HWND window, long position) {
        TableEditor editor;
        editor.Attach(window);
        std::vector<TableEditor::Cell> cells;
        if (!editor.ReadTable(position, cells)) throw std::runtime_error("Cannot read geometry fixture");
        std::vector<RECT> rectangles;
        for (const auto& cell : cells) {
            RECT rect{};
            if (!editor.CellRect(cell, rect)) throw std::runtime_error("Cannot compute cell rectangle");
            rectangles.push_back(rect);
        }
        return rectangles;
    }
};
}
using anynote::ui::RichEditView;
using Microsoft::WRL::ComPtr;
namespace {
void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void CheckGeometry(RichEditView& editor) {
    const HWND window = editor.GetHwnd();
    // Deliberately use a unique border color, unequal widths/heights, mixed
    // alignments and multi-line content. The oracle is RichEdit's raster output,
    // not a duplicate of the coordinate formula under test.
    const std::string border = "\\clbrdrt\\brdrs\\brdrw15\\brdrcf1\\clbrdrb\\brdrs\\brdrw15\\brdrcf1"
        "\\clbrdrl\\brdrs\\brdrw15\\brdrcf1\\clbrdrr\\brdrs\\brdrw15\\brdrcf1";
    const std::string rtf = "{\\rtf1\\ansi{\\fonttbl{\\f0 Segoe UI;}}{\\colortbl;\\red17\\green83\\blue129;}\\fs24"
        "\\pard Before\\par\\trowd\\trleft144\\trrh1050" + border + "\\clvertalc\\cellx1700" + border +
        "\\clvertalb\\cellx4500" + border + "\\clvertalt\\cellx7200"
        "\\pard\\intbl\\qr A\\cell\\qc B\\cell\\ql C\\cell\\row"
        "\\trowd\\trleft144\\trrh1650" + border + "\\clvertalb\\cellx1700" + border +
        "\\clvertalt\\cellx4500" + border + "\\clvertalc\\cellx7200"
        "\\pard\\intbl\\qc D\\cell\\ql E\\par second\\cell\\qr F\\cell\\row\\pard After\\par}";
    Check(editor.StreamInRTF(rtf), "Load geometry fixture failed");
    FINDTEXTEXW find{{0, -1}, L"A", {}};
    Check(SendMessageW(window, EM_FINDTEXTEXW, FR_DOWN, reinterpret_cast<LPARAM>(&find)) >= 0, "Missing geometry cell");
    const auto rectangles = anynote::ui::TableEditorGeometryTest::Rectangles(window, find.chrgText.cpMin);
    HDC screen = GetDC(window);
    HDC memory = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateCompatibleBitmap(screen, 850, 560);
    const auto previous = SelectObject(memory, bitmap);
    SendMessageW(window, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(memory), PRF_CLIENT | PRF_ERASEBKGND);
    bool correct = true;
    for (const RECT& rect : rectangles) {
        auto nearBorder = [&](int x, int y) {
            for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx)
                if (GetPixel(memory, x + dx, y + dy) == RGB(17, 83, 129)) return true;
            return false;
        };
        const int x = (rect.left + rect.right) / 2, y = (rect.top + rect.bottom) / 2;
        const bool matches = nearBorder(x, rect.top) && nearBorder(x, rect.bottom - 1) &&
            nearBorder(rect.left, y) && nearBorder(rect.right - 1, y);
        if (!matches) std::cout << "MISMATCH " << rect.left << ',' << rect.top << ',' << rect.right << ',' << rect.bottom << '\n';
        correct = correct && matches;
    }
    if (!correct) {
        for (int y=0;y<300;++y) {
            int count=0; for(int x=30;x<510;++x) if(GetPixel(memory,x,y)==RGB(17,83,129))++count;
            if(count>100) std::cout<<"native horizontal "<<y<<" pixels "<<count<<'\n';
        }
    }
    SelectObject(memory, previous);
    DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(window, screen);
    Check(correct, "Selection rectangles must coincide with rendered cell borders (within 1px)");
}
ComPtr<ITextDocument2> Document(HWND window) {
    ComPtr<IUnknown> ole;
    SendMessageW(window, EM_GETOLEINTERFACE, 0, reinterpret_cast<LPARAM>(ole.GetAddressOf()));
    ComPtr<ITextDocument2> document;
    Check(ole && SUCCEEDED(ole.As(&document)), "Missing TOM2");
    return document;
}
void Run(RichEditView& editor) {
    editor.SetText(L"");
    Check(editor.InsertTable(3, 3), "Insert table failed");
    auto doc = Document(editor.GetHwnd());
    ComPtr<ITextRange2> range;
    doc->Range2(2, 2, &range);
    long delta = 0, a = 0, b = 0;
    range->Expand(tomTable, &delta);
    range->GetStart(&a); range->GetEnd(&b);
    Check(delta > 0 && a == 0 && b > a, "Inserted table range is invalid");
    Check(editor.SelectTableCells(RichEditView::TableAlignmentScope::Column), "Select column failed");
    Check(editor.GetSelectedTableCellCount() == 3, "Column should have 3 cells");
    Check(editor.SetTableCellHorizontalAlignment(PFA_CENTER), "Set horizontal failed");
    editor.ClearTableSelection();
    auto point = [&](long cp) {
        POINT p{};
        SendMessageW(editor.GetHwnd(), EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&p), cp);
        p.x = MulDiv(26 + static_cast<int>((cp - 2) % 7) * 160 + 40, GetDpiForWindow(editor.GetHwnd()), 96);
        p.y += 5;
        return p;
    };
    auto mouse = [&](UINT message, POINT p, WPARAM flags) {
        SendMessageW(editor.GetHwnd(), message, flags, MAKELPARAM(p.x, p.y));
    };
    const POINT first = point(3), last = point(17);
    mouse(WM_LBUTTONDOWN, first, MK_LBUTTON);
    mouse(WM_MOUSEMOVE, last, MK_LBUTTON);
    mouse(WM_LBUTTONUP, last, 0);
    Check(editor.GetSelectedTableCellCount() == 3, "Vertical mouse drag must select only one column");
    // A context-menu invocation must not turn a cell selection into a text caret.
    mouse(WM_RBUTTONDOWN, first, 0);
    POINT contextPoint = first;
    ClientToScreen(editor.GetHwnd(), &contextPoint);
    SendMessageW(editor.GetHwnd(), WM_CONTEXTMENU, reinterpret_cast<WPARAM>(editor.GetHwnd()),
        MAKELPARAM(contextPoint.x, contextPoint.y));
    Check(editor.GetSelectedTableCellCount() == 3, "Right click must preserve the cell selection");
    Check(editor.SetTableCellHorizontalAlignment(PFA_RIGHT), "Drag alignment failed");
    int h = -1, v = -1;
    Check(editor.GetTableCellAlignment(&h, &v) && h == PFA_RIGHT, "Selected alignment readback failed");
    mouse(WM_LBUTTONDOWN, point(2), MK_CONTROL | MK_LBUTTON);
    mouse(WM_LBUTTONUP, point(2), MK_CONTROL);
    Check(editor.GetSelectedTableCellCount() == 4, "Ctrl click must add a cell");
    mouse(WM_LBUTTONDOWN, point(2), MK_CONTROL | MK_LBUTTON);
    mouse(WM_LBUTTONUP, point(2), MK_CONTROL);
    Check(editor.GetSelectedTableCellCount() == 3, "Ctrl click must toggle a cell off");
    mouse(WM_LBUTTONDOWN, point(18), MK_LBUTTON);
    mouse(WM_MOUSEMOVE, point(3), MK_LBUTTON);
    mouse(WM_LBUTTONUP, point(3), 0);
    Check(editor.GetSelectedTableCellCount() == 6, "Reverse rectangle must select six cells");
    editor.SetText(L"another note");
    Check(editor.GetSelectedTableCellCount() == 0, "Note replacement must clear cell positions");
    const std::string sample = "{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0 Segoe UI;}}\\fs24"
        "\\trowd\\trleft144\\trrh1440\\cellx2400\\cellx4800\\cellx7200"
        "\\pard\\intbl Alpha\\cell Bravo\\par second line\\cell Charlie\\cell\\row"
        "\\trowd\\trleft144\\trrh1440\\cellx2400\\cellx4800\\cellx7200"
        "\\pard\\intbl Delta\\cell Echo\\cell Foxtrot\\cell\\row\\pard\\par}";
    Check(editor.StreamInRTF(sample), "Load sample failed");
    const HWND window = editor.GetHwnd();
    auto find = [&](const wchar_t* text) {
        FINDTEXTEXW ft{{0, -1}, text, {}};
        Check(SendMessageW(window, EM_FINDTEXTEXW, FR_DOWN, reinterpret_cast<LPARAM>(&ft)) >= 0, "Missing sample cell");
        return ft.chrgText.cpMin;
    };
    auto textPoint = [&](const wchar_t* text) {
        POINT p{};
        SendMessageW(window, EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&p), find(text));
        p.x += 3; p.y += 3;
        return p;
    };
    auto alignment = [&](const wchar_t* text) {
        ComPtr<ITextRange2> cell;
        ComPtr<ITextPara> para;
        const long cp = find(text);
        Check(SUCCEEDED(doc->Range2(cp, cp, &cell)) && SUCCEEDED(cell->GetPara(&para)), "Missing paragraph");
        long value = -1;
        Check(SUCCEEDED(para->GetAlignment(&value)), "Read paragraph failed");
        return value;
    };
    for (const auto* name : {L"Alpha", L"Echo"}) {
        mouse(WM_LBUTTONDOWN, textPoint(name), MK_CONTROL | MK_LBUTTON);
        mouse(WM_LBUTTONUP, textPoint(name), MK_CONTROL);
    }
    Check(editor.GetSelectedTableCellCount() == 2, "Disjoint selection failed");
    Check(editor.SetTableCellHorizontalAlignment(PFA_RIGHT), "Disjoint formatting failed");
    Check(alignment(L"Alpha") == tomAlignRight && alignment(L"Echo") == tomAlignRight &&
        alignment(L"Bravo") == tomAlignLeft && alignment(L"Delta") == tomAlignLeft, "Disjoint formatting affected an unselected cell");
    Check(editor.GetTableCellAlignment(&h, &v) && h == PFA_RIGHT, "Uniform selection state failed");
    editor.Undo();
    Check(alignment(L"Alpha") == tomAlignLeft && alignment(L"Echo") == tomAlignLeft, "One undo must restore all cells");
    editor.Redo();
    Check(alignment(L"Alpha") == tomAlignRight && alignment(L"Echo") == tomAlignRight, "Redo must restore all cells");
    editor.SelectRange(find(L"Alpha"), find(L"Alpha"));
    const long top = textPoint(L"Alpha").y;
    Check(editor.SetTableCellVerticalAlignment(1), "Vertical center failed");
    const long middle = textPoint(L"Alpha").y;
    Check(editor.SetTableCellVerticalAlignment(2), "Vertical bottom failed");
    const long bottom = textPoint(L"Alpha").y;
    Check(top < middle && middle < bottom, "Vertical alignment must visibly move text inside a tall row");
    editor.ClearTableSelection();
    mouse(WM_LBUTTONDOWN, textPoint(L"Alpha"), MK_CONTROL | MK_LBUTTON);
    mouse(WM_LBUTTONUP, textPoint(L"Alpha"), MK_CONTROL);
    Check(editor.GetSelectedTableCellCount() == 1, "Resized row cell must remain selectable");
    editor.ClearTableSelection();
    auto savedOpt = editor.StreamOutRTF();
    Check(savedOpt.has_value(), "StreamOutRTF failed");
    const std::string saved = std::move(*savedOpt);
    Check(editor.StreamInRTF(saved), "RTF reload failed");
    Check(textPoint(L"Alpha").y == bottom, "Reload lost vertical layout");
    editor.SelectRange(find(L"Alpha"), find(L"Alpha"));
    Check(editor.GetTableCellAlignment(&h, &v) && h == PFA_RIGHT && v == 2, "Reload lost cell alignment");
    Check(editor.SetTableCellVerticalAlignment(0), "Top alignment failed");
    Check(textPoint(L"Alpha").y == top, "Top alignment did not restore original position");
    // Mixed selections must not advertise the first cell's format as common.
    editor.SelectRange(find(L"Alpha"), find(L"Bravo") + 2);
    Check(editor.GetTableCellAlignment(&h, &v) && h == -1, "Mixed horizontal state must be indeterminate");
    editor.ClearTableSelection();
}
}
int main() {
    OleInitialize(nullptr);
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_WIN95_CLASSES};
    InitCommonControlsEx(&controls);
    HWND host = CreateWindowExW(0, L"STATIC", L"AnyNote Table Test", WS_OVERLAPPEDWINDOW,
        60, 60, 900, 650, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    int result = 0;
    {
        RichEditView editor;
        try {
            Check(editor.Initialize(host, 0, 0, 850, 560, 100), "Editor creation failed");
            ShowWindow(host, SW_SHOWNOACTIVATE);
            Run(editor);
            CheckGeometry(editor);
            std::cout << "[PASS] Table editing\n";
        } catch (const std::exception& error) {
            std::cerr << "[FAIL] " << error.what() << '\n'; result = 1;
        }
    }
    DestroyWindow(host);
    OleUninitialize();
    return result;
}
