#include "Database.h"
#include "sqlite3.h"

namespace fhm {

std::optional<TrackedObject> Database::GetTrackedObjectAtPath(const std::wstring& volumeId,
                                                              const std::wstring& relativePath) {
    std::scoped_lock lock(mutex_);
    if (db_ == nullptr) return std::nullopt;
    sqlite3_stmt* st = nullptr;
    static constexpr const char* sql = "SELECT object_id,relative_path,is_directory FROM tracked_objects WHERE volume_id=?1 AND relative_path=?2 LIMIT 1;";
    if (sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) != SQLITE_OK) return std::nullopt;
    sqlite3_bind_text16(st, 1, volumeId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text16(st, 2, relativePath.c_str(), -1, SQLITE_TRANSIENT);
    std::optional<TrackedObject> result;
    if (sqlite3_step(st) == SQLITE_ROW) {
        TrackedObject item;
        const wchar_t* id = static_cast<const wchar_t*>(sqlite3_column_text16(st, 0));
        const wchar_t* path = static_cast<const wchar_t*>(sqlite3_column_text16(st, 1));
        item.objectId = id ? id : L""; item.relativePath = path ? path : L""; item.isDirectory = sqlite3_column_int(st, 2) != 0;
        result = std::move(item);
    }
    sqlite3_finalize(st);
    return result;
}

std::optional<TrackedObject> Database::GetTrackedObjectById(const std::wstring& volumeId,
                                                            const std::wstring& objectId) {
    std::scoped_lock lock(mutex_);
    if (db_ == nullptr) return std::nullopt;
    sqlite3_stmt* st = nullptr;
    static constexpr const char* sql = "SELECT object_id,relative_path,is_directory FROM tracked_objects WHERE volume_id=?1 AND object_id=?2 LIMIT 1;";
    if (sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) != SQLITE_OK) return std::nullopt;
    sqlite3_bind_text16(st, 1, volumeId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text16(st, 2, objectId.c_str(), -1, SQLITE_TRANSIENT);
    std::optional<TrackedObject> result;
    if (sqlite3_step(st) == SQLITE_ROW) {
        TrackedObject item;
        const wchar_t* id = static_cast<const wchar_t*>(sqlite3_column_text16(st, 0));
        const wchar_t* path = static_cast<const wchar_t*>(sqlite3_column_text16(st, 1));
        item.objectId = id ? id : L""; item.relativePath = path ? path : L""; item.isDirectory = sqlite3_column_int(st, 2) != 0;
        result = std::move(item);
    }
    sqlite3_finalize(st);
    return result;
}

bool Database::DeleteTrackedIdentityOnlyAtPath(const std::wstring& volumeId,
                                               const std::wstring& relativePath) {
    std::scoped_lock lock(mutex_);
    if (db_ == nullptr) return false;
    sqlite3_stmt* st = nullptr;
    static constexpr const char* sql = "DELETE FROM tracked_objects WHERE volume_id=?1 AND relative_path=?2;";
    if (sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text16(st, 1, volumeId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text16(st, 2, relativePath.c_str(), -1, SQLITE_TRANSIENT);
    const bool ok = sqlite3_step(st) == SQLITE_DONE;
    sqlite3_finalize(st);
    return ok;
}

} // namespace fhm
