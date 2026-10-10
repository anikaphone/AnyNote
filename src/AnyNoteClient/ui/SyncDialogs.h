#pragma once
#include "sync/SyncClient.h"
#include <windows.h>
#include <optional>
#include <vector>

namespace anynote::ui {
void RunSyncTask(HWND owner, const std::wstring& title, const std::function<void(sync::TransferControl&)>& action);
std::optional<std::wstring> SyncPrompt(HWND owner, const std::wstring& title, const std::wstring& initial = L"", bool secret = false);
std::optional<size_t> SyncChoose(HWND owner, const std::wstring& title, const std::vector<std::wstring>& choices);
std::wstring SyncControlText(HWND dialog, int id);
void SyncError(HWND owner, const std::exception& error);
}
