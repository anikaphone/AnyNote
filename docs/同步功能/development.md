# AnyNote 手动同步：开发方式与验收

状态：客户端、服务端和在线管理已实现。当前执行证据、实现细节与剩余人工验收见 [implementation-report.md](implementation-report.md)；进度见 [TODO.md](TODO.md)。

## 1. 开发约束

1. 按 [TODO.md](TODO.md) 的依赖顺序开发；每阶段提交可独立审查，禁止先铺 UI 再补数据安全。
2. 修改前检查 `git status --short`，保留已有修改与未跟踪文件。根目录有 `.codegraph/` 时，理解代码先用 CodeGraph；不可用时先 `rg` 定位，再读局部，结构搜索优先 ast-grep。
3. 保持 C++20、Unicode 宽字符文件 API、UTF-8 编译、SQLite3MC 静态目标、Debug `/MTd` 与 Release `/MT`。不得将本机安装路径写入共享配置。
4. UI 仅负责展示和操作编排；存储通过 Repository/迁移服务，HTTP 只接收 DTO。DiffEngine 无 HWND/网络依赖，便于用真实树数据测试。
5. 所有 SQL Prepare/Bind/Step/Commit、JSON 写入、备份、锁操作都检查返回值；失败不得报告成功。新增数据库操作采用 RAII 事务和 statement 管理。
6. RTF 以字节序列传递，必须保留图片、表格、代码块、链接、Unicode 和嵌入零字节；不使用 RichEdit 导出后再作为上传源，否则可能制造无意义哈希变化。
7. 所有写接口必须认证、账号隔离、版本校验、幂等和事务化；不提供无条件 force 参数。每次请求重新校验账户会话。
8. 只在独立临时库和测试账户上执行覆盖、删除、改密、异常注入。禁止真实笔记、密码、证书私钥、token、运行配置、数据库和二进制进入 Git。
9. 网络线程不得同步调用 UI 线程导致互锁。窗口关闭、取消、自动保存、快捷键、第二实例都纳入生命周期测试。
10. 已按用户要求进入代码阶段；须同时验证 Debug/Release，原生 UI 和协议测试分别记录证据，不把探针结果当作完整同步验收。

## 2. 依赖与版本管理

CMake 已新增 `find_package(nlohmann_json CONFIG REQUIRED)`、`find_package(CURL CONFIG REQUIRED)`，使用 `nlohmann_json::nlohmann_json`、`CURL::libcurl`，并链接 `bcrypt`、`crypt32`。测试目标应复用同一依赖配置。检查产物依赖确保不存在意外第三方 DLL；证书公钥文件仍属于外部运行资源。

服务端用 `go.mod`/`go.sum` 固定 Go 版本和 modernc/sqlite、x/crypto 版本，遵守 modernc 对间接依赖版本的要求。P0 选择当时受支持、两个库共同兼容的 Go 工具链并写入记录；CI 不使用 `@latest` 或漂移的工具链。构建产物记录 commit、工具链、vcpkg baseline、模块版本和许可证清单。

## 3. 客户端现有构建命令

在仓库根目录 PowerShell 执行；先确认依赖可用。不要因缓存不一致删除用户 build，改用独立目录并明确生成器。

```powershell
git status --short
Get-Command cmake, vcpkg -All
cmake --version
$env:VCPKG_ROOT
cmake --preset x64-static
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }
cmake --build --preset debug --parallel 2
if ($LASTEXITCODE -ne 0) { throw 'Debug build failed' }
cmake --build --preset release --parallel 2
if ($LASTEXITCODE -ne 0) { throw 'Release build failed' }
```

当前未注册 CTest 测试，`ctest` 没找到测试不算通过。直接运行现有五个目标，每个配置使用新的临时目录：

```powershell
foreach ($config in @('Debug', 'Release')) {
    $testBin = Join-Path $PWD.Path "build/$config"
    $testDir = Join-Path ([System.IO.Path]::GetTempPath()) ('AnyNote-tests-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $testDir | Out-Null
    Push-Location $testDir
    try {
        foreach ($name in @('test_storage', 'test_codeblock_hover', 'test_table_editing', 'test_node_search', 'test_outline')) {
            & (Join-Path $testBin "$name.exe")
            if ($LASTEXITCODE -ne 0) { throw "$config/$name failed: $LASTEXITCODE" }
        }
    } finally { Pop-Location }
}
git diff --check
git diff --cached --check
```

新增同步测试建议使用 `test_sync_storage`、`test_sync_protocol`、`test_sync_integration`，创建后才加入执行清单。测试目录保留以供失败排查，不自动删除未知路径。

## 4. 服务端开发命令

```powershell
Push-Location src/AnyNoteServer
try {
    go version
    go mod download
    if ($LASTEXITCODE -ne 0) { throw 'go mod download failed' }
    go mod verify
    if ($LASTEXITCODE -ne 0) { throw 'go mod verify failed' }
    go vet ./...
    if ($LASTEXITCODE -ne 0) { throw 'go vet failed' }
    go test ./...
    if ($LASTEXITCODE -ne 0) { throw 'go test failed' }
    $oldCgo = $env:CGO_ENABLED
    try {
        $env:CGO_ENABLED = '0'
        go build -trimpath -o ../../build/server/anynote-server.exe ./cmd/anynote-server
        if ($LASTEXITCODE -ne 0) { throw 'pure Go build failed' }
    } finally { $env:CGO_ENABLED = $oldCgo }
} finally { Pop-Location }
```

Linux amd64 CI 同样执行 `CGO_ENABLED=0 go build`，并在原生 Linux 环境运行集成测试。额外 `go test -race ./...` 在支持 race 的工具链环境运行（通常需要 CGO/C 工具链）；race 的开发依赖不改变发布程序的纯 Go 要求。`gofmt -l .` 输出必须为空。

端到端测试启动随机端口、独立数据目录与测试证书的服务进程；只有返回健康状态后才运行客户端协议测试。用进程句柄关闭测试服务，不能按模糊进程名杀死真实服务。

## 5. 自签证书与部署

以下仅是 localhost 测试证书命令，需安装 OpenSSL CLI。生产将 SAN 替换为客户端实际访问的域名/IP。私钥仅在服务端生成并保留；不要覆盖已有证书。

```powershell
New-Item -ItemType Directory -Path certs -Force | Out-Null
if ((Test-Path certs/server.key) -or (Test-Path certs/server.crt)) { throw 'Certificate already exists' }
openssl req -x509 -newkey rsa:3072 -sha256 -nodes -days 36525 -keyout certs/server.key -out certs/server.crt -subj '/CN=localhost' -addext 'subjectAltName=DNS:localhost,IP:127.0.0.1' -addext 'basicConstraints=critical,CA:FALSE' -addext 'keyUsage=critical,digitalSignature,keyEncipherment' -addext 'extendedKeyUsage=serverAuth'
if ($LASTEXITCODE -ne 0) { throw 'Certificate generation failed' }
openssl x509 -in certs/server.crt -noout -dates -fingerprint -sha256 -ext subjectAltName
```

`36525` 天按 100 年近似配置，不宣称精确到每个日历年；检查实际 NotAfter 与客户端验证结果。超长有效期、自签叶证书作为专用信任文件必须在固定的 libcurl/OpenSSL 构建上实测，不能靠关闭证书验证绕过兼容问题。

拟定服务端命令行 `anynote-server --config server.json`（未实现）。配置含 listen 地址/端口、data_dir、cert_file、key_file、registration_enabled、配额、超时、限流和备份保留值；路径相对配置目录解析。客户端私钥路径不需要配置。服务端启动时证书/配置/权限不合法立即失败，不降级为 HTTP。

Linux 使用专用非 root 用户、systemd、仅服务账户可读的私钥/数据目录；Windows 开发使用当前用户受限目录。默认测试监听回环地址，公网部署显式配置监听和防火墙。日常备份覆盖数据库、配置与私钥，存到独立受控位置；每次升级前备份，恢复演练必须包含账户登录与库下载。

## 6. 分层验收矩阵

所有条目当前状态均为“未执行”。每条须记录测试数据、操作、预期、实际、构建版本；失败/未验证不得标记通过。

| 编号 | 范围与场景 | 通过标准 |
| --- | --- | --- |
| A01 | 明文/加密旧库迁移、重复启动 | UID、sync_id 稳定，哈希正确，RTF 和结构不变；错误密码不改文件 |
| A02 | 迁移断电、只读目录、磁盘满 | 原库可打开，无半迁移库参与同步；备份可恢复 |
| A03 | INI→JSON 全字段、未知字段、外部路径 | 库顺序/活动库/所有设置保留；损坏 JSON 不覆盖原配置 |
| A04 | 同 UID 多路径、同名不同 UID、时间戳碰撞 | 提示选主副本/另存/新身份，无静默覆盖 |
| A05 | 两设备离线创建相同整数 ID | 两篇 sync_id 不同，不互相覆盖；父关系正确 |
| A06 | 修改、空内容、NULL、内嵌零字节 | RTF 字节和 SHA-256 往返一致，内容/哈希事务一致 |
| A07 | 标题、父节点、排序、时间各自变化 | 差异类别准确；整数 ID 不同不产生假差异 |
| A08 | 子树源独有/目标独有/缺失祖先 | 预览与实际增删改一致，祖先补齐、无环无悬空父 |
| A09 | 子树跨位置移动、祖先环、深层子树 | 不隐式删除范围外节点；无效操作整笔拒绝 |
| A10 | 注册、大小写重复、非法账号、长密码 | 两端同规则，账号唯一，不截断密码 |
| A11 | 登录/记住密码/自动登录/退出/改密 | DPAPI 保护，不记密码不落盘；改密全设备 401 |
| A12 | 两账户访问相同 UID 或猜测路径 | 无越权读取/修改/备注/操作状态泄漏 |
| A13 | 合法证书、陌生证书、过期、SAN 错、HTTP | 仅合法 HTTPS 成功，不显示“忽略错误继续” |
| A14 | 全库上传空库、覆盖非空库、远端独有节点 | 确认计数正确，服务端事务完整替换，备注保留 |
| A15 | 全库下载、两次确认、同 UID/新库 | 取消不改库，备份在覆盖前生成，保留本地密码 |
| A16 | 双客户端同时覆盖、旧版本写 | 仅一个成功，其余 409；必须刷新并重新确认 |
| A17 | 同 operation_id 重试/不同请求复用 | 不重复提交；不同请求冲突；响应丢失可查结果 |
| A18 | 传输取消/断网/超时/磁盘满/进程终止 | 原数据或完整新数据二选一，无半库；UI 恢复可用 |
| A19 | 服务端删除重建/备份恢复 | epoch 改变，旧客户端无法用旧 revision 覆盖 |
| A20 | 恶意 JSON/Base64/hash/大包/环/重复字段 | 有界拒绝，数据库无变化，无无限内存或循环 |
| A21 | 上传期间快捷键、菜单、切库、第二实例 | 无绕过写入，进度可响应，任务后正确解锁 |
| A22 | 新库文件成功但配置登记失败/崩溃 | 重启可恢复登记，无丢库或重复覆盖 |
| A23 | 界面窗口关闭、改账号、迟到回调 | 不访问已销毁 HWND，不把旧账号数据画到新窗口 |
| A24 | 远端删除后重算差异、本地独有节点上传 | 删除不动本地；差异行按真实状态保留/清除 |
| A25 | RTF 图片/表格/代码块/Unicode/搜索 | 下载重开后内容、格式、检索、大纲均正确 |
| A26 | 备份轮转与恢复、WAL 未清空 | 不产生不完整备份，不删除当前唯一恢复副本 |
| A27 | 20,000 节点、128 MiB 数据、16 MiB 单篇边界 | 正常上限完成，超限拒绝，记录耗时/内存/取消响应 |
| A28 | 无网络本地编辑、未配置同步、自动登录失败 | 本地功能正常，未选择自动登录不发启动网络请求 |

## 7. 原生 GUI 回归与发布门槛

使用两个隔离客户端目录、两个设备身份、两个账户、至少一个加密库。手工检查 Tab/Shift+Tab 顺序、Enter/Esc、密码遮罩、焦点恢复、右键命中、禁用状态、滚动、中文长标题、100%/150%/200% DPI、深树与空列表。覆盖后保存再重新打开，确认树、编辑内容、搜索结果与大纲一致；旧撤销栈不能恢复被替换的旧文档。

用截图和实际控件操作记录通过/失败矩阵。单元测试通过、合成输入返回成功、仅启动程序都不等于原生 GUI 验收通过。自动化报告、截图、未验证项分开列出。

发布门槛：Debug/Release 编译和相关测试全通过；全部数据安全/认证/TLS条目通过；GUI 回归通过；Linux 纯 Go 发布构建与恢复演练通过；未验证平台明确标注不支持。性能报告记录硬件、网络、数据规模、峰值内存、耗时，若 10 分钟默认限时不足则先调整限制和测试，不悄悄无限等待。

提交前执行 `git diff --check`、检查 `git diff`、逐一检查新文件和暂存差异。文档中的示例命令不代表本次已执行；此次仅检查文档完整性和差异格式。
