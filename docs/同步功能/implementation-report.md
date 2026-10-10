# 手动同步实现与验收记录

日期：2026-10-10。实现位于当前工作树，未创建 commit、未部署到外部服务器。原始 `需求.txt` 未改动。P0 第一批记录保留为历史资料；本文记录后续完整实现和当前验收范围。

## 实现范围

- 本地库迁移：库 UID、节点 UUIDv4 `sync_id`、原始 RTF SHA-256、schema 版本、generation；迁移前备份，错误密码/未来版本拒绝。内容、索引和时间同事务维护。
- `anynote.json` 原子写入、未知 INI 设置保留、路径核验、重复 UID 副本选择。云端绑定按规范化服务地址、account_id、UID 保存到 `sync-state.json`；凭据单独用 DPAPI 保护。
- `SnapshotService` 保留已知节点本地整数 ID，为新节点分配新 ID，转换父关系；完整校验、加密备份、临时库演练、事务提交、幂等收据、10 份备份轮转。新文件安装有登记恢复日志。
- 纯 Go HTTPS 服务：账号、Argon2id、会话、改密全会话失效、限流、配额、账号隔离、全库/子树读写删除、备注、manifest、epoch/revision 条件写及 operation_id 查询。Windows/Linux 服务实例均有 OS 文件锁。
- C++ HTTPS 客户端：专用 CA、严格主机/期限校验、有界流式管道、取消、进度、响应丢失查询。不会为一次未知结果自动生成新 operation_id 重写。
- 原生菜单和窗口：登录/注册/改密、全库上传/下载、两次下载确认、在线库列表/远端树/差异并集、两侧节点详情、排序、子树计数、备注和远端删除。工作线程结束后才释放模态窗口与业务禁用状态。
- 保存边界：以加载后的编辑器表示识别未编辑笔记，同时比对数据库原始 RTF，避免仅打开同步、焦点或布局变化污染内容和修改时间。下载后刷新树、搜索、大纲和撤销栈。

同步设置实际位于 `settings.Sync`（ServerUrl、CaFile、Username、RememberPassword、AutoLogin），兼容原有设置读写入口。草案 `cloud` 字段作为未知字段保留但不使用。同 UID 多副本每次显式选定本次目标，不缓存可能过期的主路径。操作收据首版不自动清理，保留时间长于设计最低恢复窗口；管理员需监控占用。

## 构建、产物与依赖

后续配置调整：按用户要求删除根目录 `vcpkg.json` 和 `vcpkg-configuration.json`，改用本机 classic 模式依赖。下表及原有验证结果记录删除前的构建环境，不代表当前本机依赖已固定为这些版本。手动安装和新目录验证说明见 development.md。 删除后已在新目录 `build/classic-check` 使用 `D:/vcpkg/installed` 完成 CMake 配置及 Debug/Release AnyNote 主程序构建；本轮未重跑同步测试。日志：`build/classic-configure.log`、`build/classic-debug.log`、`build/classic-release.log`。

客户端 C++20 / MSVC 19.44.35229 / CMake 3.31.6-msvc6，保持 SQLite3MC 静态链接、Debug `/MTd` 和 Release `/MT`。`dumpbin /DEPENDENTS build/Release/AnyNote.exe` 只显示 Windows 系统 DLL。

| 依赖 | 固定版本 |
| --- | --- |
| vcpkg baseline | `e456309491fd875487bf02b8a0dca76c1b1b00cc` |
| SQLite3MC overlay | 2.5.1，上游下载 SHA-512 校验 |
| nlohmann/json | 3.12.0#2 |
| curl / OpenSSL / zlib | 8.22.0#1 / 3.6.5#1 / 1.3.2#2 |
| Go | 1.27.2，`CGO_ENABLED=0` |
| modernc/sqlite / libc | v1.60.1 / v1.77.1 |
| x/crypto / x/sys | v0.58.0 / v0.49.0 |

客户端产物：`build/Debug/AnyNote.exe`、`build/Release/AnyNote.exe`。服务端、备份工具、证书工具分别位于 `build/server/Debug`、`build/server/Release`、`build/server/linux-amd64`。

复现命令（仓库根目录 PowerShell）：

```powershell
# VCPKG_ROOT 指向本机 vcpkg，先手动安装依赖；已有 build 请检查旧依赖缓存。
cmake --preset x64-static
cmake --build --preset debug --parallel 2
cmake --build --preset release --parallel 2
./scripts/test-sync-server.ps1 -LinuxBuild -Scale
./scripts/test-sync-p0.ps1 -ExistingRegression -Scale
./scripts/package-sync.ps1
```

`test-sync-p0.ps1` 名称沿用早期阶段，但现在执行完整同步测试。Debug/Release 不在同一 build 目录并行构建，以免 vcpkg applocal 冲突。每轮测试创建 GUID 临时目录，原有测试不在真实笔记目录运行；没有用空 CTest 结果代替测试。

打包脚本仅复制显式列出的 Release 程序、协议和说明，收集 vcpkg copyright、Go 工具链及所有模块 LICENSE/NOTICE，生成文件 SHA-256、源码 HEAD/dirty 和版本清单。不复制笔记、配置、测试账号、凭据、证书或私钥。SQLite3MC 等许可证以包内实际 copyright 为准，不能只把上游 SQLite 的公有领域声明当作整个依赖许可证。

## 当前执行证据

全部日志在忽略目录 `build/p0`，不作为源码提交；临时数据可用于当前机器复查。

- `acceptance-tests.log`：Debug/Release 八个同步测试及原有五个测试全部通过，包含最大规模协议及 Release 真实 HTTPS 大包测试。
- `delivery-tests.log`：服务地址规范化、最终在线管理细节修改后的双配置完整回归通过。
- `reopen-debug.log`、`reopen-release.log`、`reopen-tests.log`：保存原始内容保护后的最终双配置构建和全部回归通过；数据目录为 `%TEMP%/AnyNote-sync-p0-80196f06e34d46edafdf6834ced3a8ac`。
- `server-acceptance.log`：`go mod verify`、gofmt、vet、全包测试及 Windows Debug/Release、Linux amd64 三个命令构建通过。
- `linux-{api,store,lease,protocol}-delivery.log`：WSL Debian 中实际执行 Linux 测试通过，涵盖接口、备份恢复、跨进程排他锁和协议。
- `delivery-dependencies.log`：Release 主程序动态依赖核验。
- 完整规模测试目录：`%TEMP%/AnyNote-sync-p0-9d3cb4aef07a4e658529542ee2865491`。
- 后续回归目录：`%TEMP%/AnyNote-sync-p0-278d3d85abd14afa9adc511c160e2fdf`。

新增八个测试：`test_sync_protocol`、`test_sync_storage`、`test_sync_tls`、`test_sync_migration`、`test_sync_config`、`test_sync_snapshot`、`test_sync_client`、`test_sync_e2e`。原有五个：`test_storage`、`test_codeblock_hover`、`test_table_editing`、`test_node_search`、`test_outline`。

真实原生窗口已操作账户标签、注册/登录、全库上传、在线列表及右键、库详情、全库下载两次确认，并观察下载完成后的“差异 0 项”。测试库独立于用户实际数据，包含 Unicode、表格和代码块。重启复查发现的无意义时间差异已补充保存保护；最终执行下载、WM_CLOSE、重启、重新登录和库详情，仍为差异 0 项，记录在 `build/p0/gui-verification.txt`，截图 `04f096a2-23e9-4867-87a1-2acc51ac6e49-screenshot.png` 位于 `%TEMP%/orca-computer-use`。原生控件部分数据输入使用 Win32 消息，结果通过窗口状态/截图复核；不将其描述为完整人工测试。

## 最大规模实测

同一台 Windows 11 / i5-12450H，数值是本次测量，不是性能承诺。128 MiB 指解码后的 RTF；样本含 20,000 节点、8 篇各 16 MiB 内容。

| 场景 | 耗时 | 内存指标 |
| --- | ---: | ---: |
| C++ Debug 协议往返 | 32,524 ms | 288,215,040 字节峰值工作集 |
| C++ Release 协议往返 | 9,831 ms | 270,594,048 字节峰值工作集 |
| Release 实际 HTTPS 上传+下载及逐篇哈希核验 | 62,595 ms | 387,567,616 字节客户端峰值工作集 |
| Go Windows 协议规模测试 | 1.433 s | HeapSys 259,751,936 字节 |

大包上传发送超过 1 MiB 后取消，本次测得取消等待 0 ms（计时分辨率内）；远端查询为不存在，没有半库提交。服务端进程峰值 RSS 未单独测量，Go HeapSys 不等同于 RSS；不拿它代替整条链路内存指标。

## A01–A28 验收结果

“自动化通过”只覆盖列明的条件。物理断电/设备和完整 GUI 矩阵未执行时明确标注，不能据此关闭全部 P5。

| 项 | 当前结果与证据 | 尚未覆盖 |
| --- | --- | --- |
| A01 | migration：明文/加密迁移、稳定 UID/UUID、错误密码、未来版本拒绝通过 | 大量历史用户库抽样 |
| A02 | migration/snapshot：故障注入、事务回滚、备份可打开通过 | 真实断电、磁盘满、只读介质 |
| A03 | config：INI 全字段/未知键/长 Unicode/外部路径/坏 JSON/CAS 通过 | 各类存储介质 I/O 故障 |
| A04 | config/snapshot：重复 UID 副本、AsNew、新文件不覆盖；GUI 有显式选择 | 所有同名/同 UID 原生交互组合 |
| A05 | e2e：两个独立客户端及库、离线相同整数 ID、不同 UUID、父关系收敛通过 | 两台物理 Windows 设备 |
| A06 | protocol/snapshot/client：空值、零字节、二进制 RTF、SHA-256、原子内容更新通过 | 各种 RTF 内嵌对象实物 |
| A07 | protocol：标题/内容/位置/时间/格式差异，忽略整数 ID 通过 | 全部差异组合人工截图 |
| A08 | protocol/API/e2e：子树新增删除、祖先补齐、映射与收敛通过 | 大树 GUI 预览人工抽样 |
| A09 | 两端子树范围外后代保护、环/深度拒绝通过 | 物理双机交叉移动 |
| A10 | API：账号规范化、重复/非法账号、密码规则、限流通过；GUI 注册登录成功 | 全部密码边界 GUI 输入 |
| A11 | client/API：DPAPI 隔离、退出、改密撤销会话通过；GUI 登录通过 | 多设备自动登录/改密人工矩阵 |
| A12 | API：跨账号库、UID、操作状态隔离通过 | 独立渗透测试 |
| A13 | TLS：100 年证书、陌生/过期/SAN 错误/HTTP/重定向拒绝通过 | 实际跨机器证书分发/轮换 |
| A14 | API/client：空库/替换/删除与备注保留通过；GUI 全库上传成功 | 所有预览计数组合人工检查 |
| A15 | snapshot：加密备份、回滚、ID 映射、新库安装通过；GUI 两次确认下载成功 | 全部取消点和加密 GUI 组合 |
| A16 | API：并发版本竞争仅一方成功、旧版本 409 通过 | 跨公网高延迟双机 |
| A17 | client/API：同 ID 重试、异请求复用冲突、提交后丢响应查询通过 | 长时间离线后人工恢复 |
| A18 | TLS/client/snapshot：超时、取消、丢响应、SQL 故障不留下半库通过 | 真磁盘满/断电/各提交点 kill |
| A19 | store/API：删除重建及恢复 epoch 变化，旧版本失效通过 | 外部部署灾难恢复 |
| A20 | 共享 fixtures/两端校验：恶意 JSON、Base64、哈希、重复键、环和限额拒绝通过 | 长时间 fuzz/安全审计 |
| A21 | OS 跨进程库锁通过；原生模态进度和主窗业务禁用已实现 | 所有快捷键/拖拽绕过的人工矩阵 |
| A22 | snapshot：文件提交后配置失败，journal 恢复登记和不覆盖通过 | 真实进程终止时刻组合 |
| A23 | worker 在模态退出前 join；切账号清理、账户隔离测试通过 | 关闭/切账号/迟到结果完整 GUI 压测 |
| A24 | API 子树删除与差异并集规则通过；成功后重新读取比较 | 全部删除菜单人工路径 |
| A25 | 原有表格/代码块/搜索/大纲测试通过；GUI 下载后富文本和差异归零 | 含图片/对象、各 DPI 全量重开回归 |
| A26 | storage/snapshot：WAL 读者拒绝、加密备份、10 份轮转、回滚通过 | 外部介质损坏/人工恢复 GUI |
| A27 | 两端 20k/128 MiB/16 MiB 边界、实际 HTTPS、客户端内存/取消实测通过 | 服务端 RSS、慢网长时间压力 |
| A28 | 本地五个原有回归通过；自动登录需两个开关且凭据可解密 | 离线自动登录失败全部 GUI 状态 |

## 部署、升级与回滚

服务配置、证书生成/分发/轮换、Linux 权限和服务端备份恢复命令见 [服务端说明](../../src/AnyNoteServer/README.md)。客户端运行目录须可写；首次登录前配置 HTTPS 地址和专用证书。Linux 包拷贝后执行 `chmod 700 anynote-server anynote-admin anynote-cert`。不附带通用私钥或默认账号。

升级前保留原库及 INI；首次打开各库独立迁移，未解锁库不伪造 UID。`.sync-backups` 保存导入前完整库，`.sync-pending` 保存新库安装恢复日志，`.sync-operations` 保存结果未确认的远端请求身份。禁止直接删除这些目录来“修复同步”。旧版本客户端不理解新增 schema，回滚应用时先停止所有实例，使用升级前备份与匹配配置，不能把旧程序直接指向已升级的唯一副本。

此交付完成手动同步功能实现和上述验证；两台物理设备、真实故障和完整原生 GUI 发布矩阵仍是发布门槛，没有标为已通过。
