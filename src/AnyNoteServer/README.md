# AnyNote 手动同步服务

纯 Go / CGO_ENABLED=0，HTTPS only。服务端可读取笔记明文；请保护数据目录、管理员备份和私钥。

## 构建与首次运行

从仓库根目录运行 `./scripts/test-sync-server.ps1 -LinuxBuild`。Windows Debug/Release 产物在 `build/server/Debug`、`build/server/Release`；Linux 产物在 `build/server/linux-amd64`。

把 `server.example.json` 复制为部署目录的 `server.json`，设置实际监听地址。相对路径以配置文件目录为基准。默认禁止注册；首次创建账号前显式启用 `registration_enabled`，创建后可关闭并重启。

```powershell
./anynote-cert.exe -dir certs -hosts 'localhost,127.0.0.1,sync.example.internal'
./anynote-server.exe --config server.json
```

`anynote-cert` 使用 RSA 3072 和 100 个日历年有效期，已有文件绝不覆盖。SAN 必须包含客户端实际连接的 DNS/IP。只把 `server.crt` 通过可信渠道复制到客户端 `certs` 目录，在“云同步 → 登录/注册”配置 HTTPS 地址和该证书。不要分发 `server.key`，也不要导入系统全局信任库。换证书时先分发新证书并更新客户端路径，再切换服务端证书；旧证书验证失败时不会绕过校验。

Linux 使用专用用户、数据/证书目录权限 0700、私钥和备份权限 0600。systemd 配置 `WorkingDirectory`、绝对 `ExecStart` 和服务用户，`Restart=on-failure`；仅开放选定 HTTPS 端口。服务端直连 TLS，不支持把明文反向代理连接当作 HTTPS。`GET /health` 检查数据库连接。

## 备份与恢复

```powershell
# 在线一致性快照，包含已提交的 WAL 内容；目标必须不存在。
./anynote-admin.exe --source data/anynote-server.sqlite --destination backups/2026-10-10.sqlite

# 在新目录恢复，原数据目录保留。
New-Item -ItemType Directory restored-data
./anynote-admin.exe --restore --source backups/2026-10-10.sqlite --destination restored-data/anynote-server.sqlite
```

备份还需独立保存 `server.json`、证书和私钥（受限权限）。恢复命令校验 SQLite 完整性和外键，清除会话与旧幂等记录，刷新所有库 epoch。**先停止服务，再把配置的 data_dir 切换到恢复目录并重新启动**。客户端必须重新登录、重新比较；旧 epoch 的请求会被拒绝。未成功完成恢复命令的目录不能用于启动服务。不得直接复制运行中数据库的主文件作为备份。

恢复在目标目录内的独立 `.anynote-restore-*` 临时目录进行，校验、会话/epoch 更新、checkpoint 和数据库关闭全部成功后才发布目标文件。发布使用不覆盖已有路径的硬链接，目标文件系统需支持硬链接（如 NTFS、ext4）；不支持时安全失败。普通失败会清理临时数据库及其 WAL/SHM，原备份不变，可使用相同目标路径重试。进程被强制终止可能留下临时目录，但不会把它当作已发布的目标文件；已存在的目标或同名 SQLite 附属文件不会被覆盖或删除。

默认每账号最多 100 个库、1 GiB RTF；单库 20,000 节点、深度 128、单节点 16 MiB、全库 128 MiB、请求 JSON 192 MiB。账户接口按来源 IP 和规范化用户名各限制每分钟 10 次，Argon2 和大请求分别最多 2 个并发。HTTP 请求最多 10 分钟，Header 10 秒，空闲连接 60 秒。返回 429/503 后应退避，不要自动重发未确认的写请求。

协议和错误映射见 `../../protocol/openapi.json`、`../../protocol/request-identity.md`。操作记录不自动过期，管理员需监控数据库和备份占用；日志不记录密码、令牌和笔记正文。
