# v1 幂等请求身份

数据库按 `(account_id, operation_id)` 保存结果。先重新验证当前会话，再查询操作记录；同 ID 同 request_hash 返回原结果，不同哈希返回 `OPERATION_CONFLICT`。过期操作查不到不能推断未提交。

服务端解析并验证请求后构造业务描述对象，禁止直接散列 HTTP JSON 字节或客户端提供的 hash：

```json
{"method":"PUT","path":"/api/v1/vaults/0123456789abcdef/snapshot","precondition":{"create_only":true},"payload":{"protocol_version":1,"schema_version":1,"uid":"0123456789abcdef","name":"库","nodes":[]}}
```

`method` 是固定大写路由方法；`path` 是路由匹配后重新构造的标准路径，不含查询、编码别名。`precondition` 去掉 operation_id；`payload` 是校验后的业务字段，删除操作用 null，备注用 `{remark:...}`，快照用 snapshot，子树用 source 快照。快照节点按 sync_id 的字节序排序，删除每个节点的 rtf_base64，仅保留已经过原始内容 SHA-256 校验的 content_hash。其余字段均保留，包括诊断用 source_id。不得含凭据、token 或本地路径。

规范编码以 UTF-8 字节 `AnyNote/request/v1\n` 开始，然后递归编码描述对象：

| JSON 值 | 编码 |
| --- | --- |
| null / false / true | 单字节 n / f / t |
| int64 范围内整数 | i + 规范十进制 + ; |
| 字符串 | s + UTF-8 字节数的十进制 + : + 原始 UTF-8 字节 |
| 数组 | a + 元素数 + : + 顺序编码各元素 |
| 对象 | o + 键数 + : + 按 UTF-8 字节序排序后依次编码键字符串和值 |

不允许浮点数、不合法 UTF-8，最大嵌套16、编码后192 MiB。SHA-256 的64位小写hex即request_hash。长度前缀避免连接歧义，Unicode不归一化；重排 JSON 字段不影响哈希。C++/Go 与 PowerShell 独立参考实现的共用向量位于 `tests/fixtures/request-identity/v1.json`。

API 错误映射固定为：400 INVALID_REQUEST（包括所有解析/字段/树结构失败）、401 UNAUTHORIZED、404 NOT_FOUND、409 VERSION_CONFLICT / IDENTITY_CONFLICT / OPERATION_CONFLICT、413 LIMIT_EXCEEDED、422 HASH_MISMATCH、429 RATE_LIMITED、503 UNAVAILABLE；注册关闭为400 REGISTRATION_DISABLED。解析器内部错误不直接作为HTTP消息输出。失败响应只含request_id及固定、无用户数据的错误信息。429/503的retryable为true，其余为false；网络结果未知由operation_id查询处理。
