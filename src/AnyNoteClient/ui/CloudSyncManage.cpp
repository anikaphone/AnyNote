#include "CloudSync.h"
#include "sync/DiffEngine.h"
#include "common/StringUtils.h"
#include "res/resource.h"
#include <commctrl.h>
#include <windowsx.h>
#include <uxtheme.h>
#include <algorithm>
#include <map>
#include <set>
#include <ctime>

namespace anynote::ui {
namespace {
void Columns(HWND list, const std::vector<std::pair<std::wstring, int>>& columns) {
    ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_GRIDLINES);
    int index = 0; for (const auto& [text, width] : columns) { LVCOLUMNW column{}; column.mask = LVCF_TEXT | LVCF_WIDTH; column.pszText = const_cast<wchar_t*>(text.c_str()); column.cx = MulDiv(width, GetDpiForWindow(list), 96); ListView_InsertColumn(list, index++, &column); }
}
void Row(HWND list, int index, const std::vector<std::wstring>& cells) {
    LVITEMW item{}; item.mask = LVIF_TEXT; item.iItem = index; item.pszText = const_cast<wchar_t*>(cells.front().c_str()); ListView_InsertItem(list, &item);
    for (size_t i = 1; i < cells.size(); ++i) ListView_SetItemText(list, index, static_cast<int>(i), const_cast<wchar_t*>(cells[i].c_str()));
}
void Tab(HWND dialog, bool details) {
    ShowWindow(GetDlgItem(dialog, IDC_SYNC_LIST), details ? SW_HIDE : SW_SHOW);
    for (int id : {IDC_SYNC_TREE, IDC_SYNC_DIFF, IDC_SYNC_BACK}) ShowWindow(GetDlgItem(dialog, id), details ? SW_SHOW : SW_HIDE);
    TabCtrl_SetCurSel(GetDlgItem(dialog, IDC_SYNC_TAB), details ? 1 : 0);
    InvalidateRect(dialog, nullptr, TRUE);
}
std::wstring Flags(unsigned flags) {
    std::wstring result;
    for (const auto& [flag, text] : std::vector<std::pair<unsigned, const wchar_t*>>{{sync::LocalOnly, L"仅本地"}, {sync::RemoteOnly, L"仅远端"},
        {sync::Title, L"标题"}, {sync::Content, L"内容"}, {sync::Position, L"位置/排序"}, {sync::Time, L"时间"}, {sync::Format, L"格式"}})
        if (flags & flag) { if (!result.empty()) result += L"、"; result += text; }
    return result;
}
const sync::Node* Find(const sync::Snapshot& snapshot, const std::string& id) {
    auto found = std::find_if(snapshot.nodes.begin(), snapshot.nodes.end(), [&](const auto& n) { return n.syncId == id; });
    return found == snapshot.nodes.end() ? nullptr : &*found;
}
void Confirm(HWND owner, const std::wstring& text) { if (MessageBoxW(owner, text.c_str(), L"确认影响范围", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) throw std::runtime_error("CANCELLED"); }
std::wstring LocalTime(int64_t seconds) {
    __time64_t value = seconds; tm local{}; wchar_t text[64]{};
    if (_localtime64_s(&local, &value) != 0 || !wcsftime(text, std::size(text), L"%Y-%m-%d %H:%M:%S", &local))
        return std::to_wstring(seconds) + L" (Unix 秒)";
    return text;
}
std::wstring ServerTime(const std::string& value) {
    tm utc{}; int year, month, day, hour, minute, second;
    if (sscanf_s(value.c_str(), "%d-%d-%dT%d:%d:%d", &year, &month, &day, &hour, &minute, &second) != 6 || value.back() != 'Z')
        return utils::Utf8ToWide(value);
    utc.tm_year = year - 1900; utc.tm_mon = month - 1; utc.tm_mday = day; utc.tm_hour = hour; utc.tm_min = minute; utc.tm_sec = second;
    auto seconds = _mkgmtime64(&utc);
    return seconds == -1 ? utils::Utf8ToWide(value) : LocalTime(seconds);
}
}
void CloudSync::RefreshVaults(HWND dialog) {
    std::string selected;
    int old = ListView_GetNextItem(GetDlgItem(dialog, IDC_SYNC_LIST), -1, LVNI_SELECTED);
    if (old >= 0 && static_cast<size_t>(old) < m_vaults.size()) selected = m_vaults[old].at("uid");
    std::vector<nlohmann::json> vaults;
    RunSyncTask(dialog, L"读取云端笔记库", [&](sync::TransferControl& control) {
        std::string cursor; std::set<std::string> seen;
        do {
            auto page = m_client->Request("GET", "/vaults?limit=100" + (cursor.empty() ? "" : "&cursor=" + cursor), {}, control);
            for (const auto& vault : page.at("vaults")) { if (!seen.insert(vault.at("uid")).second) throw std::runtime_error("INVALID_RESPONSE"); vaults.push_back(vault); }
            cursor = page.at("next_cursor").is_null() ? "" : page.at("next_cursor").get<std::string>();
            if (vaults.size() > 100000) throw std::runtime_error("LIMIT_EXCEEDED");
        } while (!cursor.empty());
    });
    const char* keys[] = {"uid", "name", "created_at", "updated_at", "remark"};
    std::sort(vaults.begin(), vaults.end(), [&](const auto& a, const auto& b) {
        auto left = a.at(keys[m_vaultSortColumn]).template get<std::string>(), right = b.at(keys[m_vaultSortColumn]).template get<std::string>();
        if (left == right) return a.at("uid").template get<std::string>() < b.at("uid").template get<std::string>();
        return m_vaultSortAscending ? left < right : left > right;
    });
    m_vaults = std::move(vaults); HWND list = GetDlgItem(dialog, IDC_SYNC_LIST); ListView_DeleteAllItems(list);
    for (size_t i = 0; i < m_vaults.size(); ++i) {
        const auto& v = m_vaults[i]; Row(list, static_cast<int>(i), {utils::Utf8ToWide(v.at("uid")), utils::Utf8ToWide(v.at("name")),
            ServerTime(v.at("created_at")), ServerTime(v.at("updated_at")), utils::Utf8ToWide(v.at("remark"))});
        if (v.at("uid") == selected) ListView_SetItemState(list, static_cast<int>(i), LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    }
}
void CloudSync::ShowDetails(HWND dialog, const nlohmann::json& vault) {
    TVITEMW selection{}; selection.hItem = TreeView_GetSelection(GetDlgItem(dialog, IDC_SYNC_TREE)); selection.mask = TVIF_PARAM;
    if (selection.hItem && TreeView_GetItem(GetDlgItem(dialog, IDC_SYNC_TREE), &selection) && static_cast<size_t>(selection.lParam) < m_remote.nodes.size())
        m_treeSelection = m_remote.nodes[selection.lParam].syncId;
    auto manifest = Manifest(dialog, vault.at("uid"));
    auto remote = ManifestSnapshot(manifest); auto local = LocalSnapshot(dialog, remote.uid);
    m_selectedVault = manifest.at("vault"); m_remote = std::move(remote); m_local = std::move(local); RenderDetails(dialog); Tab(dialog, true);
}
void CloudSync::RenderDetails(HWND dialog) {
    HWND tree = GetDlgItem(dialog, IDC_SYNC_TREE), list = GetDlgItem(dialog, IDC_SYNC_DIFF);
    std::string selected; int old = ListView_GetNextItem(list, -1, LVNI_SELECTED); if (old >= 0 && static_cast<size_t>(old) < m_diffIds.size()) selected = m_diffIds[old];
    TreeView_DeleteAllItems(tree); ListView_DeleteAllItems(list); m_diffIds.clear();
    std::map<std::string, unsigned> flags;
    std::map<std::string, const sync::Node*> localIndex, remoteIndex, displayIndex;
    for (const auto& node : m_local.nodes) { localIndex[node.syncId] = &node; displayIndex[node.syncId] = &node; }
    for (const auto& node : m_remote.nodes) { remoteIndex[node.syncId] = &node; displayIndex[node.syncId] = &node; }
    auto differences = sync::Compare(m_local, m_remote);
    std::sort(differences.begin(), differences.end(), [&](const auto& a, const auto& b) {
        const auto* left = displayIndex.at(a.syncId);
        const auto* right = displayIndex.at(b.syncId);
        int comparison = 0;
        if (m_diffSortColumn == 3) comparison = (left->sourceId > right->sourceId) - (left->sourceId < right->sourceId);
        else if (m_diffSortColumn == 5) comparison = (left->modifiedTime > right->modifiedTime) - (left->modifiedTime < right->modifiedTime);
        else {
            auto key = [&](const sync::Node& n, unsigned flags) { return m_diffSortColumn == 0 ? utils::Utf8ToWide(n.title) : m_diffSortColumn == 1 ? Flags(flags) : m_diffSortColumn == 2 ? utils::Utf8ToWide(n.syncId) : utils::Utf8ToWide(n.parentSyncId.value_or("")); };
            comparison = key(*left, a.differences).compare(key(*right, b.differences));
        }
        if (comparison == 0) return a.syncId < b.syncId; return m_diffSortAscending ? comparison < 0 : comparison > 0;
    });
    for (const auto& difference : differences) {
        flags[difference.syncId] = difference.differences;
        const auto* local = localIndex.contains(difference.syncId) ? localIndex.at(difference.syncId) : nullptr;
        const auto* remote = remoteIndex.contains(difference.syncId) ? remoteIndex.at(difference.syncId) : nullptr;
        const auto* n = displayIndex.at(difference.syncId);
        int row = static_cast<int>(m_diffIds.size()); m_diffIds.push_back(difference.syncId);
        Row(list, row, {utils::Utf8ToWide(n->title), Flags(difference.differences), utils::Utf8ToWide(n->syncId),
            (local ? std::to_wstring(local->sourceId) : L"—") + L" / " + (remote ? std::to_wstring(remote->sourceId) : L"—"),
            n->parentSyncId ? utils::Utf8ToWide(*n->parentSyncId) : L"根", LocalTime(n->modifiedTime)});
        if (difference.syncId == selected) ListView_SetItemState(list, row, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    }
    std::map<std::string, std::vector<size_t>> children;
    for (size_t i = 0; i < m_remote.nodes.size(); ++i) children[m_remote.nodes[i].parentSyncId.value_or("")].push_back(i);
    std::function<void(const std::string&, HTREEITEM)> append = [&](const std::string& parent, HTREEITEM item) {
        auto indexes = children[parent]; std::sort(indexes.begin(), indexes.end(), [&](size_t a, size_t b) {
            if (m_remote.nodes[a].sequence != m_remote.nodes[b].sequence) return m_remote.nodes[a].sequence < m_remote.nodes[b].sequence; return m_remote.nodes[a].syncId < m_remote.nodes[b].syncId;
        });
        for (auto index : indexes) {
            const auto& node = m_remote.nodes[index]; auto text = (flags[node.syncId] ? L"[" + Flags(flags[node.syncId]) + L"] " : L"[一致] ") + utils::Utf8ToWide(node.title);
            TVINSERTSTRUCTW insert{}; insert.hParent = item ? item : TVI_ROOT; insert.hInsertAfter = TVI_LAST;
            insert.item.mask = TVIF_TEXT | TVIF_PARAM; insert.item.pszText = text.data(); insert.item.lParam = static_cast<LPARAM>(index);
            auto child = TreeView_InsertItem(tree, &insert); append(node.syncId, child); if (!item) TreeView_Expand(tree, child, TVE_EXPAND);
            if (node.syncId == m_treeSelection) TreeView_SelectItem(tree, child);
        }
    }; append("", nullptr);
    SetDlgItemTextW(dialog, IDC_SYNC_STATUS, (L"云端库：" + utils::Utf8ToWide(m_remote.name) + L"；差异 " + std::to_wstring(m_diffIds.size()) + L" 项。右键或 Shift+F10 操作。").c_str());
}
void CloudSync::ContextMenu(HWND dialog, HWND source, POINT point) {
    constexpr UINT Details = 6001, Download = 6002, Delete = 6003, Remark = 6004, Upload = 6005;
    bool vaultList = source == GetDlgItem(dialog, IDC_SYNC_LIST);
    bool tree = source == GetDlgItem(dialog, IDC_SYNC_TREE);
    if (!vaultList && !tree && source != GetDlgItem(dialog, IDC_SYNC_DIFF)) return;
    if (point.x == -1 && point.y == -1) { RECT rect{}; GetWindowRect(source, &rect); point = {rect.left + 30, rect.top + 40}; }
    else {
        POINT local = point; ScreenToClient(source, &local);
        if (tree) { TVHITTESTINFO hit{}; hit.pt = local; if (auto item = TreeView_HitTest(source, &hit)) TreeView_SelectItem(source, item); }
        else { LVHITTESTINFO hit{}; hit.pt = local; int row = ListView_HitTest(source, &hit); if (row >= 0) ListView_SetItemState(source, row, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED); }
    }
    std::string id; nlohmann::json vault; bool localExists = false, remoteExists = false;
    if (vaultList) {
        int row = ListView_GetNextItem(source, -1, LVNI_SELECTED); if (row < 0 || static_cast<size_t>(row) >= m_vaults.size()) return; vault = m_vaults[row];
    } else {
        vault = m_selectedVault;
        if (tree) { TVITEMW item{}; item.hItem = TreeView_GetSelection(source); item.mask = TVIF_PARAM; if (!item.hItem || !TreeView_GetItem(source, &item) || static_cast<size_t>(item.lParam) >= m_remote.nodes.size()) return; id = m_remote.nodes[item.lParam].syncId; }
        else { int row = ListView_GetNextItem(source, -1, LVNI_SELECTED); if (row < 0 || static_cast<size_t>(row) >= m_diffIds.size()) return; id = m_diffIds[row]; }
        localExists = Find(m_local, id) != nullptr; remoteExists = Find(m_remote, id) != nullptr;
    }
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, Details, L"查看详情");
    AppendMenuW(menu, MF_STRING | (!vaultList && !remoteExists ? MF_GRAYED : 0), Download, vaultList ? L"下载到本地…" : L"下载覆盖（整个子树）…");
    if (!vaultList) AppendMenuW(menu, MF_STRING | (!localExists ? MF_GRAYED : 0), Upload, L"上传覆盖（整个子树）…");
    AppendMenuW(menu, MF_STRING | (!vaultList && !remoteExists ? MF_GRAYED : 0), Delete, L"删除远端…");
    if (vaultList) AppendMenuW(menu, MF_STRING, Remark, L"修改备注…");
    UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, dialog, nullptr); DestroyMenu(menu);
    if (!command) return;
    if (command == Details) {
        if (vaultList) ShowDetails(dialog, vault);
        else {
            sync::SnapshotResponse downloaded;
            if (remoteExists) RunSyncTask(dialog, L"读取节点详情", [&](sync::TransferControl& control) {
                downloaded = m_client->Download("/vaults/" + vault.at("uid").get<std::string>() + "/nodes/" + id + "/subtree?epoch=" +
                    vault.at("epoch").get<std::string>() + "&revision=" + vault.at("revision").get<std::string>(), control);
            });
            const auto* local = Find(m_local, id); const auto* remote = Find(downloaded.snapshot, id);
            if (remoteExists && !remote) throw std::runtime_error("INVALID_RESPONSE");
            unsigned flags = 0; for (const auto& difference : sync::Compare(m_local, m_remote)) if (difference.syncId == id) flags = difference.differences;
            auto text = L"UID: " + utils::Utf8ToWide(vault.at("uid")) + L"\r\nsync_id: " + utils::Utf8ToWide(id) + L"\r\n差异: " + (flags ? Flags(flags) : L"一致");
            for (const auto& [label, node] : {std::pair{L"本地", local}, std::pair{L"远端", remote}}) {
                text += L"\r\n\r\n" + std::wstring(label) + (node ? L"" : L"：不存在");
                if (node) text += L" id: " + std::to_wstring(node->sourceId) + L"\r\n标题: " + utils::Utf8ToWide(node->title) +
                    L"\r\n父节点: " + utils::Utf8ToWide(node->parentSyncId.value_or("root")) + L"\r\n创建时间: " + LocalTime(node->createdTime) +
                    L"\r\n修改时间: " + LocalTime(node->modifiedTime) + L"\r\nRTF: " + std::to_wstring(node->rtf.size()) + L" 字节\r\nSHA-256: " + utils::Utf8ToWide(node->contentHash);
            }
            MessageBoxW(dialog, text.c_str(), L"节点详情", MB_OK | MB_ICONINFORMATION);
        } return;
    }
    if (command == Download) DownloadVault(dialog, vault, vaultList ? std::optional<std::string>{} : id);
    else {
        auto condition = SyncPrecondition(vault); const std::string uid = vault.at("uid"); auto path = "/vaults/" + uid;
        if (command == Remark) {
            auto remark = SyncPrompt(dialog, L"修改云端备注（最多 2000 字符）", utils::Utf8ToWide(vault.at("remark"))); if (!remark) return;
            Write(dialog, uid, condition, [&](sync::TransferControl& c) { return m_client->Request("PATCH", path + "/remark", {{"precondition", condition}, {"remark", utils::WideToUtf8(*remark)}}, c); });
        } else if (command == Delete) {
            size_t count = 0;
            if (vaultList) count = Manifest(dialog, uid).at("nodes").size();
            else { std::set<std::string> selected{id}; bool more = true; while (more) { more = false; for (const auto& n : m_remote.nodes) if (n.parentSyncId && selected.contains(*n.parentSyncId) && selected.insert(n.syncId).second) more = true; } count = selected.size(); }
            Confirm(dialog, L"将删除远端 " + std::to_wstring(count) + L" 篇笔记" + (vaultList ? L"及此笔记库" : L"（选中节点及全部后代）") + L"。\r\n本地副本保留；以后上传可能重新创建这些数据。");
            if (!vaultList) path += "/nodes/" + id + "/subtree";
            Write(dialog, uid, condition, [&](sync::TransferControl& c) { return m_client->Request("DELETE", path, {{"precondition", condition}}, c); });
        } else if (command == Upload) {
            auto local = LocalSnapshot(dialog, uid); auto planned = sync::PlanSubtree(m_remote, local, id);
            auto affected = sync::Compare(m_remote, planned);
            size_t added = 0, removed = 0, changed = 0;
            for (const auto& difference : affected) {
                if (difference.differences & sync::RemoteOnly) ++added;
                else if (difference.differences & sync::LocalOnly) ++removed;
                else ++changed;
            }
            Confirm(dialog, L"将上传本地选中节点及整个子树，补齐缺失祖先，并可能调整兄弟排序。\r\n新增 " + std::to_wstring(added) + L"，替换/移动 " + std::to_wstring(changed) + L"，删除 " + std::to_wstring(removed) + L"。\r\n目标子树内独有节点将删除。");
            Write(dialog, uid, condition, [&](sync::TransferControl& c) { return m_client->Upload(path + "/nodes/" + id + "/subtree", condition, local, true, c); });
        }
    }
    if (vaultList) { RefreshVaults(dialog); Tab(dialog, false); } else ShowDetails(dialog, vault);
}
INT_PTR CALLBACK CloudSync::ManageProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<CloudSync*>(GetWindowLongPtrW(dialog, DWLP_USER));
    try {
        if (message == WM_INITDIALOG) {
            EnableThemeDialogTexture(dialog, ETDT_ENABLETAB);
            self = reinterpret_cast<CloudSync*>(lParam); SetWindowLongPtrW(dialog, DWLP_USER, lParam);
            int index = 0; for (const auto* name : {L"数据库表", L"库详情"}) { TCITEMW item{}; item.mask = TCIF_TEXT; item.pszText = const_cast<wchar_t*>(name); TabCtrl_InsertItem(GetDlgItem(dialog, IDC_SYNC_TAB), index++, &item); }
            Columns(GetDlgItem(dialog, IDC_SYNC_LIST), {{L"UID", 100}, {L"数据库名", 110}, {L"创建时间（本地）", 125}, {L"更新时间（本地）", 125}, {L"备注", 150}});
            Columns(GetDlgItem(dialog, IDC_SYNC_DIFF), {{L"标题", 100}, {L"差异", 65}, {L"sync_id", 115}, {L"本地 / 远端 id", 75}, {L"parent_sync_id", 115}, {L"修改时间（本地）", 125}});
            Tab(dialog, false); self->RefreshVaults(dialog); return TRUE;
        }
        if (message == WM_CTLCOLORSTATIC) {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            SetBkMode(hdc, TRANSPARENT);
            return reinterpret_cast<INT_PTR>(GetStockObject(NULL_BRUSH));
        }
        if (!self) return FALSE;
        if (message == WM_CONTEXTMENU) { self->ContextMenu(dialog, reinterpret_cast<HWND>(wParam), {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)}); return TRUE; }
        if (message == WM_NOTIFY) {
            auto* header = reinterpret_cast<NMHDR*>(lParam);
            if (header->idFrom == IDC_SYNC_TAB && header->code == TCN_SELCHANGE) { Tab(dialog, TabCtrl_GetCurSel(GetDlgItem(dialog, IDC_SYNC_TAB)) == 1 && !self->m_selectedVault.empty()); return TRUE; }
            if (header->idFrom == IDC_SYNC_LIST && header->code == NM_DBLCLK) { int row = ListView_GetNextItem(header->hwndFrom, -1, LVNI_SELECTED); if (row >= 0 && static_cast<size_t>(row) < self->m_vaults.size()) self->ShowDetails(dialog, self->m_vaults[row]); return TRUE; }
            if (header->code == LVN_COLUMNCLICK) {
                int column = reinterpret_cast<NMLISTVIEW*>(lParam)->iSubItem;
                if (header->idFrom == IDC_SYNC_LIST && column >= 0 && column < 5) {
                    self->m_vaultSortAscending = column == self->m_vaultSortColumn ? !self->m_vaultSortAscending : true;
                    self->m_vaultSortColumn = column; self->RefreshVaults(dialog); return TRUE;
                }
                if (header->idFrom == IDC_SYNC_DIFF && column >= 0 && column < 6) {
                    self->m_diffSortAscending = column == self->m_diffSortColumn ? !self->m_diffSortAscending : true;
                    self->m_diffSortColumn = column; self->RenderDetails(dialog); return TRUE;
                }
            }
        }
        if (message == WM_COMMAND) {
            switch (LOWORD(wParam)) {
            case IDCANCEL: EndDialog(dialog, IDCANCEL); return TRUE;
            case IDC_SYNC_BACK: self->RefreshVaults(dialog); Tab(dialog, false); return TRUE;
            case IDC_SYNC_REFRESH: if (TabCtrl_GetCurSel(GetDlgItem(dialog, IDC_SYNC_TAB)) == 0) self->RefreshVaults(dialog); else self->ShowDetails(dialog, self->m_selectedVault); return TRUE;
            case IDC_SYNC_QUERY: self->QueryPending(dialog); return TRUE;
            }
        }
    } catch (const std::exception& error) { SyncError(dialog, error); return TRUE; }
    return FALSE;
}
}
