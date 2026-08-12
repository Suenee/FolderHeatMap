#include "Database.h"

#include "sqlite3.h"

namespace fhm {
namespace {

sqlite3_int64 FileTimeToInt64(const FILETIME& value) {
    ULARGE_INTEGER v{};
    v.LowPart = value.dwLowDateTime;
    v.HighPart = value.dwHighDateTime;
    return static_cast<sqlite3_int64>(v.QuadPart);
}

bool ExecBoundKey(sqlite3* db, const char* sql, const std::wstring& key) {
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text16(st, 1, key.c_str(), -1, SQLITE_TRANSIENT);
    const bool ok = sqlite3_step(st) == SQLITE_DONE;
    sqlite3_finalize(st);
    return ok;
}

bool ExecTreeDelete(sqlite3* db, const char* sql, const FolderIdentity& identity) {
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text16(st, 1, identity.volumeId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text16(st, 2, identity.relativePath.c_str(), -1, SQLITE_TRANSIENT);
    const bool ok = sqlite3_step(st) == SQLITE_DONE;
    sqlite3_finalize(st);
    return ok;
}

} // namespace

bool Database::ResetDirectActivity(const FolderIdentity& identity, bool isDirectory, const FILETIME* currentLastWrite) {
    std::scoped_lock lock(mutex_);
    if (db_ == nullptr) return false;
    if (sqlite3_exec(db_, "BEGIN IMMEDIATE;", nullptr, nullptr, nullptr) != SQLITE_OK) return false;

    bool ok = true;
    if (isDirectory) {
        ok = ExecBoundKey(db_, "DELETE FROM folder_usage WHERE storage_key=?1;", identity.storageKey);
        if (ok) ok = ExecBoundKey(db_, "DELETE FROM folders WHERE storage_key=?1;", identity.storageKey);
    } else if (currentLastWrite != nullptr) {
        static constexpr const char* kSql =
            "INSERT INTO file_activity(storage_key,volume_id,relative_path,last_write,write_events,active_days,first_active_day,last_active_day) "
            "VALUES(?1,?2,?3,?4,0,0,0,0) "
            "ON CONFLICT(storage_key) DO UPDATE SET volume_id=excluded.volume_id,relative_path=excluded.relative_path,"
            "last_write=excluded.last_write,write_events=0,active_days=0,first_active_day=0,last_active_day=0;";
        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(db_, kSql, -1, &st, nullptr) != SQLITE_OK) ok = false;
        if (ok) {
            sqlite3_bind_text16(st, 1, identity.storageKey.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text16(st, 2, identity.volumeId.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text16(st, 3, identity.relativePath.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(st, 4, FileTimeToInt64(*currentLastWrite));
            ok = sqlite3_step(st) == SQLITE_DONE;
        }
        if (st) sqlite3_finalize(st);
    } else {
        ok = ExecBoundKey(db_, "DELETE FROM file_activity WHERE storage_key=?1;", identity.storageKey);
    }

    sqlite3_exec(db_, ok ? "COMMIT;" : "ROLLBACK;", nullptr, nullptr, nullptr);
    return ok;
}

bool Database::ResetRecursiveActivity(const FolderIdentity& identity) {
    std::scoped_lock lock(mutex_);
    if (db_ == nullptr) return false;
    if (sqlite3_exec(db_, "BEGIN IMMEDIATE;", nullptr, nullptr, nullptr) != SQLITE_OK) return false;

    // Using substr instead of LIKE keeps folder names containing %, _, [, etc.
    // completely literal. Empty relativePath means the complete tracked volume.
    static constexpr const char* kUsageSql =
        "DELETE FROM folder_usage WHERE storage_key IN ("
        " SELECT storage_key FROM folders WHERE volume_id=?1 AND ("
        " ?2='' OR relative_path=?2 OR substr(relative_path,1,length(?2)+1)=?2||'\\'"
        " ));";
    static constexpr const char* kFoldersSql =
        "DELETE FROM folders WHERE volume_id=?1 AND ("
        " ?2='' OR relative_path=?2 OR substr(relative_path,1,length(?2)+1)=?2||'\\'"
        ");";
    static constexpr const char* kFilesSql =
        "DELETE FROM file_activity WHERE volume_id=?1 AND ("
        " ?2='' OR relative_path=?2 OR substr(relative_path,1,length(?2)+1)=?2||'\\'"
        ");";

    bool ok = ExecTreeDelete(db_, kUsageSql, identity);
    if (ok) ok = ExecTreeDelete(db_, kFoldersSql, identity);
    if (ok) ok = ExecTreeDelete(db_, kFilesSql, identity);

    sqlite3_exec(db_, ok ? "COMMIT;" : "ROLLBACK;", nullptr, nullptr, nullptr);
    return ok;
}

} // namespace fhm
