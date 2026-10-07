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
    src/main.cpp
    src/common/Window.cpp
    src/common/Window.h
    src/common/StringUtils.h
    src/common/SyntaxHighlighter.cpp
    src/common/SyntaxHighlighter.h
    src/ui/Splitter.cpp
    src/ui/Splitter.h
    src/ui/TreeView.cpp
    src/ui/TreeView.h
    src/ui/RichEditView.cpp
    src/ui/RichEditView.h
    src/ui/Toolbar.cpp
    src/ui/Toolbar.h
    src/ui/FindReplaceDialog.cpp
    src/ui/FindReplaceDialog.h
    src/ui/SearchPane.cpp
    src/ui/SearchPane.h
    src/ui/MainWindow.cpp
    src/ui/MainWindow.h
    src/storage/Database.cpp
    src/storage/Database.h
    src/storage/VaultManager.cpp
    src/storage/VaultManager.h
    src/storage/NoteModel.h
    src/storage/NoteRepository.cpp
    src/storage/NoteRepository.h
    res/AnyNote.rc
    res/app.manifest
    res/resource.h
)

add_executable(AnyNote WIN32 ${SOURCES})

target_include_directories(AnyNote PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/res
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
    src/storage/Database.cpp
    src/storage/Database.h
    src/storage/VaultManager.cpp
    src/storage/VaultManager.h
    src/storage/NoteModel.h
    src/storage/NoteRepository.cpp
    src/storage/NoteRepository.h
)
target_include_directories(test_storage PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(test_storage PRIVATE
    sqlite3mc::sqlite3mc_static
    shlwapi
)
set_property(TARGET test_storage PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)

