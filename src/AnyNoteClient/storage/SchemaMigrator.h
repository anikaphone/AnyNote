#pragma once
#include "Database.h"
#include <functional>

namespace anynote::storage {
class SchemaMigrator {
public:
    static constexpr int CurrentVersion = 1;
    // Password is needed only to verify/reopen a closed-file encrypted backup;
    // it is never retained. A failed migration leaves the old transaction intact.
    static bool Migrate(Database& database, const std::string& password,
        const std::function<std::wstring(const std::string&)>& extractPlainText);
};
}
