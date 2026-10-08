#include "ui/RichEditView.h"
#include <commctrl.h>
#include <windowsx.h>
#include <iostream>
#include <stdexcept>

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

} // namespace

int main() {
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

        Check(editor.InsertCodeBlock(L"int first = 1;\nreturn first;", anynote::common::CodeLanguage::Cpp),
            "Cannot insert first code block");
        CodeBlockInfo first;
        Check(editor.GetCodeBlockAtCursor(&first), "Cannot find first code block");
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
    } catch (const std::exception& error) {
        std::cerr << "[FAIL] " << error.what() << '\n';
        result = 1;
    }
    if (host) DestroyWindow(host);
    OleUninitialize();
    return result;
}
