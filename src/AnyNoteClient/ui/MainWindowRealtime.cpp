#include "CloudSync.h"
#include "MainWindow.h"
#include "SyncDialogs.h"
#include "common/StringUtils.h"
#include "resource.h"
#include <sqlite3.h>

namespace anynote::ui
{
void MainWindow::StopRealtime()
{
    KillTimer(m_hWnd, TIMER_REALTIME);
    m_realtime.reset();
    m_realtimeRefreshPending = false;
    m_toolbar.SetRealtimeStatus(false, L"实时同步已关闭");
}
void MainWindow::ToggleRealtime()
{
    if (m_realtime && (m_realtime->Active() || m_realtime->Checking()))
    {
        StopRealtime();
        if (m_repo) m_vaultManager.SetConfigString(L"RealtimeSync", utils::Utf8ToWide(m_repo->GetVaultUid()), L"0");
        return;
    }
    m_toolbar.SetRealtimeStatus(false, L"实时同步未开启");
    if (!m_cloudSync || !m_cloudSync->IsLoggedIn())
    {
        MessageBoxW(m_hWnd, L"请先登录云端账户，再开启实时同步。", L"实时同步", MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (!m_db || !m_repo || !SaveActiveNote())
        return;
    try
    {
        m_realtime = std::make_unique<sync::RealtimeSync>(*m_db, m_cloudSync->RealtimeClient(),
                                                          m_cloudSync->RealtimeBinding(), m_repo->GetVaultUid());
        m_realtime->Enable();
        SetTimer(m_hWnd, TIMER_REALTIME, 250, nullptr);
        m_toolbar.SetRealtimeStatus(false, m_realtime->Status());
        m_vaultManager.SetConfigString(L"RealtimeSync", utils::Utf8ToWide(m_repo->GetVaultUid()), L"1");
    }
    catch (const std::exception &e)
    {
        SyncError(m_hWnd, e);
    }
}
void MainWindow::RestoreRealtimeIfEnabled()
{
    if (!m_repo || !m_cloudSync || !m_cloudSync->IsLoggedIn()) return;
    const auto enabled = m_vaultManager.GetConfigString(L"RealtimeSync", utils::Utf8ToWide(m_repo->GetVaultUid()), L"0");
    if (enabled != L"1" || m_realtime) return;
    ToggleRealtime();
}
void MainWindow::TickRealtime()
{
    if (!m_realtime || m_realtimeDialog || m_syncBusy || m_isDragging)
        return;
    // IME composition is transient editor state. Do not serialize or apply
    // remote changes while the user is choosing a candidate word.
    if (IsImeComposing())
        return;
    std::string protectedId;
    bool dirty = false;
    if (m_activeNoteId > 0 && (m_realtime->Ready() || m_realtimeRefreshPending))
    {
        auto rtf = m_richEditView.StreamOutRTF();
        dirty = !rtf || sync::Sha256(*rtf) != m_loadedEditorHash;
        if (dirty)
        {
            storage::Database::Statement s;
            if (s.Prepare(*m_db, "SELECT sync_id FROM nodes WHERE id=?") && s.BindInt64(1, m_activeNoteId) &&
                s.Step() == SQLITE_ROW)
                protectedId = s.GetText(0);
            if (protectedId.empty())
                return;
        }
    }
    m_realtimeRefreshPending = m_realtime->Tick(protectedId) || m_realtimeRefreshPending;
    if (m_realtimeRefreshPending && !dirty)
    {
        const auto selected = m_activeNoteId;
        m_activeNoteId = 0;
        PopulateTreeViewFromDb(selected);
        if (m_isNodeSearchVisible)
            FilterTreeNodes(m_nodeSearchBar.GetKeyword());
        if (m_searchPane.GetHwnd())
            m_searchPane.PerformSearch();
        m_realtimeRefreshPending = false;
    }
    m_toolbar.SetRealtimeStatus(m_realtime->Active(), m_realtime->Status());
    const auto error = m_realtime->TakeEnableError();
    if (!error.empty())
    {
        m_realtimeDialog = true;
        if (error == "REALTIME_INITIAL_DIFFERENCE" || error == "NOT_FOUND")
            MessageBoxW(m_hWnd,
                        L"当前笔记库与云端不一致，或云端尚无此笔记库。\n请先通过云端管理完成同步，再开启实时同步。",
                        L"无法开启实时同步", MB_OK | MB_ICONINFORMATION);
        else
            SyncError(m_hWnd, std::runtime_error(error));
        m_realtimeDialog = false;
    }
}
void MainWindow::ResolveRealtime()
{
    if (!m_realtime || m_realtimeDialog || !SaveActiveNote())
        return;
    if (m_realtime->Busy())
    {
        m_toolbar.SetRealtimeStatus(m_realtime->Active(), L"正在同步，请稍后点击处理冲突");
        return;
    }
    const auto conflicts = m_realtime->Conflicts();
    if (conflicts.empty())
        return;
    m_realtimeDialog = true;
    try
    {
        storage::RealtimeStore store(*m_db);
        auto nodes = store.Metadata();
        std::vector<std::wstring> choices;
        for (const auto &value : conflicts)
        {
            auto id = value.get<std::string>();
            choices.push_back(nodes.contains(id) ? utils::Utf8ToWide(nodes.at(id).at("title").get<std::string>())
                                                 : L"本地已删除：" + utils::Utf8ToWide(id));
        }
        auto selected = SyncChoose(m_hWnd, L"选择要处理的同步冲突", choices);
        if (selected)
        {
            auto action = SyncChoose(m_hWnd, L"选择此笔记的最终版本（删除也会同步）",
                                     {L"保留本地版本", L"采用云端版本", L"打开云端管理查看差异"});
            if (action && *action < 2)
                m_realtime->Resolve(conflicts.at(*selected).get<std::string>(), *action == 0);
            else if (action)
            {
                m_realtimeDialog = false;
                OnCloudCommand(ID_CLOUD_MANAGE);
                return;
            }
        }
    }
    catch (const std::exception &e)
    {
        SyncError(m_hWnd, e);
    }
    m_realtimeDialog = false;
}
} // namespace anynote::ui
