#include "Database.h"

#include "sqlite3.h"

#include <filesystem>
#include <limits>

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
    v.QuadPart = static_cast<ULONGLONG>(value);
    FILETIME result{};
    result.dwLowDateTime = v.LowPart;
    result.dwHighDateTime = v.HighPart;
    return result;
}

} // namespace

Database::~Database() {
    Close();
}

bool Database::Open(const std::wstring& databasePath) {
    std::scoped_lock lock(mutex_);
    if (db_ != nullptr) {
        return true;
    }

    std::error_code ec;
    const std::filesystem::path path(databasePath);
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
    }

    if (sqlite3_open16(databasePath.c_str(), &db_) != SQLITE_OK) {
        if (db_ != nullptr) {
            sqlite3_close(db_);
            db_ = nullptr;
        }
        return false;
    }

    sqlite3_busy_timeout(db_, 1500);
    return EnsureSchema();
}

void Database::Close() {
    std::scoped_lock lock(mutex_);
    if (db_ != nullptr) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

bool Database::IsOpen() const {
    std::scoped_lock lock(mutex_);
    return db_ != nullptr;
}

bool Database::EnsureSchema() {
    static constexpr const char* kSql =
        "PRAGMA journal_mode=WAL;"
        "PRAGMA synchronous=NORMAL;"
        "CREATE TABLE IF NOT EXISTS folders ("
        " storage_key TEXT PRIMARY KEY,"
        " volume_id TEXT NOT NULL,"
        " relative_path TEXT NOT NULL,"
        " visits INTEGER NOT NULL DEFAULT 0,"
        " last_visit INTEGER NOT NULL DEFAULT 0"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_folders_volume_path "
        "ON folders(volume_id, relative_path);";

    char* error = nullptr;
    const int rc = sqlite3_exec(db_, kSql, nullptr, nullptr, &error);
    if (error != nullptr) {
        sqlite3_free(error);
    }
    return rc == SQLITE_OK;
}

bool Database::RecordVisit(const FolderIdentity& identity, const FILETIME& now) {
    std::scoped_lock lock(mutex_);
    if (db_ == nullptr) {
        return false;
    }

    static constexpr const char* kSql =
        "INSERT INTO folders(storage_key, volume_id, relative_path, visits, last_visit) "
        "VALUES(?1, ?2, ?3, 1, ?4) "
        "ON CONFLICT(storage_key) DO UPDATE SET "
        "visits = visits + 1, last_visit = excluded.last_visit, "
        "volume_id = excluded.volume_id, relative_path = excluded.relative_path;";

    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db_, kSql, -1, &statement, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text16(statement, 1, identity.storageKey.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text16(statement, 2, identity.volumeId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text16(statement, 3, identity.relativePath.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(statement, 4, FileTimeToInt64(now));

    const bool ok = sqlite3_step(statement) == SQLITE_DONE;
    sqlite3_finalize(statement);
    return ok;
}

std::optional<StoredActivity> Database::GetActivity(const FolderIdentity& identity) {
    std::scoped_lock lock(mutex_);
    if (db_ == nullptr) {
        return std::nullopt;
    }

    static constexpr const char* kSql =
        "SELECT visits, last_visit FROM folders WHERE storage_key = ?1;";

    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db_, kSql, -1, &statement, nullptr) != SQLITE_OK) {
        return std::nullopt;
    }

    sqlite3_bind_text16(statement, 1, identity.storageKey.c_str(), -1, SQLITE_TRANSIENT);

    std::optional<StoredActivity> result;
    if (sqlite3_step(statement) == SQLITE_ROW) {
        const sqlite3_int64 visits = sqlite3_column_int64(statement, 0);
        const sqlite3_int64 lastVisit = sqlite3_column_int64(statement, 1);

        StoredActivity activity{};
        if (visits > 0) {
            activity.visits = static_cast<std::uint64_t>(visits);
        }
        activity.lastVisit = Int64ToFileTime(lastVisit);
        result = activity;
    }

    sqlite3_finalize(statement);
    return result;
}

} // namespace fhm
