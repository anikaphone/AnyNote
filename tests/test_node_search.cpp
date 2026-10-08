#include "ui/MainWindow.h"
#include "resource.h"
#include <gdiplus.h>
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
