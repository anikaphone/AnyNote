#include "NoteRepository.h"
#include "common/StringUtils.h"
#include <sqlite3.h>
#include <chrono>

namespace anynote::storage {

using anynote::utils::WideToRtf;

namespace {

int64_t GetCurrentUnixTimestamp() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

std::string BuildDefaultWelcomeRtf() {
    std::string rtf;
    rtf += "{\\rtf1\\ansi\\deff0\\nouicompat";
    rtf += "{\\fonttbl{\\f0\\fnil\\fcharset134 Segoe UI;}{\\f1\\fnil\\fcharset0 Consolas;}}";
    rtf += "{\\colortbl ;\\red246\\green248\\blue250;\\red225\\green228\\blue232;\\red36\\green41\\blue47;\\red0\\green92\\blue197;\\red215\\green58\\blue73;\\red3\\green47\\blue98;\\red106\\green115\\blue125;\\red0\\green92\\blue197;\\red111\\green66\\blue193;\\red3\\green102\\blue214;}";
    rtf += "\\viewkind4\\uc1";
    rtf += "\\pard\\b\\fs28\\cf4 " + WideToRtf(L"欢迎使用 AnyNote - 基于 C++20 与原生 Win32 的分层树状笔记！") + "\\b0\\fs22\\par\\par";
    rtf += "\\cf3\\b " + WideToRtf(L"★ 特性高光：") + "\\b0\\par";
    rtf += WideToRtf(L"• ") + "\\b " + WideToRtf(L"原生富文本内核") + "\\b0 " + WideToRtf(L"：采用 Windows MSFTEDIT.DLL (RichEdit 5.0)，毫秒启动，极低内存；") + "\\par";
    rtf += WideToRtf(L"• ") + "\\b " + WideToRtf(L"剪贴板图片直接粘贴") + "\\b0 " + WideToRtf(L"：按 Win+Shift+S 截图，在编辑器中按 ") + "\\b Ctrl+V\\b0 " + WideToRtf(L" 即可直接粘贴截图；") + "\\par";
    rtf += WideToRtf(L"• ") + "\\b " + WideToRtf(L"原生代码框 (CodeBox)") + "\\b0 " + WideToRtf(L"：点击上方「代码块」或按 ") + "\\b Ctrl+K\\b0 " + WideToRtf(L"，支持多语言高亮、卡片底色与 Consolas 等宽排版；") + "\\par";
    rtf += WideToRtf(L"• ") + "\\b " + WideToRtf(L"单文件安全持久化") + "\\b0 " + WideToRtf(L"：静态集成 SQLite3MC，支持 AES-256 全库透明加密。") + "\\par\\par";
    rtf += "\\pard\\cf3 " + WideToRtf(L"下方即为原生嵌入的代码块示例：") + "\\par\\par";

    // 嵌入示例代码卡片
    rtf += "\\trowd\\trgaph108\\trleft360";
    rtf += "\\clbrdrt\\brdrs\\brdrw15\\brdrcf2";
    rtf += "\\clbrdrb\\brdrs\\brdrw15\\brdrcf2";
    rtf += "\\clbrdrl\\brdrs\\brdrw40\\brdrcf10";
    rtf += "\\clbrdrr\\brdrs\\brdrw15\\brdrcf2";
    rtf += "\\clcbpat1\\cellx8600\n";
    rtf += "\\pard\\intbl\\sl240\\slmult1\\sb0\\sa0\\f1\\fs19\\cf3 ";
    rtf += "\\cf7 " + WideToRtf(L"// C++ 示例代码块") + "\\par";
    rtf += "\\cf5 #include\\cf3  <iostream>\\par\\par";
    rtf += "\\cf4 int\\cf3  main() {\\par";
    rtf += "    std::cout << \\cf6 \"Hello, AnyNote Native CodeBlock!\"\\cf3  << std::endl;\\par";
    rtf += "    \\cf4 return\\cf3  0;\\par";
    rtf += "}\\cell\\row\n";
    rtf += "\\pard\\f0\\fs22\\par\\cf3 " + WideToRtf(L"可以在此继续输入正文，或选中文本后按 ") + "\\b Ctrl+K\\b0 " + WideToRtf(L" 转换为代码块！") + "\\par";
    rtf += "}";
    return rtf;
}

std::string BuildDefaultQuickStartRtf() {
    std::string rtf;
    rtf += "{\\rtf1\\ansi\\deff0\\nouicompat";
    rtf += "{\\fonttbl{\\f0\\fnil\\fcharset134 Segoe UI;}}";
    rtf += "{\\colortbl ;\\red0\\green92\\blue197;\\red36\\green41\\blue47;}";
    rtf += "\\viewkind4\\uc1";
    rtf += "\\pard\\b\\fs26\\cf1 " + WideToRtf(L"【快速上手指南】") + "\\b0\\fs22\\par\\par";
    rtf += "\\cf2 " + WideToRtf(L"1. 在左侧树形目录中右键或使用菜单添加同级或子笔记；") + "\\par";
    rtf += WideToRtf(L"2. 点击上方工具栏按钮或使用快捷键进行加粗 (Ctrl+B)、斜体 (Ctrl+I)、下划线 (Ctrl+U)；") + "\\par";
    rtf += WideToRtf(L"3. 在顶部格式栏下拉框中可一键设置 1~4 级标题，亦可使用快捷键 Ctrl+1 ~ Ctrl+4；") + "\\par";
    rtf += WideToRtf(L"4. 按 Ctrl+K 或点击「插入」->「插入代码块」，弹出代码框配置窗口，选择语言即可一键插入高亮代码块；") + "\\par";
    rtf += WideToRtf(L"5. 任意复制微信/QQ/系统截图 (Win+Shift+S)，在右侧编辑器中直接按 Ctrl+V 即可粘贴图片；") + "\\par";
    rtf += WideToRtf(L"6. 所有修改自动保存到 SQLite 数据库文件中，关闭软件数据不丢失。") + "\\par";
    rtf += "}";
    return rtf;
}

std::string BuildDefaultFeaturesRtf() {
    std::string rtf;
    rtf += "{\\rtf1\\ansi\\deff0\\nouicompat";
    rtf += "{\\fonttbl{\\f0\\fnil\\fcharset134 Segoe UI;}}";
    rtf += "{\\colortbl ;\\red0\\green92\\blue197;\\red36\\green41\\blue47;\\red106\\green115\\blue125;}";
    rtf += "\\viewkind4\\uc1";
    rtf += "\\pard\\b\\fs26\\cf1 " + WideToRtf(L"【特性与常用快捷键】") + "\\b0\\fs22\\par\\par";
    rtf += "\\cf2\\b " + WideToRtf(L"• 目录与笔记管理：") + "\\b0\\par";
    rtf += WideToRtf(L"  - Ctrl+N : 新建同级笔记") + "\\par";
    rtf += WideToRtf(L"  - Ctrl+Shift+N : 新建子笔记") + "\\par";
    rtf += WideToRtf(L"  - F2 : 重命名选中笔记") + "\\par";
    rtf += WideToRtf(L"  - Del : 删除选中笔记（仅当焦点在左侧树列表时有效）") + "\\par";
    rtf += WideToRtf(L"  - Ctrl+S : 立即保存当前笔记") + "\\par\\par";
    rtf += "\\b " + WideToRtf(L"• 编辑与排版：") + "\\b0\\par";
    rtf += WideToRtf(L"  - Ctrl+0 ~ 4 : 正文与 1~4 级标题切换") + "\\par";
    rtf += WideToRtf(L"  - Ctrl+B / Ctrl+I / Ctrl+U : 粗体 / 斜体 / 下划线") + "\\par";
    rtf += WideToRtf(L"  - Ctrl+K : 插入代码块") + "\\par";
    rtf += WideToRtf(L"  - Ctrl+Z / Ctrl+Y : 撤销 / 重做") + "\\par";
    rtf += "}";
    return rtf;
}

std::string BuildDefaultCppRtf() {
    std::string rtf;
    rtf += "{\\rtf1\\ansi\\deff0\\nouicompat";
    rtf += "{\\fonttbl{\\f0\\fnil\\fcharset134 Segoe UI;}{\\f1\\fnil\\fcharset0 Consolas;}}";
    rtf += "{\\colortbl ;\\red246\\green248\\blue250;\\red225\\green228\\blue232;\\red36\\green41\\blue47;\\red0\\green92\\blue197;\\red215\\green58\\blue73;\\red3\\green47\\blue98;\\red106\\green115\\blue125;\\red0\\green92\\blue197;\\red111\\green66\\blue193;\\red3\\green102\\blue214;}";
    rtf += "\\viewkind4\\uc1";
    rtf += "\\pard\\b\\fs26\\cf4 " + WideToRtf(L"【现代 C++ 与 Win32 架构设计】") + "\\b0\\fs22\\par\\par";
    rtf += "\\cf3 " + WideToRtf(L"本工程采用现代 C++20 面向对象封装原生 Win32 API：") + "\\par";
    rtf += "• \\b " + WideToRtf(L"GWLP_USERDATA 绑定") + "\\b0 " + WideToRtf(L"：将 HWND 映射为 C++ 类对象指针，无全局变量；") + "\\par";
    rtf += "• \\b " + WideToRtf(L"Per-Monitor V2 DPI") + "\\b0 " + WideToRtf(L"：高分屏 4K 150%/200% 清晰不发虚；") + "\\par";
    rtf += "• \\b " + WideToRtf(L"零第三方 DLL 依赖") + "\\b0 " + WideToRtf(L"：全静态链接便携发行。") + "\\par\\par";
    rtf += "\\cf3 " + WideToRtf(L"消息分发核心代码封装实现如下：") + "\\par\\par";

    rtf += "\\trowd\\trgaph108\\trleft360";
    rtf += "\\clbrdrt\\brdrs\\brdrw15\\brdrcf2";
    rtf += "\\clbrdrb\\brdrs\\brdrw15\\brdrcf2";
    rtf += "\\clbrdrl\\brdrs\\brdrw40\\brdrcf10";
    rtf += "\\clbrdrr\\brdrs\\brdrw15\\brdrcf2";
    rtf += "\\clcbpat1\\cellx8600\n";
    rtf += "\\pard\\intbl\\sl240\\slmult1\\sb0\\sa0\\f1\\fs19\\cf3 ";
    rtf += "\\cf7 " + WideToRtf(L"// Window 抽象基类消息路由回调") + "\\par";
    rtf += "\\cf9 LRESULT\\cf3  \\cf9 CALLBACK\\cf3  Window::StaticWndProc(\\cf9 HWND\\cf3  hWnd, \\cf9 UINT\\cf3  uMsg, \\cf9 WPARAM\\cf3  wParam, \\cf9 LPARAM\\cf3  lParam) {\\par";
    rtf += "    \\cf4 auto\\cf3 * pThis = \\cf4 reinterpret_cast\\cf3 <Window*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));\\par";
    rtf += "    \\cf4 if\\cf3  (uMsg == WM_NCCREATE) {\\par";
    rtf += "        \\cf4 auto\\cf3 * pCreate = \\cf4 reinterpret_cast\\cf3 <CREATESTRUCTW*>(lParam);\\par";
    rtf += "        pThis = \\cf4 reinterpret_cast\\cf3 <Window*>(pCreate->lpCreateParams);\\par";
    rtf += "        \\cf4 if\\cf3  (pThis) {\\par";
    rtf += "            pThis->m_hWnd = hWnd;\\par";
    rtf += "            SetWindowLongPtrW(hWnd, GWLP_USERDATA, \\cf4 reinterpret_cast\\cf3 <\\cf9 LONG_PTR\\cf3 >(pThis));\\par";
    rtf += "        }\\par";
    rtf += "    }\\par";
    rtf += "    \\cf4 return\\cf3  pThis ? pThis->HandleMessage(uMsg, wParam, lParam) : DefWindowProcW(hWnd, uMsg, wParam, lParam);\\par";
    rtf += "}\\cell\\row\n";
    rtf += "\\pard\\f0\\fs22\\par\\cf3 " + WideToRtf(L"这种封装方式优雅兼顾面向对象与 Win32 原生性能。") + "\\par";
    rtf += "}";
    return rtf;
}

std::string BuildDefaultSqliteRtf() {
    std::string rtf;
    rtf += "{\\rtf1\\ansi\\deff0\\nouicompat";
    rtf += "{\\fonttbl{\\f0\\fnil\\fcharset134 Segoe UI;}{\\f1\\fnil\\fcharset0 Consolas;}}";
    rtf += "{\\colortbl ;\\red246\\green248\\blue250;\\red225\\green228\\blue232;\\red36\\green41\\blue47;\\red0\\green92\\blue197;\\red215\\green58\\blue73;\\red3\\green47\\blue98;\\red106\\green115\\blue125;\\red0\\green92\\blue197;\\red111\\green66\\blue193;\\red3\\green102\\blue214;}";
    rtf += "\\viewkind4\\uc1";
    rtf += "\\pard\\b\\fs26\\cf4 " + WideToRtf(L"【SQLite3MC 数据库与安全机制】") + "\\b0\\fs22\\par\\par";
    rtf += "\\cf3 " + WideToRtf(L"SQLite3 Multiple Ciphers 支持 AES-256-CBC, AES-256-GCM 等工业级高强度加密。") + "\\par";
    rtf += WideToRtf(L"数据库核心表结构定义如下：") + "\\par\\par";

    rtf += "\\trowd\\trgaph108\\trleft360";
    rtf += "\\clbrdrt\\brdrs\\brdrw15\\brdrcf2";
    rtf += "\\clbrdrb\\brdrs\\brdrw15\\brdrcf2";
    rtf += "\\clbrdrl\\brdrs\\brdrw40\\brdrcf10";
    rtf += "\\clbrdrr\\brdrs\\brdrw15\\brdrcf2";
    rtf += "\\clcbpat1\\cellx8600\n";
    rtf += "\\pard\\intbl\\sl240\\slmult1\\sb0\\sa0\\f1\\fs19\\cf3 ";
    rtf += "\\cf7 " + WideToRtf(L"-- 节点元数据表") + "\\par";
    rtf += "\\cf4 CREATE TABLE IF NOT EXISTS\\cf3  nodes (\\par";
    rtf += "    id            \\cf4 INTEGER PRIMARY KEY AUTOINCREMENT\\cf3 ,\\par";
    rtf += "    parent_id     \\cf4 INTEGER NOT NULL DEFAULT\\cf3  0,\\par";
    rtf += "    sequence      \\cf4 INTEGER NOT NULL DEFAULT\\cf3  0,\\par";
    rtf += "    title         \\cf4 TEXT NOT NULL\\cf3 ,\\par";
    rtf += "    node_type     \\cf4 INTEGER NOT NULL DEFAULT\\cf3  0,\\par";
    rtf += "    created_time  \\cf4 INTEGER NOT NULL\\cf3 ,\\par";
    rtf += "    modified_time \\cf4 INTEGER NOT NULL\\cf3 \\par";
    rtf += ");\\par\\par";
    rtf += "\\cf7 " + WideToRtf(L"-- 正文 RTF 字节流存储表") + "\\par";
    rtf += "\\cf4 CREATE TABLE IF NOT EXISTS\\cf3  node_contents (\\par";
    rtf += "    node_id       \\cf4 INTEGER PRIMARY KEY\\cf3 ,\\par";
    rtf += "    format_type   \\cf4 INTEGER NOT NULL DEFAULT\\cf3  1,\\par";
    rtf += "    content_rtf   \\cf4 BLOB\\cf3 ,\\par";
    rtf += "    \\cf4 FOREIGN KEY\\cf3 (node_id) \\cf4 REFERENCES\\cf3  nodes(id) \\cf4 ON DELETE CASCADE\\cf3 \\par";
    rtf += ");\\cell\\row\n";
    rtf += "\\pard\\f0\\fs22\\par\\cf3 " + WideToRtf(L"全库加密时通过 sqlite3_key / sqlite3_rekey 即可实现透明加解密。") + "\\par";
    rtf += "}";
    return rtf;
}

std::string BuildDefaultTodoRtf() {
    std::string rtf;
    rtf += "{\\rtf1\\ansi\\deff0\\nouicompat";
    rtf += "{\\fonttbl{\\f0\\fnil\\fcharset134 Segoe UI;}}";
    rtf += "{\\colortbl ;\\red0\\green92\\blue197;\\red36\\green41\\blue47;}";
    rtf += "\\viewkind4\\uc1";
    rtf += "\\pard\\b\\fs26\\cf1 " + WideToRtf(L"【今日计划与待办事项】") + "\\b0\\fs22\\par\\par";
    rtf += "\\cf2 " + WideToRtf(L"[√] 搭建原生 Win32 + C++20 工程骨架") + "\\par";
    rtf += WideToRtf(L"[√] 集成 RichEdit 5.0 富文本与多语言高亮代码框") + "\\par";
    rtf += WideToRtf(L"[√] 集成 1~4 级标题层级系统") + "\\par";
    rtf += WideToRtf(L"[√] 集成 SQLite3MC 数据库持久化存储与 AES-256 加密") + "\\par";
    rtf += WideToRtf(L"[ ] 目录树节点拖拽移动与重排 (Drag & Drop)") + "\\par";
    rtf += WideToRtf(L"[ ] 全文检索与笔记搜索") + "\\par";
    rtf += "}";
    return rtf;
}

} // namespace

NoteRepository::NoteRepository(Database& db)
    : m_db(db) {
}

bool NoteRepository::InitializeSchema() {
    if (!m_db.IsOpen()) return false;

    const char* schemaSql = R"(
        CREATE TABLE IF NOT EXISTS notebook_meta (
            key   TEXT PRIMARY KEY,
            value TEXT
        );

        CREATE TABLE IF NOT EXISTS nodes (
            id            INTEGER PRIMARY KEY AUTOINCREMENT,
            parent_id     INTEGER NOT NULL DEFAULT 0,
            sequence      INTEGER NOT NULL DEFAULT 0,
            title         TEXT NOT NULL,
            node_type     INTEGER NOT NULL DEFAULT 0,
            created_time  INTEGER NOT NULL,
            modified_time INTEGER NOT NULL
        );

        CREATE INDEX IF NOT EXISTS idx_nodes_parent_seq ON nodes(parent_id, sequence);

        CREATE TABLE IF NOT EXISTS node_contents (
            node_id       INTEGER PRIMARY KEY,
            format_type   INTEGER NOT NULL DEFAULT 1,
            content_rtf   BLOB,
            FOREIGN KEY(node_id) REFERENCES nodes(id) ON DELETE CASCADE
        );
    )";

    return m_db.Execute(schemaSql);
}

int64_t NoteRepository::GetNodeCount() {
    Database::Statement stmt;
    if (!stmt.Prepare(m_db, "SELECT count(*) FROM nodes;")) {
        return 0;
    }
    if (stmt.Step() == SQLITE_ROW) {
        return stmt.GetInt64(0);
    }
    return 0;
}

std::vector<NoteNode> NoteRepository::GetAllNodes() {
    std::vector<NoteNode> list;
    Database::Statement stmt;
    if (!stmt.Prepare(m_db, "SELECT id, parent_id, sequence, title, node_type, created_time, modified_time FROM nodes ORDER BY parent_id ASC, sequence ASC, id ASC;")) {
        return list;
    }

    while (stmt.Step() == SQLITE_ROW) {
        NoteNode node;
        node.id = stmt.GetInt64(0);
        node.parentId = stmt.GetInt64(1);
        node.sequence = stmt.GetInt(2);
        node.title = stmt.GetWideText(3);
        node.nodeType = stmt.GetInt(4);
        node.createdTime = stmt.GetInt64(5);
        node.modifiedTime = stmt.GetInt64(6);
        list.push_back(std::move(node));
    }
    return list;
}

std::optional<NoteNode> NoteRepository::GetNode(int64_t id) {
    Database::Statement stmt;
    if (!stmt.Prepare(m_db, "SELECT id, parent_id, sequence, title, node_type, created_time, modified_time FROM nodes WHERE id = ?;")) {
        return std::nullopt;
    }
    stmt.BindInt64(1, id);
    if (stmt.Step() == SQLITE_ROW) {
        NoteNode node;
        node.id = stmt.GetInt64(0);
        node.parentId = stmt.GetInt64(1);
        node.sequence = stmt.GetInt(2);
        node.title = stmt.GetWideText(3);
        node.nodeType = stmt.GetInt(4);
        node.createdTime = stmt.GetInt64(5);
        node.modifiedTime = stmt.GetInt64(6);
        return node;
    }
    return std::nullopt;
}

std::string NoteRepository::GetNoteContent(int64_t nodeId) {
    Database::Statement stmt;
    if (!stmt.Prepare(m_db, "SELECT content_rtf FROM node_contents WHERE node_id = ?;")) {
        return {};
    }
    stmt.BindInt64(1, nodeId);
    if (stmt.Step() == SQLITE_ROW) {
        return stmt.GetBlob(0);
    }
    return {};
}

int64_t NoteRepository::CreateNote(int64_t parentId, const std::wstring& title, int sequence, int nodeType, const std::string& initialRtf) {
    if (!m_db.IsOpen()) return 0;

    if (sequence < 0) {
        Database::Statement seqStmt;
        if (seqStmt.Prepare(m_db, "SELECT COALESCE(MAX(sequence), -1) + 1 FROM nodes WHERE parent_id = ?;")) {
            seqStmt.BindInt64(1, parentId);
            if (seqStmt.Step() == SQLITE_ROW) {
                sequence = seqStmt.GetInt(0);
            } else {
                sequence = 0;
            }
        } else {
            sequence = 0;
        }
    }

    int64_t now = GetCurrentUnixTimestamp();

    m_db.BeginTransaction();

    Database::Statement insStmt;
    if (!insStmt.Prepare(m_db, "INSERT INTO nodes (parent_id, sequence, title, node_type, created_time, modified_time) VALUES (?, ?, ?, ?, ?, ?);")) {
        m_db.Rollback();
        return 0;
    }

    insStmt.BindInt64(1, parentId);
    insStmt.BindInt(2, sequence);
    insStmt.BindText(3, title);
    insStmt.BindInt(4, nodeType);
    insStmt.BindInt64(5, now);
    insStmt.BindInt64(6, now);

    if (insStmt.Step() != SQLITE_DONE) {
        m_db.Rollback();
        return 0;
    }

    int64_t newId = sqlite3_last_insert_rowid(m_db.GetRawHandle());

    Database::Statement cntStmt;
    if (!cntStmt.Prepare(m_db, "INSERT INTO node_contents (node_id, format_type, content_rtf) VALUES (?, 1, ?);")) {
        m_db.Rollback();
        return 0;
    }

    cntStmt.BindInt64(1, newId);
    if (!initialRtf.empty()) {
        cntStmt.BindBlob(2, initialRtf.data(), initialRtf.size());
    } else {
        cntStmt.BindNull(2);
    }

    if (cntStmt.Step() != SQLITE_DONE) {
        m_db.Rollback();
        return 0;
    }

    m_db.Commit();
    return newId;
}

bool NoteRepository::UpdateNoteTitle(int64_t nodeId, const std::wstring& title) {
    if (!m_db.IsOpen()) return false;

    int64_t now = GetCurrentUnixTimestamp();
    Database::Statement stmt;
    if (!stmt.Prepare(m_db, "UPDATE nodes SET title = ?, modified_time = ? WHERE id = ?;")) {
        return false;
    }
    stmt.BindText(1, title);
    stmt.BindInt64(2, now);
    stmt.BindInt64(3, nodeId);
    return stmt.Step() == SQLITE_DONE;
}

bool NoteRepository::UpdateNoteContent(int64_t nodeId, const std::string& rtfContent) {
    if (!m_db.IsOpen()) return false;

    int64_t now = GetCurrentUnixTimestamp();
    m_db.BeginTransaction();

    Database::Statement cntStmt;
    if (!cntStmt.Prepare(m_db, "INSERT INTO node_contents (node_id, format_type, content_rtf) VALUES (?, 1, ?) ON CONFLICT(node_id) DO UPDATE SET content_rtf = excluded.content_rtf;")) {
        m_db.Rollback();
        return false;
    }

    cntStmt.BindInt64(1, nodeId);
    if (!rtfContent.empty()) {
        cntStmt.BindBlob(2, rtfContent.data(), rtfContent.size());
    } else {
        cntStmt.BindNull(2);
    }

    if (cntStmt.Step() != SQLITE_DONE) {
        m_db.Rollback();
        return false;
    }

    Database::Statement nodeStmt;
    if (nodeStmt.Prepare(m_db, "UPDATE nodes SET modified_time = ? WHERE id = ?;")) {
        nodeStmt.BindInt64(1, now);
        nodeStmt.BindInt64(2, nodeId);
        nodeStmt.Step();
    }

    m_db.Commit();
    return true;
}

bool NoteRepository::UpdateNodeHierarchy(int64_t nodeId, int64_t newParentId, int newSequence) {
    if (!m_db.IsOpen()) return false;

    int64_t now = GetCurrentUnixTimestamp();
    Database::Statement stmt;
    if (!stmt.Prepare(m_db, "UPDATE nodes SET parent_id = ?, sequence = ?, modified_time = ? WHERE id = ?;")) {
        return false;
    }
    stmt.BindInt64(1, newParentId);
    stmt.BindInt(2, newSequence);
    stmt.BindInt64(3, now);
    stmt.BindInt64(4, nodeId);
    return stmt.Step() == SQLITE_DONE;
}

bool NoteRepository::DeleteNote(int64_t nodeId) {
    if (!m_db.IsOpen()) return false;

    m_db.BeginTransaction();

    // 递归删除所有后代正文
    Database::Statement cntStmt;
    if (cntStmt.Prepare(m_db,
        "WITH RECURSIVE to_delete AS ("
        "  SELECT ? AS id UNION ALL SELECT n.id FROM nodes n JOIN to_delete d ON n.parent_id = d.id"
        ") DELETE FROM node_contents WHERE node_id IN (SELECT id FROM to_delete);")) {
        cntStmt.BindInt64(1, nodeId);
        cntStmt.Step();
    }

    // 递归删除所有后代节点
    Database::Statement nodeStmt;
    if (nodeStmt.Prepare(m_db,
        "WITH RECURSIVE to_delete AS ("
        "  SELECT ? AS id UNION ALL SELECT n.id FROM nodes n JOIN to_delete d ON n.parent_id = d.id"
        ") DELETE FROM nodes WHERE id IN (SELECT id FROM to_delete);")) {
        nodeStmt.BindInt64(1, nodeId);
        if (nodeStmt.Step() != SQLITE_DONE) {
            m_db.Rollback();
            return false;
        }
    }

    m_db.Commit();
    return true;
}

bool NoteRepository::CreateDefaultWelcomeNotes() {
    if (!m_db.IsOpen()) return false;

    int64_t root1 = CreateNote(0, L"📌 欢迎使用 AnyNote", 0, 0, BuildDefaultWelcomeRtf());
    if (root1 > 0) {
        CreateNote(root1, L"🚀 快速上手指南", 0, 0, BuildDefaultQuickStartRtf());
        CreateNote(root1, L"💡 特性与快捷键说明", 1, 0, BuildDefaultFeaturesRtf());
    }

    int64_t root2 = CreateNote(0, L"💻 开发与技术积累", 1, 0, "");
    if (root2 > 0) {
        CreateNote(root2, L"📘 现代 C++ 与 Win32 架构", 0, 0, BuildDefaultCppRtf());
        CreateNote(root2, L"🔒 SQLite3MC 数据库与安全", 1, 0, BuildDefaultSqliteRtf());
        CreateNote(root2, L"🎨 RichEdit 富文本与代码块", 2, 0, BuildDefaultWelcomeRtf());
    }

    int64_t root3 = CreateNote(0, L"📝 随手记 / 待办", 2, 0, "");
    if (root3 > 0) {
        CreateNote(root3, L"计划清单", 0, 0, BuildDefaultTodoRtf());
    }

    return true;
}

bool NoteRepository::SetMeta(const std::string& key, const std::string& value) {
    if (!m_db.IsOpen()) return false;

    Database::Statement stmt;
    if (!stmt.Prepare(m_db, "INSERT INTO notebook_meta (key, value) VALUES (?, ?) ON CONFLICT(key) DO UPDATE SET value = excluded.value;")) {
        return false;
    }
    stmt.BindText(1, key);
    stmt.BindText(2, value);
    return stmt.Step() == SQLITE_DONE;
}

std::string NoteRepository::GetMeta(const std::string& key, const std::string& defaultValue) {
    if (!m_db.IsOpen()) return defaultValue;

    Database::Statement stmt;
    if (!stmt.Prepare(m_db, "SELECT value FROM notebook_meta WHERE key = ?;")) {
        return defaultValue;
    }
    stmt.BindText(1, key);
    if (stmt.Step() == SQLITE_ROW) {
        return stmt.GetText(0);
    }
    return defaultValue;
}

} // namespace anynote::storage
