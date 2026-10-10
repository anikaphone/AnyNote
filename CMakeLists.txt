cmake_minimum_required(VERSION 3.20)
project(AnyNote LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 确保 MSVC 正确解析 UTF-8 编码，并禁用 min/max 宏冲突
if (MSVC)
    add_compile_options(/utf-8 /permissive-)
    set(CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS} /c65001")
endif()
add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)


# 寻找 sqlite3mc (vcpkg 静态库配置)
find_package(sqlite3mc CONFIG REQUIRED)

# 源码与资源列表
set(SOURCES
    src/AnyNoteClient/main.cpp
    src/AnyNoteClient/common/Window.cpp
    src/AnyNoteClient/common/Window.h
    src/AnyNoteClient/common/StringUtils.h
    src/AnyNoteClient/common/SyntaxHighlighter.cpp
    src/AnyNoteClient/common/SyntaxHighlighter.h
    src/AnyNoteClient/ui/Splitter.cpp
    src/AnyNoteClient/ui/Splitter.h
    src/AnyNoteClient/ui/TreeView.cpp
    src/AnyNoteClient/ui/TreeView.h
    src/AnyNoteClient/ui/RichEditView.cpp
    src/AnyNoteClient/ui/RichEditView.h
    src/AnyNoteClient/ui/TableEditor.cpp
    src/AnyNoteClient/ui/TableEditor.h
    src/AnyNoteClient/ui/CodeBlockHoverBar.cpp
    src/AnyNoteClient/ui/CodeBlockHoverBar.h
    src/AnyNoteClient/ui/Toolbar.cpp
    src/AnyNoteClient/ui/Toolbar.h
    src/AnyNoteClient/ui/FindReplaceDialog.cpp
    src/AnyNoteClient/ui/FindReplaceDialog.h
    src/AnyNoteClient/ui/SearchPane.cpp
    src/AnyNoteClient/ui/SearchPane.h
    src/AnyNoteClient/ui/NodeSearchBar.cpp
    src/AnyNoteClient/ui/NodeSearchBar.h
    src/AnyNoteClient/ui/OutlinePane.cpp
    src/AnyNoteClient/ui/OutlinePane.h
    src/AnyNoteClient/ui/MainWindow.cpp
    src/AnyNoteClient/ui/MainWindow.h
    src/AnyNoteClient/storage/Database.cpp
    src/AnyNoteClient/storage/Database.h
    src/AnyNoteClient/storage/VaultManager.cpp
    src/AnyNoteClient/storage/VaultManager.h
    src/AnyNoteClient/storage/NoteModel.h
    src/AnyNoteClient/storage/NoteRepository.cpp
    src/AnyNoteClient/storage/NoteRepository.h
    src/AnyNoteClient/res/AnyNote.rc
    src/AnyNoteClient/res/app.manifest
    src/AnyNoteClient/res/resource.h
)

add_executable(AnyNote WIN32 ${SOURCES})

if (MSVC)
    # Keep a Release PDB beside AnyNote.exe so WER dumps can be symbolized.
    target_compile_options(AnyNote PRIVATE $<$<CONFIG:Release>:/Zi>)
    target_link_options(AnyNote PRIVATE $<$<CONFIG:Release>:/DEBUG:FULL>)
    set_property(TARGET AnyNote PROPERTY
        MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Release>:ProgramDatabase>")
endif()

target_include_directories(AnyNote PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/AnyNoteClient
    ${CMAKE_CURRENT_SOURCE_DIR}/src/AnyNoteClient/res
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# 链接 sqlite3mc_static 以及 Windows 基础 UI 系统库
target_link_libraries(AnyNote PRIVATE
    sqlite3mc::sqlite3mc_static
    comctl32
    ole32
    uxtheme
    shlwapi
    gdiplus
)

# 采用静态 C/C++ 运行时 (/MT)，生成零第三方依赖的单一便携式可执行文件
set_property(TARGET AnyNote PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)

# 单元测试目标
add_executable(test_storage
    tests/test_storage.cpp
    src/AnyNoteClient/storage/Database.cpp
    src/AnyNoteClient/storage/Database.h
    src/AnyNoteClient/storage/VaultManager.cpp
    src/AnyNoteClient/storage/VaultManager.h
    src/AnyNoteClient/storage/NoteModel.h
    src/AnyNoteClient/storage/NoteRepository.cpp
    src/AnyNoteClient/storage/NoteRepository.h
    src/AnyNoteClient/common/SyntaxHighlighter.cpp
    src/AnyNoteClient/common/SyntaxHighlighter.h
)
target_include_directories(test_storage PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/AnyNoteClient
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(test_storage PRIVATE
    sqlite3mc::sqlite3mc_static
    shlwapi
)
set_property(TARGET test_storage PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)

# Native editor regression test; uses a scratch window without opening any vault.
add_executable(test_codeblock_hover
    tests/test_codeblock_hover.cpp
    src/AnyNoteClient/ui/RichEditView.cpp
    src/AnyNoteClient/ui/TableEditor.cpp
    src/AnyNoteClient/ui/CodeBlockHoverBar.cpp
    src/AnyNoteClient/common/SyntaxHighlighter.cpp
)
target_include_directories(test_codeblock_hover PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/AnyNoteClient
    ${CMAKE_CURRENT_SOURCE_DIR}/src/AnyNoteClient/res
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(test_codeblock_hover PRIVATE comctl32 ole32 gdiplus)
set_property(TARGET test_codeblock_hover PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)

# Table selection and alignment regression test using a real RichEdit window.
add_executable(test_table_editing
    tests/test_table_editing.cpp
    src/AnyNoteClient/ui/RichEditView.cpp
    src/AnyNoteClient/ui/TableEditor.cpp
    src/AnyNoteClient/ui/CodeBlockHoverBar.cpp
    src/AnyNoteClient/common/SyntaxHighlighter.cpp
)
target_include_directories(test_table_editing PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/AnyNoteClient
    ${CMAKE_CURRENT_SOURCE_DIR}/src/AnyNoteClient/res
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(test_table_editing PRIVATE comctl32 ole32 gdiplus)
set_property(TARGET test_table_editing PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)

# Exercise tree filtering and global navigation through the real window handlers.
set(NODE_SEARCH_TEST_SOURCES ${SOURCES})
list(REMOVE_ITEM NODE_SEARCH_TEST_SOURCES src/AnyNoteClient/main.cpp)
add_executable(test_node_search tests/test_node_search.cpp ${NODE_SEARCH_TEST_SOURCES})
target_include_directories(test_node_search PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/AnyNoteClient
    ${CMAKE_CURRENT_SOURCE_DIR}/src/AnyNoteClient/res
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(test_node_search PRIVATE
    sqlite3mc::sqlite3mc_static comctl32 ole32 uxtheme shlwapi gdiplus
)
set_property(TARGET test_node_search PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)

# Outline view extraction and navigation regression test
add_executable(test_outline
    tests/test_outline.cpp
    src/AnyNoteClient/ui/OutlinePane.cpp
    src/AnyNoteClient/ui/OutlinePane.h
    src/AnyNoteClient/ui/RichEditView.cpp
    src/AnyNoteClient/ui/RichEditView.h
    src/AnyNoteClient/ui/TreeView.cpp
    src/AnyNoteClient/ui/TreeView.h
    src/AnyNoteClient/ui/TableEditor.cpp
    src/AnyNoteClient/ui/TableEditor.h
    src/AnyNoteClient/ui/CodeBlockHoverBar.cpp
    src/AnyNoteClient/ui/CodeBlockHoverBar.h
    src/AnyNoteClient/common/SyntaxHighlighter.cpp
    src/AnyNoteClient/common/SyntaxHighlighter.h
    src/AnyNoteClient/common/Window.cpp
    src/AnyNoteClient/common/Window.h
)
target_include_directories(test_outline PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/AnyNoteClient
    ${CMAKE_CURRENT_SOURCE_DIR}/src/AnyNoteClient/res
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(test_outline PRIVATE
    comctl32 ole32 uxtheme shlwapi gdiplus
)
set_property(TARGET test_outline PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)
