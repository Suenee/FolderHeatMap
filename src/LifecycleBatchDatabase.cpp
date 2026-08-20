#include "Database.h"
#include "sqlite3.h"

#include <string>

namespace fhm {
namespace {

bool EnsureTracked(sqlite3* db) {
    static constexpr const char* sql =
        "CREATE TABLE IF NOT EXISTS tracked_objects ("
        " volume_id TEXT NOT NULL, object_id TEXT NOT NULL, relative_path TEXT NOT NULL,"
        " is_directory INTEGER NOT NULL DEFAULT 0, PRIMARY KEY(volume_id, object_id));"
        "CREATE INDEX IF NOT EXISTS idx_tracked_objects_path ON tracked_objects(volume_id, relative_path);";
    return sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
}

std::wstring Key(const std::wstring& volume, const std::wstring& relative) {
    return volume + L"|" + relative;
}

bool ExecMove3(sqlite3* db, const char* sql, const std::wstring& volume,
               const std::wstring& oldPath, const std::wstring& newPath) {
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text16(st, 1, volume.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text16(st, 2, oldPath.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text16(st, 3, newPath.c_str(), -1, SQLITE_TRANSIENT);
    const bool ok = sqlite3_step(st) == SQLITE_DONE;
    sqlite3_finalize(st);
    return ok;
}

bool MoveHistory(sqlite3* db, const std::wstring& volume, const std::wstring& oldPath,
                 const std::wstring& newPath, bool isDirectory) {
    if (oldPath.empty() || oldPath == newPath) return true;
    sqlite3_stmt* st = nullptr;
    bool ok = true;
    const std::wstring oldKey = Key(volume, oldPath);
    const std::wstring newKey = Key(volume, newPath);

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
            "UPDATE OR REPLACE folders SET storage_key=?1||'|'||?3||substr(relative_path,length(?2)+1),"
            " relative_path=?3||substr(relative_path,length(?2)+1) WHERE volume_id=?1 AND "
            "(relative_path=?2 OR substr(relative_path,1,length(?2)+1)=?2||'\\');";
        if (ok) ok = ExecMove3(db, foldersSql, volume, oldPath, newPath);

        static constexpr const char* filesSql =
            "UPDATE OR REPLACE file_activity SET storage_key=?1||'|'||?3||substr(relative_path,length(?2)+1),"
            " relative_path=?3||substr(relative_path,length(?2)+1) WHERE volume_id=?1 AND "
            "(relative_path=?2 OR substr(relative_path,1,length(?2)+1)=?2||'\\');";
        if (ok) ok = ExecMove3(db, filesSql, volume, oldPath, newPath);

        static constexpr const char* trackSql =
            "UPDATE OR REPLACE tracked_objects SET relative_path=?3||substr(relative_path,length(?2)+1) "
            "WHERE volume_id=?1 AND (relative_path=?2 OR substr(relative_path,1,length(?2)+1)=?2||'\\');";
        if (ok) ok = ExecMove3(db, trackSql, volume, oldPath, newPath);
    } else {
        static constexpr const char* fileSql =
            "UPDATE OR REPLACE file_activity SET storage_key=?3,relative_path=?4 WHERE storage_key=?2;";
        if (sqlite3_prepare_v2(db, fileSql, -1, &st, nullptr) != SQLITE_OK) ok = false;
        if (ok) {
            sqlite3_bind_text16(st, 2, oldKey.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text16(st, 3, newKey.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text16(st, 4, newPath.c_str(), -1, SQLITE_TRANSIENT);
            ok = sqlite3_step(st) == SQLITE_DONE;
        }
        if (st) sqlite3_finalize(st);
    }
    return ok;
}

bool DeleteHistory(sqlite3* db, const std::wstring& volume, const std::wstring& path, bool isDirectory) {
    sqlite3_stmt* st = nullptr;
    bool ok = true;
    if (isDirectory) {
        static constexpr const char* usageSql =
            "DELETE FROM folder_usage WHERE storage_key IN (SELECT storage_key FROM folders WHERE volume_id=?1 AND "
            "(relative_path=?2 OR substr(relative_path,1,length(?2)+1)=?2||'\\'));";
        if (sqlite3_prepare_v2(db, usageSql, -1, &st, nullptr) != SQLITE_OK) ok = false;
        if (ok) {
            sqlite3_bind_text16(st, 1, volume.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text16(st, 2, path.c_str(), -1, SQLITE_TRANSIENT);
            ok = sqlite3_step(st) == SQLITE_DONE;
        }
        if (st) { sqlite3_finalize(st); st = nullptr; }
        const char* sqls[] = {
            "DELETE FROM folders WHERE volume_id=?1 AND (relative_path=?2 OR substr(relative_path,1,length(?2)+1)=?2||'\\');",
            "DELETE FROM file_activity WHERE volume_id=?1 AND (relative_path=?2 OR substr(relative_path,1,length(?2)+1)=?2||'\\');",
            "DELETE FROM tracked_objects WHERE volume_id=?1 AND (relative_path=?2 OR substr(relative_path,1,length(?2)+1)=?2||'\\');"
        };
        for (const char* sql : sqls) {
            if (!ok || sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK) { ok = false; break; }
            sqlite3_bind_text16(st, 1, volume.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text16(st, 2, path.c_str(), -1, SQLITE_TRANSIENT);
            ok = sqlite3_step(st) == SQLITE_DONE;
            sqlite3_finalize(st); st = nullptr;
        }
    } else {
        const std::wstring key = Key(volume, path);
        if (sqlite3_prepare_v2(db, "DELETE FROM file_activity WHERE storage_key=?1;", -1, &st, nullptr) != SQLITE_OK) ok = false;
        if (ok) {
            sqlite3_bind_text16(st, 1, key.c_str(), -1, SQLITE_TRANSIENT);
            ok = sqlite3_step(st) == SQLITE_DONE;
        }
        if (st) { sqlite3_finalize(st); st = nullptr; }
        if (ok && sqlite3_prepare_v2(db, "DELETE FROM tracked_objects WHERE volume_id=?1 AND relative_path=?2;", -1, &st, nullptr) == SQLITE_OK) {
            sqlite3_bind_text16(st, 1, volume.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text16(st, 2, path.c_str(), -1, SQLITE_TRANSIENT);
            ok = sqlite3_step(st) == SQLITE_DONE;
        } else if (ok) ok = false;
        if (st) sqlite3_finalize(st);
    }
    return ok;
}

bool FindTrackedPath(sqlite3* db, sqlite3_stmt* find, const std::wstring& volume,
                     const std::wstring& objectId, std::wstring& out) {
    out.clear();
    sqlite3_reset(find);
    sqlite3_clear_bindings(find);
    sqlite3_bind_text16(find, 1, volume.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text16(find, 2, objectId.c_str(), -1, SQLITE_TRANSIENT);
    const int rc = sqlite3_step(find);
    if (rc == SQLITE_ROW) {
        const wchar_t* p = static_cast<const wchar_t*>(sqlite3_column_text16(find, 0));
        if (p) out = p;
        return true;
    }
    return rc == SQLITE_DONE;
}

} // namespace

bool Database::ApplyTrackedLifecycleBatch(const std::wstring& volumeId,
                                          const std::vector<TrackedObservation>& observations,
                                          const std::vector<TrackedAction>& explicitActions,
                                          std::vector<TrackedAction>* appliedActions) {
    if (appliedActions) appliedActions->clear();
    std::scoped_lock lock(mutex_);
    if (db_ == nullptr || !EnsureTracked(db_)) return false;
    if (sqlite3_exec(db_, "BEGIN IMMEDIATE;", nullptr, nullptr, nullptr) != SQLITE_OK) return false;

    bool ok = true;
    sqlite3_stmt* find = nullptr;
    sqlite3_stmt* upsert = nullptr;
    static constexpr const char* findSql =
        "SELECT relative_path FROM tracked_objects WHERE volume_id=?1 AND object_id=?2;";
    static constexpr const char* upsertSql =
        "INSERT INTO tracked_objects(volume_id,object_id,relative_path,is_directory) VALUES(?1,?2,?3,?4) "
        "ON CONFLICT(volume_id,object_id) DO UPDATE SET relative_path=excluded.relative_path,is_directory=excluded.is_directory;";
    if (sqlite3_prepare_v2(db_, findSql, -1, &find, nullptr) != SQLITE_OK ||
        sqlite3_prepare_v2(db_, upsertSql, -1, &upsert, nullptr) != SQLITE_OK) ok = false;

    // Apply disappearance/move actions before observations. This ordering is
    // essential for delete+recreate-at-the-same-path: the old object's history
    // must be removed before the replacement object is inserted. The previous
    // ordering inserted the replacement first and then deleted by path, which
    // could leave stale RAM/history visible or remove the new tracking row.
    for (const auto& action : explicitActions) {
        if (!ok) break;
        if (action.kind == TrackedActionKind::Move) {
            ok = MoveHistory(db_, volumeId, action.oldRelativePath, action.newRelativePath, action.isDirectory);
            if (ok) {
                sqlite3_reset(upsert);
                sqlite3_clear_bindings(upsert);
                sqlite3_bind_text16(upsert, 1, volumeId.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text16(upsert, 2, action.objectId.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text16(upsert, 3, action.newRelativePath.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int(upsert, 4, action.isDirectory ? 1 : 0);
                ok = sqlite3_step(upsert) == SQLITE_DONE;
            }
        } else {
            ok = DeleteHistory(db_, volumeId, action.oldRelativePath, action.isDirectory);
        }
        if (ok && appliedActions) appliedActions->push_back(action);
    }

    for (const auto& obs : observations) {
        if (!ok || obs.objectId.empty()) break;
        std::wstring oldPath;
        ok = FindTrackedPath(db_, find, volumeId, obs.objectId, oldPath);
        if (!ok) break;
        if (!oldPath.empty() && oldPath != obs.relativePath) {
            ok = MoveHistory(db_, volumeId, oldPath, obs.relativePath, obs.isDirectory);
            if (!ok) break;
            if (appliedActions) appliedActions->push_back({TrackedActionKind::Move, obs.objectId, oldPath, obs.relativePath, obs.isDirectory});
        }
        sqlite3_reset(upsert);
        sqlite3_clear_bindings(upsert);
        sqlite3_bind_text16(upsert, 1, volumeId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text16(upsert, 2, obs.objectId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text16(upsert, 3, obs.relativePath.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(upsert, 4, obs.isDirectory ? 1 : 0);
        ok = sqlite3_step(upsert) == SQLITE_DONE;
    }

    if (find) sqlite3_finalize(find);
    if (upsert) sqlite3_finalize(upsert);
    sqlite3_exec(db_, ok ? "COMMIT;" : "ROLLBACK;", nullptr, nullptr, nullptr);
    if (!ok && appliedActions) appliedActions->clear();
    return ok;
}

} // namespace fhm
