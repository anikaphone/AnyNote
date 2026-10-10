#include "SyncDialogs.h"
#include "res/resource.h"
#include <thread>

namespace anynote::ui {
namespace {
struct Progress {
    std::wstring title;
    std::function<void(sync::TransferControl&)> action;
    sync::TransferControl control;
    std::atomic<bool> done{false};
    std::exception_ptr error;
    std::jthread thread;
};
INT_PTR CALLBACK ProgressProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* context = reinterpret_cast<Progress*>(GetWindowLongPtrW(dialog, DWLP_USER));
    if (message == WM_INITDIALOG) {
        context = reinterpret_cast<Progress*>(lParam); SetWindowLongPtrW(dialog, DWLP_USER, lParam);
        SetWindowTextW(dialog, context->title.c_str());
        context->thread = std::jthread([context] { try { context->action(context->control); } catch (...) { context->error = std::current_exception(); } context->done = true; });
        SetTimer(dialog, 1, 100, nullptr); return TRUE;
    }
    if (!context) return FALSE;
    if (message == WM_TIMER) {
        if (context->done) { KillTimer(dialog, 1); context->thread.join(); EndDialog(dialog, IDOK); return TRUE; }
        auto status = std::wstring(context->control.cancelled ? L"正在取消，请等待当前事务结束…" : L"正在处理，请稍候…") +
            L"\r\n已发送 " + std::to_wstring(context->control.sent.load() / 1024) + L" KiB，已接收 " + std::to_wstring(context->control.received.load() / 1024) + L" KiB";
        SetDlgItemTextW(dialog, IDC_SYNC_STATUS, status.c_str()); return TRUE;
    }
    if (message == WM_CLOSE || (message == WM_COMMAND && LOWORD(wParam) == IDCANCEL)) {
        context->control.cancelled = true; EnableWindow(GetDlgItem(dialog, IDCANCEL), FALSE); return TRUE;
    }
    return FALSE;
}
struct Prompt { std::wstring title, value; bool secret; bool accepted = false; };
INT_PTR CALLBACK PromptProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* context = reinterpret_cast<Prompt*>(GetWindowLongPtrW(dialog, DWLP_USER));
    if (message == WM_INITDIALOG) {
        context = reinterpret_cast<Prompt*>(lParam); SetWindowLongPtrW(dialog, DWLP_USER, lParam); SetWindowTextW(dialog, context->title.c_str());
        HWND edit = GetDlgItem(dialog, IDC_SYNC_INPUT);
        if (context->secret) { SetWindowLongPtrW(edit, GWL_STYLE, (GetWindowLongPtrW(edit, GWL_STYLE) & ~(ES_MULTILINE | WS_VSCROLL)) | ES_PASSWORD | ES_AUTOHSCROLL); SendMessageW(edit, EM_SETPASSWORDCHAR, L'●', 0); }
        SendMessageW(edit, EM_SETLIMITTEXT, context->secret ? 512 : 2000, 0); SetWindowTextW(edit, context->value.c_str());
        SetFocus(edit); SendMessageW(edit, EM_SETSEL, 0, -1); return FALSE;
    }
    if (message == WM_COMMAND && context) {
        if (LOWORD(wParam) == IDOK) { context->value = SyncControlText(dialog, IDC_SYNC_INPUT); context->accepted = true; SetDlgItemTextW(dialog, IDC_SYNC_INPUT, L""); EndDialog(dialog, IDOK); return TRUE; }
        if (LOWORD(wParam) == IDCANCEL) { EndDialog(dialog, IDCANCEL); return TRUE; }
    }
    return FALSE;
}
struct Choice { std::wstring title; std::vector<std::wstring> choices; std::optional<size_t> result; };
INT_PTR CALLBACK ChoiceProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* context = reinterpret_cast<Choice*>(GetWindowLongPtrW(dialog, DWLP_USER));
    if (message == WM_INITDIALOG) {
        context = reinterpret_cast<Choice*>(lParam); SetWindowLongPtrW(dialog, DWLP_USER, lParam); SetWindowTextW(dialog, context->title.c_str());
        for (const auto& item : context->choices) SendDlgItemMessageW(dialog, IDC_SYNC_LIST, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item.c_str()));
        SendDlgItemMessageW(dialog, IDC_SYNC_LIST, LB_SETCURSEL, 0, 0); return TRUE;
    }
    if (message == WM_COMMAND && context) {
        if (LOWORD(wParam) == IDOK || (LOWORD(wParam) == IDC_SYNC_LIST && HIWORD(wParam) == LBN_DBLCLK)) {
            auto selected = SendDlgItemMessageW(dialog, IDC_SYNC_LIST, LB_GETCURSEL, 0, 0);
            if (selected != LB_ERR) { context->result = static_cast<size_t>(selected); EndDialog(dialog, IDOK); } return TRUE;
        }
        if (LOWORD(wParam) == IDCANCEL) { EndDialog(dialog, IDCANCEL); return TRUE; }
    }
    return FALSE;
}
}
void RunSyncTask(HWND owner, const std::wstring& title, const std::function<void(sync::TransferControl&)>& action) {
    Progress context; context.title = title; context.action = action;
    if (DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_SYNC_PROGRESS), owner, ProgressProc, reinterpret_cast<LPARAM>(&context)) == -1) throw std::runtime_error("PROGRESS_WINDOW_FAILED");
    if (context.error) std::rethrow_exception(context.error);
}
std::optional<std::wstring> SyncPrompt(HWND owner, const std::wstring& title, const std::wstring& initial, bool secret) {
    Prompt context{title, initial, secret}; DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_SYNC_TEXT), owner, PromptProc, reinterpret_cast<LPARAM>(&context));
    if (context.accepted) return context.value; return {};
}
std::optional<size_t> SyncChoose(HWND owner, const std::wstring& title, const std::vector<std::wstring>& choices) {
    Choice context{title, choices, {}}; DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_SYNC_CHOOSE), owner, ChoiceProc, reinterpret_cast<LPARAM>(&context)); return context.result;
}
std::wstring SyncControlText(HWND dialog, int id) {
    HWND control = GetDlgItem(dialog, id); int size = GetWindowTextLengthW(control); std::wstring text(static_cast<size_t>(size) + 1, L'\0');
    GetWindowTextW(control, text.data(), size + 1); text.resize(size); return text;
}
void SyncError(HWND owner, const std::exception& error) { MessageBoxW(owner, sync::DescribeSyncError(error.what()).c_str(), L"云同步", MB_OK | MB_ICONWARNING); }
}
