# P0 第一批实现与验证记录

日期：2026-10-10。代码在当前工作树，尚未提交，不填写虚构 commit。

本文保留 P0 第一批的历史记录，以下“尚未完成”描述只代表当时状态。后续 P1–P4 实现、完整测试及发布验收结果以 [implementation-report.md](implementation-report.md) 和 [TODO.md](TODO.md) 为准。

## 已落地

- `vcpkg.json` 固定 baseline `e456309491fd875487bf02b8a0dca76c1b1b00cc`；SQLite3MC overlay 从上游 v2.5.1 下载并校验 SHA-512，移除对本机源码目录的依赖。
- 实际构建依赖：SQLite3MC 2.5.1、nlohmann/json 3.12.0#2、curl 8.22.0#1（仅 OpenSSL 特性）、OpenSSL 3.6.5#1、zlib 1.3.2#2。C++20、静态库、`/MTd`/`/MT` 保持。
- Go 1.27.2；modernc/sqlite v1.60.1、其要求的 libc v1.77.1、x/crypto v0.58.0；`go.mod` / `go.sum` 固定直接和间接依赖。
- C++/Go 按节点流式读写快照；SHA-256、规范 Base64、int64 字符串、重复键、Unicode、树深/环/缺失父和大小限制校验。12 个共享协议样本。
- `DiffEngine` 按 sync_id 比较并集，忽略两端整数 ID；子树覆盖预览补齐祖先、移除目标子树独有节点，拒绝隐式影响范围外后代。
- `Database::CheckpointAndClose` 拒绝未释放语句、事务和不完整 checkpoint；成功后才允许调用方在持有独占库锁的前提下复制关闭的文件。该方法尚未接入生产备份流程。
- JSON Schema 与 15 操作的 OpenAPI 草案、隔离 TLS 故障服务、可复现验证脚本。TLS 私钥仅驻留测试进程内存。

## 实际执行

构建环境：Windows 11，MSVC 19.44.35229、CMake 3.31.6-msvc6；CPU i5-12450H。服务端另在 WSL Debian 中运行 Linux amd64 纯 Go 测试二进制。

1. 从固定 baseline 在独立 vcpkg checkout 构建全部静态依赖。中文路径的 vcpkg 子进程工具检测曾失败，改用临时 ASCII 工具链目录成功；共享配置没有添加本机绝对路径。失败缓存中的 `Z_VCPKG_ROOT_DIR` 需要清除后重新配置。
2. `cmake --preset x64-static`，`cmake --build --preset debug --parallel 2` 和 `cmake --build --preset release --parallel 2` 成功。
3. `scripts/test-sync-p0.ps1 -ExistingRegression`：Debug/Release 各运行 `test_sync_protocol`、`test_sync_storage`、`test_sync_tls` 及原有五个测试，全部通过。
4. `go mod verify`、`go vet ./...`、`go test ./...` 通过；`CGO_ENABLED=0` 的 Windows/Linux 探针、Linux 协议测试二进制构建通过，Linux 测试实际运行通过。
5. Go parser 5 秒 fuzz：3,747 次执行，无崩溃（短时探测，不等于完整安全审计）。
6. 新增测试使用显式失败检查，Release 不依赖被 `NDEBUG` 移除的 assert。

自动化原始日志及临时数据：`%TEMP%/AnyNote-sync-p0-4f379011228d46fea7f58fb205e19fcb`。此目录不是交付依赖；后续运行脚本每次生成新的 GUID 目录。

### TLS

实测 curl 输出 `8.22.0 / OpenSSL/3.6.5`。合法自签叶证书通过；陌生信任文件、SAN 错误、过期证书被拒绝；HTTP 被禁用，不跟随重定向；超时和取消返回相应错误。

实际测试证书 `NotBefore=2026-10-10 08:46:15 UTC`，`NotAfter=2126-10-10 09:46:15 UTC`；SAN 为 localhost / 127.0.0.1。测试生成器使用日历年加 100 年。该证书只用于回环测试，不作为部署证书。

`dumpbin /dependents build/Debug/test_sync_tls.exe` 只列出 Windows 系统 DLL，没有第三方或动态 CRT DLL。

### 备份与身份

明文与 AES-256-CBC 测试库均完成 WAL checkpoint、严格关闭、复制、重新打开、`integrity_check` 与二进制内容比较。加密副本不接受空/错误密码；写入失败后事务回滚保留原内容；活跃 WAL 读者阻止备份。

身份演练目前是两份独立内存快照：相同整数 ID/不同 sync_id 不配对，相同 sync_id/不同整数 ID 无假差异；缺失祖先、目标独有后代删除、跨子树附带删除和环均覆盖。**这不是两台实体离线设备的端到端验证。**

### 最大规模

20,000 节点，其中 8 篇各 16 MiB，其余为空，总原始内容 128 MiB。包在内存流中生成，不写明文协议临时文件。

| 环境 | 耗时 | 内存读数 |
| --- | --- | --- |
| C++ Debug | 93.362 秒 | 进程峰值工作集 287,408,128 字节 |
| C++ Release | 9.918 秒 | 进程峰值工作集 269,373,440 字节 |
| Go Windows | 1.498 秒 | HeapSys 289,144,832 字节 |
| Go Linux / WSL | 1.878 秒 | HeapSys 267,812,864 字节 |

C++ 数字包含同进程样本生成、Base64 编解码和校验。Go HeapSys 是堆保留内存，不是进程峰值 RSS，不可直接与 C++ 工作集比较。测试尚未包含真实网络、加密暂存导入和 UI 取消响应；不能将此表当作完整同步吞吐量。

## 尚未完成与阶段门槛

P0 **尚未整体关闭**。以下项目继续保留未完成状态：

- HTTP 写入规范化 request_hash、错误映射及完整请求/响应的跨语言 fixtures 尚未冻结；现有共用 fixtures 仅覆盖快照。OpenAPI 是接口草案，不是已实现端点。
- 两台实体离线设备演练、进程峰值内存的统一测量、最大请求取消响应仍待验证。
- 账号隔离测试依赖尚未实现的认证服务；当前隔离服务仅用于 TLS/传输故障。

P1–P5 尚未实现：旧库 UID/sync_id/hash 迁移、INI→JSON、生产导入/备份/锁、认证与远端存储、客户端账户/传输协调、云同步菜单与在线管理。未执行完整同步 GUI 回归，也未修改生产笔记 schema 或配置格式。

下一步先补齐上述协议冻结与 P0 证据，再进入 P1；不得把当前通过的探针当作迁移、账户隔离或同步界面验收。

## 复现

```powershell
# VCPKG_ROOT 指向支持固定 baseline 的 Git checkout；工具链建议放 ASCII 路径。
cmake --preset x64-static
cmake --build --preset debug --parallel 2
cmake --build --preset release --parallel 2
./scripts/test-sync-server.ps1 -Scale -LinuxBuild
./scripts/test-sync-p0.ps1 -Scale -ExistingRegression
```

脚本测试会新建临时目录并保留证据，只终止其自身启动的探针进程。LinuxBuild 只表示交叉构建；Linux 运行仍需在 Linux/WSL 中执行生成的测试二进制，并将工作目录设为 `src/AnyNoteServer/internal/protocol`。
