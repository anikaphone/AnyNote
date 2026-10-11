cmake_minimum_required(VERSION 3.20)
project(AnyNote LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 确保 MSVC 正确解析 UTF-8 编码，并禁用 min/max 宏冲突
if (MSVC)
    # Let MSVC compile independent translation units concurrently. This is
    # especially useful for the client, which has many UI/storage sources.
    add_compile_options(/utf-8 /permissive- /MP)
    set(CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS} /c65001")
endif()
add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)

# 成品可执行程序输出到 build/bin，中间库与归档文件输出到 build/temp/lib
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/bin")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}/build/bin/Debug")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/build/bin/Release")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/temp/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}/build/temp/lib/Debug")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/build/temp/lib/Release")
set(CMAKE_PDB_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/bin")
set(CMAKE_PDB_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}/build/bin/Debug")
set(CMAKE_PDB_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/build/bin/Release")

# 寻找 sqlite3mc (vcpkg 静态库配置)
find_package(sqlite3mc CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(CURL CONFIG REQUIRED)

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

add_custom_command(TARGET AnyNote POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:AnyNote>"
        "${CMAKE_SOURCE_DIR}/build/bin/$<TARGET_FILE_NAME:AnyNote>"
    COMMENT "Copying executable to build/bin"
)

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

# 启用预编译头 (PCH)，大幅减少 Windows SDK、STL 及大型三方库 (nlohmann_json/sqlite) 的重复解析耗时
target_precompile_headers(AnyNote PRIVATE
    <windows.h>
    <windowsx.h>
    <commctrl.h>
    <richedit.h>
    <ole2.h>
    <gdiplus.h>
    <shlwapi.h>
    <uxtheme.h>
    <string>
    <string_view>
    <vector>
    <memory>
    <unordered_map>
    <map>
    <algorithm>
    <chrono>
    <filesystem>
    <optional>
    <functional>
    <sqlite3.h>
    <nlohmann/json.hpp>
)

# 单元测试目标
add_executable(test_storage EXCLUDE_FROM_ALL
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
add_executable(test_codeblock_hover EXCLUDE_FROM_ALL
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
add_executable(test_table_editing EXCLUDE_FROM_ALL
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
add_executable(test_node_search EXCLUDE_FROM_ALL tests/test_node_search.cpp ${NODE_SEARCH_TEST_SOURCES})
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
add_executable(test_outline EXCLUDE_FROM_ALL
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

# P0 synchronization probes are independent of user vaults and UI state.
add_library(anynote_sync_protocol STATIC
    src/AnyNoteClient/sync/SyncProtocol.cpp
    src/AnyNoteClient/sync/DiffEngine.cpp
    src/AnyNoteClient/sync/RequestIdentity.cpp
    src/AnyNoteClient/common/Identity.cpp
)
target_include_directories(anynote_sync_protocol PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/AnyNoteClient)
target_link_libraries(anynote_sync_protocol PUBLIC nlohmann_json::nlohmann_json bcrypt crypt32)
add_executable(test_sync_protocol EXCLUDE_FROM_ALL tests/test_sync_protocol.cpp)
target_link_libraries(test_sync_protocol PRIVATE anynote_sync_protocol psapi)
add_executable(test_sync_storage EXCLUDE_FROM_ALL tests/test_sync_storage.cpp src/AnyNoteClient/storage/Database.cpp)
target_include_directories(test_sync_storage PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/AnyNoteClient)
target_link_libraries(test_sync_storage PRIVATE sqlite3mc::sqlite3mc_static)
add_executable(test_sync_tls EXCLUDE_FROM_ALL tests/test_sync_tls.cpp)
target_link_libraries(test_sync_tls PRIVATE CURL::libcurl nlohmann_json::nlohmann_json)
foreach(target anynote_sync_protocol test_sync_protocol test_sync_storage test_sync_tls)
    set_property(TARGET ${target} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endforeach()


add_executable(test_sync_migration EXCLUDE_FROM_ALL tests/test_sync_migration.cpp
    src/AnyNoteClient/storage/Database.cpp
    src/AnyNoteClient/storage/NoteRepository.cpp
    src/AnyNoteClient/storage/SchemaMigrator.cpp
    src/AnyNoteClient/storage/VaultFileLease.cpp
    src/AnyNoteClient/common/SyntaxHighlighter.cpp)
target_link_libraries(test_sync_migration PRIVATE anynote_sync_protocol sqlite3mc::sqlite3mc_static)
set_property(TARGET test_sync_migration PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

foreach(target AnyNote test_storage test_node_search test_sync_storage)
    target_sources(${target} PRIVATE
        src/AnyNoteClient/storage/SchemaMigrator.cpp
        src/AnyNoteClient/storage/VaultFileLease.cpp)
    target_link_libraries(${target} PRIVATE anynote_sync_protocol)
endforeach()

foreach(target AnyNote test_storage test_node_search)
    target_sources(${target} PRIVATE src/AnyNoteClient/storage/ConfigStore.cpp)
endforeach()

add_executable(test_sync_config EXCLUDE_FROM_ALL tests/test_sync_config.cpp
    src/AnyNoteClient/storage/VaultManager.cpp
    src/AnyNoteClient/storage/ConfigStore.cpp
    src/AnyNoteClient/storage/VaultFileLease.cpp)
target_link_libraries(test_sync_config PRIVATE anynote_sync_protocol)
set_property(TARGET test_sync_config PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

foreach(target AnyNote test_storage test_node_search test_sync_migration)
    target_sources(${target} PRIVATE src/AnyNoteClient/storage/SnapshotService.cpp)
endforeach()
add_executable(test_sync_snapshot EXCLUDE_FROM_ALL tests/test_sync_snapshot.cpp
    src/AnyNoteClient/storage/Database.cpp
    src/AnyNoteClient/storage/NoteRepository.cpp
    src/AnyNoteClient/storage/SchemaMigrator.cpp
    src/AnyNoteClient/storage/VaultFileLease.cpp
    src/AnyNoteClient/storage/SnapshotService.cpp
    src/AnyNoteClient/common/SyntaxHighlighter.cpp)
target_link_libraries(test_sync_snapshot PRIVATE anynote_sync_protocol sqlite3mc::sqlite3mc_static)
set_property(TARGET test_sync_snapshot PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
foreach(target AnyNote test_storage test_node_search)
    target_sources(${target} PRIVATE src/AnyNoteClient/storage/VaultInstaller.cpp)
endforeach()
target_sources(test_sync_snapshot PRIVATE
    src/AnyNoteClient/storage/VaultInstaller.cpp
    src/AnyNoteClient/storage/VaultManager.cpp
    src/AnyNoteClient/storage/ConfigStore.cpp)

add_library(anynote_sync_client STATIC
    src/AnyNoteClient/sync/SyncClient.cpp
    src/AnyNoteClient/sync/SyncCoordinator.cpp
    src/AnyNoteClient/sync/CredentialStore.cpp)
target_link_libraries(anynote_sync_client PUBLIC anynote_sync_protocol CURL::libcurl)
set_property(TARGET anynote_sync_client PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
target_link_libraries(AnyNote PRIVATE anynote_sync_client)
add_executable(test_sync_client EXCLUDE_FROM_ALL tests/test_sync_client.cpp
    src/AnyNoteClient/storage/ConfigStore.cpp
    src/AnyNoteClient/storage/VaultFileLease.cpp)
target_link_libraries(test_sync_client PRIVATE anynote_sync_client psapi)
set_property(TARGET test_sync_client PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
foreach(target AnyNote test_node_search)
    target_sources(${target} PRIVATE
        src/AnyNoteClient/ui/MainWindowRealtime.cpp
        src/AnyNoteClient/storage/RealtimeStore.cpp
        src/AnyNoteClient/sync/RealtimeSync.cpp
        src/AnyNoteClient/ui/SyncDialogs.cpp
        src/AnyNoteClient/ui/CloudSyncAccount.cpp
        src/AnyNoteClient/ui/CloudSyncOperations.cpp
        src/AnyNoteClient/ui/CloudSyncManage.cpp)
    target_link_libraries(${target} PRIVATE anynote_sync_client)
endforeach()

get_target_property(SYNC_E2E_SOURCES test_sync_snapshot SOURCES)
list(REMOVE_ITEM SYNC_E2E_SOURCES tests/test_sync_snapshot.cpp)
add_executable(test_sync_e2e EXCLUDE_FROM_ALL tests/test_sync_e2e.cpp ${SYNC_E2E_SOURCES})
target_link_libraries(test_sync_e2e PRIVATE anynote_sync_client sqlite3mc::sqlite3mc_static)
set_property(TARGET test_sync_e2e PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_executable(test_realtime_sync EXCLUDE_FROM_ALL tests/test_realtime_sync.cpp ${SYNC_E2E_SOURCES}
    src/AnyNoteClient/storage/RealtimeStore.cpp src/AnyNoteClient/sync/RealtimeSync.cpp)
target_link_libraries(test_realtime_sync PRIVATE anynote_sync_client sqlite3mc::sqlite3mc_static)
set_property(TARGET test_realtime_sync PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
