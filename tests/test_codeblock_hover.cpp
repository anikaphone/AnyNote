#include "ui/RichEditView.h"
#include <commctrl.h>
#include <windowsx.h>
#include <iostream>
#include <stdexcept>
#include <tom.h>
#include <wrl/client.h>

using anynote::ui::CodeBlockInfo;
using anynote::ui::RichEditView;

namespace {

void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void PumpMessages() {
    MSG msg;
    // Bound the loop so the original flicker cannot hang the test.
    for (int i = 0; i < 200 && PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE); ++i) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

bool SameRect(const RECT& a, const RECT& b) {
    return EqualRect(&a, &b) != FALSE;
}

struct CursorRestore {
    POINT original{};
    CursorRestore() { GetCursorPos(&original); }
    ~CursorRestore() { SetCursorPos(original.x, original.y); }
};

int positionChanges = 0;

LRESULT CALLBACK MonitorBar(HWND wnd, UINT message, WPARAM wparam, LPARAM lparam,
    UINT_PTR, DWORD_PTR) {
    if (message == WM_WINDOWPOSCHANGING) {
        auto* pos = reinterpret_cast<WINDOWPOS*>(lparam);
        if (!(pos->flags & SWP_NOMOVE) || !(pos->flags & SWP_NOSIZE)) ++positionChanges;
    }
    return DefSubclassProc(wnd, message, wparam, lparam);
}

LRESULT CALLBACK PreviewHost(HWND wnd, UINT message, WPARAM wparam, LPARAM lparam,
    UINT_PTR, DWORD_PTR data) {
    auto* editor = reinterpret_cast<RichEditView*>(data);
    if (message == WM_SIZE) editor->SetBounds(0, 0, LOWORD(lparam), HIWORD(lparam));
    if (message == WM_CLOSE) {
        ShowWindow(wnd, SW_HIDE);
        PostQuitMessage(0);
        return 0;
    }
    return DefSubclassProc(wnd, message, wparam, lparam);
}

Microsoft::WRL::ComPtr<ITextDocument2> GetDocument(HWND editor) {
    Microsoft::WRL::ComPtr<IUnknown> ole;
    SendMessageW(editor, EM_GETOLEINTERFACE, 0, reinterpret_cast<LPARAM>(ole.GetAddressOf()));
    Microsoft::WRL::ComPtr<ITextDocument2> doc;
    Check(ole && SUCCEEDED(ole.As(&doc)), "Missing TOM2");
    return doc;
}

void CheckRowWidth(RichEditView& editor, const CodeBlockInfo& info, const char* message) {
    auto doc = GetDocument(editor.GetHwnd());
    Microsoft::WRL::ComPtr<ITextRange2> range;
    Microsoft::WRL::ComPtr<ITextRow> row;
    Check(SUCCEEDED(doc->Range2(info.codeStart, info.codeStart, &range)) &&
        SUCCEEDED(range->GetRow(&row)), "Missing code row");
    row->Reset(tomRowUpdate);
    long width = 0;
    Check(SUCCEEDED(row->GetCellWidth(&width)), "Cannot read code width");
    RECT format;
    SendMessageW(editor.GetHwnd(), EM_GETRECT, 0, reinterpret_cast<LPARAM>(&format));
    Check(abs(width - MulDiv(format.right - format.left - 2, 1440, GetDpiForWindow(editor.GetHwnd()))) <= 2,
        message);
}

void CheckCodeSpacing(RichEditView& editor, const CodeBlockInfo& info) {
    auto doc = GetDocument(editor.GetHwnd());
    Microsoft::WRL::ComPtr<ITextRange2> range;
    doc->Range2(info.codeStart, info.codeEnd, &range);
    BSTR raw = nullptr;
    range->GetText(&raw);
    const std::wstring text = raw ? std::wstring(raw, SysStringLen(raw)) : std::wstring();
    SysFreeString(raw);
    Check(!text.empty(), "Missing code text for spacing check");
    const size_t lastParagraph = text.rfind(L'\r');
    for (size_t start = 0; start < text.size();) {
        range->SetRange(info.codeStart + static_cast<long>(start), info.codeStart + static_cast<long>(start));
        Microsoft::WRL::ComPtr<ITextPara> para;
        Check(SUCCEEDED(range->GetPara(&para)), "Cannot read paragraph spacing");
        float before = 0, after = 0;
        para->GetSpaceBefore(&before);
        para->GetSpaceAfter(&after);
        Check(before == (start == 0 ? 10.0f : 0.0f), "Only first code paragraph may have space before");
        const bool last = lastParagraph == std::wstring::npos || start > lastParagraph;
        Check(after == (last ? 10.0f : 0.0f), "Only last code paragraph may have space after");
        const size_t next = text.find(L'\r', start);
        if (next == std::wstring::npos) break;
        start = next + 1;
    }
}

void TestCodeBlockLayout(RichEditView& editor) {
    editor.SetText(L"");
    editor.SetBounds(0, 0, 700, 610);
    Check(editor.InsertCodeBlock(L"short", anynote::common::CodeLanguage::Cpp), "Cannot insert geometry reference");
    CodeBlockInfo info;
    Check(editor.GetCodeBlockAtCursor(&info), "Missing geometry reference");
    const RECT reference = editor.GetCodeBlockRect(info);
    RECT referenceFormat{};
    SendMessageW(editor.GetHwnd(), EM_GETRECT, 0, reinterpret_cast<LPARAM>(&referenceFormat));
    editor.SetText(L"");
    std::wstring longCode;
    for (int i = 0; i < 80; ++i) longCode += L"int value = 1;\n";
    longCode += L"return value;";
    Check(editor.InsertCodeBlock(longCode, anynote::common::CodeLanguage::Cpp), "Cannot insert tall code block");
    Check(editor.GetCodeBlockAtCursor(&info), "Missing tall code block");
    SendMessageW(editor.GetHwnd(), WM_VSCROLL, SB_TOP, 0);
    RECT block = editor.GetCodeBlockRect(info);
    RECT client;
    GetClientRect(editor.GetHwnd(), &client);
    Check(block.bottom > client.bottom, "Tall code block endpoint must be offscreen");
    RECT tallFormat{};
    SendMessageW(editor.GetHwnd(), EM_GETRECT, 0, reinterpret_cast<LPARAM>(&tallFormat));
    // RichEdit reserves a scrollbar for the tall block, reducing its row width.
    const LONG widthDifference = (tallFormat.right - tallFormat.left) -
        (referenceFormat.right - referenceFormat.left);
    Check(abs(block.left - reference.left) <= 1 && abs(block.right - reference.right - widthDifference) <= 1,
        "Offscreen code block must use the same cell borders as a visible block");
    SendMessageW(editor.GetHwnd(), EM_LINESCROLL, 0, 12);
    const RECT scrolled = editor.GetCodeBlockRect(info);
    Check(scrolled.top < block.top && scrolled.top < 0, "Code block start must scroll offscreen");
    Check(abs(scrolled.left - block.left) <= 1 && abs(scrolled.right - block.right) <= 1,
        "Scrolling tall code block must preserve actual cell borders");
    std::cout << "[PASS] Offscreen code block borders remain aligned after scrolling.\n";

    editor.SetText(L"");
    editor.SetBounds(0, 0, 480, 610);
    Check(editor.InsertCodeBlock(L"int first = 1;\nreturn first;", anynote::common::CodeLanguage::Cpp),
        "Cannot insert undo test block");
    Check(editor.GetCodeBlockAtCursor(&info), "Missing undo test block");
    SendMessageW(editor.GetHwnd(), EM_EMPTYUNDOBUFFER, 0, 0);
    Check(editor.SwitchCodeBlockLanguage(info, anynote::common::CodeLanguage::Python), "Cannot switch narrow block language");
    editor.SetBounds(0, 0, 1020, 610);
    editor.Undo();
    Check(editor.GetCodeBlockAt(info.tableStart, &info) && info.currentLang == anynote::common::CodeLanguage::Cpp,
        "Undo must restore original language");
    CheckRowWidth(editor, info, "Undo restored obsolete code block width");
    editor.Redo();
    Check(editor.GetCodeBlockAt(info.tableStart, &info) && info.currentLang == anynote::common::CodeLanguage::Python,
        "Redo must restore changed language");
    CheckRowWidth(editor, info, "Redo restored obsolete code block width");

    const std::wstring originalCode = info.codeText;
    editor.SelectRange(info.codeStart, info.codeStart);
    SendMessageW(editor.GetHwnd(), EM_STOPGROUPTYPING, 0, 0);
    SendMessageW(editor.GetHwnd(), EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"first\rsecond\r"));
    Check(editor.GetCodeBlockAtCursor(&info), "Replacement lost code block");
    Check(info.codeText.find(L"first\r\nsecond\r\n") == 0, "Multiline replacement changed inserted code");
    CheckCodeSpacing(editor, info);
    const LONG insertedEnd = info.codeStart + 13;
    CHARRANGE selection;
    SendMessageW(editor.GetHwnd(), EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&selection));
    Check(selection.cpMin == insertedEnd && selection.cpMax == insertedEnd, "Layout normalization moved the caret");
    editor.Undo();
    Check(editor.GetCodeBlockAt(info.tableStart, &info) && info.codeText == originalCode,
        "Multiline insertion must undo as one edit");
    CheckCodeSpacing(editor, info);
    editor.Redo();
    Check(editor.GetCodeBlockAt(info.tableStart, &info), "Redo lost code block");
    CheckCodeSpacing(editor, info);
    editor.SelectRange(info.codeStart, info.codeStart + 6);
    SendMessageW(editor.GetHwnd(), WM_CLEAR, 0, 0);
    Check(editor.GetCodeBlockAtCursor(&info) && info.codeText.find(L"second\r\n") == 0,
        "Clearing first paragraph changed remaining code");
    CheckCodeSpacing(editor, info);
    editor.Undo();
    Check(editor.GetCodeBlockAt(info.tableStart, &info), "Undo clear lost code block");
    CheckCodeSpacing(editor, info);
    editor.SelectRange(info.codeStart, info.codeStart);
    SendMessageW(editor.GetHwnd(), WM_CHAR, VK_RETURN, 0);
    Check(editor.GetCodeBlockAtCursor(&info), "Return lost code block");
    CheckCodeSpacing(editor, info);
    std::cout << "[PASS] Undo/redo retain current width; multiline edits and clear retain spacing, caret and undo history.\n";
}

} // namespace

int main(int argc, char** argv) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    OleInitialize(nullptr);
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);
    int result = 0;
    HWND host = CreateWindowExW(WS_EX_TOOLWINDOW, L"STATIC", L"AnyNote hover regression",
        WS_OVERLAPPEDWINDOW, 80, 80, 1050, 650, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    try {
        Check(host != nullptr, "Cannot create scratch host");
        CursorRestore restore;
        RichEditView editor;
        Check(editor.Initialize(host, 0, 0, 1020, 610, 1), "Cannot create editor");
        ShowWindow(host, SW_SHOWNOACTIVATE);
        editor.SetBounds(0, 0, 1020, 610);
        if (argc > 1 && std::string(argv[1]) == "--preview") {
            editor.SetText(L"正文开始：代码块样式预览\r");
            editor.SelectRange(static_cast<LONG>(editor.GetText().size()), static_cast<LONG>(editor.GetText().size()));
            editor.InsertCodeBlock(L"function greet(user) {\n    // 支持中文注释、缩进和语法高亮\n    const message = \"Welcome to AnyNote!\";\n    console.log(message, user);\n}\n\ngreet(\"Developer\");", anynote::common::CodeLanguage::JavaScript);
            SetWindowTextW(host, L"AnyNote 代码块样式预览（独立测试窗口）");
            SetWindowSubclass(host, PreviewHost, 3, reinterpret_cast<DWORD_PTR>(&editor));
            SetFocus(editor.GetHwnd());
            MSG message;
            while (IsWindow(host) && GetMessageW(&message, nullptr, 0, 0) > 0) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            RemoveWindowSubclass(host, PreviewHost, 3);
            // Let the editor release its native resources before OLE shutdown.
        } else {

            Check(editor.InsertCodeBlock(L"int first = 1;\nreturn first;", anynote::common::CodeLanguage::Cpp),
                "Cannot insert first code block");
            CodeBlockInfo first;
            Check(editor.GetCodeBlockAtCursor(&first), "Cannot find first code block");
            {
                using Microsoft::WRL::ComPtr;
                ComPtr<IUnknown> ole;
                SendMessageW(editor.GetHwnd(), EM_GETOLEINTERFACE, 0, reinterpret_cast<LPARAM>(ole.GetAddressOf()));
                ComPtr<ITextDocument2> doc;
                Check(SUCCEEDED(ole.As(&doc)), "Missing TOM2");
                ComPtr<ITextRange2> range;
                doc->Range2(first.codeStart, first.codeStart, &range);
                ComPtr<ITextRow> row;
                Check(SUCCEEDED(range->GetRow(&row)), "Missing code row");
                long width = 0, count = 0;
                row->Reset(tomRowUpdate);
                row->GetCellCount(&count);
                row->GetCellWidth(&width);
                RECT formatRect;
                SendMessageW(editor.GetHwnd(), EM_GETRECT, 0, reinterpret_cast<LPARAM>(&formatRect));
                Check(count == 1, "Code block must remain one cell");
                Check(abs(width - MulDiv(formatRect.right - formatRect.left - 2, 1440, GetDpiForWindow(editor.GetHwnd()))) <= 2,
                    "Code block did not expand to editor width");
            }
            CHARFORMAT2W format{sizeof(format)};
            SendMessageW(editor.GetHwnd(), EM_GETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&format));
            Check(std::wstring(format.szFaceName) == L"Consolas", "Code insertion must retain monospace typing font");
            Check((format.dwEffects & CFE_HIDDEN) == 0, "Typing must not inherit hidden language metadata");
            const auto saved = editor.StreamOutRTF();
            Check(editor.StreamInRTF(saved), "Cannot reload code RTF");
            Check(editor.GetCodeBlockAt(first.codeStart, &first), "Reload lost code metadata");
            Check(first.codeText.find(L"int first = 1;") != std::wstring::npos, "Reload changed code text");
            const RECT wideBlock = editor.GetCodeBlockRect(first);
            editor.SetBounds(0, 0, 480, 610);
            const RECT narrowBlock = editor.GetCodeBlockRect(first);
            Check(narrowBlock.right - narrowBlock.left < wideBlock.right - wideBlock.left,
                "Code block must shrink with editor");
            Check(editor.GetCodeBlockAt(first.codeStart, &first) && first.codeText.find(L"return first;") != std::wstring::npos,
                "Resizing changed code text");
            editor.SetBounds(0, 0, 1020, 610);
            Check(editor.SwitchCodeBlockLanguage(first, anynote::common::CodeLanguage::PlainText), "Cannot switch language");
            Check(editor.GetCodeBlockAtCursor(&first) && first.currentLang == anynote::common::CodeLanguage::PlainText,
                "Language change lost metadata");
            editor.SelectRange(first.codeStart, first.codeStart + 3);
            SendMessageW(editor.GetHwnd(), EM_GETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&format));
            Check(format.crTextColor == RGB(36, 41, 47), "Plain text must not keep syntax colors");
            Check(editor.SwitchCodeBlockLanguage(first, anynote::common::CodeLanguage::Cpp), "Cannot restore C++");
            Check(editor.GetCodeBlockAtCursor(&first), "Lost block after switching language");
            SendMessageW(editor.GetHwnd(), EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"edited "));
            editor.SetBounds(0, 0, 700, 610);
            editor.Undo();
            Check(editor.GetCodeBlockAt(first.codeStart, &first) && first.codeText.find(L"edited ") == std::wstring::npos,
                "Layout refresh must not pollute undo history");
            editor.SetBounds(0, 0, 1020, 610);
            editor.SelectRange(first.tableEnd, first.tableEnd);
            SendMessageW(editor.GetHwnd(), EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"\r\r"));
            Check(editor.InsertCodeBlock(L"second = 2\nprint(second)", anynote::common::CodeLanguage::Python),
                "Cannot insert second code block");
            CodeBlockInfo second;
            Check(editor.GetCodeBlockAtCursor(&second), "Cannot find second code block");
            Check(first.tableStart != second.tableStart, "Code blocks must be separate");
            editor.SelectRange(first.codeStart, first.codeStart);

            HWND bar = FindWindowExW(editor.GetHwnd(), nullptr, L"AnyNoteCodeBlockHoverBar", nullptr);
            Check(bar != nullptr, "Cannot find hover bar");
            Check(SetWindowSubclass(bar, MonitorBar, 2, 0) != FALSE, "Cannot monitor hover bar");

            RECT secondBlock = editor.GetCodeBlockRect(second);
            POINT pointInSecond{secondBlock.left + 20, secondBlock.top + 10};
            editor.OnMouseMove(second.codeStart, pointInSecond);
            RECT expected;
            GetWindowRect(bar, &expected);
            Check(IsWindowVisible(bar) != FALSE, "Hover bar should be visible");
            Check(SetCursorPos((expected.left + expected.right) / 2, (expected.top + expected.bottom) / 2) != FALSE,
                "Cannot place mouse over hover bar");

            // Leaving the editor for its child hover bar must not return the bar to
            // the first block merely because the text caret remains there.
            editor.OnMouseLeave();
            RECT actual;
            GetWindowRect(bar, &actual);
            Check(SameRect(expected, actual), "Hover bar moved to caret block while mouse entered bar");

            POINT pointOnBar{(expected.left + expected.right) / 2, (expected.top + expected.bottom) / 2};
            ScreenToClient(editor.GetHwnd(), &pointOnBar);
            for (int i = 0; i < 30; ++i) {
                editor.OnMouseLeave();
                editor.OnSelChange();
                // Queued RichEdit hit tests must not change the active child control.
                editor.OnMouseMove(first.codeStart, pointOnBar);
                PumpMessages();
                GetWindowRect(bar, &actual);
                Check(SameRect(expected, actual), "Hover target oscillated between blocks");
            }

            // Identical hover updates must not keep moving/recreating the window.
            SetCursorPos(10, 10);
            PumpMessages();
            editor.OnMouseMove(second.codeStart, pointInSecond);
            positionChanges = 0;
            for (int i = 0; i < 30; ++i) editor.OnMouseMove(second.codeStart, pointInSecond);
            Check(positionChanges == 0, "Unchanged hover position caused redundant window moves");

            editor.OnMouseMove(first.codeStart, POINT{20, 20});
            GetWindowRect(bar, &actual);
            Check(!SameRect(expected, actual), "Hover bar must still switch when the pointer leaves it");
            editor.SetText(L"replacement note");
            Check(IsWindowVisible(bar) == FALSE, "Replacing note must hide hover bar");

            RemoveWindowSubclass(bar, MonitorBar, 2);
            std::cout << "[PASS] Multiple code blocks retain a stable hover target; unchanged geometry does not move the bar.\n";
            std::cout << "[PASS] Code font, plain-text colors, RTF reload and responsive width.\n";
            TestCodeBlockLayout(editor);
        }
    } catch (const std::exception& error) {
        std::cerr << "[FAIL] " << error.what() << '\n';
        result = 1;
    }
    if (host) DestroyWindow(host);
    OleUninitialize();
    return result;
}
