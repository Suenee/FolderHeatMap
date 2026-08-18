#include "Database.h"
#include "sqlite3.h"

#include <algorithm>

namespace fhm {
namespace {
sqlite3_int64 FileTimeToInt64(const FILETIME& value) {
    ULARGE_INTEGER v{};
    v.LowPart = value.dwLowDateTime;
    v.HighPart = value.dwHighDateTime;
    return static_cast<sqlite3_int64>(v.QuadPart);
}

FILETIME Int64ToFileTime(sqlite3_int64 value) {
    ULARGE_INTEGER v{};
    v.QuadPart = static_cast<ULONGLONG>(std::max<sqlite3_int64>(0, value));
    FILETIME result{};
    result.dwLowDateTime = v.LowPart;
    result.dwHighDateTime = v.HighPart;
    return result;
}

bool EnsureRuntimeCacheTable(sqlite3* db) {
    static constexpr const char* sql =
        "CREATE TABLE IF NOT EXISTS runtime_cache ("
        " path TEXT PRIMARY KEY,"
        " flags INTEGER NOT NULL DEFAULT 0,"
        " heat REAL NOT NULL DEFAULT 0,"
        " visits INTEGER NOT NULL DEFAULT 0,"
        " writes INTEGER NOT NULL DEFAULT 0,"
        " last_visit INTEGER NOT NULL DEFAULT 0,"
        " last_write INTEGER NOT NULL DEFAULT 0,"
        " heat_level INTEGER NOT NULL DEFAULT 0,"
        " color_step INTEGER NOT NULL DEFAULT 0"
        ");";
    return sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
}
}

bool Database::SaveRuntimeCache(const std::vector<RuntimeCacheRecord>& records) {
    std::scoped_lock lock(mutex_);
    if (db_ == nullptr || !EnsureRuntimeCacheTable(db_)) return false;
    if (sqlite3_exec(db_, "BEGIN IMMEDIATE;", nullptr, nullptr, nullptr) != SQLITE_OK) return false;

    bool ok = sqlite3_exec(db_, "DELETE FROM runtime_cache;", nullptr, nullptr, nullptr) == SQLITE_OK;
    sqlite3_stmt* statement = nullptr;
    static constexpr const char* sql =
        "INSERT INTO runtime_cache(path,flags,heat,visits,writes,last_visit,last_write,heat_level,color_step) "
        "VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9);";

    if (ok && sqlite3_prepare_v2(db_, sql, -1, &statement, nullptr) != SQLITE_OK) ok = false;
    if (ok) {
        for (const auto& record : records) {
            sqlite3_reset(statement);
            sqlite3_clear_bindings(statement);
            sqlite3_bind_text16(statement, 1, record.path.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(statement, 2, static_cast<sqlite3_int64>(record.flags));
            sqlite3_bind_double(statement, 3, record.heat);
            sqlite3_bind_int64(statement, 4, static_cast<sqlite3_int64>(record.visits));
            sqlite3_bind_int64(statement, 5, static_cast<sqlite3_int64>(record.writes));
            sqlite3_bind_int64(statement, 6, FileTimeToInt64(record.lastVisit));
            sqlite3_bind_int64(statement, 7, FileTimeToInt64(record.lastWrite));
            sqlite3_bind_int(statement, 8, record.heatLevel);
            sqlite3_bind_int(statement, 9, record.colorStep);
            if (sqlite3_step(statement) != SQLITE_DONE) {
                ok = false;
                break;
            }
        }
    }
    if (statement) sqlite3_finalize(statement);
    sqlite3_exec(db_, ok ? "COMMIT;" : "ROLLBACK;", nullptr, nullptr, nullptr);
    return ok;
}

std::vector<RuntimeCacheRecord> Database::LoadRuntimeCache() {
    std::scoped_lock lock(mutex_);
    std::vector<RuntimeCacheRecord> result;
    if (db_ == nullptr || !EnsureRuntimeCacheTable(db_)) return result;

    sqlite3_stmt* statement = nullptr;
    static constexpr const char* sql =
        "SELECT path,flags,heat,visits,writes,last_visit,last_write,heat_level,color_step FROM runtime_cache;";
    if (sqlite3_prepare_v2(db_, sql, -1, &statement, nullptr) != SQLITE_OK) return result;

    while (sqlite3_step(statement) == SQLITE_ROW) {
        RuntimeCacheRecord record{};
        const void* path = sqlite3_column_text16(statement, 0);
        if (path) record.path = static_cast<const wchar_t*>(path);
        record.flags = static_cast<std::uint32_t>(sqlite3_column_int64(statement, 1));
        record.heat = sqlite3_column_double(statement, 2);
        record.visits = static_cast<std::int64_t>(sqlite3_column_int64(statement, 3));
        record.writes = static_cast<std::int64_t>(sqlite3_column_int64(statement, 4));
        record.lastVisit = Int64ToFileTime(sqlite3_column_int64(statement, 5));
        record.lastWrite = Int64ToFileTime(sqlite3_column_int64(statement, 6));
        record.heatLevel = sqlite3_column_int(statement, 7);
        record.colorStep = sqlite3_column_int(statement, 8);
        if (!record.path.empty()) result.push_back(std::move(record));
    }
    sqlite3_finalize(statement);
    return result;
}

} // namespace fhm
