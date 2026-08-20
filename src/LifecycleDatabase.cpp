#include "Database.h"
#include "sqlite3.h"

#include <string>

namespace fhm {
namespace {

bool EnsureTrackedObjects(sqlite3* db) {
    static constexpr const char* sql =
        "CREATE TABLE IF NOT EXISTS tracked_objects ("
        " volume_id TEXT NOT NULL,"
        " object_id TEXT NOT NULL,"
        " relative_path TEXT NOT NULL,"
        " is_directory INTEGER NOT NULL DEFAULT 0,"
        " PRIMARY KEY(volume_id, object_id)"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_tracked_objects_path ON tracked_objects(volume_id, relative_path);";
    return sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
}

std::wstring StorageKey(const std::wstring& volumeId, const std::wstring& relativePath) {
    return volumeId + L"|" + relativePath;
}

bool ExecMove(sqlite3* db, const char* sql, const std::wstring& volumeId,
              const std::wstring& oldRelative, const std::wstring& newRelative) {
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text16(st, 1, volumeId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text16(st, 2, oldRelative.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text16(st, 3, newRelative.c_str(), -1, SQLITE_TRANSIENT);
    const bool ok = sqlite3_step(st) == SQLITE_DONE;
    sqlite3_finalize(st);
    return ok;
}

bool MoveHistoryUnlocked(sqlite3* db, const std::wstring& volumeId,
                         const std::wstring& oldRelative, const std::wstring& newRelative,
                         bool isDirectory) {
    if (oldRelative == newRelative) return true;
    const std::wstring oldKey = StorageKey(volumeId, oldRelative);
    const std::wstring newKey = StorageKey(volumeId, newRelative);
    sqlite3_stmt* st = nullptr;
    bool ok = true;

    if (isDirectory) {
        static constexpr const char* usageSql =
            "UPDATE OR REPLACE folder_usage SET storage_key=?3||substr(storage_key,length(?2)+1) "
            "WHERE storage_key=?2 OR substr(storage_key,1,length(?2)+1)=?2||'\\';";
        if (sqlite3_prepare_v2(db, usageSql, -1, &st, nullptr) != SQLITE_OK) ok = false;
        if (ok) {
            sqlite3_bind_text16(st, 2, oldKey.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text16(st, 3, newKey.c_str(), -1, SQLITE_TRANSIENT);
            ok = sqlite3_step(st) == SQLITE_DONE;
        }
        if (st) { sqlite3_finalize(st); st = nullptr; }

        static constexpr const char* foldersSql =
            "UPDATE OR REPLACE folders SET "
            " storage_key=?1||'|'||?3||substr(relative_path,length(?2)+1),"
            " relative_path=?3||substr(relative_path,length(?2)+1) "
            "WHERE volume_id=?1 AND (relative_path=?2 OR substr(relative_path,1,length(?2)+1)=?2||'\\');";
        if (ok) ok = ExecMove(db, foldersSql, volumeId, oldRelative, newRelative);

        static constexpr const char* filesSql =
            "UPDATE OR REPLACE file_activity SET "
            " storage_key=?1||'|'||?3||substr(relative_path,length(?2)+1),"
            " relative_path=?3||substr(relative_path,length(?2)+1) "
            "WHERE volume_id=?1 AND (relative_path=?2 OR substr(relative_path,1,length(?2)+1)=?2||'\\');";
        if (ok) ok = ExecMove(db, filesSql, volumeId, oldRelative, newRelative);

        static constexpr const char* trackedSql =
            "UPDATE OR REPLACE tracked_objects SET relative_path=?3||substr(relative_path,length(?2)+1) "
            "WHERE volume_id=?1 AND (relative_path=?2 OR substr(relative_path,1,length(?2)+1)=?2||'\\');";
        if (ok) ok = ExecMove(db, trackedSql, volumeId, oldRelative, newRelative);
    } else {
        static constexpr const char* fileSql =
            "UPDATE OR REPLACE file_activity SET storage_key=?3,relative_path=?4 "
            "WHERE storage_key=?2;";
        if (sqlite3_prepare_v2(db, fileSql, -1, &st, nullptr) != SQLITE_OK) ok = false;
        if (ok) {
            sqlite3_bind_text16(st, 2, oldKey.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text16(st, 3, newKey.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text16(st, 4, newRelative.c_str(), -1, SQLITE_TRANSIENT);
            ok = sqlite3_step(st) == SQLITE_DONE;
        }
        if (st) sqlite3_finalize(st);
    }
    return ok;
}

bool DeleteHistoryUnlocked(sqlite3* db, const std::wstring& volumeId,
                           const std::wstring& relativePath, bool isDirectory) {
    sqlite3_stmt* st = nullptr;
    bool ok = true;
    if (isDirectory) {
        static constexpr const char* usageSql =
            "DELETE FROM folder_usage WHERE storage_key IN (SELECT storage_key FROM folders "
            "WHERE volume_id=?1 AND (relative_path=?2 OR substr(relative_path,1,length(?2)+1)=?2||'\\'));";
        if (sqlite3_prepare_v2(db, usageSql, -1, &st, nullptr) != SQLITE_OK) ok = false;
        if (ok) {
            sqlite3_bind_text16(st, 1, volumeId.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text16(st, 2, relativePath.c_str(), -1, SQLITE_TRANSIENT);
            ok = sqlite3_step(st) == SQLITE_DONE;
        }
        if (st) { sqlite3_finalize(st); st = nullptr; }
        const char* deletes[] = {
            "DELETE FROM folders WHERE volume_id=?1 AND (relative_path=?2 OR substr(relative_path,1,length(?2)+1)=?2||'\\');",
            "DELETE FROM file_activity WHERE volume_id=?1 AND (relative_path=?2 OR substr(relative_path,1,length(?2)+1)=?2||'\\');",
            "DELETE FROM tracked_objects WHERE volume_id=?1 AND (relative_path=?2 OR substr(relative_path,1,length(?2)+1)=?2||'\\');"
        };
        for (const char* sql : deletes) {
            if (!ok || sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK) { ok = false; break; }
            sqlite3_bind_text16(st, 1, volumeId.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text16(st, 2, relativePath.c_str(), -1, SQLITE_TRANSIENT);
            ok = sqlite3_step(st) == SQLITE_DONE;
            sqlite3_finalize(st); st = nullptr;
        }
    } else {
        const std::wstring key = StorageKey(volumeId, relativePath);
        static constexpr const char* fileSql = "DELETE FROM file_activity WHERE storage_key=?1;";
        if (sqlite3_prepare_v2(db, fileSql, -1, &st, nullptr) != SQLITE_OK) ok = false;
        if (ok) {
            sqlite3_bind_text16(st, 1, key.c_str(), -1, SQLITE_TRANSIENT);
            ok = sqlite3_step(st) == SQLITE_DONE;
        }
        if (st) { sqlite3_finalize(st); st = nullptr; }
        static constexpr const char* trackSql = "DELETE FROM tracked_objects WHERE volume_id=?1 AND relative_path=?2;";
        if (ok && sqlite3_prepare_v2(db, trackSql, -1, &st, nullptr) == SQLITE_OK) {
            sqlite3_bind_text16(st, 1, volumeId.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text16(st, 2, relativePath.c_str(), -1, SQLITE_TRANSIENT);
            ok = sqlite3_step(st) == SQLITE_DONE;
        } else if (ok) ok = false;
        if (st) sqlite3_finalize(st);
    }
    return ok;
}

} // namespace

std::vector<TrackedObject> Database::GetTrackedChildren(const std::wstring& volumeId,
                                                        const std::wstring& parentRelativePath) {
    std::scoped_lock lock(mutex_);
    std::vector<TrackedObject> result;
    if (db_ == nullptr || !EnsureTrackedObjects(db_)) return result;

    sqlite3_stmt* st = nullptr;
    static constexpr const char* sql =
        "SELECT object_id,relative_path,is_directory FROM tracked_objects WHERE volume_id=?1 AND ("
        " (?2='' AND instr(relative_path,'\\')=0) OR "
        " (?2<>'' AND substr(relative_path,1,length(?2)+1)=?2||'\\' "
        "  AND instr(substr(relative_path,length(?2)+2),'\\')=0));";
    if (sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) != SQLITE_OK) return result;
    sqlite3_bind_text16(st, 1, volumeId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text16(st, 2, parentRelativePath.c_str(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(st) == SQLITE_ROW) {
        TrackedObject item;
        const wchar_t* id = static_cast<const wchar_t*>(sqlite3_column_text16(st, 0));
        const wchar_t* path = static_cast<const wchar_t*>(sqlite3_column_text16(st, 1));
        item.objectId = id ? id : L"";
        item.relativePath = path ? path : L"";
        item.isDirectory = sqlite3_column_int(st, 2) != 0;
        result.push_back(std::move(item));
    }
    sqlite3_finalize(st);
    return result;
}

bool Database::ObserveTrackedObject(const FolderIdentity& identity, const std::wstring& objectId,
                                    bool isDirectory, std::wstring* movedFromRelativePath) {
    if (movedFromRelativePath) movedFromRelativePath->clear();
    std::scoped_lock lock(mutex_);
    if (db_ == nullptr || objectId.empty() || !EnsureTrackedObjects(db_)) return false;
    if (sqlite3_exec(db_, "BEGIN IMMEDIATE;", nullptr, nullptr, nullptr) != SQLITE_OK) return false;

    bool ok = true;
    std::wstring oldRelative;
    sqlite3_stmt* st = nullptr;
    static constexpr const char* findSql =
        "SELECT relative_path FROM tracked_objects WHERE volume_id=?1 AND object_id=?2;";
    if (sqlite3_prepare_v2(db_, findSql, -1, &st, nullptr) != SQLITE_OK) ok = false;
    if (ok) {
        sqlite3_bind_text16(st, 1, identity.volumeId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text16(st, 2, objectId.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(st) == SQLITE_ROW) {
            const wchar_t* p = static_cast<const wchar_t*>(sqlite3_column_text16(st, 0));
            if (p) oldRelative = p;
        }
    }
    if (st) { sqlite3_finalize(st); st = nullptr; }

    if (ok && !oldRelative.empty() && oldRelative != identity.relativePath) {
        ok = MoveHistoryUnlocked(db_, identity.volumeId, oldRelative, identity.relativePath, isDirectory);
        if (ok && movedFromRelativePath) *movedFromRelativePath = oldRelative;
    }

    static constexpr const char* upsertSql =
        "INSERT INTO tracked_objects(volume_id,object_id,relative_path,is_directory) VALUES(?1,?2,?3,?4) "
        "ON CONFLICT(volume_id,object_id) DO UPDATE SET relative_path=excluded.relative_path,is_directory=excluded.is_directory;";
    if (ok && sqlite3_prepare_v2(db_, upsertSql, -1, &st, nullptr) == SQLITE_OK) {
        sqlite3_bind_text16(st, 1, identity.volumeId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text16(st, 2, objectId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text16(st, 3, identity.relativePath.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(st, 4, isDirectory ? 1 : 0);
        ok = sqlite3_step(st) == SQLITE_DONE;
    } else if (ok) ok = false;
    if (st) sqlite3_finalize(st);

    sqlite3_exec(db_, ok ? "COMMIT;" : "ROLLBACK;", nullptr, nullptr, nullptr);
    return ok;
}

bool Database::MoveTrackedObject(const std::wstring& volumeId, const std::wstring& objectId,
                                 const std::wstring& oldRelativePath, const std::wstring& newRelativePath,
                                 bool isDirectory) {
    std::scoped_lock lock(mutex_);
    if (db_ == nullptr || !EnsureTrackedObjects(db_)) return false;
    if (sqlite3_exec(db_, "BEGIN IMMEDIATE;", nullptr, nullptr, nullptr) != SQLITE_OK) return false;
    bool ok = MoveHistoryUnlocked(db_, volumeId, oldRelativePath, newRelativePath, isDirectory);
    sqlite3_stmt* st = nullptr;
    static constexpr const char* sql =
        "UPDATE tracked_objects SET relative_path=?3,is_directory=?4 WHERE volume_id=?1 AND object_id=?2;";
    if (ok && sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) == SQLITE_OK) {
        sqlite3_bind_text16(st, 1, volumeId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text16(st, 2, objectId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text16(st, 3, newRelativePath.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(st, 4, isDirectory ? 1 : 0);
        ok = sqlite3_step(st) == SQLITE_DONE;
    } else if (ok) ok = false;
    if (st) sqlite3_finalize(st);
    sqlite3_exec(db_, ok ? "COMMIT;" : "ROLLBACK;", nullptr, nullptr, nullptr);
    return ok;
}

bool Database::DeleteTrackedObject(const std::wstring& volumeId, const std::wstring& objectId,
                                   const std::wstring& relativePath, bool isDirectory) {
    std::scoped_lock lock(mutex_);
    if (db_ == nullptr || !EnsureTrackedObjects(db_)) return false;
    if (sqlite3_exec(db_, "BEGIN IMMEDIATE;", nullptr, nullptr, nullptr) != SQLITE_OK) return false;
    bool ok = DeleteHistoryUnlocked(db_, volumeId, relativePath, isDirectory);
    sqlite3_stmt* st = nullptr;
    static constexpr const char* sql = "DELETE FROM tracked_objects WHERE volume_id=?1 AND object_id=?2;";
    if (ok && sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) == SQLITE_OK) {
        sqlite3_bind_text16(st, 1, volumeId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text16(st, 2, objectId.c_str(), -1, SQLITE_TRANSIENT);
        ok = sqlite3_step(st) == SQLITE_DONE;
    } else if (ok) ok = false;
    if (st) sqlite3_finalize(st);
    sqlite3_exec(db_, ok ? "COMMIT;" : "ROLLBACK;", nullptr, nullptr, nullptr);
    return ok;
}

} // namespace fhm
