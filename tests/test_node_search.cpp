#include "ui/MainWindow.h"
#include "resource.h"
#include <commdlg.h>
#include <gdiplus.h>
#include <tom.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {

void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

std::wstring Text(HWND window) {
    const int length = GetWindowTextLengthW(window);
    std::wstring value(length + 1, L'\0');
    GetWindowTextW(window, value.data(), length + 1);
    value.resize(length);
    return value;
}

struct ScratchDirectory {
    std::filesystem::path path;
    ScratchDirectory() {
        const auto tempRoot = std::filesystem::temp_directory_path();
        path = tempRoot /
            (L"AnyNoteNodeSearch-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
        Check(path.is_absolute() && std::filesystem::equivalent(path.parent_path(), tempRoot),
            "Test path must stay inside temp directory");
        Check(std::filesystem::create_directory(path), "Cannot create unique test directory");
    }
    ~ScratchDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

struct CloseWindow {
    HWND window;
    ~CloseWindow() { if (IsWindow(window)) DestroyWindow(window); }
};

void CreateVault(const std::filesystem::path& path, bool first, bool empty = false) {
    using namespace anynote::storage;
    Database db;
    Check(db.Open(path.wstring(), ""), "Cannot open test vault");
    NoteRepository repo(db);
    Check(repo.InitializeSchema(), "Cannot initialize test vault");
    if (!empty) {
        Check(repo.CreateNote(0, first ? L"Alpha" : L"Alpha one", -1, 0,
            "{\\rtf1\\ansi alpha body}", L"alpha body") > 0, "Cannot create first note");
        if (!first) {
            Check(repo.CreateNote(0, L"Alpha two", -1, 0,
                "{\\rtf1\\ansi second alpha body}", L"second alpha body") > 0, "Cannot create second match");
        }
        Check(repo.CreateNote(0, L"Beta", -1, 0,
            "{\\rtf1\\ansi bodyneedle in hidden note}", L"bodyneedle in hidden note") > 0, "Cannot create hidden note");
    }
}

void ActivateGlobalResult(HWND mainWindow, const wchar_t* keyword) {
    SendMessageW(mainWindow, WM_COMMAND, ID_SEARCH_GLOBAL, 0);
    HWND pane = FindWindowExW(mainWindow, nullptr, anynote::ui::SearchPane::GetClassName(), nullptr);
    Check(pane != nullptr, "Cannot find global search pane");
    HWND searchEdit = FindWindowExW(pane, nullptr, L"EDIT", nullptr);
    HWND searchButton = FindWindowExW(pane, nullptr, L"BUTTON", nullptr);
    Check(searchEdit && searchButton, "Cannot find global search controls");
    SetWindowTextW(searchEdit, keyword);
    SendMessageW(searchButton, BM_CLICK, 0, 0);

    HWND list = FindWindowExW(pane, nullptr, WC_LISTVIEWW, nullptr);
    Check(list && ListView_GetItemCount(list) == 1, "Expected one global result");
    ListView_SetItemState(list, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    NMHDR notification{list, static_cast<UINT_PTR>(GetDlgCtrlID(list)), NM_RETURN};
    SendMessageW(pane, WM_NOTIFY, notification.idFrom, reinterpret_cast<LPARAM>(&notification));
}

void RunTests() {
    ScratchDirectory scratch;
    const auto firstPath = scratch.path / L"first.anynote";
    const auto secondPath = scratch.path / L"second.anynote";
    const auto emptyPath = scratch.path / L"empty.anynote";
    const auto configPath = scratch.path / L"anynote.ini";
    CreateVault(firstPath, true);
    CreateVault(secondPath, false);
    CreateVault(emptyPath, false, true);
    anynote::storage::VaultManager config(configPath.wstring());
    Check(config.AddVault(L"First", firstPath.wstring()), "Cannot register first vault");
    Check(config.AddVault(L"Second", secondPath.wstring()), "Cannot register second vault");
    Check(config.AddVault(L"Empty", emptyPath.wstring()), "Cannot register empty vault");
    config.SetActiveVaultPath(firstPath.wstring());

    // The real MainWindow reads only our isolated config. It stays hidden and
    // receives the same command/notification messages as normal user actions.
    anynote::ui::MainWindow window(configPath.wstring());
    Check(window.Initialize(GetModuleHandleW(nullptr), SW_HIDE), "Cannot initialize test window");
    CloseWindow close{window.GetHwnd()};
    HWND tree = GetDlgItem(window.GetHwnd(), IDC_MAIN_TREEVIEW);
    HWND editor = GetDlgItem(window.GetHwnd(), IDC_MAIN_RICHEDIT);
    SendMessageW(window.GetHwnd(), WM_COMMAND, ID_TREE_SEARCH, 0);
    HWND bar = GetDlgItem(window.GetHwnd(), IDC_MAIN_NODE_SEARCH_BAR);
    HWND filterEdit = GetDlgItem(bar, IDC_NODE_SEARCH_EDIT);
    HWND count = GetDlgItem(bar, IDC_NODE_SEARCH_COUNT);
    Check(tree && editor && filterEdit && count, "Cannot find node search controls");

    SetWindowTextW(filterEdit, L"Alpha");
    Check(TreeView_GetCount(tree) == 1 && Text(count) == L"1 项", "Initial filter is wrong");
    Check(window.SwitchToVault(secondPath.wstring()), "Cannot switch to second vault");
    Check(Text(filterEdit) == L"Alpha", "Switching vault lost query");
    Check(TreeView_GetCount(tree) == 2 && Text(count) == L"2 项", "Vault switch left stale filter/count");
    Check(window.SwitchToVault(emptyPath.wstring()), "Cannot switch to empty vault");
    Check(TreeView_GetCount(tree) == 0 && Text(count) == L"无匹配", "Empty vault left stale count");
    Check(window.SwitchToVault(secondPath.wstring()), "Cannot return to second vault");
    Check(TreeView_GetCount(tree) == 2 && Text(count) == L"2 项", "Filter did not recover from empty vault");
    std::cout << "[PASS] Vault reload retains query and updates filtered nodes and match counts.\n";

    ActivateGlobalResult(window.GetHwnd(), L"Alpha two");
    Check(Text(filterEdit) == L"Alpha" && TreeView_GetCount(tree) == 2, "Visible global result unnecessarily cleared filter");
    Check(Text(editor).find(L"second alpha body") != std::wstring::npos, "Visible result did not load correct note");

    SetWindowTextW(editor, L"unsaved edits before navigation");
    SetWindowTextW(filterEdit, L"zzzz");
    Check(TreeView_GetCount(tree) == 0, "Expected zero filtered nodes");
    ActivateGlobalResult(window.GetHwnd(), L"bodyneedle");
    Check(Text(filterEdit).empty() && TreeView_GetCount(tree) == 3, "Hidden global result did not restore full tree");
    Check(Text(editor).find(L"bodyneedle in hidden note") != std::wstring::npos, "Hidden global result did not load correct note");
    const HTREEITEM selected = TreeView_GetSelection(tree);
    TVITEMW item{};
    item.hItem = selected;
    item.mask = TVIF_TEXT;
    wchar_t title[64]{};
    item.pszText = title;
    item.cchTextMax = static_cast<int>(std::size(title));
    Check(selected && TreeView_GetItem(tree, &item) && std::wstring(title) == L"Beta", "Hidden result is not selected in tree");

    anynote::storage::Database verifyDb;
    Check(verifyDb.Open(secondPath.wstring(), ""), "Cannot verify saved edits");
    anynote::storage::NoteRepository verifyRepo(verifyDb);
    const auto saved = verifyRepo.SearchNotes(L"unsaved edits before navigation", false, true);
    Check(saved.size() == 1 && saved.front().title == L"Alpha two", "Global navigation lost previous note edits");
    std::cout << "[PASS] Global results open visible/hidden notes correctly and preserve edits.\n";

    // 验证表格对齐功能（水平对齐、表格整体对齐、整列/整行/全表批量对齐、垂直对齐及防偏移）
    auto getRtf = [editor]() -> std::string {
        std::string rtf;
        EDITSTREAM es{};
        es.dwCookie = reinterpret_cast<DWORD_PTR>(&rtf);
        es.pfnCallback = [](DWORD_PTR dwCookie, LPBYTE pbBuff, LONG cb, LONG* pcb) -> DWORD {
            auto* pStr = reinterpret_cast<std::string*>(dwCookie);
            pStr->append(reinterpret_cast<const char*>(pbBuff), static_cast<size_t>(cb));
            *pcb = cb;
            return 0;
        };
        SendMessageW(editor, EM_STREAMOUT, SF_RTF, reinterpret_cast<LPARAM>(&es));
        return rtf;
    };

    auto setRtf = [editor](const std::string& rtf) {
        SendMessageW(editor, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(L""));
        std::string rtfCopy = rtf;
        EDITSTREAM es{};
        es.dwCookie = reinterpret_cast<DWORD_PTR>(&rtfCopy);
        es.pfnCallback = [](DWORD_PTR dwCookie, LPBYTE pbBuff, LONG cb, LONG* pcb) -> DWORD {
            auto* pStr = reinterpret_cast<std::string*>(dwCookie);
            LONG toCopy = std::min(cb, static_cast<LONG>(pStr->size()));
            memcpy(pbBuff, pStr->data(), toCopy);
            pStr->erase(0, toCopy);
            *pcb = toCopy;
            return 0;
        };
        SendMessageW(editor, EM_STREAMIN, SF_RTF, reinterpret_cast<LPARAM>(&es));
    };

    auto findCellRange = [editor](const wchar_t* text) -> CHARRANGE {
        FINDTEXTEXW ft{};
        ft.chrg.cpMin = 0;
        ft.chrg.cpMax = -1;
        ft.lpstrText = text;
        LRESULT res = SendMessageW(editor, EM_FINDTEXTEXW, FR_DOWN, reinterpret_cast<LPARAM>(&ft));
        if (res != -1) {
            return ft.chrgText;
        }
        return { -1, -1 };
    };

    auto getAlignmentAt = [editor, &findCellRange](const wchar_t* text) -> WORD {
        CHARRANGE savedSel = {};
        SendMessageW(editor, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&savedSel));
        CHARRANGE cr = findCellRange(text);
        Check(cr.cpMin != -1, "Target cell text must be found");
        SendMessageW(editor, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&cr));
        PARAFORMAT2 pf{ sizeof(PARAFORMAT2) };
        SendMessageW(editor, EM_GETPARAFORMAT, 0, reinterpret_cast<LPARAM>(&pf));
        SendMessageW(editor, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&savedSel));
        return pf.wAlignment;
    };

    // 1. 验证单元格水平对齐及撤销，且绝不污染表格行对齐（彻底杜绝偏移与错位）
    SetWindowTextW(editor, L"");
    SendMessageW(window.GetHwnd(), WM_COMMAND, ID_INSERT_TABLE, 0);
    SendMessageW(window.GetHwnd(), WM_COMMAND, ID_TABLE_ALIGN_CENTER, 0);
    PARAFORMAT2 pfFirstCell{ sizeof(PARAFORMAT2) };
    SendMessageW(editor, EM_GETPARAFORMAT, 0, reinterpret_cast<LPARAM>(&pfFirstCell));
    Check(pfFirstCell.wAlignment == PFA_CENTER, "Cell alignment must be PFA_CENTER");
    std::string rtfNoPollute = getRtf();
    Check(rtfNoPollute.find("\\qc") != std::string::npos, "Cell RTF must contain \\qc");
    Check(rtfNoPollute.find("\\trqc") == std::string::npos, "Cell alignment must NOT pollute row with \\trqc");
    Check(rtfNoPollute.find("\\trqr") == std::string::npos, "Cell alignment must NOT pollute row with \\trqr");

    SendMessageW(editor, EM_UNDO, 0, 0);
    SendMessageW(editor, EM_GETPARAFORMAT, 0, reinterpret_cast<LPARAM>(&pfFirstCell));
    Check(pfFirstCell.wAlignment == PFA_LEFT, "Undo must restore previous cell alignment");

    // 1b. 验证全新空表格全选并修改对齐（用户最核心反馈：全选空表或刚插入表格改对齐绝不导致整行偏移）
    SetWindowTextW(editor, L"");
    SendMessageW(window.GetHwnd(), WM_COMMAND, ID_INSERT_TABLE, 0);
    CHARRANGE crSelectAll = { 0, -1 };
    SendMessageW(editor, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&crSelectAll));
    SendMessageW(window.GetHwnd(), WM_COMMAND, ID_TABLE_ALL_ALIGN_RIGHT, 0);
    std::string rtfEmptyAllRight = getRtf();
    Check(rtfEmptyAllRight.find("\\trqr") == std::string::npos, "All-cell align on empty table must NOT produce \\trqr");
    Check(rtfEmptyAllRight.find("\\trqc") == std::string::npos, "All-cell align on empty table must NOT produce \\trqc");

    // 2. 验证多单元格拖选修改对齐（同行部分单元格修改对齐时，其他单元格及整行不错位）
    std::string testTableRtf = "{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0 Segoe UI;}}\\trowd\\trleft144\\cellx2400\\cellx4800\\cellx7200\\pard\\intbl Cell1-1\\cell Cell1-2\\cell Cell1-3\\cell\\row\\trowd\\trleft144\\cellx2400\\cellx4800\\cellx7200\\pard\\intbl Cell2-1\\cell Cell2-2\\cell Cell2-3\\cell\\row\\pard\\par}";
    setRtf(testTableRtf);

    CHARRANGE cr1 = findCellRange(L"Cell1-1");
    CHARRANGE cr2 = findCellRange(L"Cell1-2");
    CHARRANGE crMulti = { cr1.cpMin, cr2.cpMax };
    SendMessageW(editor, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&crMulti));
    CHARRANGE crSelected = {};
    SendMessageW(editor, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&crSelected));

    SendMessageW(window.GetHwnd(), WM_COMMAND, ID_TABLE_ALIGN_RIGHT, 0);

    CHARRANGE selAfterMulti = {};
    SendMessageW(editor, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&selAfterMulti));
    Check(selAfterMulti.cpMin == crSelected.cpMin && selAfterMulti.cpMax == crSelected.cpMax,
        "Multi-cell selection must be preserved after SetTableCellHorizontalAlignment");

    int curH = 0, curV = 0;
    Check(window.GetRichEditView().GetTableCellAlignment(&curH, &curV), "GetTableCellAlignment must succeed");
    Check(curH == PFA_RIGHT, "GetTableCellAlignment on multi-cell selection must accurately return PFA_RIGHT");

    Check(getAlignmentAt(L"Cell1-1") == PFA_RIGHT, "Cell1-1 must be PFA_RIGHT");
    Check(getAlignmentAt(L"Cell1-2") == PFA_RIGHT, "Cell1-2 must be PFA_RIGHT");
    Check(getAlignmentAt(L"Cell1-3") == PFA_LEFT, "Cell1-3 must remain PFA_LEFT without being corrupted");
    Check(getAlignmentAt(L"Cell2-1") == PFA_LEFT, "Cell2-1 must remain PFA_LEFT");
    Check(getAlignmentAt(L"Cell2-2") == PFA_LEFT, "Cell2-2 must remain PFA_LEFT");
    Check(getAlignmentAt(L"Cell2-3") == PFA_LEFT, "Cell2-3 must remain PFA_LEFT");

    std::string rtfMulti = getRtf();
    Check(rtfMulti.find("\\trqr") == std::string::npos, "Multi-cell alignment must NOT pollute row with \\trqr");

    SendMessageW(editor, EM_UNDO, 0, 0);
    Check(getAlignmentAt(L"Cell1-1") == PFA_LEFT, "Undo must restore Cell1-1");
    Check(getAlignmentAt(L"Cell1-2") == PFA_LEFT, "Undo must restore Cell1-2");

    SendMessageW(editor, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&crMulti));
    SendMessageW(window.GetHwnd(), WM_COMMAND, ID_TABLE_ALIGN_RIGHT, 0);
    Check(getAlignmentAt(L"Cell1-1") == PFA_RIGHT, "Re-apply must set Cell1-1 to PFA_RIGHT");
    Check(getAlignmentAt(L"Cell1-2") == PFA_RIGHT, "Re-apply must set Cell1-2 to PFA_RIGHT");

    // 3. 验证整列单元格对齐（ID_TABLE_COL_ALIGN_CENTER）
    CHARRANGE crCol1 = findCellRange(L"Cell1-2");
    SendMessageW(editor, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&crCol1));
    SendMessageW(window.GetHwnd(), WM_COMMAND, ID_TABLE_COL_ALIGN_CENTER, 0);
    Check(getAlignmentAt(L"Cell1-2") == PFA_CENTER, "Column 1 row 0 (Cell1-2) must be PFA_CENTER");
    Check(getAlignmentAt(L"Cell2-2") == PFA_CENTER, "Column 1 row 1 (Cell2-2) must be PFA_CENTER");
    Check(getAlignmentAt(L"Cell1-1") == PFA_RIGHT, "Column 0 row 0 (Cell1-1) must remain PFA_RIGHT");
    Check(getAlignmentAt(L"Cell2-1") == PFA_LEFT, "Column 0 row 1 (Cell2-1) must remain PFA_LEFT");

    // 4. 验证整行单元格对齐（ID_TABLE_ROW_ALIGN_RIGHT）
    CHARRANGE crRow2 = findCellRange(L"Cell2-1");
    SendMessageW(editor, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&crRow2));
    SendMessageW(window.GetHwnd(), WM_COMMAND, ID_TABLE_ROW_ALIGN_RIGHT, 0);
    Check(getAlignmentAt(L"Cell2-1") == PFA_RIGHT, "Row 1 cell 0 must be PFA_RIGHT");
    Check(getAlignmentAt(L"Cell2-2") == PFA_RIGHT, "Row 1 cell 1 must be PFA_RIGHT");
    Check(getAlignmentAt(L"Cell2-3") == PFA_RIGHT, "Row 1 cell 2 must be PFA_RIGHT");
    Check(getAlignmentAt(L"Cell1-2") == PFA_CENTER, "Row 0 cell 1 must remain PFA_CENTER");

    // 5. 验证全表单元格对齐（ID_TABLE_ALL_ALIGN_LEFT）
    SendMessageW(window.GetHwnd(), WM_COMMAND, ID_TABLE_ALL_ALIGN_LEFT, 0);
    Check(getAlignmentAt(L"Cell1-1") == PFA_LEFT, "Cell1-1 must be PFA_LEFT after all-align-left");
    Check(getAlignmentAt(L"Cell1-2") == PFA_LEFT, "Cell1-2 must be PFA_LEFT after all-align-left");
    Check(getAlignmentAt(L"Cell1-3") == PFA_LEFT, "Cell1-3 must be PFA_LEFT after all-align-left");
    Check(getAlignmentAt(L"Cell2-1") == PFA_LEFT, "Cell2-1 must be PFA_LEFT after all-align-left");
    Check(getAlignmentAt(L"Cell2-2") == PFA_LEFT, "Cell2-2 must be PFA_LEFT after all-align-left");
    Check(getAlignmentAt(L"Cell2-3") == PFA_LEFT, "Cell2-3 must be PFA_LEFT after all-align-left");

    // 6. 验证单元格垂直对齐（顶端/垂直居中/底端对齐）及可视位移效果
    SetWindowTextW(editor, L"");
    SendMessageW(window.GetHwnd(), WM_COMMAND, ID_INSERT_TABLE, 0);
    CHARRANGE crFirst = { 2, 2 };
    SendMessageW(editor, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&crFirst));
    SendMessageW(editor, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(L"Item1"));
    CHARRANGE crItem1 = findCellRange(L"Item1");
    POINT ptTop = {};
    SendMessageW(editor, EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&ptTop), crItem1.cpMin);

    SendMessageW(editor, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&crItem1));
    SendMessageW(window.GetHwnd(), WM_COMMAND, ID_TABLE_VALIGN_CENTER, 0);
    POINT ptCenter = {};
    SendMessageW(editor, EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&ptCenter), crItem1.cpMin);
    std::string rtfVCenter = getRtf();
    Check(rtfVCenter.find("clvertalc") != std::string::npos, "Vertical center alignment must produce clvertalc in RTF");
    Check(ptCenter.y > ptTop.y, "Vertical center must visibly move text downward compared to top alignment");

    SendMessageW(window.GetHwnd(), WM_COMMAND, ID_TABLE_VALIGN_BOTTOM, 0);
    POINT ptBottom = {};
    SendMessageW(editor, EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&ptBottom), crItem1.cpMin);
    std::string rtfVBottom = getRtf();
    Check(rtfVBottom.find("clvertalb") != std::string::npos, "Vertical bottom alignment must produce clvertalb in RTF");
    Check(ptBottom.y > ptCenter.y, "Vertical bottom must visibly move text downward compared to center alignment");

    SendMessageW(window.GetHwnd(), WM_COMMAND, ID_TABLE_VALIGN_TOP, 0);
    POINT ptBackTop = {};
    SendMessageW(editor, EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&ptBackTop), crItem1.cpMin);
    std::string rtfVTop = getRtf();
    Check(rtfVTop.find("clvertalc") == std::string::npos && rtfVTop.find("clvertalb") == std::string::npos,
        "Vertical top alignment must clear clvertalc and clvertalb in RTF");
    Check(ptBackTop.y == ptTop.y, "Vertical top must restore text back to top position");

    // 7. 验证整列/整行/全表垂直对齐批处理
    SendMessageW(window.GetHwnd(), WM_COMMAND, ID_TABLE_COL_VALIGN_CENTER, 0);
    std::string rtfColVCenter = getRtf();
    Check(rtfColVCenter.find("clvertalc") != std::string::npos, "Column vertical center must set clvertalc");

    SendMessageW(window.GetHwnd(), WM_COMMAND, ID_TABLE_ROW_VALIGN_BOTTOM, 0);
    std::string rtfRowVBottom = getRtf();
    Check(rtfRowVBottom.find("clvertalb") != std::string::npos, "Row vertical bottom must set clvertalb");

    SendMessageW(window.GetHwnd(), WM_COMMAND, ID_TABLE_ALL_VALIGN_CENTER, 0);
    std::string rtfAllVCenter = getRtf();
    Check(rtfAllVCenter.find("clvertalc") != std::string::npos, "All-cell vertical center must set clvertalc");

    // 8. 验证前后带有普通正文段落时的全选修改对齐（复杂富文本场景防偏移及跨界鲁棒性）
    std::string mixedRtf = "{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0 Segoe UI;}}\\pard Paragraph before table\\par\\trowd\\trleft144\\cellx2400\\cellx4800\\cellx7200\\pard\\intbl Cell1-1\\cell Cell1-2\\cell Cell1-3\\cell\\row\\pard Paragraph after table\\par}";
    setRtf(mixedRtf);
    CHARRANGE crSelectAllMixed = { 0, -1 };
    SendMessageW(editor, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&crSelectAllMixed));
    SendMessageW(window.GetHwnd(), WM_COMMAND, ID_TABLE_ALL_ALIGN_RIGHT, 0);
    Check(getAlignmentAt(L"Cell1-1") == PFA_RIGHT, "Cell1-1 in mixed doc must be PFA_RIGHT");
    Check(getAlignmentAt(L"Cell1-2") == PFA_RIGHT, "Cell1-2 in mixed doc must be PFA_RIGHT");
    Check(getAlignmentAt(L"Cell1-3") == PFA_RIGHT, "Cell1-3 in mixed doc must be PFA_RIGHT");
    std::string rtfMixed = getRtf();
    Check(rtfMixed.find("\\trqr") == std::string::npos, "Mixed doc must NOT pollute row with \\trqr");
    Check(rtfMixed.find("Paragraph before table") != std::string::npos, "Header paragraph must be preserved");
    Check(rtfMixed.find("Paragraph after table") != std::string::npos, "Footer paragraph must be preserved");

    std::cout << "[PASS] Table alignment tests (cell horizontal/vertical, col/row/all scopes, no row shifting, mixed notes) all passed.\n";
}

} // namespace

int main() {
    Check(SUCCEEDED(OleInitialize(nullptr)), "Cannot initialize OLE");
    Gdiplus::GdiplusStartupInput input;
    ULONG_PTR token = 0;
    Check(Gdiplus::GdiplusStartup(&token, &input, nullptr) == Gdiplus::Ok, "Cannot initialize GDI+");
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_WIN95_CLASSES | ICC_COOL_CLASSES | ICC_BAR_CLASSES};
    InitCommonControlsEx(&controls);
    int result = 0;
    try {
        RunTests();
    } catch (const std::exception& error) {
        std::cerr << "[FAIL] " << error.what() << '\n';
        result = 1;
    }
    Gdiplus::GdiplusShutdown(token);
    OleUninitialize();
    return result;
}
