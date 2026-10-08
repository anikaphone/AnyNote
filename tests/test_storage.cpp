#include "storage/Database.h"
#include "storage/NoteRepository.h"
#include "storage/VaultManager.h"
#include "common/SyntaxHighlighter.h"
#include <iostream>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <filesystem>

using namespace anynote::storage;

int main() {
    std::wstring dbPath = L"test_notebook.anynote";
    if (std::filesystem::exists(dbPath)) {
        std::filesystem::remove(dbPath);
    }

    std::cout << "[TEST] 1. Creating database and initializing schema..." << std::endl;
    {
        Database db;
        assert(db.Open(dbPath, ""));
        assert(db.IsOpen());
        assert(!db.IsEncrypted());

        NoteRepository repo(db);
        assert(repo.InitializeSchema());
        assert(repo.GetNodeCount() == 0);

        // 创建根节点
        int64_t rootId = repo.CreateNote(0, L"我的根笔记", 0, 0, "{\\rtf1\\ansi Hello Root Note}");
        assert(rootId > 0);
        assert(repo.GetNodeCount() == 1);

        // 创建子节点
        int64_t childId = repo.CreateNote(rootId, L"子笔记 1", 0, 0, "{\\rtf1\\ansi Hello Child Note}");
        assert(childId > 0);
        assert(repo.GetNodeCount() == 2);

        // 读取正文验证
        std::string rootRtf = repo.GetNoteContent(rootId);
        assert(rootRtf == "{\\rtf1\\ansi Hello Root Note}");

        std::string childRtf = repo.GetNoteContent(childId);
        assert(childRtf == "{\\rtf1\\ansi Hello Child Note}");

        // 修改正文与标题
        assert(repo.UpdateNoteTitle(childId, L"重命名后的子笔记"));
        assert(repo.UpdateNoteContent(childId, "{\\rtf1\\ansi Updated Content}"));

        auto updatedChild = repo.GetNode(childId);
        assert(updatedChild.has_value());
        assert(updatedChild->title == L"重命名后的子笔记");
        assert(repo.GetNoteContent(childId) == "{\\rtf1\\ansi Updated Content}");

        // 设置 AES-256 加密密码
        std::cout << "[TEST] 2. Encrypting database with password 'secret123'..." << std::endl;
        bool encrypted = db.SetPassword("secret123");
        if (!encrypted) {
            std::cerr << "[TEST] SetPassword failed: " << db.GetLastError() << std::endl;
        }
        assert(encrypted);
        assert(db.IsEncrypted());

        db.Close();
    }

    std::cout << "[TEST] 3. Testing reopening encrypted database without password..." << std::endl;
    {
        Database db;
        bool res = db.Open(dbPath, "");
        assert(!res);
        assert(db.GetLastError() == "ENCRYPTED_REQUIRES_PASSWORD");
    }

    std::cout << "[TEST] 4. Testing reopening encrypted database with wrong password..." << std::endl;
    {
        Database db;
        bool res = db.Open(dbPath, "wrong_pass");
        assert(!res);
    }

    std::cout << "[TEST] 5. Testing reopening encrypted database with correct password..." << std::endl;
    {
        Database db;
        bool res = db.Open(dbPath, "secret123");
        assert(res);
        assert(db.IsOpen());
        assert(db.IsEncrypted());

        NoteRepository repo(db);
        assert(repo.GetNodeCount() == 2);

        auto nodes = repo.GetAllNodes();
        assert(nodes.size() == 2);
        assert(nodes[0].title == L"我的根笔记");
        assert(nodes[1].title == L"重命名后的子笔记");
        assert(repo.GetNoteContent(nodes[1].id) == "{\\rtf1\\ansi Updated Content}");

        // 测试级联递归删除
        std::cout << "[TEST] 6. Testing recursive cascade deletion..." << std::endl;
        assert(repo.DeleteNote(nodes[0].id)); // 删除根节点，子节点应一并被级联清理
        assert(repo.GetNodeCount() == 0);
        assert(repo.GetNoteContent(nodes[1].id).empty());

        // 解除加密为明文
        std::cout << "[TEST] 7. Testing decrypting database back to plaintext..." << std::endl;
        assert(db.SetPassword(""));
        assert(!db.IsEncrypted());

        db.Close();
    }

    std::cout << "[TEST] 8. Reopening decrypted database without password..." << std::endl;
    {
        Database db;
        assert(db.Open(dbPath, ""));
        assert(db.IsOpen());
        assert(!db.IsEncrypted());

        NoteRepository repo(db);

        // 创建测试树结构：
        // Root A (seq 0)
        //   - Child A1 (seq 0)
        //   - Child A2 (seq 1)
        // Root B (seq 1)
        std::cout << "[TEST] 9. Testing MoveNode (Drag and Drop operations)..." << std::endl;
        int64_t rootA = repo.CreateNote(0, L"Root A", 0);
        int64_t childA1 = repo.CreateNote(rootA, L"Child A1", 0);
        int64_t childA2 = repo.CreateNote(rootA, L"Child A2", 1);
        int64_t rootB = repo.CreateNote(0, L"Root B", 1);

        // 9.1 测试 Reorder: 将 Child A2 移动到 Child A1 之前 (Before)
        assert(repo.MoveNode(childA2, childA1, DropPosition::Before));
        auto nodeA2 = repo.GetNode(childA2);
        auto nodeA1 = repo.GetNode(childA1);
        assert(nodeA2->parentId == rootA && nodeA2->sequence == 0);
        assert(nodeA1->parentId == rootA && nodeA1->sequence == 1);

        // 9.2 测试 AsChild: 将 Child A1 移动到 Root B 之下作为子节点
        assert(repo.MoveNode(childA1, rootB, DropPosition::AsChild));
        nodeA1 = repo.GetNode(childA1);
        assert(nodeA1->parentId == rootB && nodeA1->sequence == 0);

        // 9.3 测试 AtRootEnd: 将 Child A2 移动到根节点末尾
        assert(repo.MoveNode(childA2, 0, DropPosition::AtRootEnd));
        nodeA2 = repo.GetNode(childA2);
        assert(nodeA2->parentId == 0);

        // 9.4 测试循环依赖防护: 不能将 Root B 移动到自己的子节点 Child A1 之下
        assert(!repo.MoveNode(rootB, childA1, DropPosition::AsChild));

        // 10. 测试 SearchNotes (全库搜索: 标题与正文模糊搜索)
        std::cout << "[TEST] 10. Testing SearchNotes..." << std::endl;
        int64_t searchNote1 = repo.CreateNote(0, L"C++ 性能优化专题", 2, 0, "", L"关于 MSVC 现代 C++20 与原生 Win32 的极致响应速度。");
        int64_t searchNote2 = repo.CreateNote(0, L"读书笔记", 3, 0, "", L"这是一篇普通的读书笔记，记录了哲学与生活。");

        // 搜索标题
        auto resTitle = repo.SearchNotes(L"性能优化", false, true);
        assert(!resTitle.empty());
        assert(resTitle[0].nodeId == searchNote1);
        assert(resTitle[0].matchInTitle);

        // 搜索正文
        auto resContent = repo.SearchNotes(L"Win32", false, true);
        assert(!resContent.empty());
        bool foundWin32 = false;
        for (const auto& r : resContent) {
            if (r.nodeId == searchNote1 && !r.matchInTitle) {
                foundWin32 = true;
                assert(!r.snippet.empty());
            }
        }
        assert(foundWin32);

        // 大小写敏感搜索测试
        auto resCaseSensitive1 = repo.SearchNotes(L"win32", true, true);
        assert(resCaseSensitive1.empty()); // 正文是大写 Win32，大小写敏感应搜不到
        auto resCaseSensitive2 = repo.SearchNotes(L"Win32", true, true);
        assert(!resCaseSensitive2.empty()); // 大小写匹配

        // 仅搜索标题测试 (searchContent = false)
        auto resTitleOnly = repo.SearchNotes(L"Win32", false, false);
        assert(resTitleOnly.empty()); // Win32 仅在正文中，不应命中

        // 更新正文纯文本并搜索验证
        repo.UpdateNoteContent(searchNote2, "fake-rtf", L"更新后的读书笔记正文，包含了架构模式与重构实战。");
        auto resUpdated = repo.SearchNotes(L"重构实战", false, true);
        assert(!resUpdated.empty());
        assert(resUpdated[0].nodeId == searchNote2);
        assert(!resUpdated[0].matchInTitle);

        // 11. 测试旧笔记 RTF 纯文本自动回填 (RTF Backfill)
        std::cout << "[TEST] 11. Testing RTF backfill and Chinese full-text search..." << std::endl;
        // 创建一个只有 RTF 但没有 plain_text 的笔记，模拟旧版数据
        int64_t oldNoteId = repo.CreateNote(0, L"旧版架构笔记", 4, 0,
            "{\\rtf1\\ansi\\deff0\\nouicompat{\\fonttbl{\\f0\\fnil\\fcharset134 Segoe UI;}}\\viewkind4\\uc1\\pard "
            "\\u21407?\\u29983? Win32 \\u19982? SQLite3MC \\u20840?\\u24211?\\u21152?\\u23494?\\par}");
        // 初始 plainText 为空
        assert(repo.GetNotePlainText(oldNoteId).empty());

        // 重新调用 InitializeSchema 触发自动回填
        assert(repo.InitializeSchema());
        std::wstring backfilled = repo.GetNotePlainText(oldNoteId);
        assert(!backfilled.empty());
        assert(backfilled.find(L"原生") != std::wstring::npos);
        assert(backfilled.find(L"全库加密") != std::wstring::npos);

        // 搜索回填的正文中文
        auto resBackfill = repo.SearchNotes(L"全库加密", false, true);
        assert(!resBackfill.empty());
        assert(resBackfill[0].nodeId == oldNoteId);

        db.Close();
    }

    if (std::filesystem::exists(dbPath)) {
        std::filesystem::remove(dbPath);
    }

    // 12. 测试多库配置管理器 VaultManager (INI 读写与全生命周期)
    std::cout << "[TEST] 12. Testing VaultManager multi-vault operations and INI persistence..." << std::endl;
    const std::filesystem::path vaultTestDir = std::filesystem::temp_directory_path() / L"AnyNoteVaultManagerTest";
    std::error_code vaultTestEc;
    std::filesystem::remove_all(vaultTestDir, vaultTestEc);
    std::filesystem::create_directories(vaultTestDir, vaultTestEc);
    assert(!vaultTestEc);
    {
        std::wstring iniPath = (vaultTestDir / L"anynote.ini").wstring();
        VaultManager mgr(iniPath);

        // 初次加载测试
        assert(mgr.Load());
        assert(mgr.GetVaultCount() >= 1);
        size_t initialCount = mgr.GetVaultCount();

        // 路径生成
        std::wstring newVaultPath = mgr.GenerateNewVaultPath(L"工作规划 2026");
        assert(newVaultPath.find(L"工作规划 2026.anynote") != std::wstring::npos);

        // 添加新库 (带中文字符)
        assert(mgr.AddVault(L"工作规划 2026", newVaultPath));
        assert(mgr.GetVaultCount() == initialCount + 1);

        // 大小写不敏感查找
        int foundIdx = mgr.FindVaultByPath(newVaultPath);
        assert(foundIdx >= 0);

        // 切换激活库
        mgr.SetActiveVaultPath(newVaultPath);
        assert(mgr.GetActiveVaultPath() == newVaultPath);
        assert(mgr.GetActiveVaultDisplayName() == L"工作规划 2026");

        // 重命名库
        assert(mgr.RenameVault(static_cast<size_t>(foundIdx), L"年度工作目标"));
        assert(mgr.GetActiveVaultDisplayName() == L"年度工作目标");

        // 新建另一个测试库
        std::wstring lifeVaultPath = mgr.GenerateNewVaultPath(L"个人随想");
        assert(mgr.AddVault(L"个人随想", lifeVaultPath));
        size_t countBeforeReload = mgr.GetVaultCount();

        // 重新实例化 VaultManager 模拟重启读取 INI 文件
        VaultManager reloadMgr(iniPath);
        assert(reloadMgr.Load());
        assert(reloadMgr.GetVaultCount() == countBeforeReload);
        assert(reloadMgr.GetActiveVaultPath() == newVaultPath);
        assert(reloadMgr.GetActiveVaultDisplayName() == L"年度工作目标");

        int lifeIdx = reloadMgr.FindVaultByPath(lifeVaultPath);
        assert(lifeIdx >= 0);
        assert(reloadMgr.GetVault(static_cast<size_t>(lifeIdx))->name == L"个人随想");

        // 验证通用 INI 配置读取与持久化 (LastCodeLanguage 等)
        assert(reloadMgr.GetConfigString(L"Editor", L"LastCodeLanguage", L"PlainText") == L"PlainText");
        assert(reloadMgr.SetConfigString(L"Editor", L"LastCodeLanguage", L"Python"));
        assert(reloadMgr.GetConfigString(L"Editor", L"LastCodeLanguage", L"PlainText") == L"Python");

        // 重新加载测试配置是否已写入磁盘
        VaultManager reloadedConfigMgr(iniPath);
        assert(reloadedConfigMgr.GetConfigString(L"Editor", L"LastCodeLanguage", L"PlainText") == L"Python");

        // 移除测试库
        assert(reloadMgr.RemoveVault(static_cast<size_t>(lifeIdx)));
        assert(reloadMgr.GetVaultCount() == countBeforeReload - 1);
        assert(reloadMgr.FindVaultByPath(lifeVaultPath) == -1);

    }
    std::filesystem::remove_all(vaultTestDir, vaultTestEc);

    // 13. 验证代码块 RTF 生成纯净性 (绝不能在代码块前后插入任何多余的 \par 空白行)
    std::cout << "[TEST] 13. Testing RTF CodeBlock purity (no stray blank lines before or after)..." << std::endl;
    {
        using namespace anynote::common;
        std::string rtfCpp = SyntaxHighlighter::GenerateRtfCodeBlock(L"int x = 42;\nreturn x;", CodeLanguage::Cpp);
        
        // 验证代码块前绝无多余 \par
        assert(rtfCpp.find("\\par\n\\trowd") == std::string::npos);
        assert(rtfCpp.find("\\par\r\n\\trowd") == std::string::npos);
        assert(rtfCpp.find("\\viewkind4\\uc1\n\\trowd") != std::string::npos);

        // 验证代码块后绝无多余 \par
        assert(rtfCpp.find("\\cell\\row\n}") != std::string::npos);
        assert(rtfCpp.find("\\cell\\row\n\\pard\\f0\\fs22\\par\n}") == std::string::npos);

        // 验证语言元数据标签正确嵌入隐藏标记
        assert(rtfCpp.find("{\\v [lang:Cpp]\\v0}") != std::string::npos);

        // 验证换行符在表格单元格内正确携带 \\intbl 属性
        assert(rtfCpp.find("\\par\\intbl") != std::string::npos);

        // 切换语言生成的 RTF 同样结构纯净
        std::string rtfPy = SyntaxHighlighter::GenerateRtfCodeBlock(L"x = 42\nprint(x)", CodeLanguage::Python);
        assert(rtfPy.find("\\par\n\\trowd") == std::string::npos);
        assert(rtfPy.find("{\\v [lang:Python]\\v0}") != std::string::npos);
    }

    std::cout << "[TEST] ALL PERSISTENCE, ENCRYPTION, MULTI-VAULT, INI CONFIG AND CODEBLOCK TESTS PASSED SUCCESSFULLY!" << std::endl;
    return 0;
}
