#pragma once

#include <string>
#include <cstdint>

namespace anynote::storage {

enum class DropPosition {
    None,
    AsChild,
    Before,
    After,
    AtRootEnd
};

struct NoteNode {
    int64_t id = 0;
    int64_t parentId = 0;
    int sequence = 0;
    std::wstring title;
    int nodeType = 0; // 0: 普通富文本笔记
    int64_t createdTime = 0;
    int64_t modifiedTime = 0;
};

struct NoteContent {
    int64_t nodeId = 0;
    int formatType = 1; // 1: RTF (Rich Text Format)
    std::string contentRtf;
};

} // namespace anynote::storage
