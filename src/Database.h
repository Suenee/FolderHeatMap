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

struct StoredActivity {
    std::uint64_t visits = 0;
    FILETIME lastVisit{};

    std::uint64_t heatVisits = 0;
    std::uint64_t recentVisits = 0;
    std::uint64_t activeDays = 0;
    std::int64_t firstActiveDay = 0;
    std::int64_t lastActiveDay = 0;
    FILETIME lastEffectiveVisit{};
};

struct StoredFileActivity {
    FILETIME lastWrite{};
    std::uint64_t writeEvents = 0;
    std::uint64_t activeDays = 0;
    std::int64_t firstActiveDay = 0;
    std::int64_t lastActiveDay = 0;
};

struct RuntimeCacheRecord {
    std::wstring path;
    std::uint32_t flags = 0;
    double heat = 0.0;
    std::int64_t visits = 0;
    std::int64_t writes = 0;
    FILETIME lastVisit{};
    FILETIME lastWrite{};
    std::int32_t heatLevel = 0;
    std::int32_t colorStep = 0;
};

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
    std::optional<StoredFileActivity> GetFileActivity(const FolderIdentity& identity);
    std::vector<std::pair<std::wstring, StoredFileActivity>> GetVolumeFileActivities(const std::wstring& volumeId);

    // Runtime cache persistence. SQLite is a durable backup of the latest
    // complete RAM generation, never the foreground source for WDX reads.
    bool SaveRuntimeCache(const std::vector<RuntimeCacheRecord>& records);
    std::vector<RuntimeCacheRecord> LoadRuntimeCache();

    // Reset only the selected item's own activity. For files, currentLastWrite
    // becomes the new cold baseline so the same timestamp cannot immediately
    // reheat the file after the reset.
    bool ResetDirectActivity(const FolderIdentity& identity, bool isDirectory, const FILETIME* currentLastWrite = nullptr);

    // Reset a folder and all tracked folders/files below it. Ancestor heat is
    // not stored, so it automatically recalculates from the remaining sources.
    bool ResetRecursiveActivity(const FolderIdentity& identity);

    int GetRecentActiveDays(const FILETIME& now, int windowDays);

private:
    bool EnsureSchema();

    sqlite3* db_ = nullptr;
    mutable std::mutex mutex_;
};

} // namespace fhm
