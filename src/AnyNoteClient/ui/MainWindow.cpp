#include "MainWindow.h"
#include "resource.h"
#include "common/StringUtils.h"
#include "common/SyntaxHighlighter.h"
#include <tom.h>
#include <commctrl.h>
#include <uxtheme.h>
#include <windowsx.h>
#include <shlwapi.h>
#include <commdlg.h>
#include <shellapi.h>
#include <filesystem>
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace anynote::ui {

using TableAlignmentScope = RichEditView::TableAlignmentScope;

namespace {

std::wstring TrimWhitespace(std::wstring_view s) {
    size_t start = 0;
    while (start < s.size() && iswspace(s[start])) {
        ++start;
    }
    size_t end = s.size();
    while (end > start && iswspace(s[end - 1])) {
        --end;
    }
    return std::wstring(s.substr(start, end - start));
}

bool FuzzySubsequenceMatch(std::wstring_view pattern, std::wstring_view text) {
    if (pattern.empty()) return true;
    size_t p = 0;
    for (wchar_t tc : text) {
        if (towlower(tc) == towlower(pattern[p])) {
            if (++p == pattern.size()) return true;
        }
    }
    return false;
}

std::wstring GetDefaultNotebookPath() {
    wchar_t exePath[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    PathRemoveFileSpecW(exePath);
    return std::wstring(exePath) + L"\\notebook.anynote";
}

struct CodeDialogContext {
    std::wstring initialCode;
    common::CodeLanguage selectedLang = common::CodeLanguage::Cpp;
    std::wstring resultText;
    bool ok = false;
};

INT_PTR CALLBACK CodeDialogProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_INITDIALOG: {
        SetWindowLongPtrW(hDlg, DWLP_USER, lParam);
        auto* pCtx = reinterpret_cast<CodeDialogContext*>(lParam);

        HWND hCombo = GetDlgItem(hDlg, IDC_CODE_LANG);
        const auto& languages = common::GetSupportedLanguages();
        for (const auto& langInfo : languages) {
            SendMessageW(hCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(langInfo.name));
        }
        SendMessageW(hCombo, CB_SETCURSEL, 0, 0);

        HWND hEdit = GetDlgItem(hDlg, IDC_CODE_TEXT);
        HFONT hFont = CreateFontW(
            -MulDiv(10, GetDpiForWindow(hDlg), 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas"
        );
        SetPropW(hDlg, L"CodeFont", reinterpret_cast<HANDLE>(hFont));
        SendMessageW(hEdit, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);

        if (pCtx && !pCtx->initialCode.empty()) {
            SetWindowTextW(hEdit, pCtx->initialCode.c_str());
        } else if (!languages.empty()) {
            SetWindowTextW(hEdit, languages[0].defaultTemplate);
        }
        SendMessageW(hEdit, EM_SETSEL, 0, -1);
        SetFocus(hEdit);
        return FALSE;
    }

    case WM_COMMAND: {
        WORD id = LOWORD(wParam);
        WORD code = HIWORD(wParam);

        if (id == IDC_CODE_LANG && code == CBN_SELCHANGE) {
            HWND hCombo = GetDlgItem(hDlg, IDC_CODE_LANG);
            int idx = static_cast<int>(SendMessageW(hCombo, CB_GETCURSEL, 0, 0));
            HWND hEdit = GetDlgItem(hDlg, IDC_CODE_TEXT);
            const auto& languages = common::GetSupportedLanguages();
            if (idx >= 0 && idx < static_cast<int>(languages.size())) {
                int textLen = GetWindowTextLengthW(hEdit);
                if (textLen == 0) {
                    SetWindowTextW(hEdit, languages[idx].defaultTemplate);
                } else {
                    std::wstring currentText(textLen + 1, L'\0');
                    GetWindowTextW(hEdit, currentText.data(), textLen + 1);
                    currentText.resize(textLen);
                    for (const auto& otherLang : languages) {
                        if (currentText == otherLang.defaultTemplate) {
                            SetWindowTextW(hEdit, languages[idx].defaultTemplate);
                            break;
                        }
                    }
                }
            }
            return TRUE;
        }

        if (id == IDOK) {
            auto* pCtx = reinterpret_cast<CodeDialogContext*>(GetWindowLongPtrW(hDlg, DWLP_USER));
            if (pCtx) {
                HWND hCombo = GetDlgItem(hDlg, IDC_CODE_LANG);
                int idx = static_cast<int>(SendMessageW(hCombo, CB_GETCURSEL, 0, 0));
                const auto& languages = common::GetSupportedLanguages();
                if (idx >= 0 && idx < static_cast<int>(languages.size())) {
                    pCtx->selectedLang = languages[idx].lang;
                }

                HWND hEdit = GetDlgItem(hDlg, IDC_CODE_TEXT);
                int len = GetWindowTextLengthW(hEdit);
                if (len > 0) {
                    std::wstring buf(len + 1, L'\0');
                    GetWindowTextW(hEdit, buf.data(), len + 1);
                    buf.resize(len);
                    pCtx->resultText = std::move(buf);
                }
                pCtx->ok = true;
            }
            EndDialog(hDlg, IDOK);
            return TRUE;
        }

        if (id == IDCANCEL) {
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;
    }

    case WM_NCDESTROY: {
        auto hFont = reinterpret_cast<HFONT>(RemovePropW(hDlg, L"CodeFont"));
        if (hFont) {
            DeleteObject(hFont);
        }
        break;
    }
    }
    return FALSE;
}

struct PasswordPromptContext {
    std::string password;
    bool ok = false;
};

INT_PTR CALLBACK PasswordPromptDialogProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_INITDIALOG:
        SetWindowLongPtrW(hDlg, DWLP_USER, lParam);
        SetFocus(GetDlgItem(hDlg, IDC_PASSWORD_INPUT));
        return FALSE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK) {
            auto* pCtx = reinterpret_cast<PasswordPromptContext*>(GetWindowLongPtrW(hDlg, DWLP_USER));
            if (pCtx) {
                HWND hEdit = GetDlgItem(hDlg, IDC_PASSWORD_INPUT);
                int len = GetWindowTextLengthW(hEdit);
                if (len > 0) {
                    std::wstring wPwd(len + 1, L'\0');
                    GetWindowTextW(hEdit, wPwd.data(), len + 1);
                    wPwd.resize(len);
                    pCtx->password = anynote::utils::WideToUtf8(wPwd);
                }
                pCtx->ok = true;
            }
            EndDialog(hDlg, IDOK);
            return TRUE;
        } else if (LOWORD(wParam) == IDCANCEL) {
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

struct SetPasswordContext {
    std::string newPassword;
    bool removePassword = false;
    bool ok = false;
};

INT_PTR CALLBACK SetPasswordDialogProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_INITDIALOG:
        SetWindowLongPtrW(hDlg, DWLP_USER, lParam);
        SetFocus(GetDlgItem(hDlg, IDC_PASSWORD_NEW));
        return FALSE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK) {
            HWND hNew = GetDlgItem(hDlg, IDC_PASSWORD_NEW);
            HWND hConfirm = GetDlgItem(hDlg, IDC_PASSWORD_CONFIRM);

            int lenNew = GetWindowTextLengthW(hNew);
            int lenConfirm = GetWindowTextLengthW(hConfirm);

            std::wstring wNew(lenNew + 1, L'\0');
            GetWindowTextW(hNew, wNew.data(), lenNew + 1);
            wNew.resize(lenNew);

            std::wstring wConfirm(lenConfirm + 1, L'\0');
            GetWindowTextW(hConfirm, wConfirm.data(), lenConfirm + 1);
            wConfirm.resize(lenConfirm);

            if (wNew != wConfirm) {
                MessageBoxW(hDlg, L"两次输入的密码不一致，请核对后重新输入！", L"密码不一致", MB_OK | MB_ICONWARNING);
                SetFocus(hConfirm);
                return TRUE;
            }

            auto* pCtx = reinterpret_cast<SetPasswordContext*>(GetWindowLongPtrW(hDlg, DWLP_USER));
            if (pCtx) {
                if (wNew.empty()) {
                    if (MessageBoxW(hDlg, L"确定要解除密码加密，将笔记本转换为明文格式存储吗？", L"确认解除加密", MB_YESNO | MB_ICONQUESTION) != IDYES) {
                        return TRUE;
                    }
                    pCtx->removePassword = true;
                    pCtx->newPassword = "";
                } else {
                    pCtx->newPassword = anynote::utils::WideToUtf8(wNew);
                    pCtx->removePassword = false;
                }
                pCtx->ok = true;
            }
            EndDialog(hDlg, IDOK);
            return TRUE;
        } else if (LOWORD(wParam) == IDCANCEL) {
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

// ======================== 新建笔记本库对话框 ========================
struct NewVaultContext {
    storage::VaultManager* mgr = nullptr;
    std::wstring name;
    std::wstring path;
    bool ok = false;
};

INT_PTR CALLBACK NewVaultDialogProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_INITDIALOG: {
        SetWindowLongPtrW(hDlg, DWLP_USER, lParam);
        auto* pCtx = reinterpret_cast<NewVaultContext*>(lParam);
        HWND hEditName = GetDlgItem(hDlg, IDC_NEW_VAULT_NAME);
        HWND hEditPath = GetDlgItem(hDlg, IDC_NEW_VAULT_PATH);

        std::wstring defaultName = L"我的笔记";
        if (pCtx && pCtx->mgr) {
            int counter = 1;
            std::wstring candName = L"新笔记库";
            while (std::filesystem::exists(pCtx->mgr->GenerateNewVaultPath(candName))) {
                candName = L"新笔记库_" + std::to_wstring(++counter);
            }
            defaultName = candName;
            std::wstring path = pCtx->mgr->GenerateNewVaultPath(defaultName);
            SetWindowTextW(hEditPath, path.c_str());
        }
        SetWindowTextW(hEditName, defaultName.c_str());
        SendMessageW(hEditName, EM_SETSEL, 0, -1);
        SetFocus(hEditName);
        return FALSE;
    }

    case WM_COMMAND: {
        WORD id = LOWORD(wParam);
        WORD code = HIWORD(wParam);

        if (id == IDC_NEW_VAULT_NAME && code == EN_CHANGE) {
            auto* pCtx = reinterpret_cast<NewVaultContext*>(GetWindowLongPtrW(hDlg, DWLP_USER));
            if (pCtx && pCtx->mgr) {
                HWND hEditName = GetDlgItem(hDlg, IDC_NEW_VAULT_NAME);
                int len = GetWindowTextLengthW(hEditName);
                std::wstring name(len + 1, L'\0');
                GetWindowTextW(hEditName, name.data(), len + 1);
                name.resize(len);

                std::wstring path = pCtx->mgr->GenerateNewVaultPath(name.empty() ? L"未命名" : name);
                SetWindowTextW(GetDlgItem(hDlg, IDC_NEW_VAULT_PATH), path.c_str());
            }
            return TRUE;
        }

        if (id == IDOK) {
            auto* pCtx = reinterpret_cast<NewVaultContext*>(GetWindowLongPtrW(hDlg, DWLP_USER));
            if (pCtx && pCtx->mgr) {
                HWND hEditName = GetDlgItem(hDlg, IDC_NEW_VAULT_NAME);
                int len = GetWindowTextLengthW(hEditName);
                if (len <= 0) {
                    MessageBoxW(hDlg, L"请输入有效的笔记本库名称！", L"提示", MB_OK | MB_ICONWARNING);
                    SetFocus(hEditName);
                    return TRUE;
                }
                std::wstring name(len + 1, L'\0');
                GetWindowTextW(hEditName, name.data(), len + 1);
                name.resize(len);

                std::wstring path = pCtx->mgr->GenerateNewVaultPath(name);
                if (std::filesystem::exists(path)) {
                    if (MessageBoxW(hDlg, (L"该位置已存在笔记库文件：\r\n" + path + L"\r\n\r\n是否直接将该现有库加入列表并打开？").c_str(), L"文件已存在", MB_YESNO | MB_ICONQUESTION) != IDYES) {
                        return TRUE;
                    }
                }

                pCtx->name = std::move(name);
                pCtx->path = std::move(path);
                pCtx->ok = true;
            }
            EndDialog(hDlg, IDOK);
            return TRUE;
        }

        if (id == IDCANCEL) {
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    }
    return FALSE;
}

// ======================== 重命名笔记本库对话框 ========================
struct RenameVaultContext {
    std::wstring initialName;
    std::wstring resultName;
    bool ok = false;
};

INT_PTR CALLBACK RenameVaultDialogProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_INITDIALOG: {
        SetWindowLongPtrW(hDlg, DWLP_USER, lParam);
        auto* pCtx = reinterpret_cast<RenameVaultContext*>(lParam);
        HWND hEdit = GetDlgItem(hDlg, IDC_RENAME_VAULT_INPUT);
        if (pCtx) {
            SetWindowTextW(hEdit, pCtx->initialName.c_str());
            SendMessageW(hEdit, EM_SETSEL, 0, -1);
        }
        SetFocus(hEdit);
        return FALSE;
    }

    case WM_COMMAND: {
        WORD id = LOWORD(wParam);
        if (id == IDOK) {
            auto* pCtx = reinterpret_cast<RenameVaultContext*>(GetWindowLongPtrW(hDlg, DWLP_USER));
            if (pCtx) {
                HWND hEdit = GetDlgItem(hDlg, IDC_RENAME_VAULT_INPUT);
                int len = GetWindowTextLengthW(hEdit);
                if (len <= 0) {
                    MessageBoxW(hDlg, L"库显示名称不能为空！", L"提示", MB_OK | MB_ICONWARNING);
                    SetFocus(hEdit);
                    return TRUE;
                }
                std::wstring name(len + 1, L'\0');
                GetWindowTextW(hEdit, name.data(), len + 1);
                name.resize(len);
                pCtx->resultName = std::move(name);
                pCtx->ok = true;
            }
            EndDialog(hDlg, IDOK);
            return TRUE;
        } else if (id == IDCANCEL) {
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    }
    return FALSE;
}

// ======================== 管理笔记本库对话框 ========================
struct ManageVaultsContext {
    storage::VaultManager* mgr = nullptr;
    MainWindow* mainWindow = nullptr;
    std::wstring selectedSwitchPath;
    bool switchRequested = false;
    bool listModified = false;
};

static void RefreshVaultList(HWND hDlg, ManageVaultsContext* pCtx) {
    HWND hList = GetDlgItem(hDlg, IDC_VAULT_LIST);
    ListView_DeleteAllItems(hList);
    if (!pCtx || !pCtx->mgr) return;

    const auto& vaults = pCtx->mgr->GetVaults();
    std::wstring activePath = pCtx->mgr->GetActiveVaultPath();

    for (int i = 0; i < static_cast<int>(vaults.size()); ++i) {
        const auto& v = vaults[i];

        std::wstring displayName = v.name;
        if (_wcsicmp(v.path.c_str(), activePath.c_str()) == 0) {
            displayName += L" (当前)";
        }

        LVITEMW item = {};
        item.mask = LVIF_TEXT | LVIF_PARAM;
        item.iItem = i;
        item.iSubItem = 0;
        item.pszText = const_cast<LPWSTR>(displayName.c_str());
        item.lParam = static_cast<LPARAM>(i);
        ListView_InsertItem(hList, &item);

        std::wstring statusStr = L"正常";
        std::wstring sizeStr = L"-";
        std::error_code ec;
        if (!std::filesystem::exists(v.path, ec)) {
            statusStr = L"文件缺失";
        } else {
            auto fsize = std::filesystem::file_size(v.path, ec);
            if (!ec) {
                if (fsize < 1024) {
                    sizeStr = std::to_wstring(fsize) + L" B";
                } else if (fsize < 1024 * 1024) {
                    sizeStr = std::to_wstring(fsize / 1024) + L" KB";
                } else {
                    double mb = static_cast<double>(fsize) / (1024.0 * 1024.0);
                    wchar_t mbBuf[32];
                    swprintf_s(mbBuf, L"%.2f MB", mb);
                    sizeStr = mbBuf;
                }
            }
        }
        ListView_SetItemText(hList, i, 1, const_cast<LPWSTR>(statusStr.c_str()));
        ListView_SetItemText(hList, i, 2, const_cast<LPWSTR>(sizeStr.c_str()));
        ListView_SetItemText(hList, i, 3, const_cast<LPWSTR>(v.path.c_str()));

        if (_wcsicmp(v.path.c_str(), activePath.c_str()) == 0) {
            ListView_SetItemState(hList, i, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        }
    }
}

INT_PTR CALLBACK ManageVaultsDialogProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_INITDIALOG: {
        SetWindowLongPtrW(hDlg, DWLP_USER, lParam);
        auto* pCtx = reinterpret_cast<ManageVaultsContext*>(lParam);

        HWND hList = GetDlgItem(hDlg, IDC_VAULT_LIST);
        ListView_SetExtendedListViewStyle(hList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);

        LVCOLUMNW col = {};
        col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;

        col.pszText = const_cast<LPWSTR>(L"库名称");
        col.cx = 120;
        col.iSubItem = 0;
        ListView_InsertColumn(hList, 0, &col);

        col.pszText = const_cast<LPWSTR>(L"状态");
        col.cx = 65;
        col.iSubItem = 1;
        ListView_InsertColumn(hList, 1, &col);

        col.pszText = const_cast<LPWSTR>(L"大小");
        col.cx = 70;
        col.iSubItem = 2;
        ListView_InsertColumn(hList, 2, &col);

        col.pszText = const_cast<LPWSTR>(L"存储路径");
        col.cx = 240;
        col.iSubItem = 3;
        ListView_InsertColumn(hList, 3, &col);

        RefreshVaultList(hDlg, pCtx);
        return TRUE;
    }

    case WM_NOTIFY: {
        auto* pNMHdr = reinterpret_cast<NMHDR*>(lParam);
        if (pNMHdr->idFrom == IDC_VAULT_LIST && pNMHdr->code == NM_DBLCLK) {
            SendMessageW(hDlg, WM_COMMAND, MAKEWPARAM(IDC_VAULT_BTN_SWITCH, BN_CLICKED), 0);
            return TRUE;
        }
        break;
    }

    case WM_COMMAND: {
        WORD id = LOWORD(wParam);
        auto* pCtx = reinterpret_cast<ManageVaultsContext*>(GetWindowLongPtrW(hDlg, DWLP_USER));
        if (!pCtx || !pCtx->mgr) break;

        HWND hList = GetDlgItem(hDlg, IDC_VAULT_LIST);
        int sel = ListView_GetNextItem(hList, -1, LVNI_SELECTED);

        if (id == IDC_VAULT_BTN_NEW) {
            NewVaultContext nvCtx;
            nvCtx.mgr = pCtx->mgr;
            if (DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_NEW_VAULT), hDlg, NewVaultDialogProc, reinterpret_cast<LPARAM>(&nvCtx)) == IDOK && nvCtx.ok) {
                std::error_code ec;
                std::filesystem::create_directories(pCtx->mgr->GetNotebooksDir(), ec);
                if (!std::filesystem::exists(nvCtx.path)) {
                    storage::Database newDb;
                    if (newDb.Open(nvCtx.path, "")) {
                        storage::NoteRepository newRepo(newDb);
                        newRepo.InitializeSchema();
                        newDb.Close();
                    }
                }
                pCtx->mgr->AddVault(nvCtx.name, nvCtx.path);
                pCtx->listModified = true;
                RefreshVaultList(hDlg, pCtx);
            }
            return TRUE;
        }

        if (id == IDC_VAULT_BTN_RENAME) {
            if (sel < 0) {
                MessageBoxW(hDlg, L"请先在列表中选中一个笔记本库！", L"提示", MB_OK | MB_ICONINFORMATION);
                return TRUE;
            }
            const auto* v = pCtx->mgr->GetVault(static_cast<size_t>(sel));
            if (!v) return TRUE;

            RenameVaultContext rvCtx;
            rvCtx.initialName = v->name;
            if (DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_RENAME_VAULT), hDlg, RenameVaultDialogProc, reinterpret_cast<LPARAM>(&rvCtx)) == IDOK && rvCtx.ok) {
                pCtx->mgr->RenameVault(static_cast<size_t>(sel), rvCtx.resultName);
                pCtx->listModified = true;
                RefreshVaultList(hDlg, pCtx);
            }
            return TRUE;
        }

        if (id == IDC_VAULT_BTN_REMOVE) {
            if (sel < 0) {
                MessageBoxW(hDlg, L"请先在列表中选中一个笔记本库！", L"提示", MB_OK | MB_ICONINFORMATION);
                return TRUE;
            }
            if (pCtx->mgr->GetVaultCount() <= 1) {
                MessageBoxW(hDlg, L"列表中仅剩最后一个笔记本库，不可移除！", L"提示", MB_OK | MB_ICONINFORMATION);
                return TRUE;
            }
            const auto* v = pCtx->mgr->GetVault(static_cast<size_t>(sel));
            if (!v) return TRUE;

            std::wstring prompt = L"确定从库列表中移除“" + v->name + L"”吗？\r\n\r\n（注意：这仅从列表中移除引用，不会删除磁盘上的数据库文件）";
            if (MessageBoxW(hDlg, prompt.c_str(), L"确认移除", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                if (_wcsicmp(v->path.c_str(), pCtx->mgr->GetActiveVaultPath().c_str()) == 0) {
                    std::wstring fallbackPath;
                    for (size_t i = 0; i < pCtx->mgr->GetVaultCount(); ++i) {
                        if (i != static_cast<size_t>(sel)) {
                            const auto* fallback = pCtx->mgr->GetVault(i);
                            if (fallback) { fallbackPath = fallback->path; break; }
                        }
                    }
                    if (!pCtx->mainWindow || fallbackPath.empty() || !pCtx->mainWindow->SwitchToVault(fallbackPath)) {
                        MessageBoxW(hDlg, L"无法切换到其他笔记本库，当前库未移除。", L"切换失败", MB_OK | MB_ICONWARNING);
                        return TRUE;
                    }
                }
                pCtx->mgr->RemoveVault(static_cast<size_t>(sel));
                pCtx->listModified = true;
                RefreshVaultList(hDlg, pCtx);
            }
            return TRUE;
        }

        if (id == IDC_VAULT_BTN_EXPLORER) {
            if (sel < 0) {
                MessageBoxW(hDlg, L"请先在列表中选中一个笔记本库！", L"提示", MB_OK | MB_ICONINFORMATION);
                return TRUE;
            }
            const auto* v = pCtx->mgr->GetVault(static_cast<size_t>(sel));
            if (!v) return TRUE;

            if (std::filesystem::exists(v->path)) {
                ShellExecuteW(nullptr, L"open", L"explorer.exe", (L"/select,\"" + v->path + L"\"").c_str(), nullptr, SW_SHOWNORMAL);
            } else if (std::filesystem::exists(pCtx->mgr->GetNotebooksDir())) {
                ShellExecuteW(nullptr, L"open", L"explorer.exe", pCtx->mgr->GetNotebooksDir().c_str(), nullptr, SW_SHOWNORMAL);
            } else {
                MessageBoxW(hDlg, L"该笔记库文件在磁盘上不存在！", L"文件缺失", MB_OK | MB_ICONWARNING);
            }
            return TRUE;
        }

        if (id == IDC_VAULT_BTN_SWITCH) {
            if (sel < 0) {
                MessageBoxW(hDlg, L"请先在列表中选中一个笔记本库！", L"提示", MB_OK | MB_ICONINFORMATION);
                return TRUE;
            }
            const auto* v = pCtx->mgr->GetVault(static_cast<size_t>(sel));
            if (v) {
                pCtx->selectedSwitchPath = v->path;
                pCtx->switchRequested = true;
            }
            EndDialog(hDlg, IDOK);
            return TRUE;
        }

        if (id == IDCANCEL) {
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    }
    return FALSE;
}

// ======================== 左侧目录树顶部切换栏 ========================
struct VaultBarState {
    bool isHovered = false;
    bool isPressed = false;
    MainWindow* pMain = nullptr;
};

LRESULT CALLBACK VaultBarSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    auto* pState = reinterpret_cast<VaultBarState*>(dwRefData);
    switch (uMsg) {
    case WM_MOUSEMOVE: {
        if (pState && !pState->isHovered) {
            pState->isHovered = true;
            TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, hWnd, 0 };
            TrackMouseEvent(&tme);
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        break;
    }
    case WM_MOUSELEAVE: {
        if (pState) {
            pState->isHovered = false;
            pState->isPressed = false;
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        break;
    }
    case WM_LBUTTONDOWN: {
        if (pState) {
            pState->isPressed = true;
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        break;
    }
    case WM_LBUTTONUP: {
        if (pState && pState->isPressed) {
            pState->isPressed = false;
            InvalidateRect(hWnd, nullptr, FALSE);
            if (pState->pMain) {
                pState->pMain->ShowVaultMenu();
            }
            return 0;
        }
        break;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rc;
        GetClientRect(hWnd, &rc);

        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBitmap = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);

        COLORREF bgCol = RGB(255, 255, 255);
        if (pState && pState->isPressed) {
            bgCol = RGB(232, 238, 245);
        } else if (pState && pState->isHovered) {
            bgCol = RGB(244, 247, 251);
        }
        HBRUSH hBg = CreateSolidBrush(bgCol);
        FillRect(memDC, &rc, hBg);
        DeleteObject(hBg);

        // 底部细边框线
        HBRUSH hLine = CreateSolidBrush(RGB(226, 230, 234));
        RECT lineRc = { rc.left, rc.bottom - 1, rc.right, rc.bottom };
        FillRect(memDC, &lineRc, hLine);
        DeleteObject(hLine);

        UINT dpi = GetDpiForWindow(hWnd);
        HFONT hFont = CreateFontW(
            -MulDiv(10, dpi, 72), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
        );
        HFONT oldFont = (HFONT)SelectObject(memDC, hFont);
        SetBkMode(memDC, TRANSPARENT);
        SetTextColor(memDC, RGB(45, 52, 60));

        std::wstring vaultTitle = L"📚 ";
        if (pState && pState->pMain) {
            vaultTitle += pState->pMain->GetActiveVaultDisplayName();
        }

        RECT textRc = rc;
        textRc.left += MulDiv(10, dpi, 96);
        textRc.right -= MulDiv(26, dpi, 96);
        textRc.bottom -= 1;
        DrawTextW(memDC, vaultTitle.c_str(), -1, &textRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        // 右侧下拉箭头
        SetTextColor(memDC, RGB(130, 140, 150));
        RECT arrowRc = rc;
        arrowRc.left = rc.right - MulDiv(24, dpi, 96);
        arrowRc.bottom -= 1;
        DrawTextW(memDC, L"▾", -1, &arrowRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        SelectObject(memDC, oldFont);
        DeleteObject(hFont);

        BitBlt(hdc, 0, 0, rc.right, rc.bottom, memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, oldBitmap);
        DeleteObject(memBitmap);
        DeleteDC(memDC);

        EndPaint(hWnd, &ps);
        return 0;
    }
    case WM_NCDESTROY: {
        delete pState;
        RemoveWindowSubclass(hWnd, VaultBarSubclassProc, uIdSubclass);
        break;
    }
    }
    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

} // namespace

static bool s_mainClassRegistered = false;

MainWindow::MainWindow(const std::wstring& iniPath)
    : m_vaultManager(iniPath), m_findReplaceDialog(m_richEditView) {
}

void MainWindow::RegisterClassIfNeeded(HINSTANCE hInstance) {
    if (s_mainClassRegistered) return;

    WNDCLASSEXW wc = {sizeof(WNDCLASSEXW)};
    wc.lpfnWndProc = [](HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) -> LRESULT {
        auto* pThis = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
        if (uMsg == WM_NCCREATE) {
            auto* pCreate = reinterpret_cast<CREATESTRUCTW*>(lParam);
            pThis = reinterpret_cast<MainWindow*>(pCreate->lpCreateParams);
            if (pThis) {
                pThis->m_hWnd = hWnd;
                SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
            }
        }
        if (pThis) {
            return pThis->HandleMessage(uMsg, wParam, lParam);
        }
        return DefWindowProcW(hWnd, uMsg, wParam, lParam);
    };
    wc.hInstance = hInstance;
    wc.lpszClassName = GetClassName();
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_SHARED);
    wc.hIconSm = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

    RegisterClassExW(&wc);
    s_mainClassRegistered = true;
}

bool MainWindow::Initialize(HINSTANCE hInstance, int nCmdShow) {
    RegisterClassIfNeeded(hInstance);

    HMENU hMenu = LoadMenuW(hInstance, MAKEINTRESOURCEW(IDR_MAIN_MENU));

    if (!Create(
        GetClassName(),
        L"AnyNote - 树状富文本笔记 (C++ & Win32)",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        0,
        CW_USEDEFAULT, CW_USEDEFAULT, 1200, 780,
        nullptr,
        hMenu,
        hInstance
    )) {
        return false;
    }

    // 设置窗口大/小图标
    HICON hIconBig = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR);
    HICON hIconSmall = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
    if (hIconBig) {
        SendMessageW(m_hWnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(hIconBig));
    }
    if (hIconSmall) {
        SendMessageW(m_hWnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(hIconSmall));
    }

    RECT rcClient;
    GetClientRect(m_hWnd, &rcClient);
    int clientW = rcClient.right - rcClient.left;
    int clientH = rcClient.bottom - rcClient.top;

    UINT dpi = GetDpiForWindow(m_hWnd);
    int statusbarH = MulDiv(24, dpi, 96);

    // 1. 初始化顶部格式工具栏
    m_toolbar.Initialize(m_hWnd, IDC_MAIN_TOOLBAR);
    int toolbarH = m_toolbar.GetPreferredHeight();

    // 2. 初始化底部状态栏 (纯白现代无边框扁平风格，移除老旧 SBARS_SIZEGRIP，纵向紧凑美观)
    m_hStatusBar = CreateWindowExW(
        0,
        STATUSCLASSNAME,
        L"",
        WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0,
        m_hWnd,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_MAIN_STATUSBAR)),
        GetModuleHandleW(nullptr),
        nullptr
    );

    if (m_hStatusBar) {
        SetWindowTheme(m_hStatusBar, L"", L"");
        SendMessageW(m_hStatusBar, SB_SETBKCOLOR, 0, static_cast<LPARAM>(RGB(255, 255, 255)));
        SendMessageW(m_hStatusBar, SB_SETMINHEIGHT, MulDiv(20, dpi, 96), 0);

        SetWindowSubclass(m_hStatusBar, [](HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR /*dwRefData*/) -> LRESULT {
            LRESULT lr = DefSubclassProc(hWnd, uMsg, wParam, lParam);
            if (uMsg == WM_PAINT) {
                HDC hdc = GetDC(hWnd);
                if (hdc) {
                    RECT rc;
                    GetClientRect(hWnd, &rc);
                    HBRUSH hLine = CreateSolidBrush(RGB(226, 230, 234));
                    RECT lineRc = { rc.left, rc.top, rc.right, rc.top + 1 };
                    FillRect(hdc, &lineRc, hLine);
                    DeleteObject(hLine);
                    ReleaseDC(hWnd, hdc);
                }
            }
            if (uMsg == WM_NCDESTROY) {
                RemoveWindowSubclass(hWnd, nullptr, uIdSubclass);
            }
            return lr;
        }, 1, 0);

        HFONT hStatusFont = CreateFontW(
            -MulDiv(9, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
        );
        if (hStatusFont) {
            SendMessageW(m_hStatusBar, WM_SETFONT, reinterpret_cast<WPARAM>(hStatusFont), TRUE);
        }
    }

    int statWidths[] = { MulDiv(380, dpi, 96), MulDiv(580, dpi, 96), -1 };
    SendMessageW(m_hStatusBar, SB_SETPARTS, 3, reinterpret_cast<LPARAM>(statWidths));
    SendMessageW(m_hStatusBar, SB_SETTEXTW, 0 | SBT_NOBORDERS, reinterpret_cast<LPARAM>(L"就绪 | 欢迎使用 AnyNote"));
    SendMessageW(m_hStatusBar, SB_SETTEXTW, 1 | SBT_NOBORDERS, reinterpret_cast<LPARAM>(L"状态: 已保存"));
    SendMessageW(m_hStatusBar, SB_SETTEXTW, 2 | SBT_NOBORDERS, reinterpret_cast<LPARAM>(L"加密: 未加密"));

    // 3. 初始化分割条
    m_splitter.Initialize(m_hWnd, m_splitterPos, toolbarH, clientH - toolbarH - statusbarH, false);

    // 4. 初始化左侧目录树顶部切换栏与目录树
    int vaultBarH = MulDiv(30, dpi, 96);
    m_hVaultBar = CreateWindowExW(
        0,
        L"BUTTON",
        L"",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        0, toolbarH, m_splitterPos, vaultBarH,
        m_hWnd,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_MAIN_VAULT_BAR)),
        hInstance,
        nullptr
    );
    if (m_hVaultBar) {
        auto* pState = new VaultBarState();
        pState->pMain = this;
        SetWindowSubclass(m_hVaultBar, VaultBarSubclassProc, 1, reinterpret_cast<DWORD_PTR>(pState));
    }

    int treeH = std::max(40, clientH - toolbarH - statusbarH - vaultBarH);
    m_treeView.Initialize(m_hWnd, 0, toolbarH + vaultBarH, m_splitterPos, treeH, IDC_MAIN_TREEVIEW);

    // 5. 初始化右侧富文本编辑器
    if (!m_richEditView.Initialize(
        m_hWnd,
        m_splitterPos + 5, toolbarH,
        clientW - m_splitterPos - 5, clientH - toolbarH - statusbarH,
        IDC_MAIN_RICHEDIT
    )) {
        return false;
    }

    // 6. 初始化仿 Notepad++ 独立浮动查找/替换/标记对话框
    m_findReplaceDialog.Initialize(m_hWnd);

    // 7. 初始化下方水平分割条与全库搜索窗格
    m_searchSplitter.Initialize(m_hWnd, m_splitterPos + 5, 0, clientW - m_splitterPos - 5, true);
    m_searchPane.Initialize(m_hWnd, nullptr);
    m_searchPane.SetOnResultSelected([this](const storage::SearchResult& result) {
        OnSearchResultSelected(result);
    });
    m_searchPane.SetOnClose([this]() {
        m_isSearchPaneVisible = false;
        RECT rc;
        GetClientRect(m_hWnd, &rc);
        LayoutChildren(rc.right - rc.left, rc.bottom - rc.top);
        SetFocus(m_richEditView.GetHwnd());
    });

    // 7.1 初始化左侧目录树底部搜索栏
    m_nodeSearchBar.Initialize(m_hWnd);
    m_nodeSearchBar.SetOnFilterChanged([this](const std::wstring& keyword) {
        FilterTreeNodes(keyword);
    });
    m_nodeSearchBar.SetOnEnterPressed([this]() {
        if (m_treeView.GetHwnd()) {
            SetFocus(m_treeView.GetHwnd());
            HTREEITEM hSel = m_treeView.GetSelectedItem();
            if (!hSel) {
                hSel = TreeView_GetRoot(m_treeView.GetHwnd());
                if (hSel) {
                    m_treeView.SelectItem(hSel);
                }
            }
            if (hSel) {
                m_treeView.EnsureVisible(hSel);
                int64_t nodeId = static_cast<int64_t>(m_treeView.GetItemData(hSel));
                if (nodeId > 0 && nodeId != m_activeNoteId) {
                    if (SaveActiveNote()) {
                        m_activeNoteId = nodeId;
                        LoadNoteForId(nodeId);
                        UpdateStatusBar(L"当前笔记: " + m_treeView.GetItemText(hSel));
                    }
                }
            }
        }
    });
    m_nodeSearchBar.SetOnClose([this]() {
        ShowNodeSearch(false);
    });

    // 7.2 初始化右侧文档大纲面板与垂直分割条
    m_outlineSplitter.Initialize(m_hWnd, 0, toolbarH, clientH - toolbarH - statusbarH, false);
    m_outlinePane.Initialize(m_hWnd);
    m_outlinePane.SetOnItemSelected([this](LONG charPos) {
        m_richEditView.ScrollToCharPos(charPos);
    });
    m_outlinePane.SetOnClose([this]() {
        ShowOutlinePane(false);
    });

    // 8. 加载多库配置并打开活动库 (若失效则优雅回退；仅当首次无任何库时填充欢迎笔记)
    m_vaultManager.Load();
    m_isOutlineVisible = (m_vaultManager.GetConfigString(L"View", L"OutlineVisible", L"0") == L"1");
    std::wstring outlineWStr = m_vaultManager.GetConfigString(L"View", L"OutlineWidth", L"220");
    if (!outlineWStr.empty()) {
        try { m_outlineWidth = std::clamp(std::stoi(outlineWStr), 140, 600); } catch (...) {}
    }
    hMenu = GetMenu(m_hWnd);
    if (hMenu) {
        CheckMenuItem(hMenu, ID_VIEW_OUTLINE, m_isOutlineVisible ? MF_CHECKED : MF_UNCHECKED);
    }
    m_toolbar.SetOutlineButtonChecked(m_isOutlineVisible);

    m_isAlwaysOnTop = (m_vaultManager.GetConfigString(L"Window", L"AlwaysOnTop", L"0") == L"1");
    if (m_isAlwaysOnTop) {
        SetWindowPos(m_hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    if (hMenu) {
        CheckMenuItem(hMenu, ID_HELP_ALWAYS_ON_TOP, m_isAlwaysOnTop ? MF_CHECKED : MF_UNCHECKED);
    }
    m_lastCodeLanguage = LoadLastCodeLanguage();
    bool shouldAddWelcomeNotes = m_vaultManager.IsFirstTimeCreation();
    std::wstring activePath = m_vaultManager.GetActiveVaultPath();
    if (!std::filesystem::exists(activePath)) {
        bool found = false;
        for (const auto& v : m_vaultManager.GetVaults()) {
            if (std::filesystem::exists(v.path)) {
                activePath = v.path;
                m_vaultManager.SetActiveVaultPath(activePath);
                found = true;
                break;
            }
        }
        if (!found) {
            std::error_code ec;
            std::filesystem::create_directories(m_vaultManager.GetNotebooksDir(), ec);
            activePath = m_vaultManager.GenerateNewVaultPath(L"我的笔记");
            m_vaultManager.AddVault(L"我的笔记", activePath);
            m_vaultManager.SetActiveVaultPath(activePath);
            shouldAddWelcomeNotes = true;
        }
    }
    OpenNotebook(activePath, "", shouldAddWelcomeNotes);
    UpdateVaultBarUI();

    LayoutChildren(clientW, clientH);
    Show(nCmdShow);
    Update();

    return true;
}

void MainWindow::LayoutChildren(int clientWidth, int clientHeight) {
    if (clientWidth <= 0 || clientHeight <= 0) return;

    UINT dpi = GetDpiForWindow(m_hWnd);
    int toolbarH = m_toolbar.GetPreferredHeight();
    int statusbarH = MulDiv(21, dpi, 96);
    int vaultBarH = MulDiv(30, dpi, 96);
    int searchBarH = m_isNodeSearchVisible ? MulDiv(34, dpi, 96) : 0;
    int workH = std::max(100, clientHeight - toolbarH - statusbarH);
    int treeH = std::max(40, workH - vaultBarH - searchBarH);

    m_toolbar.SetBounds(0, 0, clientWidth, toolbarH);

    m_splitterPos = std::clamp(m_splitterPos, 150, std::max(160, clientWidth - 250));

    if (m_hVaultBar) {
        SetWindowPos(m_hVaultBar, nullptr, 0, toolbarH, m_splitterPos, vaultBarH, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    m_treeView.SetBounds(0, toolbarH + vaultBarH, m_splitterPos, treeH);

    if (m_isNodeSearchVisible) {
        m_nodeSearchBar.SetBounds(0, toolbarH + vaultBarH + treeH, m_splitterPos, searchBarH);
        ShowWindow(m_nodeSearchBar.GetHwnd(), SW_SHOW);
    } else {
        ShowWindow(m_nodeSearchBar.GetHwnd(), SW_HIDE);
    }

    m_splitter.SetBounds(m_splitterPos, toolbarH, 5, workH);

    int editorX = m_splitterPos + 5;

    int outlineAreaW = 0;
    if (m_isOutlineVisible) {
        int maxOutlineW = std::max(140, clientWidth - editorX - 150);
        m_outlineWidth = std::clamp(m_outlineWidth, 140, maxOutlineW);
        outlineAreaW = m_outlineWidth + 5;
    }

    int editorW = std::max(50, clientWidth - editorX - outlineAreaW);
    int outlineSplitterX = editorX + editorW;
    int outlinePaneX = outlineSplitterX + 5;

    if (m_isSearchPaneVisible) {
        const int splitterH = 5;
        int maxPaneH = std::max(80, workH - 120);
        m_searchPaneHeight = std::clamp(m_searchPaneHeight, 100, maxPaneH);

        int editorH = std::max(40, workH - splitterH - m_searchPaneHeight);
        int splitterY = toolbarH + editorH;
        int paneY = splitterY + splitterH;

        m_richEditView.SetBounds(editorX, toolbarH, editorW, editorH);
        m_searchSplitter.SetBounds(editorX, splitterY, editorW, splitterH);
        m_searchPane.SetBounds(editorX, paneY, editorW, m_searchPaneHeight);

        ShowWindow(m_searchSplitter.GetHwnd(), SW_SHOW);
        ShowWindow(m_searchPane.GetHwnd(), SW_SHOW);
    } else {
        m_richEditView.SetBounds(editorX, toolbarH, editorW, workH);
        ShowWindow(m_searchSplitter.GetHwnd(), SW_HIDE);
        ShowWindow(m_searchPane.GetHwnd(), SW_HIDE);
    }

    if (m_isOutlineVisible) {
        m_outlineSplitter.SetBounds(outlineSplitterX, toolbarH, 5, workH);
        m_outlinePane.SetBounds(outlinePaneX, toolbarH, m_outlineWidth, workH);
        ShowWindow(m_outlineSplitter.GetHwnd(), SW_SHOW);
        ShowWindow(m_outlinePane.GetHwnd(), SW_SHOW);
    } else {
        ShowWindow(m_outlineSplitter.GetHwnd(), SW_HIDE);
        ShowWindow(m_outlinePane.GetHwnd(), SW_HIDE);
    }

    if (m_hStatusBar) {
        SendMessageW(m_hStatusBar, WM_SIZE, 0, 0);
    }
}

void MainWindow::UpdateStatusBar(const std::wstring& text) {
    if (m_hStatusBar) {
        SendMessageW(m_hStatusBar, SB_SETTEXTW, 0 | SBT_NOBORDERS, reinterpret_cast<LPARAM>(text.c_str()));
    }
}

void MainWindow::UpdateEncryptionStatusUI() {
    if (!m_hStatusBar) return;

    if (m_db && m_db->IsEncrypted()) {
        SendMessageW(m_hStatusBar, SB_SETTEXTW, 2 | SBT_NOBORDERS, reinterpret_cast<LPARAM>(L"加密: 已加密 (AES-256)"));
    } else {
        SendMessageW(m_hStatusBar, SB_SETTEXTW, 2 | SBT_NOBORDERS, reinterpret_cast<LPARAM>(L"加密: 未加密"));
    }
}

common::CodeLanguage MainWindow::LoadLastCodeLanguage() {
    std::wstring str = m_vaultManager.GetConfigString(L"Editor", L"LastCodeLanguage", L"PlainText");
    return common::StringToCodeLanguage(str);
}

void MainWindow::SaveLastCodeLanguage(common::CodeLanguage lang) {
    m_vaultManager.SetConfigString(L"Editor", L"LastCodeLanguage", common::CodeLanguageToString(lang));
}

void MainWindow::OnInsertCodeBlock() {
    std::wstring sel = m_richEditView.GetSelectedText();
    common::CodeLanguage lang = m_lastCodeLanguage;
    if (sel.empty()) {
        m_richEditView.InsertCodeBlock(L"\n\n", lang);
    } else {
        m_richEditView.InsertCodeBlock(sel, lang);
    }
    UpdateStatusBar(L"已插入代码卡片");
}

void MainWindow::ShowInsertCodeDialog() {
    OnInsertCodeBlock();
}

void MainWindow::OnInsertImageFromFile() {
    wchar_t szFile[MAX_PATH] = { 0 };
    OPENFILENAMEW ofn = { sizeof(OPENFILENAMEW) };
    ofn.hwndOwner = m_hWnd;
    ofn.lpstrFilter =
        L"所有支持的图片 (*.png;*.jpg;*.jpeg;*.bmp;*.gif)\0*.png;*.jpg;*.jpeg;*.bmp;*.gif\0"
        L"PNG 图片 (*.png)\0*.png\0"
        L"JPEG 图片 (*.jpg;*.jpeg)\0*.jpg;*.jpeg\0"
        L"位图文件 (*.bmp)\0*.bmp\0"
        L"所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"选择要插入的图片";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;

    if (GetOpenFileNameW(&ofn)) {
        if (m_richEditView.InsertImageFromFile(szFile)) {
            UpdateStatusBar(L"已插入图片");
        } else {
            MessageBoxW(m_hWnd, L"插入图片失败，请检查文件是否存在且格式有效。", L"插入图片", MB_OK | MB_ICONWARNING);
        }
    }
}

bool MainWindow::OpenNotebook(const std::wstring& filePath, const std::string& password, bool createWelcomeIfEmpty) {
    if (!SaveActiveNote()) return false;

    auto newDb = std::make_unique<storage::Database>();
    std::string currentPwd = password;

    bool opened = newDb->Open(filePath, currentPwd);
    while (!opened && newDb->GetLastError() == "ENCRYPTED_REQUIRES_PASSWORD") {
        PasswordPromptContext ctx;
        INT_PTR res = DialogBoxParamW(
            GetModuleHandleW(nullptr),
            MAKEINTRESOURCEW(IDD_ENTER_PASSWORD),
            m_hWnd,
            PasswordPromptDialogProc,
            reinterpret_cast<LPARAM>(&ctx)
        );

        if (res != IDOK || !ctx.ok) {
            return false;
        }

        currentPwd = ctx.password;
        opened = newDb->Open(filePath, currentPwd);
        if (!opened) {
            MessageBoxW(m_hWnd, L"密码不正确，无法解密该笔记本！请重新输入。", L"密码错误", MB_OK | MB_ICONERROR);
        }
    }

    if (!opened) {
        std::wstring err = anynote::utils::Utf8ToWide(newDb->GetLastError());
        MessageBoxW(m_hWnd, (L"无法打开笔记本文件：\r\n" + err).c_str(), L"打开失败", MB_OK | MB_ICONERROR);
        return false;
    }

    auto newRepo = std::make_unique<storage::NoteRepository>(*newDb);
    if (!newRepo->InitializeSchema()) {
        MessageBoxW(m_hWnd, L"初始化笔记本表结构失败！", L"数据库错误", MB_OK | MB_ICONERROR);
        return false;
    }

    if (createWelcomeIfEmpty && newRepo->GetNodeCount() == 0) {
        newRepo->CreateDefaultWelcomeNotes();
    }

    m_db = std::move(newDb);
    m_repo = std::move(newRepo);
    m_searchPane.SetRepository(m_repo.get());
    m_currentNotebookPath = filePath;
    // The previous note was saved before opening the new vault. Its node ID
    // must not be used by selection notifications from rebuilding the new tree.
    m_activeNoteId = 0;

    // 更新窗口标题 (优先显示库的友好名称或文件名)
    std::wstring displayName = m_vaultManager.GetActiveVaultDisplayName();
    if (displayName.empty() || displayName == L"未选择库") {
        displayName = std::filesystem::path(filePath).stem().wstring();
    }
    std::wstring title = L"AnyNote - [" + displayName + L"]";
    SetWindowTextW(m_hWnd, title.c_str());

    UpdateEncryptionStatusUI();
    PopulateTreeViewFromDb();
    // Reapply the node filter after rebuilding the tree for a different vault.
    // The search bar belongs to the main window and keeps its query while the
    // notebook contents are replaced.
    if (m_isNodeSearchVisible) {
        const std::wstring keyword = m_nodeSearchBar.GetKeyword();
        if (!TrimWhitespace(keyword).empty()) {
            FilterTreeNodes(keyword);
        }
    }
    UpdateVaultBarUI();

    return true;
}

void MainWindow::CancelDragOperation() {
    if (!m_isDragging) return;

    m_isDragging = false;
    ReleaseCapture();

    if (m_hDragImageList) {
        ImageList_DragLeave(m_hWnd);
        ImageList_EndDrag();
        ImageList_Destroy(m_hDragImageList);
        m_hDragImageList = nullptr;
    }

    m_treeView.ClearInsertMark();
    m_treeView.ClearDropHighlight();
    m_hDragItem = nullptr;
    m_hDropTarget = nullptr;
    m_dropPosition = storage::DropPosition::None;
    SetCursor(LoadCursorW(nullptr, IDC_ARROW));
}

void MainWindow::PopulateTreeViewFromDb(int64_t selectNodeId) {
    m_treeView.ClearAll();
    m_activeNoteId = 0;
    if (!m_repo) return;

    auto nodes = m_repo->GetAllNodes();
    if (nodes.empty()) {
        m_richEditView.SetText(L"");
        m_outlinePane.ClearOutline();
        UpdateStatusBar(L"就绪 | 当前笔记本库为空，可在目录树右键或按 Ctrl+N 新建笔记");
        return;
    }

    std::unordered_map<int64_t, HTREEITEM> idToItem;
    std::vector<storage::NoteNode> remaining = std::move(nodes);

    bool insertedAny = true;
    while (!remaining.empty() && insertedAny) {
        insertedAny = false;
        std::vector<storage::NoteNode> nextRemaining;
        for (auto& node : remaining) {
            if (node.parentId == 0) {
                HTREEITEM hItem = m_treeView.InsertNode(nullptr, node.title, static_cast<LPARAM>(node.id), true);
                idToItem[node.id] = hItem;
                insertedAny = true;
            } else {
                auto it = idToItem.find(node.parentId);
                if (it != idToItem.end()) {
                    HTREEITEM hItem = m_treeView.InsertNode(it->second, node.title, static_cast<LPARAM>(node.id), true);
                    idToItem[node.id] = hItem;
                    insertedAny = true;
                } else {
                    nextRemaining.push_back(std::move(node));
                }
            }
        }
        remaining = std::move(nextRemaining);
    }

    // 孤儿节点（父节点不存在）安全挂在根目录下展示
    for (const auto& orphan : remaining) {
        HTREEITEM hItem = m_treeView.InsertNode(nullptr, orphan.title, static_cast<LPARAM>(orphan.id), true);
        idToItem[orphan.id] = hItem;
    }

    HTREEITEM hToSelect = nullptr;
    if (selectNodeId > 0) {
        auto it = idToItem.find(selectNodeId);
        if (it != idToItem.end()) {
            hToSelect = it->second;
        }
    }

    if (!hToSelect) {
        hToSelect = TreeView_GetRoot(m_treeView.GetHwnd());
    }

    if (hToSelect) {
        m_treeView.SelectItem(hToSelect);
        m_treeView.EnsureVisible(hToSelect);
    }
}

bool MainWindow::SaveActiveNote() {
    if (m_activeNoteId <= 0) return true;
    if (!m_repo || !m_richEditView.GetHwnd()) return false;

    std::string rtf = m_richEditView.StreamOutRTF();
    std::wstring plainText = m_richEditView.GetPlainText();
    if (!m_repo->UpdateNoteContent(m_activeNoteId, rtf, plainText)) {
        if (m_hStatusBar) {
            SendMessageW(m_hStatusBar, SB_SETTEXTW, 1 | SBT_NOBORDERS, reinterpret_cast<LPARAM>(L"状态: 保存失败"));
        }
        MessageBoxW(m_hWnd, L"当前笔记保存失败，原笔记本仍保持打开状态。", L"保存失败", MB_OK | MB_ICONERROR);
        return false;
    }

    if (m_hStatusBar) {
        SendMessageW(m_hStatusBar, SB_SETTEXTW, 1 | SBT_NOBORDERS, reinterpret_cast<LPARAM>(L"状态: 已保存"));
    }
    return true;
}

void MainWindow::LoadNoteForId(int64_t nodeId) {
    if (nodeId <= 0 || !m_repo || !m_richEditView.GetHwnd()) return;

    std::string rtf = m_repo->GetNoteContent(nodeId);
    if (!rtf.empty()) {
        m_richEditView.StreamInRTF(rtf);
    } else {
        m_richEditView.SetText(L"");
    }
    UpdateOutline();
}

void MainWindow::OnTreeSelectionChanged(NMTREEVIEWW* pNmtv) {
    if (!pNmtv) return;
    if (m_revertingTreeSelection) return;
    if (m_isFilteringTree) return;

    if (!pNmtv->itemNew.hItem) {
        if (m_richEditView.GetHwnd()) {
            SaveActiveNote();
            m_richEditView.SetText(L"");
        }
        m_activeNoteId = 0;
        m_outlinePane.ClearOutline();
        UpdateStatusBar(L"未选择笔记");
        return;
    }

    HTREEITEM newItem = pNmtv->itemNew.hItem;
    int64_t newId = static_cast<int64_t>(m_treeView.GetItemData(newItem));
    std::wstring title = m_treeView.GetItemText(newItem);
    UpdateStatusBar(L"当前笔记: " + title);

    if (!m_richEditView.GetHwnd()) return;

    if (m_richEditView.HasActiveMarks()) {
        m_richEditView.ClearMarks();
    }

    if (m_activeNoteId > 0 && m_activeNoteId != newId) {
        if (!SaveActiveNote()) {
            if (pNmtv->itemOld.hItem) {
                m_revertingTreeSelection = true;
                m_treeView.SelectItem(pNmtv->itemOld.hItem);
                m_revertingTreeSelection = false;
                UpdateStatusBar(L"当前笔记: " + m_treeView.GetItemText(pNmtv->itemOld.hItem));
            }
            return;
        }
    }

    m_activeNoteId = newId;
    LoadNoteForId(newId);
}

LRESULT MainWindow::HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_EDITOR_FORMAT_CHANGED:
        m_toolbar.SetSelectedHeadingIndex(m_richEditView.GetCurrentHeadingLevel());
        UpdateOutline();
        return 0;

    case WM_CONTEXTMENU: {
        HWND hWndTarget = reinterpret_cast<HWND>(wParam);
        int xPos = GET_X_LPARAM(lParam);
        int yPos = GET_Y_LPARAM(lParam);

        if (hWndTarget == m_treeView.GetHwnd()) {
            ShowTreeContextMenu(xPos, yPos);
            return 0;
        } else if (hWndTarget == m_richEditView.GetHwnd()) {
            ShowEditorContextMenu(xPos, yPos);
            return 0;
        }
        break;
    }

    case WM_SIZE: {
        int width = LOWORD(lParam);
        int height = HIWORD(lParam);
        LayoutChildren(width, height);
        return 0;
    }

    case WM_DPICHANGED: {
        const RECT* prcNewWindow = reinterpret_cast<const RECT*>(lParam);
        SetWindowPos(m_hWnd, nullptr,
            prcNewWindow->left, prcNewWindow->top,
            prcNewWindow->right - prcNewWindow->left,
            prcNewWindow->bottom - prcNewWindow->top,
            SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }

    case WM_SPLITTER_MOVED: {
        int newPos = static_cast<int>(wParam);
        int isHorizontal = static_cast<int>(lParam);
        RECT rc;
        GetClientRect(m_hWnd, &rc);
        if (isHorizontal) {
            UINT dpi = GetDpiForWindow(m_hWnd);
            int statusbarH = MulDiv(24, dpi, 96);
            int clientH = rc.bottom - rc.top;
            int newPaneH = (clientH - statusbarH - 5) - newPos;
            m_searchPaneHeight = std::max(60, newPaneH);
        } else if (m_outlineSplitter.IsDragging()) {
            int clientW = rc.right - rc.left;
            m_outlineWidth = std::clamp(clientW - newPos - 5, 140, std::max(150, clientW - m_splitterPos - 100));
        } else {
            m_splitterPos = newPos;
        }
        LayoutChildren(rc.right - rc.left, rc.bottom - rc.top);
        return 0;
    }

    case WM_MOUSEMOVE: {
        if (m_isDragging) {
            POINT ptScreen;
            GetCursorPos(&ptScreen);

            if (m_hDragImageList) {
                POINT ptMain = ptScreen;
                ScreenToClient(m_hWnd, &ptMain);
                ImageList_DragMove(ptMain.x, ptMain.y);
            }

            POINT ptTree = ptScreen;
            ScreenToClient(m_treeView.GetHwnd(), &ptTree);

            RECT rcTree;
            GetClientRect(m_treeView.GetHwnd(), &rcTree);

            // 自动向上/向下滚动
            if (ptTree.y < 24) {
                SendMessageW(m_treeView.GetHwnd(), WM_VSCROLL, SB_LINEUP, 0);
            } else if (ptTree.y > rcTree.bottom - 24) {
                SendMessageW(m_treeView.GetHwnd(), WM_VSCROLL, SB_LINEDOWN, 0);
            }

            if (PtInRect(&rcTree, ptTree)) {
                UINT flags = 0;
                HTREEITEM hHit = m_treeView.HitTest(ptTree, &flags);

                if (hHit) {
                    if (hHit == m_hDragItem || m_treeView.IsDescendant(m_hDragItem, hHit)) {
                        if (m_hDragImageList) ImageList_DragShowNolock(FALSE);
                        m_treeView.ClearInsertMark();
                        m_treeView.ClearDropHighlight();
                        if (m_hDragImageList) ImageList_DragShowNolock(TRUE);

                        m_hDropTarget = nullptr;
                        m_dropPosition = storage::DropPosition::None;
                        SetCursor(LoadCursorW(nullptr, IDC_NO));
                    } else {
                        RECT rcItem = m_treeView.GetItemRect(hHit, false);
                        int itemH = rcItem.bottom - rcItem.top;
                        int relY = ptTree.y - rcItem.top;

                        if (m_hDragImageList) ImageList_DragShowNolock(FALSE);

                        if (relY < itemH / 4) {
                            m_dropPosition = storage::DropPosition::Before;
                            m_hDropTarget = hHit;
                            m_treeView.ClearDropHighlight();
                            m_treeView.SetInsertMark(hHit, false);
                        } else if (relY > itemH * 3 / 4) {
                            m_dropPosition = storage::DropPosition::After;
                            m_hDropTarget = hHit;
                            m_treeView.ClearDropHighlight();
                            m_treeView.SetInsertMark(hHit, true);
                        } else {
                            m_dropPosition = storage::DropPosition::AsChild;
                            m_hDropTarget = hHit;
                            m_treeView.ClearInsertMark();
                            m_treeView.SetDropHighlight(hHit);
                        }

                        if (m_hDragImageList) ImageList_DragShowNolock(TRUE);
                        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
                    }
                } else {
                    // 拖拽到底部空白区 -> 放置为根节点末尾
                    if (m_hDragImageList) ImageList_DragShowNolock(FALSE);
                    m_treeView.ClearInsertMark();
                    m_treeView.ClearDropHighlight();
                    if (m_hDragImageList) ImageList_DragShowNolock(TRUE);

                    m_hDropTarget = nullptr;
                    m_dropPosition = storage::DropPosition::AtRootEnd;
                    SetCursor(LoadCursorW(nullptr, IDC_ARROW));
                }
            } else {
                // 移出树控件区域
                if (m_hDragImageList) ImageList_DragShowNolock(FALSE);
                m_treeView.ClearInsertMark();
                m_treeView.ClearDropHighlight();
                if (m_hDragImageList) ImageList_DragShowNolock(TRUE);

                m_hDropTarget = nullptr;
                m_dropPosition = storage::DropPosition::None;
                SetCursor(LoadCursorW(nullptr, IDC_NO));
            }
            return 0;
        }
        break;
    }

    case WM_LBUTTONUP: {
        if (m_isDragging) {
            m_isDragging = false;
            ReleaseCapture();

            if (m_hDragImageList) {
                ImageList_DragLeave(m_hWnd);
                ImageList_EndDrag();
                ImageList_Destroy(m_hDragImageList);
                m_hDragImageList = nullptr;
            }

            m_treeView.ClearInsertMark();
            m_treeView.ClearDropHighlight();
            SetCursor(LoadCursorW(nullptr, IDC_ARROW));

            if (m_hDragItem && m_dropPosition != storage::DropPosition::None) {
                int64_t dragId = static_cast<int64_t>(m_treeView.GetItemData(m_hDragItem));
                int64_t targetId = m_hDropTarget ? static_cast<int64_t>(m_treeView.GetItemData(m_hDropTarget)) : 0;

                // 先保存编辑器，保存失败时不执行移动，避免移动后刷新目录覆盖未保存内容。
                if (m_repo && SaveActiveNote() && m_repo->MoveNode(dragId, targetId, m_dropPosition)) {
                    PopulateTreeViewFromDb(dragId);
                    UpdateStatusBar(L"已调整笔记顺序与层级");
                }
            }

            m_hDragItem = nullptr;
            m_hDropTarget = nullptr;
            m_dropPosition = storage::DropPosition::None;
            return 0;
        }
        break;
    }

    case WM_CANCELMODE:
        CancelDragOperation();
        return 0;

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE && m_isDragging) {
            CancelDragOperation();
            return 0;
        }
        break;

    case WM_NOTIFY: {
        auto* pNmhdr = reinterpret_cast<NMHDR*>(lParam);
        if (pNmhdr && pNmhdr->hwndFrom == m_toolbar.GetHwnd() && pNmhdr->code == NM_CUSTOMDRAW) {
            auto* draw = reinterpret_cast<NMTBCUSTOMDRAW*>(lParam);
            if (draw->nmcd.dwDrawStage == CDDS_PREERASE) {
                FillRect(draw->nmcd.hdc, &draw->nmcd.rc,
                    static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
                return CDRF_SKIPDEFAULT;
            }
            return CDRF_DODEFAULT;
        }
        if (pNmhdr) {
            if (pNmhdr->code == TTN_GETDISPINFOW || pNmhdr->code == TTN_NEEDTEXTW) {
                auto* pDispInfo = reinterpret_cast<NMTTDISPINFOW*>(lParam);
                const wchar_t* tipText = nullptr;
                switch (pDispInfo->hdr.idFrom) {
                case ID_FORMAT_BOLD:        tipText = L"加粗 (Ctrl+B)"; break;
                case ID_FORMAT_ITALIC:      tipText = L"斜体 (Ctrl+I)"; break;
                case ID_FORMAT_UNDERLINE:   tipText = L"下划线 (Ctrl+U)"; break;
                case ID_FORMAT_CODE_BLOCK:  tipText = L"代码块 (Ctrl+K)"; break;
                case ID_FORMAT_BULLET_LIST: tipText = L"项目符号列表"; break;
                case ID_FORMAT_NUMBER_LIST: tipText = L"编号列表"; break;
                case ID_INSERT_IMAGE:       tipText = L"插入图片"; break;
                case ID_INSERT_TABLE:       tipText = L"插入表格 (Ctrl+Shift+T)"; break;
                case ID_FILE_SAVE:          tipText = L"保存笔记 (Ctrl+S)"; break;
                case ID_VIEW_OUTLINE:       tipText = L"大纲目录 (Ctrl+Shift+O)"; break;
                default: break;
                }
                if (tipText) {
                    wcsncpy_s(pDispInfo->szText, tipText, _TRUNCATE);
                    pDispInfo->lpszText = pDispInfo->szText;
                    pDispInfo->hinst = nullptr;
                    return 0;
                }
            }
        }

        if (pNmhdr && pNmhdr->idFrom == IDC_MAIN_TREEVIEW) {
            if (pNmhdr->code == NM_CUSTOMDRAW) {
                auto* pTvCd = reinterpret_cast<NMTVCUSTOMDRAW*>(lParam);
                switch (pTvCd->nmcd.dwDrawStage) {
                case CDDS_PREPAINT:
                    return CDRF_NOTIFYITEMDRAW;
                case CDDS_ITEMPREPAINT: {
                    HTREEITEM hItem = reinterpret_cast<HTREEITEM>(pTvCd->nmcd.dwItemSpec);
                    bool isSelected = (pTvCd->nmcd.uItemState & CDIS_SELECTED) || (m_treeView.GetSelectedItem() == hItem);
                    if (isSelected) {
                        pTvCd->clrText = RGB(0, 102, 204);     // Fluent 现代选中深蓝
                        pTvCd->clrTextBk = RGB(225, 238, 255); // 现代浅蓝柔和高亮底色
                    } else {
                        pTvCd->clrText = RGB(33, 37, 41);
                        pTvCd->clrTextBk = RGB(247, 248, 250); // 侧边栏微灰底色
                    }
                    return CDRF_NEWFONT;
                }
                default:
                    return CDRF_DODEFAULT;
                }
            } else if (pNmhdr->code == TVN_SELCHANGEDW) {
                OnTreeSelectionChanged(reinterpret_cast<NMTREEVIEWW*>(lParam));
                return 0;
            } else if (pNmhdr->code == TVN_BEGINDRAGW) {
                if (m_isNodeSearchVisible && !m_nodeSearchBar.GetKeyword().empty()) {
                    return 0; // 过滤搜索状态下禁止拖拽重排
                }
                auto* pNmtv = reinterpret_cast<NMTREEVIEWW*>(lParam);
                if (pNmtv && pNmtv->itemNew.hItem) {
                    m_hDragItem = pNmtv->itemNew.hItem;
                    m_hDropTarget = nullptr;
                    m_dropPosition = storage::DropPosition::None;
                    m_isDragging = true;

                    SetCapture(m_hWnd);

                    m_hDragImageList = TreeView_CreateDragImage(m_treeView.GetHwnd(), m_hDragItem);
                    if (m_hDragImageList) {
                        POINT ptScreen;
                        GetCursorPos(&ptScreen);
                        POINT ptMain = ptScreen;
                        ScreenToClient(m_hWnd, &ptMain);
                        ImageList_BeginDrag(m_hDragImageList, 0, 8, 8);
                        ImageList_DragEnter(m_hWnd, ptMain.x, ptMain.y);
                    }
                }
                return 0;
            } else if (pNmhdr->code == TVN_KEYDOWN) {
                auto* pTvKey = reinterpret_cast<NMTVKEYDOWN*>(lParam);
                if (pTvKey->wVKey == VK_ESCAPE) {
                    if (m_isDragging) {
                        // 拖拽期间焦点仍在 TreeView，Escape 通过 TVN_KEYDOWN 到达父窗口。
                        CancelDragOperation();
                        return 1;
                    }
                    if (m_isNodeSearchVisible) {
                        ShowNodeSearch(false);
                        return 1;
                    }
                }
                if (pTvKey->wVKey == VK_DELETE) {
                    // 仅当焦点在左侧树控件且未在就地编辑文本时响应 Del 删除笔记
                    if (!TreeView_GetEditControl(m_treeView.GetHwnd())) {
                        SendMessageW(m_hWnd, WM_COMMAND, MAKEWPARAM(ID_FILE_DELETE_NOTE, 0), 0);
                        return 1;
                    }
                }
            } else if (pNmhdr->code == TVN_BEGINLABELEDITW) {
                auto* editInfo = reinterpret_cast<NMTVDISPINFOW*>(lParam);
                m_treeView.PrepareLabelEdit(editInfo->item.hItem);
                return FALSE;
            } else if (pNmhdr->code == TVN_ENDLABELEDITW) {
                auto* pDispInfo = reinterpret_cast<NMTVDISPINFOW*>(lParam);
                if (pDispInfo->item.pszText && wcslen(pDispInfo->item.pszText) > 0) {
                    int64_t nodeId = static_cast<int64_t>(m_treeView.GetItemData(pDispInfo->item.hItem));
                    if (m_repo) {
                        m_repo->UpdateNoteTitle(nodeId, pDispInfo->item.pszText);
                    }
                    m_treeView.SetItemText(pDispInfo->item.hItem, pDispInfo->item.pszText);
                    UpdateStatusBar(L"当前笔记: " + std::wstring(pDispInfo->item.pszText));
                    return TRUE;
                }
                return FALSE;
            }
        } else if (pNmhdr && pNmhdr->idFrom == IDC_MAIN_RICHEDIT) {
            if (pNmhdr->code == EN_SELCHANGE) {
                int level = m_richEditView.GetCurrentHeadingLevel();
                m_toolbar.SetSelectedHeadingIndex(level);
                SyncOutlineSelection();
                return 0;
            }
        }
        break;
    }

    case WM_CODEBLOCK_COPIED:
        UpdateStatusBar(L"已复制代码到剪贴板 ✔");
        return 0;

    case WM_CODEBLOCK_LANG_CHANGED: {
        m_lastCodeLanguage = static_cast<common::CodeLanguage>(wParam);
        SaveLastCodeLanguage(m_lastCodeLanguage);
        const auto& langs = common::GetSupportedLanguages();
        for (const auto& l : langs) {
            if (l.lang == m_lastCodeLanguage) {
                UpdateStatusBar(std::wstring(L"已切换代码语言为: ") + l.name);
                break;
            }
        }
        PostMessageW(m_hWnd, WM_EDITOR_FORMAT_CHANGED, 0, 0);
        return 0;
    }

    case WM_COMMAND: {
        if (LOWORD(wParam) == IDC_MAIN_RICHEDIT && HIWORD(wParam) == EN_KILLFOCUS) {
            if (m_isOutlineVisible) {
                UpdateOutline();
            }
            return 0;
        }

        WORD cmdId = LOWORD(wParam);
        if (cmdId == ID_VIEW_OUTLINE) {
            ToggleOutlinePane();
            return 0;
        }

        if (cmdId == ID_HELP_ALWAYS_ON_TOP) {
            ToggleAlwaysOnTop();
            return 0;
        }

        if (cmdId >= ID_VAULT_SWITCH_BASE && cmdId <= ID_VAULT_SWITCH_MAX) {
            size_t idx = static_cast<size_t>(cmdId - ID_VAULT_SWITCH_BASE);
            const auto* vault = m_vaultManager.GetVault(idx);
            if (vault) {
                SwitchToVault(vault->path);
            }
            return 0;
        }

        if (cmdId >= ID_CODE_LANG_BASE && cmdId < ID_CODE_LANG_BASE + static_cast<WORD>(common::GetSupportedLanguages().size())) {
            size_t idx = static_cast<size_t>(cmdId - ID_CODE_LANG_BASE);
            common::CodeLanguage newLang = common::GetSupportedLanguages()[idx].lang;
            CodeBlockInfo info;
            if (m_richEditView.GetCodeBlockAtCursor(&info)) {
                m_richEditView.SwitchCodeBlockLanguage(info, newLang);
            }
            m_lastCodeLanguage = newLang;
            SaveLastCodeLanguage(m_lastCodeLanguage);
            UpdateStatusBar(std::wstring(L"已切换代码语言为: ") + common::GetSupportedLanguages()[idx].name);
            PostMessageW(m_hWnd, WM_EDITOR_FORMAT_CHANGED, 0, 0);
            return 0;
        }

        if (cmdId == ID_CODEBLOCK_COPY) {
            CodeBlockInfo info;
            if (m_richEditView.GetCodeBlockAtCursor(&info)) {
                m_richEditView.CopyCodeBlockText(info);
                UpdateStatusBar(L"已复制代码到剪贴板 ✔");
            }
            return 0;
        }

        switch (cmdId) {
        // 工具栏标题下拉框事件
        case IDC_TOOLBAR_HEADING_COMBO: {
            if (HIWORD(wParam) == CBN_SELCHANGE) {
                int idx = m_toolbar.GetSelectedHeadingIndex();
                m_richEditView.ApplyHeading(idx);
                SetFocus(m_richEditView.GetHwnd());
                return 0;
            }
            break;
        }

        // 标题与正文样式命令
        case ID_FORMAT_HEADING_0:
            m_richEditView.ApplyHeading(0);
            m_toolbar.SetSelectedHeadingIndex(0);
            return 0;
        case ID_FORMAT_HEADING_1:
            m_richEditView.ApplyHeading(1);
            m_toolbar.SetSelectedHeadingIndex(1);
            return 0;
        case ID_FORMAT_HEADING_2:
            m_richEditView.ApplyHeading(2);
            m_toolbar.SetSelectedHeadingIndex(2);
            return 0;
        case ID_FORMAT_HEADING_3:
            m_richEditView.ApplyHeading(3);
            m_toolbar.SetSelectedHeadingIndex(3);
            return 0;
        case ID_FORMAT_HEADING_4:
            m_richEditView.ApplyHeading(4);
            m_toolbar.SetSelectedHeadingIndex(4);
            return 0;

        // 富文本格式化命令
        case ID_FORMAT_BOLD:
            m_richEditView.ToggleBold();
            return 0;
        case ID_FORMAT_ITALIC:
            m_richEditView.ToggleItalic();
            return 0;
        case ID_FORMAT_UNDERLINE:
            m_richEditView.ToggleUnderline();
            return 0;
        case ID_FORMAT_STRIKE:
            m_richEditView.ToggleStrike();
            return 0;
        case ID_FORMAT_CODE_BLOCK:
            OnInsertCodeBlock();
            return 0;
        case ID_FORMAT_BULLET_LIST:
            m_richEditView.InsertBulletList();
            return 0;
        case ID_FORMAT_NUMBER_LIST:
            m_richEditView.InsertNumberedList();
            return 0;

        // 编辑命令
        case ID_EDIT_UNDO:
            m_richEditView.Undo();
            return 0;
        case ID_EDIT_REDO:
            m_richEditView.Redo();
            return 0;
        case ID_EDIT_CUT:
            m_richEditView.Cut();
            return 0;
        case ID_EDIT_COPY:
            m_richEditView.Copy();
            return 0;
        case ID_EDIT_PASTE:
            m_richEditView.Paste();
            return 0;
        case ID_EDIT_SELECTALL:
            m_richEditView.SelectAll();
            return 0;

        // 搜索、替换、标记相关命令
        case ID_TREE_SEARCH:
            ShowNodeSearch(true);
            return 0;

        case ID_EDIT_FIND:
            if (GetFocus() == m_treeView.GetHwnd() || (m_isNodeSearchVisible && GetFocus() == m_nodeSearchBar.GetHwnd())) {
                ShowNodeSearch(true);
                return 0;
            }
            OpenFindReplaceDialog(FindTabMode::Find);
            return 0;
        case ID_EDIT_REPLACE:
            OpenFindReplaceDialog(FindTabMode::Replace);
            return 0;
        case ID_EDIT_MARK:
            OpenFindReplaceDialog(FindTabMode::Mark);
            return 0;
        case ID_EDIT_FIND_NEXT:
            m_findReplaceDialog.FindNext(true);
            return 0;
        case ID_EDIT_FIND_PREV:
            m_findReplaceDialog.FindNext(false);
            return 0;
        case ID_SEARCH_GLOBAL:
            if (!m_isSearchPaneVisible) {
                ToggleSearchPane();
            } else {
                m_searchPane.FocusSearchBox();
            }
            return 0;

        // 插入当前时间戳
        case ID_INSERT_DATETIME: {
            auto now = std::chrono::system_clock::now();
            auto in_time_t = std::chrono::system_clock::to_time_t(now);
            std::tm tmNow;
            localtime_s(&tmNow, &in_time_t);
            std::wstringstream wss;
            wss << std::put_time(&tmNow, L"[%Y-%m-%d %H:%M:%S] ");
            SendMessageW(m_richEditView.GetHwnd(), EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(wss.str().c_str()));
            return 0;
        }

        // 插入表格
        case ID_INSERT_TABLE:
            m_richEditView.InsertTable(2, 3);
            return 0;

        // 表格行列调整命令
        case ID_TABLE_INSERT_ROW_ABOVE:
            m_richEditView.InsertTableRow(false);
            return 0;
        case ID_TABLE_INSERT_ROW_BELOW:
            m_richEditView.InsertTableRow(true);
            return 0;
        case ID_TABLE_INSERT_COL_LEFT:
            m_richEditView.InsertTableColumn(false);
            return 0;
        case ID_TABLE_INSERT_COL_RIGHT:
            m_richEditView.InsertTableColumn(true);
            return 0;
        case ID_TABLE_DELETE_ROW:
            m_richEditView.DeleteTableRow();
            return 0;
        case ID_TABLE_DELETE_COL:
            m_richEditView.DeleteTableColumn();
            return 0;
        case ID_TABLE_DELETE_TABLE:
            m_richEditView.DeleteTable();
            return 0;

        // 单元格对齐 (当前/选区)
        case ID_TABLE_ALIGN_LEFT:
            m_richEditView.SetTableCellHorizontalAlignment(PFA_LEFT, TableAlignmentScope::Selection);
            return 0;
        case ID_TABLE_ALIGN_CENTER:
            m_richEditView.SetTableCellHorizontalAlignment(PFA_CENTER, TableAlignmentScope::Selection);
            return 0;
        case ID_TABLE_ALIGN_RIGHT:
            m_richEditView.SetTableCellHorizontalAlignment(PFA_RIGHT, TableAlignmentScope::Selection);
            return 0;
        case ID_TABLE_VALIGN_TOP:
            m_richEditView.SetTableCellVerticalAlignment(0, TableAlignmentScope::Selection);
            return 0;
        case ID_TABLE_VALIGN_CENTER:
            m_richEditView.SetTableCellVerticalAlignment(1, TableAlignmentScope::Selection);
            return 0;
        case ID_TABLE_VALIGN_BOTTOM:
            m_richEditView.SetTableCellVerticalAlignment(2, TableAlignmentScope::Selection);
            return 0;

        // 整列对齐
        case ID_TABLE_COL_ALIGN_LEFT:
            m_richEditView.SetTableCellHorizontalAlignment(PFA_LEFT, TableAlignmentScope::Column);
            return 0;
        case ID_TABLE_COL_ALIGN_CENTER:
            m_richEditView.SetTableCellHorizontalAlignment(PFA_CENTER, TableAlignmentScope::Column);
            return 0;
        case ID_TABLE_COL_ALIGN_RIGHT:
            m_richEditView.SetTableCellHorizontalAlignment(PFA_RIGHT, TableAlignmentScope::Column);
            return 0;
        case ID_TABLE_COL_VALIGN_TOP:
            m_richEditView.SetTableCellVerticalAlignment(0, TableAlignmentScope::Column);
            return 0;
        case ID_TABLE_COL_VALIGN_CENTER:
            m_richEditView.SetTableCellVerticalAlignment(1, TableAlignmentScope::Column);
            return 0;
        case ID_TABLE_COL_VALIGN_BOTTOM:
            m_richEditView.SetTableCellVerticalAlignment(2, TableAlignmentScope::Column);
            return 0;

        // 整行对齐
        case ID_TABLE_ROW_ALIGN_LEFT:
            m_richEditView.SetTableCellHorizontalAlignment(PFA_LEFT, TableAlignmentScope::Row);
            return 0;
        case ID_TABLE_ROW_ALIGN_CENTER:
            m_richEditView.SetTableCellHorizontalAlignment(PFA_CENTER, TableAlignmentScope::Row);
            return 0;
        case ID_TABLE_ROW_ALIGN_RIGHT:
            m_richEditView.SetTableCellHorizontalAlignment(PFA_RIGHT, TableAlignmentScope::Row);
            return 0;
        case ID_TABLE_ROW_VALIGN_TOP:
            m_richEditView.SetTableCellVerticalAlignment(0, TableAlignmentScope::Row);
            return 0;
        case ID_TABLE_ROW_VALIGN_CENTER:
            m_richEditView.SetTableCellVerticalAlignment(1, TableAlignmentScope::Row);
            return 0;
        case ID_TABLE_ROW_VALIGN_BOTTOM:
            m_richEditView.SetTableCellVerticalAlignment(2, TableAlignmentScope::Row);
            return 0;

        // 全表单元格对齐
        case ID_TABLE_ALL_ALIGN_LEFT:
            m_richEditView.SetTableCellHorizontalAlignment(PFA_LEFT, TableAlignmentScope::All);
            return 0;
        case ID_TABLE_ALL_ALIGN_CENTER:
            m_richEditView.SetTableCellHorizontalAlignment(PFA_CENTER, TableAlignmentScope::All);
            return 0;
        case ID_TABLE_ALL_ALIGN_RIGHT:
            m_richEditView.SetTableCellHorizontalAlignment(PFA_RIGHT, TableAlignmentScope::All);
            return 0;
        case ID_TABLE_ALL_VALIGN_TOP:
            m_richEditView.SetTableCellVerticalAlignment(0, TableAlignmentScope::All);
            return 0;
        case ID_TABLE_ALL_VALIGN_CENTER:
            m_richEditView.SetTableCellVerticalAlignment(1, TableAlignmentScope::All);
            return 0;
        case ID_TABLE_ALL_VALIGN_BOTTOM:
            m_richEditView.SetTableCellVerticalAlignment(2, TableAlignmentScope::All);
            return 0;

        // 插入图片
        case ID_INSERT_IMAGE:
            OnInsertImageFromFile();
            return 0;

        // 树节点增删
        case ID_FILE_NEW_NOTE: {
            if (!SaveActiveNote()) return 0;
            HTREEITEM hCur = m_treeView.GetSelectedItem();
            HTREEITEM hParent = hCur ? TreeView_GetParent(m_treeView.GetHwnd(), hCur) : nullptr;
            int64_t parentId = hParent ? static_cast<int64_t>(m_treeView.GetItemData(hParent)) : 0;
            int64_t newId = m_repo ? m_repo->CreateNote(parentId, L"新建笔记", -1, 0, "") : 0;
            if (newId <= 0) {
                std::wstring error = m_db ? anynote::utils::Utf8ToWide(m_db->GetLastError()) : L"数据库未打开";
                MessageBoxW(m_hWnd, (L"创建笔记失败：\r\n" + error).c_str(), L"数据库错误", MB_OK | MB_ICONERROR);
                return 0;
            }

            HTREEITEM hNew = m_treeView.InsertNode(hParent, L"新建笔记", static_cast<LPARAM>(newId), true);
            if (hNew) {
                m_treeView.SelectItem(hNew);
                SetFocus(m_treeView.GetHwnd());
                TreeView_EditLabel(m_treeView.GetHwnd(), hNew);
            }
            return 0;
        }
        case ID_FILE_NEW_SUB_NOTE: {
            if (!SaveActiveNote()) return 0;
            HTREEITEM hCur = m_treeView.GetSelectedItem();
            int64_t parentId = hCur ? static_cast<int64_t>(m_treeView.GetItemData(hCur)) : 0;
            int64_t newId = m_repo ? m_repo->CreateNote(parentId, L"新建子笔记", -1, 0, "") : 0;
            if (newId <= 0) {
                std::wstring error = m_db ? anynote::utils::Utf8ToWide(m_db->GetLastError()) : L"数据库未打开";
                MessageBoxW(m_hWnd, (L"创建子笔记失败：\r\n" + error).c_str(), L"数据库错误", MB_OK | MB_ICONERROR);
                return 0;
            }

            HTREEITEM hNew = m_treeView.InsertNode(hCur, L"新建子笔记", static_cast<LPARAM>(newId), true);
            if (hNew) {
                m_treeView.SelectItem(hNew);
                SetFocus(m_treeView.GetHwnd());
                TreeView_EditLabel(m_treeView.GetHwnd(), hNew);
            }
            return 0;
        }
        case ID_FILE_RENAME_NOTE: {
            HTREEITEM hCur = m_treeView.GetSelectedItem();
            if (hCur) {
                SetFocus(m_treeView.GetHwnd());
                TreeView_EditLabel(m_treeView.GetHwnd(), hCur);
            }
            return 0;
        }
        case ID_FILE_DELETE_NOTE: {
            HTREEITEM hCur = m_treeView.GetSelectedItem();
            if (hCur && MessageBoxW(m_hWnd, L"确定要删除当前选中的笔记及其所有子笔记吗？此操作无法撤销。", L"确认删除", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                int64_t nodeId = static_cast<int64_t>(m_treeView.GetItemData(hCur));
                if (!m_repo || !m_repo->DeleteNote(nodeId)) {
                    std::wstring error = m_db ? anynote::utils::Utf8ToWide(m_db->GetLastError()) : L"数据库未打开";
                    MessageBoxW(m_hWnd, (L"删除笔记失败：\r\n" + error).c_str(), L"数据库错误", MB_OK | MB_ICONERROR);
                    return 0;
                }
                if (m_activeNoteId == nodeId) {
                    m_activeNoteId = 0;
                    m_richEditView.SetText(L"");
                    m_outlinePane.ClearOutline();
                }
                m_treeView.DeleteItem(hCur);
            }
            return 0;
        }

        case ID_TREE_EXPAND_ALL:
            m_treeView.ExpandAll(true);
            return 0;

        case ID_TREE_COLLAPSE_ALL:
            m_treeView.ExpandAll(false);
            return 0;

        case ID_FILE_SAVE:
            if (SaveActiveNote()) {
                UpdateStatusBar(L"笔记本已保存至数据库");
                if (m_hStatusBar) {
                    SendMessageW(m_hStatusBar, SB_SETTEXTW, 1, reinterpret_cast<LPARAM>(L"状态: 已保存"));
                }
            }
            return 0;

        case ID_FILE_NEW_NOTEBOOK: {
            OnNewVault();
            return 0;
        }

        case ID_FILE_OPEN_NOTEBOOK: {
            OnOpenExternalVault();
            return 0;
        }

        case ID_FILE_MANAGE_VAULTS: {
            OnManageVaults();
            return 0;
        }

        case IDC_MAIN_VAULT_BAR: {
            ShowVaultMenu();
            return 0;
        }

        case ID_SECURITY_ENCRYPT: {
            if (!m_db || !m_db->IsOpen()) {
                MessageBoxW(m_hWnd, L"尚未打开任何笔记本！", L"提示", MB_OK | MB_ICONWARNING);
                return 0;
            }
            SetPasswordContext ctx;
            INT_PTR res = DialogBoxParamW(
                GetModuleHandleW(nullptr),
                MAKEINTRESOURCEW(IDD_SET_PASSWORD),
                m_hWnd,
                SetPasswordDialogProc,
                reinterpret_cast<LPARAM>(&ctx)
            );
            if (res == IDOK && ctx.ok) {
                if (m_db->SetPassword(ctx.newPassword)) {
                    UpdateEncryptionStatusUI();
                    if (ctx.removePassword) {
                        MessageBoxW(m_hWnd, L"已成功解除密码，当前笔记本为未加密明文存储。", L"成功", MB_OK | MB_ICONINFORMATION);
                    } else {
                        MessageBoxW(m_hWnd, L"密码设置成功！全库已采用 AES-256 高强度加密。\r\n下次打开该文件时需要输入此密码。", L"成功", MB_OK | MB_ICONINFORMATION);
                    }
                } else {
                    std::wstring err = anynote::utils::Utf8ToWide(m_db->GetLastError());
                    MessageBoxW(m_hWnd, (L"修改密码失败：\r\n" + err).c_str(), L"错误", MB_OK | MB_ICONERROR);
                }
            }
            return 0;
        }

        case ID_HELP_ABOUT:
            MessageBoxW(
                m_hWnd,
                L"AnyNote v1.0.0 (Preview)\r\n\r\n基于 C++20 与原生 Win32 API 打造的高性能树状富文本笔记软件。\r\n• 编辑内核: Windows RichEdit 5.0 (MSFTEDIT.DLL)\r\n• 数据存储: SQLite3MC (AES-256 全库透明加密)\r\n• 特色特性: 代码框卡片、标题阶梯层级、便携零第三方依赖",
                L"关于 AnyNote",
                MB_OK | MB_ICONINFORMATION
            );
            return 0;

        case ID_FILE_EXIT:
            SendMessageW(m_hWnd, WM_CLOSE, 0, 0);
            return 0;
        }
        break;
    }

    case WM_CLOSE:
        if (!SaveActiveNote()) return 0;
        DestroyWindow(m_hWnd);
        return 0;

    case WM_DESTROY:
        SaveActiveNote();
        m_vaultManager.SetConfigString(L"View", L"OutlineVisible", m_isOutlineVisible ? L"1" : L"0");
        m_vaultManager.SetConfigString(L"View", L"OutlineWidth", std::to_wstring(m_outlineWidth));
        m_vaultManager.SetConfigString(L"Window", L"AlwaysOnTop", m_isAlwaysOnTop ? L"1" : L"0");
        if (m_db) {
            m_db->Close();
        }
        PostQuitMessage(0);
        return 0;
    }

    return common::Window::HandleMessage(uMsg, wParam, lParam);
}

bool MainWindow::SelectNodeById(int64_t nodeId) {
    if (!m_treeView.GetHwnd() || nodeId <= 0) return false;

    std::function<HTREEITEM(HTREEITEM)> findItem = [&](HTREEITEM hItem) -> HTREEITEM {
        while (hItem) {
            if (static_cast<int64_t>(m_treeView.GetItemData(hItem)) == nodeId) {
                return hItem;
            }
            HTREEITEM hChild = TreeView_GetChild(m_treeView.GetHwnd(), hItem);
            if (hChild) {
                HTREEITEM found = findItem(hChild);
                if (found) return found;
            }
            hItem = TreeView_GetNextSibling(m_treeView.GetHwnd(), hItem);
        }
        return nullptr;
    };

    HTREEITEM hRoot = TreeView_GetRoot(m_treeView.GetHwnd());
    HTREEITEM targetItem = findItem(hRoot);
    if (targetItem) {
        m_treeView.SelectItem(targetItem);
        m_treeView.EnsureVisible(targetItem);
        return true;
    }
    return false;
}

void MainWindow::OpenFindReplaceDialog(FindTabMode mode) {
    m_findReplaceDialog.ShowTab(mode);
}

void MainWindow::ToggleSearchPane() {
    m_isSearchPaneVisible = !m_isSearchPaneVisible;
    RECT rc;
    GetClientRect(m_hWnd, &rc);
    LayoutChildren(rc.right - rc.left, rc.bottom - rc.top);
    m_searchPane.ShowPane(m_isSearchPaneVisible);
    if (!m_isSearchPaneVisible) {
        SetFocus(m_richEditView.GetHwnd());
    }
}

void MainWindow::OnSearchResultSelected(const storage::SearchResult& result) {
    if (!SelectNodeById(result.nodeId)) {
        // A global result may be hidden by the node filter. Preserve the query
        // for visible results; clear it only when restoring the tree is needed.
        if (!m_isNodeSearchVisible || TrimWhitespace(m_nodeSearchBar.GetKeyword()).empty()) return;
        if (!SaveActiveNote()) return;
        ShowNodeSearch(false);
        if (!SelectNodeById(result.nodeId)) return;
    }
    // Selection can be rejected when saving the previous note fails. Never
    // apply a search highlight to that previous note.
    if (m_activeNoteId != result.nodeId) return;

    if (!result.matchInTitle) {
        std::wstring kw = m_searchPane.GetSearchKeyword();
        if (!kw.empty()) {
            CHARRANGE cr = {0, 0};
            SendMessageW(m_richEditView.GetHwnd(), EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&cr));
            m_richEditView.FindAndSelect(kw, true, m_searchPane.IsMatchCase(), false);
        } else if (result.matchOffsetInText >= 0) {
            m_richEditView.SelectRange(result.matchOffsetInText, result.matchOffsetInText + 1);
        }
    }
    SetFocus(m_richEditView.GetHwnd());
}

void MainWindow::ShowTreeContextMenu(int xScreen, int yScreen) {
    if (!m_treeView.GetHwnd()) return;

    POINT ptScreen = { xScreen, yScreen };
    HTREEITEM hHit = nullptr;

    if (xScreen == -1 && yScreen == -1) {
        hHit = m_treeView.GetSelectedItem();
        if (hHit) {
            RECT rcItem = m_treeView.GetItemRect(hHit, true);
            ptScreen.x = rcItem.left;
            ptScreen.y = rcItem.bottom;
            ClientToScreen(m_treeView.GetHwnd(), &ptScreen);
        } else {
            RECT rcTree;
            GetClientRect(m_treeView.GetHwnd(), &rcTree);
            ptScreen.x = rcTree.left + 20;
            ptScreen.y = rcTree.top + 20;
            ClientToScreen(m_treeView.GetHwnd(), &ptScreen);
        }
    } else {
        POINT ptClient = ptScreen;
        ScreenToClient(m_treeView.GetHwnd(), &ptClient);
        UINT flags = 0;
        hHit = m_treeView.HitTest(ptClient, &flags);
        if (hHit) {
            m_treeView.SelectItem(hHit);
        }
    }

    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return;

    if (hHit) {
        AppendMenuW(hMenu, MF_STRING, ID_FILE_NEW_NOTE, L"新建笔记(&N)\tCtrl+N");
        AppendMenuW(hMenu, MF_STRING, ID_FILE_NEW_SUB_NOTE, L"新建子笔记(&S)\tCtrl+Shift+N");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hMenu, MF_STRING, ID_TREE_SEARCH, L"搜索节点(&F)...\tCtrl+P");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hMenu, MF_STRING, ID_FILE_RENAME_NOTE, L"重命名(&R)\tF2");
        AppendMenuW(hMenu, MF_STRING, ID_FILE_DELETE_NOTE, L"删除(&D)\tDel");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hMenu, MF_STRING, ID_TREE_EXPAND_ALL, L"全部展开(&E)");
        AppendMenuW(hMenu, MF_STRING, ID_TREE_COLLAPSE_ALL, L"全部折叠(&C)");
    } else {
        AppendMenuW(hMenu, MF_STRING, ID_FILE_NEW_NOTE, L"新建根笔记(&N)\tCtrl+N");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hMenu, MF_STRING, ID_TREE_SEARCH, L"搜索节点(&F)...\tCtrl+P");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hMenu, MF_STRING, ID_TREE_EXPAND_ALL, L"全部展开(&E)");
        AppendMenuW(hMenu, MF_STRING, ID_TREE_COLLAPSE_ALL, L"全部折叠(&C)");
    }

    SetForegroundWindow(m_hWnd);
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON | TPM_LEFTALIGN, ptScreen.x, ptScreen.y, 0, m_hWnd, nullptr);
    DestroyMenu(hMenu);
}

void MainWindow::ShowEditorContextMenu(int xScreen, int yScreen) {
    if (!m_richEditView.GetHwnd()) return;

    POINT ptScreen = { xScreen, yScreen };
    if (xScreen == -1 && yScreen == -1) {
        CHARRANGE sel = {};
        SendMessageW(m_richEditView.GetHwnd(), EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&sel));
        POINTL ptl = {};
        SendMessageW(m_richEditView.GetHwnd(), EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&ptl), sel.cpMin);
        ptScreen.x = ptl.x;
        ptScreen.y = ptl.y + 20;
        ClientToScreen(m_richEditView.GetHwnd(), &ptScreen);
    } else {
        POINT ptClient = ptScreen;
        ScreenToClient(m_richEditView.GetHwnd(), &ptClient);
        POINTL ptl = { ptClient.x, ptClient.y };
        LRESULT charIdx = SendMessageW(m_richEditView.GetHwnd(), EM_CHARFROMPOS, 0, reinterpret_cast<LPARAM>(&ptl));
        if (charIdx >= 0) {
            CHARRANGE curSel = {};
            SendMessageW(m_richEditView.GetHwnd(), EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&curSel));
            const bool keepTableSelection = m_richEditView.HasTableSelection() &&
                m_richEditView.IsPointInTableSelection(ptClient);
            if (!keepTableSelection && (curSel.cpMin == curSel.cpMax || charIdx < curSel.cpMin || charIdx > curSel.cpMax)) {
                CHARRANGE clickSel = { static_cast<LONG>(charIdx), static_cast<LONG>(charIdx) };
                SendMessageW(m_richEditView.GetHwnd(), EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&clickSel));
            }
        }
    }

    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return;

    bool canUndo = SendMessageW(m_richEditView.GetHwnd(), EM_CANUNDO, 0, 0) != 0;
    bool canRedo = SendMessageW(m_richEditView.GetHwnd(), EM_CANREDO, 0, 0) != 0;

    CHARRANGE sel = {};
    SendMessageW(m_richEditView.GetHwnd(), EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&sel));
    bool hasSel = (sel.cpMax > sel.cpMin);

    bool canPaste = (IsClipboardFormatAvailable(CF_UNICODETEXT) ||
                     IsClipboardFormatAvailable(CF_TEXT) ||
                     IsClipboardFormatAvailable(CF_BITMAP) ||
                     IsClipboardFormatAvailable(CF_DIB));

    // 1. 撤销/重做
    AppendMenuW(hMenu, MF_STRING | (canUndo ? MF_ENABLED : MF_GRAYED), ID_EDIT_UNDO, L"撤销(&U)\tCtrl+Z");
    AppendMenuW(hMenu, MF_STRING | (canRedo ? MF_ENABLED : MF_GRAYED), ID_EDIT_REDO, L"恢复(&R)\tCtrl+Y");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    // 2. 剪切/复制/粘贴/全选
    AppendMenuW(hMenu, MF_STRING | (hasSel ? MF_ENABLED : MF_GRAYED), ID_EDIT_CUT, L"剪切(&T)\tCtrl+X");
    AppendMenuW(hMenu, MF_STRING | (hasSel ? MF_ENABLED : MF_GRAYED), ID_EDIT_COPY, L"复制(&C)\tCtrl+C");
    AppendMenuW(hMenu, MF_STRING | (canPaste ? MF_ENABLED : MF_GRAYED), ID_EDIT_PASTE, L"粘贴(&P)\tCtrl+V");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, ID_EDIT_SELECTALL, L"全选(&A)\tCtrl+A");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    // 3. 常用格式快速排版子菜单
    HMENU hSubFormat = CreatePopupMenu();
    AppendMenuW(hSubFormat, MF_STRING, ID_FORMAT_BOLD, L"粗体(&B)\tCtrl+B");
    AppendMenuW(hSubFormat, MF_STRING, ID_FORMAT_ITALIC, L"斜体(&I)\tCtrl+I");
    AppendMenuW(hSubFormat, MF_STRING, ID_FORMAT_UNDERLINE, L"下划线(&U)\tCtrl+U");
    AppendMenuW(hSubFormat, MF_STRING, ID_FORMAT_STRIKE, L"删除线(&S)");
    AppendMenuW(hSubFormat, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hSubFormat, MF_STRING, ID_FORMAT_HEADING_0, L"正文(&0)\tCtrl+0");
    AppendMenuW(hSubFormat, MF_STRING, ID_FORMAT_HEADING_1, L"标题 1(&1)\tCtrl+1");
    AppendMenuW(hSubFormat, MF_STRING, ID_FORMAT_HEADING_2, L"标题 2(&2)\tCtrl+2");
    AppendMenuW(hSubFormat, MF_STRING, ID_FORMAT_HEADING_3, L"标题 3(&3)\tCtrl+3");
    AppendMenuW(hSubFormat, MF_STRING, ID_FORMAT_HEADING_4, L"标题 4(&4)\tCtrl+4");
    AppendMenuW(hMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(hSubFormat), L"格式(&O)");

    // 4. 代码块或表格操作
    CodeBlockInfo cbInfo;
    bool inCodeBlock = m_richEditView.GetCodeBlockAtCursor(&cbInfo);
    if (inCodeBlock) {
        AppendMenuW(hMenu, MF_STRING, ID_CODEBLOCK_COPY, L"📋 复制代码块\tCtrl+C");
        HMENU hLangSub = CreatePopupMenu();
        const auto& languages = common::GetSupportedLanguages();
        for (size_t i = 0; i < languages.size(); ++i) {
            UINT flags = MF_STRING;
            if (languages[i].lang == cbInfo.currentLang) {
                flags |= MF_CHECKED;
            }
            AppendMenuW(hLangSub, flags, ID_CODE_LANG_BASE + i, languages[i].name);
        }
        AppendMenuW(hMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(hLangSub), L"⚙ 切换代码语言");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    } else {
        bool inTable = m_richEditView.IsCursorInTable();
        if (inTable) {
            HMENU hSubTable = CreatePopupMenu();
            AppendMenuW(hSubTable, MF_STRING, ID_TABLE_INSERT_ROW_ABOVE, L"在上方插入行(&A)");
            AppendMenuW(hSubTable, MF_STRING, ID_TABLE_INSERT_ROW_BELOW, L"在下方插入行(&B)");
            AppendMenuW(hSubTable, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(hSubTable, MF_STRING, ID_TABLE_INSERT_COL_LEFT, L"在左侧插入列(&L)");
            AppendMenuW(hSubTable, MF_STRING, ID_TABLE_INSERT_COL_RIGHT, L"在右侧插入列(&R)");
            AppendMenuW(hSubTable, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(hSubTable, MF_STRING, ID_TABLE_DELETE_ROW, L"删除当前行(&R)");
            AppendMenuW(hSubTable, MF_STRING, ID_TABLE_DELETE_COL, L"删除当前列(&C)");
            AppendMenuW(hSubTable, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(hSubTable, MF_STRING, ID_TABLE_DELETE_TABLE, L"删除表格(&T)");
            AppendMenuW(hSubTable, MF_SEPARATOR, 0, nullptr);

            // 统一的表格对齐菜单：拖选矩形或 Ctrl 多选后只对选中单元格生效。
            int curHorz = PFA_LEFT;
            int curVert = 0;
            m_richEditView.GetTableCellAlignment(&curHorz, &curVert);

            HMENU hSubCellAlign = CreatePopupMenu();
            UINT flagHLeft   = MF_STRING | (curHorz == PFA_LEFT   ? MF_CHECKED : MF_UNCHECKED);
            UINT flagHCenter = MF_STRING | (curHorz == PFA_CENTER ? MF_CHECKED : MF_UNCHECKED);
            UINT flagHRight  = MF_STRING | (curHorz == PFA_RIGHT  ? MF_CHECKED : MF_UNCHECKED);
            AppendMenuW(hSubCellAlign, flagHLeft,   ID_TABLE_ALIGN_LEFT,   L"水平左对齐(&L)");
            AppendMenuW(hSubCellAlign, flagHCenter, ID_TABLE_ALIGN_CENTER, L"水平居中(&C)");
            AppendMenuW(hSubCellAlign, flagHRight,  ID_TABLE_ALIGN_RIGHT,  L"水平右对齐(&R)");
            AppendMenuW(hSubCellAlign, MF_SEPARATOR, 0, nullptr);
            UINT flagVTop    = MF_STRING | (curVert == 0 ? MF_CHECKED : MF_UNCHECKED);
            UINT flagVMiddle = MF_STRING | (curVert == 1 ? MF_CHECKED : MF_UNCHECKED);
            UINT flagVBottom = MF_STRING | (curVert == 2 ? MF_CHECKED : MF_UNCHECKED);
            AppendMenuW(hSubCellAlign, flagVTop,    ID_TABLE_VALIGN_TOP,    L"顶端对齐(&T)");
            AppendMenuW(hSubCellAlign, flagVMiddle, ID_TABLE_VALIGN_CENTER, L"垂直居中(&M)");
            AppendMenuW(hSubCellAlign, flagVBottom, ID_TABLE_VALIGN_BOTTOM, L"底端对齐(&B)");
            AppendMenuW(hSubTable, MF_POPUP, reinterpret_cast<UINT_PTR>(hSubCellAlign), L"对齐");


            AppendMenuW(hMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(hSubTable), L"表格(&B)");
        } else {
            AppendMenuW(hMenu, MF_STRING, ID_INSERT_TABLE, L"插入表格(3×2)(&T)\tCtrl+Shift+T");
        }

        AppendMenuW(hMenu, MF_STRING, ID_FORMAT_CODE_BLOCK, L"插入代码块(&K)\tCtrl+K");
    }

    AppendMenuW(hMenu, MF_STRING, ID_INSERT_IMAGE, L"插入图片(&P)...");
    AppendMenuW(hMenu, MF_STRING, ID_INSERT_DATETIME, L"插入当前时间戳(&D)");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, ID_EDIT_FIND, L"在当前笔记中查找(&F)...\tCtrl+F");
    AppendMenuW(hMenu, MF_STRING, ID_EDIT_REPLACE, L"替换(&R)...\tCtrl+H");

    SetForegroundWindow(m_hWnd);
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON | TPM_LEFTALIGN, ptScreen.x, ptScreen.y, 0, m_hWnd, nullptr);
    DestroyMenu(hMenu);
}

void MainWindow::UpdateVaultBarUI() {
    if (m_hVaultBar) {
        InvalidateRect(m_hVaultBar, nullptr, TRUE);
    }
}

void MainWindow::ShowVaultMenu() {
    if (!m_hVaultBar) return;
    RECT rc;
    GetWindowRect(m_hVaultBar, &rc);

    HMENU hMenu = CreatePopupMenu();
    const auto& vaults = m_vaultManager.GetVaults();
    std::wstring activePath = m_vaultManager.GetActiveVaultPath();

    for (size_t i = 0; i < vaults.size() && (ID_VAULT_SWITCH_BASE + i <= ID_VAULT_SWITCH_MAX); ++i) {
        UINT flags = MF_STRING;
        if (_wcsicmp(vaults[i].path.c_str(), activePath.c_str()) == 0) {
            flags |= MF_CHECKED;
        }
        std::wstring itemText = vaults[i].name;
        AppendMenuW(hMenu, flags, ID_VAULT_SWITCH_BASE + i, itemText.c_str());
    }

    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, ID_FILE_NEW_NOTEBOOK, L"➕ 新建笔记本库(&N)...");
    AppendMenuW(hMenu, MF_STRING, ID_FILE_OPEN_NOTEBOOK, L"📂 打开外部笔记本库(&O)...");
    AppendMenuW(hMenu, MF_STRING, ID_FILE_MANAGE_VAULTS, L"⚙ 管理笔记本库(&M)...");

    TrackPopupMenuEx(hMenu, TPM_LEFTALIGN | TPM_TOPALIGN, rc.left, rc.bottom, m_hWnd, nullptr);
    DestroyMenu(hMenu);
}

void MainWindow::OnNewVault() {
    NewVaultContext ctx;
    ctx.mgr = &m_vaultManager;
    INT_PTR res = DialogBoxParamW(
        GetModuleHandleW(nullptr),
        MAKEINTRESOURCEW(IDD_NEW_VAULT),
        m_hWnd,
        NewVaultDialogProc,
        reinterpret_cast<LPARAM>(&ctx)
    );

    if (res == IDOK && ctx.ok) {
        std::error_code ec;
        std::filesystem::create_directories(m_vaultManager.GetNotebooksDir(), ec);
        m_vaultManager.AddVault(ctx.name, ctx.path);
        SwitchToVault(ctx.path, true);
    }
}

void MainWindow::OnOpenExternalVault() {
    wchar_t szFile[MAX_PATH] = {0};
    OPENFILENAMEW ofn = { sizeof(OPENFILENAMEW) };
    ofn.hwndOwner = m_hWnd;
    ofn.lpstrFilter = L"AnyNote 笔记本 (*.anynote)\0*.anynote\0所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

    if (GetOpenFileNameW(&ofn)) {
        std::wstring path = szFile;
        std::wstring name = std::filesystem::path(path).stem().wstring();
        m_vaultManager.AddVault(name, path);
        SwitchToVault(path);
    }
}

void MainWindow::OnManageVaults() {
    ManageVaultsContext ctx;
    ctx.mgr = &m_vaultManager;
    ctx.mainWindow = this;
    INT_PTR res = DialogBoxParamW(
        GetModuleHandleW(nullptr),
        MAKEINTRESOURCEW(IDD_MANAGE_VAULTS),
        m_hWnd,
        ManageVaultsDialogProc,
        reinterpret_cast<LPARAM>(&ctx)
    );

    if (ctx.switchRequested && !ctx.selectedSwitchPath.empty()) {
        SwitchToVault(ctx.selectedSwitchPath);
    } else {
        UpdateVaultBarUI();
    }
}

bool MainWindow::SwitchToVault(const std::wstring& path, bool createIfMissing) {
    if (_wcsicmp(path.c_str(), m_currentNotebookPath.c_str()) == 0) {
        m_vaultManager.SetActiveVaultPath(path);
        UpdateVaultBarUI();
        return true;
    }

    if (!createIfMissing && !std::filesystem::exists(path)) {
        std::wstring msg = L"未找到笔记库文件：\r\n" + path + L"\r\n\r\n该文件可能已被移动或删除。是否从库列表中移除此项？";
        if (MessageBoxW(m_hWnd, msg.c_str(), L"库文件未找到", MB_YESNO | MB_ICONWARNING) == IDYES) {
            int idx = m_vaultManager.FindVaultByPath(path);
            if (idx >= 0) {
                m_vaultManager.RemoveVault(static_cast<size_t>(idx));
            }
            UpdateVaultBarUI();
        }
        return false;
    }

    std::wstring previousActivePath = m_vaultManager.GetActiveVaultPath();
    m_vaultManager.SetActiveVaultPath(path);
    if (OpenNotebook(path, "")) {
        UpdateVaultBarUI();
        return true;
    }
    m_vaultManager.SetActiveVaultPath(previousActivePath);
    return false;
}

void MainWindow::ShowNodeSearch(bool show) {
    if (m_isNodeSearchVisible == show) {
        if (show) {
            m_nodeSearchBar.FocusSearchBox();
        }
        return;
    }

    m_isNodeSearchVisible = show;
    m_nodeSearchBar.ShowBar(show);

    RECT rc;
    GetClientRect(m_hWnd, &rc);
    LayoutChildren(rc.right - rc.left, rc.bottom - rc.top);

    if (show) {
        m_nodeSearchBar.FocusSearchBox();
        std::wstring kw = m_nodeSearchBar.GetKeyword();
        if (!kw.empty()) {
            FilterTreeNodes(kw);
        }
    } else {
        m_nodeSearchBar.ClearKeyword();
        PopulateTreeViewFromDb(m_activeNoteId);
        if (m_treeView.GetHwnd()) {
            SetFocus(m_treeView.GetHwnd());
        }
    }
}

void MainWindow::FilterTreeNodes(const std::wstring& keyword) {
    if (!m_repo || !m_treeView.GetHwnd()) return;

    std::wstring trimmed = TrimWhitespace(keyword);
    if (trimmed.empty()) {
        m_nodeSearchBar.SetMatchCount(0, false);
        PopulateTreeViewFromDb(m_activeNoteId);
        return;
    }

    auto allNodes = m_repo->GetAllNodes();
    if (allNodes.empty()) {
        m_nodeSearchBar.SetMatchCount(0, true);
        m_treeView.ClearAll();
        return;
    }

    // 1. 建立节点父子关系查找表
    std::unordered_map<int64_t, std::vector<int64_t>> childrenMap;
    std::unordered_map<int64_t, const storage::NoteNode*> nodeMap;
    for (const auto& node : allNodes) {
        nodeMap[node.id] = &node;
        childrenMap[node.parentId].push_back(node.id);
    }

    // 2. 智能子序列模糊匹配直接命中项
    std::unordered_set<int64_t> directMatches;
    for (const auto& node : allNodes) {
        if (FuzzySubsequenceMatch(trimmed, node.title)) {
            directMatches.insert(node.id);
        }
    }

    int matchCount = static_cast<int>(directMatches.size());
    m_nodeSearchBar.SetMatchCount(matchCount, true);

    if (directMatches.empty()) {
        // 无匹配：清空树视图
        HWND hTree = m_treeView.GetHwnd();
        SendMessageW(hTree, WM_SETREDRAW, FALSE, 0);
        m_isFilteringTree = true;
        m_treeView.ClearAll();
        m_isFilteringTree = false;
        SendMessageW(hTree, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(hTree, nullptr, TRUE);
        return;
    }

    // 3. 收集所有需展示的节点集合：
    // - 直接命中节点
    // - 父节点命中时，递归展示其所有下级子孙节点
    // - 子节点命中时，追溯保留其所有祖先节点（保证路径完整）
    std::unordered_set<int64_t> visibleIds;
    std::function<void(int64_t)> addDescendants = [&](int64_t parentId) {
        auto it = childrenMap.find(parentId);
        if (it != childrenMap.end()) {
            for (int64_t childId : it->second) {
                visibleIds.insert(childId);
                addDescendants(childId);
            }
        }
    };

    for (int64_t matchedId : directMatches) {
        visibleIds.insert(matchedId);
        addDescendants(matchedId);

        // 向上追溯祖先
        int64_t currId = matchedId;
        while (true) {
            auto it = nodeMap.find(currId);
            if (it == nodeMap.end() || it->second->parentId == 0) break;
            currId = it->second->parentId;
            visibleIds.insert(currId);
        }
    }

    // 4. 重建过滤树视图（关闭重绘以消除闪烁）
    HWND hTree = m_treeView.GetHwnd();
    SendMessageW(hTree, WM_SETREDRAW, FALSE, 0);
    m_isFilteringTree = true;
    m_treeView.ClearAll();

    std::unordered_map<int64_t, HTREEITEM> idToItem;
    std::vector<storage::NoteNode> remaining;
    for (const auto& node : allNodes) {
        if (visibleIds.count(node.id)) {
            remaining.push_back(node);
        }
    }

    bool insertedAny = true;
    while (!remaining.empty() && insertedAny) {
        insertedAny = false;
        std::vector<storage::NoteNode> nextRemaining;
        for (auto& node : remaining) {
            if (node.parentId == 0 || visibleIds.count(node.parentId) == 0) {
                HTREEITEM hItem = m_treeView.InsertNode(nullptr, node.title, static_cast<LPARAM>(node.id), true);
                idToItem[node.id] = hItem;
                insertedAny = true;
            } else {
                auto it = idToItem.find(node.parentId);
                if (it != idToItem.end()) {
                    HTREEITEM hItem = m_treeView.InsertNode(it->second, node.title, static_cast<LPARAM>(node.id), true);
                    idToItem[node.id] = hItem;
                    insertedAny = true;
                } else {
                    nextRemaining.push_back(std::move(node));
                }
            }
        }
        remaining = std::move(nextRemaining);
    }

    // 孤儿节点兜底
    for (const auto& orphan : remaining) {
        HTREEITEM hItem = m_treeView.InsertNode(nullptr, orphan.title, static_cast<LPARAM>(orphan.id), true);
        idToItem[orphan.id] = hItem;
    }

    // 展开所有过滤节点的分支
    m_treeView.ExpandAll(true);

    // 选中项处理：优先保留当前笔记；若不在结果中，则高亮第一个直接命中项
    HTREEITEM hToSelect = nullptr;
    if (m_activeNoteId > 0 && idToItem.count(m_activeNoteId)) {
        hToSelect = idToItem[m_activeNoteId];
    } else {
        for (const auto& node : allNodes) {
            if (directMatches.count(node.id) && idToItem.count(node.id)) {
                hToSelect = idToItem[node.id];
                break;
            }
        }
    }

    if (!hToSelect) {
        hToSelect = TreeView_GetRoot(hTree);
    }

    if (hToSelect) {
        TreeView_Select(hTree, hToSelect, TVGN_CARET);
        m_treeView.EnsureVisible(hToSelect);
    }

    m_isFilteringTree = false;
    SendMessageW(hTree, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(hTree, nullptr, TRUE);
}

void MainWindow::ShowOutlinePane(bool show) {
    if (m_isOutlineVisible == show) return;
    m_isOutlineVisible = show;

    HMENU hMenu = GetMenu(m_hWnd);
    if (hMenu) {
        CheckMenuItem(hMenu, ID_VIEW_OUTLINE, m_isOutlineVisible ? MF_CHECKED : MF_UNCHECKED);
    }
    m_toolbar.SetOutlineButtonChecked(m_isOutlineVisible);

    m_vaultManager.SetConfigString(L"View", L"OutlineVisible", m_isOutlineVisible ? L"1" : L"0");

    RECT rc;
    GetClientRect(m_hWnd, &rc);
    LayoutChildren(rc.right - rc.left, rc.bottom - rc.top);

    if (m_isOutlineVisible) {
        UpdateOutline();
    }
}

void MainWindow::ToggleOutlinePane() {
    ShowOutlinePane(!m_isOutlineVisible);
}

void MainWindow::UpdateOutline() {
    if (!m_isOutlineVisible || m_isUpdatingOutline) return;
    m_isUpdatingOutline = true;

    auto items = m_richEditView.ExtractOutlineItems();
    m_outlinePane.SetOutlineItems(items);

    CHARRANGE cr = {};
    SendMessageW(m_richEditView.GetHwnd(), EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&cr));
    m_outlinePane.SelectNearestItem(cr.cpMin);

    m_isUpdatingOutline = false;
}

void MainWindow::SyncOutlineSelection() {
    if (!m_isOutlineVisible || m_isUpdatingOutline) return;
    CHARRANGE cr = {};
    SendMessageW(m_richEditView.GetHwnd(), EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&cr));
    m_outlinePane.SelectNearestItem(cr.cpMin);
}

void MainWindow::SetAlwaysOnTop(bool enable) {
    m_isAlwaysOnTop = enable;

    SetWindowPos(
        m_hWnd,
        enable ? HWND_TOPMOST : HWND_NOTOPMOST,
        0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE
    );

    HMENU hMenu = GetMenu(m_hWnd);
    if (hMenu) {
        CheckMenuItem(hMenu, ID_HELP_ALWAYS_ON_TOP,
            enable ? MF_CHECKED : MF_UNCHECKED);
    }

    UpdateStatusBar(enable ? L"已开启窗口置顶" : L"已取消窗口置顶");
    m_vaultManager.SetConfigString(L"Window", L"AlwaysOnTop", enable ? L"1" : L"0");
}

void MainWindow::ToggleAlwaysOnTop() {
    SetAlwaysOnTop(!m_isAlwaysOnTop);
}

} // namespace anynote::ui
