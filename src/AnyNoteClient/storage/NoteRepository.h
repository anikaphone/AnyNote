#pragma once

#include "Database.h"
#include "NoteModel.h"
#include <vector>
#include <optional>
#include <string>
#include <cstdint>

namespace anynote::storage {

class NoteRepository {
public:
    explicit NoteRepository(Database& db);
    ~NoteRepository() = default;

    // 初始化表结构与索引
    bool InitializeSchema();

    // 统计节点总数
    int64_t GetNodeCount();

    // 获取所有树节点（按 parent_id, sequence, id 排序）
    std::vector<NoteNode> GetAllNodes();

    // 获取单个节点信息
    std::optional<NoteNode> GetNode(int64_t id);

    // 获取笔记 RTF 正文与纯文本
    std::string GetNoteContent(int64_t nodeId);
    std::wstring GetNotePlainText(int64_t nodeId);

    // 创建笔记节点 (若 sequence 为 -1 则自动排在同级末尾)
    int64_t CreateNote(int64_t parentId, const std::wstring& title, int sequence = -1, int nodeType = 0, const std::string& initialRtf = "", const std::wstring& initialPlainText = L"");

    // 更新笔记标题
    bool UpdateNoteTitle(int64_t nodeId, const std::wstring& title);

    // 更新笔记正文 (RTF 格式与纯文本)
    bool UpdateNoteContent(int64_t nodeId, const std::string& rtfContent, const std::wstring& plainText = L"");

    // 全库搜索笔记 (支持标题与正文搜索、大小写匹配选项)
    std::vector<SearchResult> SearchNotes(const std::wstring& keyword, bool matchCase = false, bool searchContent = true);

    // 更新节点层级与顺序
    bool UpdateNodeHierarchy(int64_t nodeId, int64_t newParentId, int newSequence);

    // 移动与重排节点 (拖拽支持：支持 AsChild, Before, After, AtRootEnd)
    bool MoveNode(int64_t dragNodeId, int64_t targetNodeId, DropPosition position);
    bool IsDescendantOf(int64_t checkId, int64_t ancestorId);
    bool ReorderSiblings(int64_t parentId);

    // 删除笔记 (递归级联删除该节点及其所有子孙节点与内容)
    bool DeleteNote(int64_t nodeId);

    // 生成默认初始示例笔记 (用于新创建或空白笔记本)
    bool CreateDefaultWelcomeNotes();

    // 笔记本元数据操作
    bool SetMeta(const std::string& key, const std::string& value);
    std::string GetMeta(const std::string& key, const std::string& defaultValue = "");

private:
    Database& m_db;
};

} // namespace anynote::storage
