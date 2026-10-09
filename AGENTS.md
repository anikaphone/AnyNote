# AnyNote 开发指南

## 沟通与工作范围

- 默认使用中文沟通，保留代码标识符、命令和路径的原文。
- 修改前检查 `git status --short` 和相关源码，保留用户已有的修改。
- 围绕当前任务做最小必要改动，避免顺手重构无关模块。
- 完成时说明改动、实际执行的验证及未验证部分；不把历史测试结果当作当前结果。

## 项目与目录

AnyNote 是 Windows 原生笔记应用，使用 C++20、Win32 通用控件、RichEdit、GDI+ 和 SQLite3MC，支持富文本笔记、树形目录、搜索、多笔记库和数据库加密。

- `src/main.cpp`：程序入口、DPI、OLE/GDI+ 初始化和消息循环。
- `src/common/`：窗口基类、字符串处理、语法高亮与代码块 RTF 生成。
- `src/ui/`：主窗口、树控件、富文本编辑器、工具栏、搜索及查找替换界面。
- `src/storage/`：数据库与语句封装、笔记模型和仓储、多笔记库及配置管理。
- `res/`：资源脚本、资源 ID、manifest 和图标。
- `tests/`：独立测试程序。
- `CMakeLists.txt`、`CMakePresets.json`：构建目标、依赖与共享预设。

## 构建

在仓库根目录使用 PowerShell。需要 CMake 3.20 或更新版本、Visual Studio 2022 C++ 工具链、Windows SDK 和 vcpkg。共享预设使用 `VCPKG_ROOT` 指向的工具链及 `x64-windows-static` triplet；依赖包必须提供 `sqlite3mc::sqlite3mc_static`。

先确认本机工具和依赖，不把某台机器的绝对安装路径写入共享配置：

```powershell
Get-Command cmake, vcpkg -All
cmake --version
$env:VCPKG_ROOT
cmake --preset x64-static
cmake --build --preset debug --parallel 2
cmake --build --preset release --parallel 2
```

产物位于 `build/Debug/` 或 `build/Release/`，主程序为 `AnyNote.exe`。当前没有 vcpkg manifest；配置前需在对应 vcpkg 环境中准备依赖。若已有构建目录的生成器或工具链与预设不一致，先核查缓存，使用独立构建目录，避免直接删除已有产物。

保持现有静态链接约定：SQLite3MC 静态目标和 MSVC `/MT`（Debug 为 `/MTd`）。保留 UTF-8 编译选项及 `UNICODE`、`_UNICODE`、`NOMINMAX` 等定义。新增源文件或链接依赖时同步更新相关 CMake 目标。

## 验证

当前 CMake 没有 `enable_testing()` / `add_test()` 注册，不能将 `ctest` 没找到测试视为测试通过。按改动范围直接运行对应程序：

- `test_storage.exe`：存储、加密、多笔记库、INI 配置、代码块生成及搜索匹配等断言。
- `test_codeblock_hover.exe`：通过原生编辑器测试窗口验证代码块悬浮栏行为。
- `test_node_search.exe`：通过真实窗口处理逻辑验证树过滤及全库搜索导航。

`test_storage` 使用相对测试路径并删除同名文件，应在新建的临时目录中运行。以下示例针对 Debug；验证 Release 时替换配置名：

```bash
$testBin = Join-Path $PWD.Path 'build/Debug'
$testDir = Join-Path ([System.IO.Path]::GetTempPath()) ('AnyNote-tests-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testDir | Out-Null
Push-Location $testDir
try {
    foreach ($testName in @('test_storage', 'test_codeblock_hover', 'test_node_search')) {
        & (Join-Path $testBin "$testName.exe")
        if ($LASTEXITCODE -ne 0) { throw "$testName 失败，退出码：$LASTEXITCODE" }
    }
} finally {
    Pop-Location
}
```

涉及界面行为时，在独立测试笔记库中实际操作相关控件，检查保存与重新打开、焦点、键盘导航和相关布局。原生测试程序通过不等于完整 GUI 回归通过；仅检查源码或启动进程也不能证明交互正确。发布前按任务要求验证 Debug 和 Release。

提交结果前运行 `git diff --check`，检查 `git diff` 及新增文件；纯文档修改无需构建应用。

## 代码约定与数据保护

- 遵循相邻代码：四空格缩进、头文件 `#pragma once`、`anynote::common/ui/storage` 命名空间、类与方法 PascalCase、成员变量 `m_` 前缀。
- Windows 文本与路径沿用宽字符接口；数据库文本转换、RTF 字节内容和 Unicode 偏移需区分处理。
- 界面逻辑放在 `ui`，数据库操作优先通过 `storage` 封装；复用现有字符串及 RTF 工具。
- 保持 Win32 句柄、GDI 对象、子类化回调和 OLE/GDI+ 生命周期正确，销毁窗口后不得使用旧句柄。
- 修改笔记切换、搜索导航或树重建时检查未保存内容及当前选中节点；修改节点移动时检查顺序、父子关系和环保护。
- 不使用用户实际笔记库做破坏性测试；不提交笔记数据、密码、本机配置、构建缓存或生成的二进制文件。
- 如果用户需求模糊，应先采用提问方式将用户需求梳理明确。
- 不将用户给出的技术性结论作为权威，应尊重客观事实，不可附和用户。
