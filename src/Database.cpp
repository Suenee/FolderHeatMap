#include "Database.h"

#include "sqlite3.h"

#include <algorithm>
#include <filesystem>

namespace fhm {
namespace {
constexpr sqlite3_int64 kTicksPerSecond = 10000000LL;
constexpr sqlite3_int64 kTicksPerDay = kTicksPerSecond * 60LL * 60LL * 24LL;
constexpr sqlite3_int64 kHeatCooldownTicks = kTicksPerSecond * 90LL;
constexpr sqlite3_int64 kSessionGapTicks = kTicksPerSecond * 60LL * 60LL * 8LL;

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

void BootstrapUsageFromLegacy(StoredActivity& a) {
    if (!a.visits || FileTimeToInt64(a.lastVisit) == 0) return;
    a.heatVisits = a.visits;
    a.recentVisits = std::min<std::uint64_t>(a.visits, 8);
    a.activeDays = 1;
    const auto day = FileTimeToInt64(a.lastVisit) / kTicksPerDay;
    a.firstActiveDay = day;
    a.lastActiveDay = day;
    a.lastEffectiveVisit = a.lastVisit;
}

void ReadUsageColumns(sqlite3_stmt* st, int firstColumn, StoredActivity& a) {
    if (sqlite3_column_type(st, firstColumn) == SQLITE_NULL) {
        BootstrapUsageFromLegacy(a);
        return;
    }
    const auto heatVisits = sqlite3_column_int64(st, firstColumn + 0);
    const auto recentVisits = sqlite3_column_int64(st, firstColumn + 1);
    const auto activeDays = sqlite3_column_int64(st, firstColumn + 2);
    if (heatVisits > 0) a.heatVisits = static_cast<std::uint64_t>(heatVisits);
    if (recentVisits > 0) a.recentVisits = static_cast<std::uint64_t>(recentVisits);
    if (activeDays > 0) a.activeDays = static_cast<std::uint64_t>(activeDays);
    a.firstActiveDay = sqlite3_column_int64(st, firstColumn + 3);
    a.lastActiveDay = sqlite3_column_int64(st, firstColumn + 4);
    a.lastEffectiveVisit = Int64ToFileTime(sqlite3_column_int64(st, firstColumn + 5));
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
        "CREATE TABLE IF NOT EXISTS folder_usage ("
        " storage_key TEXT PRIMARY KEY,"
        " heat_visits INTEGER NOT NULL DEFAULT 0,"
        " recent_visits INTEGER NOT NULL DEFAULT 0,"
        " active_days INTEGER NOT NULL DEFAULT 0,"
        " first_active_day INTEGER NOT NULL DEFAULT 0,"
        " last_active_day INTEGER NOT NULL DEFAULT 0,"
        " last_effective_visit INTEGER NOT NULL DEFAULT 0"
        ");"
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

    const sqlite3_int64 nowTicks = FileTimeToInt64(now);
    const sqlite3_int64 today = nowTicks / kTicksPerDay;

    if (sqlite3_exec(db_, "BEGIN IMMEDIATE;", nullptr, nullptr, nullptr) != SQLITE_OK) return false;
    bool ok = true;
    sqlite3_stmt* statement = nullptr;

    static constexpr const char* kFolderSql =
        "INSERT INTO folders(storage_key, volume_id, relative_path, visits, last_visit) "
        "VALUES(?1, ?2, ?3, 1, ?4) "
        "ON CONFLICT(storage_key) DO UPDATE SET visits=visits+1, last_visit=excluded.last_visit, "
        "volume_id=excluded.volume_id, relative_path=excluded.relative_path;";
    if (sqlite3_prepare_v2(db_, kFolderSql, -1, &statement, nullptr) != SQLITE_OK) ok = false;
    if (ok) {
        sqlite3_bind_text16(statement, 1, identity.storageKey.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text16(statement, 2, identity.volumeId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text16(statement, 3, identity.relativePath.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(statement, 4, nowTicks);
        ok = sqlite3_step(statement) == SQLITE_DONE;
        sqlite3_finalize(statement);
        statement = nullptr;
    }

    sqlite3_int64 heatVisits = 0;
    sqlite3_int64 recentVisits = 0;
    sqlite3_int64 activeDays = 0;
    sqlite3_int64 firstActiveDay = 0;
    sqlite3_int64 lastActiveDay = 0;
    sqlite3_int64 lastEffectiveVisit = 0;

    if (ok) {
        static constexpr const char* kUsageRead =
            "SELECT heat_visits,recent_visits,active_days,first_active_day,last_active_day,last_effective_visit "
            "FROM folder_usage WHERE storage_key=?1;";
        if (sqlite3_prepare_v2(db_, kUsageRead, -1, &statement, nullptr) != SQLITE_OK) ok = false;
        if (ok) {
            sqlite3_bind_text16(statement, 1, identity.storageKey.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(statement) == SQLITE_ROW) {
                heatVisits = sqlite3_column_int64(statement, 0);
                recentVisits = sqlite3_column_int64(statement, 1);
                activeDays = sqlite3_column_int64(statement, 2);
                firstActiveDay = sqlite3_column_int64(statement, 3);
                lastActiveDay = sqlite3_column_int64(statement, 4);
                lastEffectiveVisit = sqlite3_column_int64(statement, 5);
            }
            sqlite3_finalize(statement);
            statement = nullptr;
        }
    }

    const bool effective = ok && (lastEffectiveVisit == 0 || nowTicks - lastEffectiveVisit >= kHeatCooldownTicks);
    if (effective) {
        ++heatVisits;
        if (lastEffectiveVisit == 0 || nowTicks - lastEffectiveVisit > kSessionGapTicks) recentVisits = 1;
        else recentVisits = std::max<sqlite3_int64>(1, recentVisits + 1);

        if (lastActiveDay != today) {
            ++activeDays;
            lastActiveDay = today;
            if (firstActiveDay == 0) firstActiveDay = today;
        }
        lastEffectiveVisit = nowTicks;

        static constexpr const char* kUsageWrite =
            "INSERT INTO folder_usage(storage_key,heat_visits,recent_visits,active_days,first_active_day,last_active_day,last_effective_visit) "
            "VALUES(?1,?2,?3,?4,?5,?6,?7) "
            "ON CONFLICT(storage_key) DO UPDATE SET heat_visits=excluded.heat_visits,recent_visits=excluded.recent_visits,"
            "active_days=excluded.active_days,first_active_day=excluded.first_active_day,last_active_day=excluded.last_active_day,"
            "last_effective_visit=excluded.last_effective_visit;";
        if (sqlite3_prepare_v2(db_, kUsageWrite, -1, &statement, nullptr) != SQLITE_OK) ok = false;
        if (ok) {
            sqlite3_bind_text16(statement, 1, identity.storageKey.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(statement, 2, heatVisits);
            sqlite3_bind_int64(statement, 3, recentVisits);
            sqlite3_bind_int64(statement, 4, activeDays);
            sqlite3_bind_int64(statement, 5, firstActiveDay);
            sqlite3_bind_int64(statement, 6, lastActiveDay);
            sqlite3_bind_int64(statement, 7, lastEffectiveVisit);
            ok = sqlite3_step(statement) == SQLITE_DONE;
            sqlite3_finalize(statement);
            statement = nullptr;
        }

        if (ok) {
            static constexpr const char* kDaySql =
                "INSERT INTO active_days(day_key, visits) VALUES(?1,1) "
                "ON CONFLICT(day_key) DO UPDATE SET visits=visits+1;";
            if (sqlite3_prepare_v2(db_, kDaySql, -1, &statement, nullptr) != SQLITE_OK) ok = false;
            if (ok) {
                sqlite3_bind_int64(statement, 1, today);
                ok = sqlite3_step(statement) == SQLITE_DONE;
                sqlite3_finalize(statement);
                statement = nullptr;
            }
        }
    }

    if (statement) sqlite3_finalize(statement);
    sqlite3_exec(db_, ok ? "COMMIT;" : "ROLLBACK;", nullptr, nullptr, nullptr);
    return ok;
}

std::optional<StoredActivity> Database::GetActivity(const FolderIdentity& identity) {
    std::scoped_lock lock(mutex_);
    if (db_ == nullptr) return std::nullopt;
    static constexpr const char* kSql =
        "SELECT f.visits,f.last_visit,u.heat_visits,u.recent_visits,u.active_days,u.first_active_day,u.last_active_day,u.last_effective_visit "
        "FROM folders f LEFT JOIN folder_usage u ON u.storage_key=f.storage_key WHERE f.storage_key=?1;";
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db_, kSql, -1, &st, nullptr) != SQLITE_OK) return std::nullopt;
    sqlite3_bind_text16(st, 1, identity.storageKey.c_str(), -1, SQLITE_TRANSIENT);
    std::optional<StoredActivity> result;
    if (sqlite3_step(st) == SQLITE_ROW) {
        StoredActivity a{};
        const auto visits = sqlite3_column_int64(st, 0);
        if (visits > 0) a.visits = static_cast<std::uint64_t>(visits);
        a.lastVisit = Int64ToFileTime(sqlite3_column_int64(st, 1));
        ReadUsageColumns(st, 2, a);
        result = a;
    }
    sqlite3_finalize(st);
    return result;
}

std::vector<std::pair<std::wstring, StoredActivity>> Database::GetVolumeActivities(const std::wstring& volumeId) {
    std::scoped_lock lock(mutex_);
    std::vector<std::pair<std::wstring, StoredActivity>> out;
    if (db_ == nullptr) return out;
    static constexpr const char* kSql =
        "SELECT f.relative_path,f.visits,f.last_visit,u.heat_visits,u.recent_visits,u.active_days,u.first_active_day,u.last_active_day,u.last_effective_visit "
        "FROM folders f LEFT JOIN folder_usage u ON u.storage_key=f.storage_key WHERE f.volume_id=?1;";
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db_, kSql, -1, &st, nullptr) != SQLITE_OK) return out;
    sqlite3_bind_text16(st, 1, volumeId.c_str(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(st) == SQLITE_ROW) {
        const wchar_t* p = static_cast<const wchar_t*>(sqlite3_column_text16(st, 0));
        StoredActivity a{};
        const auto visits = sqlite3_column_int64(st, 1);
        if (visits > 0) a.visits = static_cast<std::uint64_t>(visits);
        a.lastVisit = Int64ToFileTime(sqlite3_column_int64(st, 2));
        ReadUsageColumns(st, 3, a);
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
