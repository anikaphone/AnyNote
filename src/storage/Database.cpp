#include "Database.h"
#include "common/StringUtils.h"
#include <sqlite3.h>
#include <sqlite3mc.h>

namespace anynote::storage {

Database::Database() = default;

Database::~Database() {
    Close();
}

Database::Database(Database&& other) noexcept
    : m_db(other.m_db)
    , m_filePath(std::move(other.m_filePath))
    , m_lastError(std::move(other.m_lastError))
    , m_isEncrypted(other.m_isEncrypted) {
    other.m_db = nullptr;
}

Database& Database::operator=(Database&& other) noexcept {
    if (this != &other) {
        Close();
        m_db = other.m_db;
        m_filePath = std::move(other.m_filePath);
        m_lastError = std::move(other.m_lastError);
        m_isEncrypted = other.m_isEncrypted;
        other.m_db = nullptr;
    }
    return *this;
}

bool Database::Open(const std::wstring& filePath, const std::string& password) {
    Close();
    m_filePath = filePath;
    m_lastError.clear();
    m_isEncrypted = false;

    std::string utf8Path = anynote::utils::WideToUtf8(filePath);

    int rc = sqlite3_open_v2(
        utf8Path.c_str(),
        &m_db,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
        nullptr
    );

    if (rc != SQLITE_OK) {
        m_lastError = m_db ? sqlite3_errmsg(m_db) : "打开数据库文件失败";
        Close();
        return false;
    }

    // 启用 WAL 模式与外键约束以提高并发写入性能和数据完整性
    sqlite3_busy_timeout(m_db, 5000);

    // 如果提供了密码，调用 sqlite3_key 进行密钥注入
    if (!password.empty()) {
        rc = sqlite3_key(m_db, password.data(), static_cast<int>(password.size()));
        if (rc != SQLITE_OK) {
            m_lastError = "注入数据库加密密钥失败";
            Close();
            return false;
        }
        m_isEncrypted = true;
    }

    // 验证连接与解密状态 (查询 sqlite_master 表)
    char* zErrMsg = nullptr;
    rc = sqlite3_exec(m_db, "SELECT count(*) FROM sqlite_master;", nullptr, nullptr, &zErrMsg);
    if (rc != SQLITE_OK) {
        if (password.empty() && rc == SQLITE_NOTADB) {
            m_lastError = "ENCRYPTED_REQUIRES_PASSWORD"; // 标记需要密码
        } else {
            m_lastError = zErrMsg ? zErrMsg : "密码不正确或数据库已损坏";
        }
        if (zErrMsg) {
            sqlite3_free(zErrMsg);
        }
        Close();
        return false;
    }

    // 开启外键与常规同步模式
    Execute("PRAGMA foreign_keys = ON;");
    Execute("PRAGMA synchronous = NORMAL;");

    return true;
}

void Database::Close() {
    if (m_db) {
        sqlite3_close_v2(m_db);
        m_db = nullptr;
    }
}

bool Database::SetPassword(const std::string& newPassword) {
    if (!m_db) return false;

    int rc = 0;
    if (newPassword.empty()) {
        // 解除加密为明文库
        rc = sqlite3_rekey(m_db, nullptr, 0);
        if (rc == SQLITE_OK) {
            m_isEncrypted = false;
        }
    } else {
        // 设置或修改密码
        rc = sqlite3_rekey(m_db, newPassword.data(), static_cast<int>(newPassword.size()));
        if (rc == SQLITE_OK) {
            m_isEncrypted = true;
        }
    }

    if (rc != SQLITE_OK) {
        m_lastError = sqlite3_errmsg(m_db);
        return false;
    }

    return true;
}

bool Database::Execute(const std::string& sql) {
    if (!m_db) return false;

    char* zErrMsg = nullptr;
    int rc = sqlite3_exec(m_db, sql.c_str(), nullptr, nullptr, &zErrMsg);
    if (rc != SQLITE_OK) {
        m_lastError = zErrMsg ? zErrMsg : sqlite3_errmsg(m_db);
        if (zErrMsg) sqlite3_free(zErrMsg);
        return false;
    }
    return true;
}

bool Database::BeginTransaction() {
    return Execute("BEGIN TRANSACTION;");
}

bool Database::Commit() {
    return Execute("COMMIT;");
}

bool Database::Rollback() {
    return Execute("ROLLBACK;");
}

// -------------------------------------------------------------
// Statement 实现
// -------------------------------------------------------------

Database::Statement::~Statement() {
    if (m_stmt) {
        sqlite3_finalize(m_stmt);
        m_stmt = nullptr;
    }
}

Database::Statement::Statement(Statement&& other) noexcept
    : m_stmt(other.m_stmt) {
    other.m_stmt = nullptr;
}

Database::Statement& Database::Statement::operator=(Statement&& other) noexcept {
    if (this != &other) {
        if (m_stmt) {
            sqlite3_finalize(m_stmt);
        }
        m_stmt = other.m_stmt;
        other.m_stmt = nullptr;
    }
    return *this;
}

bool Database::Statement::Prepare(Database& db, const std::string& sql) {
    if (m_stmt) {
        sqlite3_finalize(m_stmt);
        m_stmt = nullptr;
    }

    if (!db.GetRawHandle()) return false;

    int rc = sqlite3_prepare_v2(db.GetRawHandle(), sql.c_str(), -1, &m_stmt, nullptr);
    if (rc != SQLITE_OK) {
        db.m_lastError = sqlite3_errmsg(db.GetRawHandle());
        return false;
    }
    return true;
}

bool Database::Statement::BindInt64(int index, int64_t val) {
    return m_stmt && sqlite3_bind_int64(m_stmt, index, val) == SQLITE_OK;
}

bool Database::Statement::BindInt(int index, int val) {
    return m_stmt && sqlite3_bind_int(m_stmt, index, val) == SQLITE_OK;
}

bool Database::Statement::BindText(int index, std::string_view text) {
    if (!m_stmt) return false;
    return sqlite3_bind_text(m_stmt, index, text.data(), static_cast<int>(text.size()), SQLITE_TRANSIENT) == SQLITE_OK;
}

bool Database::Statement::BindText(int index, std::wstring_view text) {
    if (!m_stmt) return false;
    std::string utf8 = anynote::utils::WideToUtf8(text);
    return sqlite3_bind_text(m_stmt, index, utf8.data(), static_cast<int>(utf8.size()), SQLITE_TRANSIENT) == SQLITE_OK;
}

bool Database::Statement::BindBlob(int index, const void* data, size_t size) {
    if (!m_stmt) return false;
    return sqlite3_bind_blob(m_stmt, index, data, static_cast<int>(size), SQLITE_TRANSIENT) == SQLITE_OK;
}

bool Database::Statement::BindNull(int index) {
    if (!m_stmt) return false;
    return sqlite3_bind_null(m_stmt, index) == SQLITE_OK;
}

int Database::Statement::Step() {
    if (!m_stmt) return SQLITE_ERROR;
    return sqlite3_step(m_stmt);
}

bool Database::Statement::Reset() {
    return m_stmt && sqlite3_reset(m_stmt) == SQLITE_OK;
}

bool Database::Statement::ClearBindings() {
    return m_stmt && sqlite3_clear_bindings(m_stmt) == SQLITE_OK;
}

int64_t Database::Statement::GetInt64(int col) const {
    return m_stmt ? sqlite3_column_int64(m_stmt, col) : 0;
}

int Database::Statement::GetInt(int col) const {
    return m_stmt ? sqlite3_column_int(m_stmt, col) : 0;
}

std::string Database::Statement::GetText(int col) const {
    if (!m_stmt) return {};
    const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(m_stmt, col));
    int bytes = sqlite3_column_bytes(m_stmt, col);
    if (!text || bytes <= 0) return {};
    return std::string(text, static_cast<size_t>(bytes));
}

std::wstring Database::Statement::GetWideText(int col) const {
    std::string utf8 = GetText(col);
    return anynote::utils::Utf8ToWide(utf8);
}

std::string Database::Statement::GetBlob(int col) const {
    if (!m_stmt) return {};
    const void* blob = sqlite3_column_blob(m_stmt, col);
    int bytes = sqlite3_column_bytes(m_stmt, col);
    if (!blob || bytes <= 0) return {};
    return std::string(reinterpret_cast<const char*>(blob), static_cast<size_t>(bytes));
}

} // namespace anynote::storage
