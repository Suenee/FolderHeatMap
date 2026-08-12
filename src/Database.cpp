#include "Database.h"

#include "sqlite3.h"

#include <filesystem>

namespace fhm {
namespace {
constexpr sqlite3_int64 kTicksPerDay = 10000000LL * 60LL * 60LL * 24LL;

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

Database::~Database() { Close(); }

bool Database::Open(const std::wstring& databasePath) {
    std::scoped_lock lock(mutex_);
    if (db_ != nullptr) return true;

    std::error_code ec;
    const std::filesystem::path path(databasePath);
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);

    if (sqlite3_open16(databasePath.c_str(), &db_) != SQLITE_OK) {
        if (db_ != nullptr) { sqlite3_close(db_); db_ = nullptr; }
        return false;
    }
    sqlite3_busy_timeout(db_, 1500);
    return EnsureSchema();
}

void Database::Close() {
    std::scoped_lock lock(mutex_);
    if (db_ != nullptr) { sqlite3_close(db_); db_ = nullptr; }
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
        "CREATE INDEX IF NOT EXISTS idx_folders_volume_path ON folders(volume_id, relative_path);"
        "CREATE TABLE IF NOT EXISTS active_days ("
        " day_key INTEGER PRIMARY KEY,"
        " visits INTEGER NOT NULL DEFAULT 0"
        ");";
    char* error = nullptr;
    const int rc = sqlite3_exec(db_, kSql, nullptr, nullptr, &error);
    if (error != nullptr) sqlite3_free(error);
    return rc == SQLITE_OK;
}

bool Database::RecordVisit(const FolderIdentity& identity, const FILETIME& now) {
    std::scoped_lock lock(mutex_);
    if (db_ == nullptr) return false;

    static constexpr const char* kFolderSql =
        "INSERT INTO folders(storage_key, volume_id, relative_path, visits, last_visit) "
        "VALUES(?1, ?2, ?3, 1, ?4) "
        "ON CONFLICT(storage_key) DO UPDATE SET visits=visits+1, last_visit=excluded.last_visit, "
        "volume_id=excluded.volume_id, relative_path=excluded.relative_path;";
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db_, kFolderSql, -1, &statement, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text16(statement, 1, identity.storageKey.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text16(statement, 2, identity.volumeId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text16(statement, 3, identity.relativePath.c_str(), -1, SQLITE_TRANSIENT);
    const sqlite3_int64 nowTicks = FileTimeToInt64(now);
    sqlite3_bind_int64(statement, 4, nowTicks);
    bool ok = sqlite3_step(statement) == SQLITE_DONE;
    sqlite3_finalize(statement);
    if (!ok) return false;

    static constexpr const char* kDaySql =
        "INSERT INTO active_days(day_key, visits) VALUES(?1,1) "
        "ON CONFLICT(day_key) DO UPDATE SET visits=visits+1;";
    if (sqlite3_prepare_v2(db_, kDaySql, -1, &statement, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int64(statement, 1, nowTicks / kTicksPerDay);
    ok = sqlite3_step(statement) == SQLITE_DONE;
    sqlite3_finalize(statement);
    return ok;
}

std::optional<StoredActivity> Database::GetActivity(const FolderIdentity& identity) {
    std::scoped_lock lock(mutex_);
    if (db_ == nullptr) return std::nullopt;
    static constexpr const char* kSql = "SELECT visits,last_visit FROM folders WHERE storage_key=?1;";
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db_, kSql, -1, &st, nullptr) != SQLITE_OK) return std::nullopt;
    sqlite3_bind_text16(st, 1, identity.storageKey.c_str(), -1, SQLITE_TRANSIENT);
    std::optional<StoredActivity> result;
    if (sqlite3_step(st) == SQLITE_ROW) {
        StoredActivity a{};
        const auto visits = sqlite3_column_int64(st, 0);
        if (visits > 0) a.visits = static_cast<std::uint64_t>(visits);
        a.lastVisit = Int64ToFileTime(sqlite3_column_int64(st, 1));
        result = a;
    }
    sqlite3_finalize(st);
    return result;
}

std::vector<std::pair<std::wstring, StoredActivity>> Database::GetVolumeActivities(const std::wstring& volumeId) {
    std::scoped_lock lock(mutex_);
    std::vector<std::pair<std::wstring, StoredActivity>> out;
    if (db_ == nullptr) return out;
    static constexpr const char* kSql = "SELECT relative_path,visits,last_visit FROM folders WHERE volume_id=?1;";
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db_, kSql, -1, &st, nullptr) != SQLITE_OK) return out;
    sqlite3_bind_text16(st, 1, volumeId.c_str(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(st) == SQLITE_ROW) {
        const wchar_t* p = static_cast<const wchar_t*>(sqlite3_column_text16(st, 0));
        StoredActivity a{};
        const auto visits = sqlite3_column_int64(st, 1);
        if (visits > 0) a.visits = static_cast<std::uint64_t>(visits);
        a.lastVisit = Int64ToFileTime(sqlite3_column_int64(st, 2));
        out.emplace_back(p ? p : L"", a);
    }
    sqlite3_finalize(st);
    return out;
}

int Database::GetRecentActiveDays(const FILETIME& now, int windowDays) {
    std::scoped_lock lock(mutex_);
    if (db_ == nullptr || windowDays <= 0) return 0;
    const sqlite3_int64 today = FileTimeToInt64(now) / kTicksPerDay;
    static constexpr const char* kSql = "SELECT COUNT(*) FROM active_days WHERE day_key>=?1 AND day_key<=?2;";
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db_, kSql, -1, &st, nullptr) != SQLITE_OK) return 0;
    sqlite3_bind_int64(st, 1, today - windowDays + 1);
    sqlite3_bind_int64(st, 2, today);
    int count = 0;
    if (sqlite3_step(st) == SQLITE_ROW) count = sqlite3_column_int(st, 0);
    sqlite3_finalize(st);
    return count;
}

} // namespace fhm
