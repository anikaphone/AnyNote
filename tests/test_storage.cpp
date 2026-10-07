#include "storage/Database.h"
#include "storage/NoteRepository.h"
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

        db.Close();
    }

    if (std::filesystem::exists(dbPath)) {
        std::filesystem::remove(dbPath);
    }

    std::cout << "[TEST] ALL PERSISTENCE AND ENCRYPTION TESTS PASSED SUCCESSFULLY!" << std::endl;
    return 0;
}
