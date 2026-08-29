#pragma once

#include "FolderIdentity.h"

#include <windows.h>

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

struct sqlite3;

namespace fhm {

struct StoredActivity { std::uint64_t visits = 0; FILETIME lastVisit{}; std::uint64_t heatVisits = 0; std::uint64_t recentVisits = 0; std::uint64_t activeDays = 0; std::int64_t firstActiveDay = 0; std::int64_t lastActiveDay = 0; FILETIME lastEffectiveVisit{}; };
struct StoredFileActivity { FILETIME lastWrite{}; std::uint64_t writeEvents = 0; std::uint64_t activeDays = 0; std::int64_t firstActiveDay = 0; std::int64_t lastActiveDay = 0; };
struct RuntimeCacheRecord { std::wstring path; std::uint32_t flags = 0; double heat = 0.0; std::int64_t visits = 0; std::int64_t writes = 0; FILETIME lastVisit{}; FILETIME lastWrite{}; std::int32_t heatLevel = 0; std::int32_t colorStep = 0; };
struct TrackedObject { std::wstring objectId; std::wstring relativePath; bool isDirectory = false; };
struct TrackedObservation { std::wstring objectId; std::wstring relativePath; bool isDirectory = false; };
enum class TrackedActionKind { Move, Delete };
struct TrackedAction { TrackedActionKind kind = TrackedActionKind::Delete; std::wstring objectId; std::wstring oldRelativePath; std::wstring newRelativePath; bool isDirectory = false; };

class Database {
public:
    Database() = default;
    ~Database();
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;
    bool Open(const std::wstring& databasePath);
    void Close();
    bool IsOpen() const;
    bool RecordVisit(const FolderIdentity& identity, const FILETIME& now, int cooldownSeconds, int sessionResetHours);
    std::optional<StoredActivity> GetActivity(const FolderIdentity& identity);
    std::vector<std::pair<std::wstring, StoredActivity>> GetVolumeActivities(const std::wstring& volumeId);
    bool ObserveFileWrite(const FolderIdentity& identity, const FILETIME& lastWrite);
    bool ObserveFileWriteBaselineSafe(const FolderIdentity& identity, const FILETIME& lastWrite);
    std::optional<StoredFileActivity> GetFileActivity(const FolderIdentity& identity);
    std::vector<std::pair<std::wstring, StoredFileActivity>> GetVolumeFileActivities(const std::wstring& volumeId);
    bool SaveRuntimeCache(const std::vector<RuntimeCacheRecord>& records);
    std::vector<RuntimeCacheRecord> LoadRuntimeCache();
    bool ResetDirectActivity(const FolderIdentity& identity, bool isDirectory, const FILETIME* currentLastWrite = nullptr);
    bool ResetRecursiveActivity(const FolderIdentity& identity);
    std::vector<TrackedObject> GetTrackedChildren(const std::wstring& volumeId, const std::wstring& parentRelativePath);
    std::optional<TrackedObject> GetTrackedObjectAtPath(const std::wstring& volumeId, const std::wstring& relativePath);
    std::optional<TrackedObject> GetTrackedObjectById(const std::wstring& volumeId, const std::wstring& objectId);
    bool DeleteTrackedIdentityOnlyAtPath(const std::wstring& volumeId, const std::wstring& relativePath);
    bool ApplyTrackedLifecycleBatch(const std::wstring& volumeId, const std::vector<TrackedObservation>& observations, const std::vector<TrackedAction>& explicitActions, std::vector<TrackedAction>* appliedActions = nullptr);
    bool ObserveTrackedObject(const FolderIdentity& identity, const std::wstring& objectId, bool isDirectory, std::wstring* movedFromRelativePath = nullptr);
    bool MoveTrackedObject(const std::wstring& volumeId, const std::wstring& objectId, const std::wstring& oldRelativePath, const std::wstring& newRelativePath, bool isDirectory);
    bool DeleteTrackedObject(const std::wstring& volumeId, const std::wstring& objectId, const std::wstring& relativePath, bool isDirectory);
    int GetRecentActiveDays(const FILETIME& now, int windowDays);
private:
    bool EnsureSchema();
    sqlite3* db_ = nullptr;
    mutable std::mutex mutex_;
};

} // namespace fhm
