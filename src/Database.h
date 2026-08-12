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
    int GetRecentActiveDays(const FILETIME& now, int windowDays);

private:
    bool EnsureSchema();

    sqlite3* db_ = nullptr;
    mutable std::mutex mutex_;
};

} // namespace fhm
