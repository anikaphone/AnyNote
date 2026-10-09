#pragma once

#include <string>
#include <string_view>
#include <memory>
#include <cstdint>

struct sqlite3;
struct sqlite3_stmt;

namespace anynote::storage {

class Database {
public:
    Database();
    ~Database();

    // 禁用拷贝，允许移动
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;
    Database(Database&& other) noexcept;
    Database& operator=(Database&& other) noexcept;

    // 打开或创建数据库文件 (可选传入密码，若库已加密则解密打开)
    bool Open(const std::wstring& filePath, const std::string& password = "");
    void Close();

    bool IsOpen() const noexcept { return m_db != nullptr; }
    const std::wstring& GetFilePath() const noexcept { return m_filePath; }
    const std::string& GetLastError() const noexcept { return m_lastError; }
    bool IsEncrypted() const noexcept { return m_isEncrypted; }
    bool IsPasswordRequiredOrWrong() const noexcept {
        return m_lastError == "ENCRYPTED_REQUIRES_PASSWORD" || m_lastError == "ENCRYPTED_WRONG_PASSWORD";
    }

    // 设置或更改数据库密码 (传入空密码则解除加密为明文库)
    bool SetPassword(const std::string& newPassword);

    // 执行无返回结果的 SQL 语句
    bool Execute(const std::string& sql);

    // 事务控制
    bool BeginTransaction();
    bool Commit();
    bool Rollback();

    sqlite3* GetRawHandle() const noexcept { return m_db; }

    class Statement {
    public:
        Statement() = default;
        ~Statement();

        Statement(const Statement&) = delete;
        Statement& operator=(const Statement&) = delete;
        Statement(Statement&& other) noexcept;
        Statement& operator=(Statement&& other) noexcept;

        bool Prepare(Database& db, const std::string& sql);

        bool BindInt64(int index, int64_t val);
        bool BindInt(int index, int val);
        bool BindText(int index, std::string_view text);
        bool BindText(int index, std::wstring_view text);
        bool BindBlob(int index, const void* data, size_t size);
        bool BindNull(int index);

        int Step(); // 返回 SQLITE_ROW, SQLITE_DONE 或错误码
        bool Reset();
        bool ClearBindings();

        int64_t GetInt64(int col) const;
        int GetInt(int col) const;
        std::string GetText(int col) const;
        std::wstring GetWideText(int col) const;
        std::string GetBlob(int col) const;

        bool IsValid() const noexcept { return m_stmt != nullptr; }
        sqlite3_stmt* GetRawStmt() const noexcept { return m_stmt; }

    private:
        sqlite3_stmt* m_stmt = nullptr;
    };

private:
    sqlite3* m_db = nullptr;
    std::wstring m_filePath;
    std::string m_lastError;
    bool m_isEncrypted = false;
};

} // namespace anynote::storage
