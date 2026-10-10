# 同步 v1 协议实现基线

`sync-v1.schema.json` 定义快照、写入前提、凭据和错误结构；`openapi.json` 覆盖已实现的 15 个 HTTP 操作。实现位于 `src/AnyNoteServer` 和 `src/AnyNoteClient/sync`，发布验证范围见 `docs/同步功能/implementation-report.md`。运行 `scripts/export-sync-openapi.ps1` 可重建接口描述，Go 测试检查引用与操作标识。

两端测试共同读取 `tests/fixtures/sync-v1`；`valid-` 必须接受，`invalid-` 必须拒绝。新增协议边界应先增加共享样本，避免 C++/Go 各自接受不同格式。

JSON Schema 不能表达的校验由两端代码执行：

- 重复字段（包括转义后的同名字段）、无效 UTF-8、未配对 UTF-16 surrogate、尾随数据一律拒绝。
- 十进制字符串必须规范化，无正负号、前导零、小数或指数；范围为非负 int64，节点 ID 必须大于零。
- UUID 仅接受小写 UUIDv4。`parent_sync_id` 的根值必须是 null，不能是空串或零。
- 节点 `source_id` 与 `sync_id` 各自在快照中唯一；只有 `sync_id` 用于跨端配对。
- RTF 使用规范 RFC4648 Base64（含必要 padding，无空白）；解码后 <=16 MiB，SHA-256 必须与原始字节一致。空串是零字节，不能用 null 替代。
- 全库 RTF <=128 MiB，节点 <=20,000，树深 <=128（根深度为1），JSON <=192 MiB；无环、无缺失父节点。
- `node_type=0`、`format_type=1` 是现有客户端唯一支持的枚举。目录操作指具有子节点的笔记，不新增不兼容的目录枚举。
- 名称/标题长度按 Unicode 标量数计算；时间是非负 Unix 秒字符串，不使用时间判断覆盖优先级。
- SAX/Decoder 在完整树校验前可交付节点，但接收方只能写入可丢弃的暂存区，绝不能直接更新活动库。

P0 中 C++ 使用 nlohmann/json 公共 `json_sax` / `sax_parse`，Go 使用 `encoding/json.Decoder.Token` 按字段、按节点解析；不建立整库 Base64 DOM。输出按节点编码。有界不代表常量小内存：单个最大节点的 Base64 字符串仍会分配，测试报告需包含峰值。

资料：[nlohmann SAX](https://json.nlohmann.me/features/parsing/sax_interface/)、[Go encoding/json](https://pkg.go.dev/encoding/json)、[modernc/sqlite](https://pkg.go.dev/modernc.org/sqlite)。

全库写请求使用 `{precondition, snapshot}`，子树写请求使用 `{precondition, source}`；创建为 `create_only:true`，修改为 `expected_epoch` + `expected_revision`，两者互斥。UID 必须同时与 URL 和快照匹配。幂等哈希见 `request-identity.md`，跨语言固定向量见 `tests/fixtures/request-identity`；HTTP 行为由服务端接口测试和两个真实 C++ 客户端的 HTTPS 测试验证。
